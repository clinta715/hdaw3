# HDAW MCP Server — Design Spec

**Status:** Draft v1 — for review
**Date:** 2026-06-29
**Target version:** HDAW 0.3.x (post-v0.2.2)

## Overview

Expose HDAW as a **Model Context Protocol (MCP) server** so external LLM clients (Claude Desktop, opencode, etc.) can drive the DAW: inspect the project, edit tracks/clips/notes, control transport, run the PhraseGenerator, manage FX and automation, and export audio.

**Headline decisions** (from brainstorming):

- **Direction:** HDAW is the MCP **server**. LLM clients connect in.
- **Transports:** **both** stdio and Streamable HTTP. Stdio is the standard "launched as a child" path; HTTP is an opt-in local server for tools that prefer it.
- **Surface scope:** a **broad write surface** — tools cover transport, tracks, clips, MIDI notes, PhraseGenerator, FX, automation, export, and undo/redo.
- **Architecture:** **in-process**, hand-rolled JSON-RPC, a single tool registry shared by both transports. No new third-party dependencies. No second binary.
- **Threading:** every tool handler runs on the main thread. MCP is "another main-thread mutator" alongside the GUI, reusing the same engine-access boundaries (and therefore preserving `AGENTS.md`'s realtime-safety rules).
- **Testing:** this spec introduces the project's first test suite (GoogleTest), scoped to the MCP module.

## Goals

1. An LLM client can launch HDAW as a subprocess (stdio) and interactively arrange, edit, and generate music.
2. A developer can run the same server with the HTTP transport enabled, bound to loopback, for tooling that prefers HTTP.
3. Every destructive action is **undoable** (via the existing `UndoManager`) and supports a **`dryRun`** preview.
4. The design composes with, rather than duplicates or circumvents, the existing engine, model, UI, and safety architecture.
5. The MCP module is the pilot for the project's automated test infrastructure.

## Non-goals (v1)

- **Resources and prompts** (`resources/*`, `prompts/*` MCP methods). The surface is tools-only. The same registry pattern extends to them later without protocol changes.
- **HTTP authentication / authorization.** The HTTP transport binds to `127.0.0.1` only and is unauthenticated. Exposing it beyond loopback is explicitly out of scope and would require a token scheme (a follow-up).
- **Remote / networked operation.** No binding to non-loopback addresses; no cloud relay.
- **SSE streaming of partial tool results.** Every tool returns a complete result. Streamable HTTP is supported, but we use the single-shot `application/json` response variant.
- **Headless audio playback with a real device.** Headless mode updates transport state and runs the engine; if no audio device is available, playback advances position without producing sound. Acceptable for an automation/inspection/generation tool; real headless playback is a follow-up.
- **Exhaustive DAW surface in v1.** The tool set is curated (see §5). Adding more tools is purely additive — register a handler.
- **Migrating or refactoring the existing engine/model/UI** beyond the single prerequisite model change (§6.1).

## 1. Architecture & lifecycle

A new module `src/mcp/` owns the server. It is constructed in `main.cpp` alongside `AudioEngine`, so it works in both GUI and headless modes and shares the existing engine — no second process, no IPC.

```
src/mcp/
  McpServer.{h,cpp}            // protocol core + tool registry (transport-agnostic)
  McpToolDef.h                 // tool record, McpToolResult, McpHandler
  McpJsonRpc.{h,cpp}           // request/response/notification framing on QJsonDocument
  McpSchema.{h,cpp}            // minimal hand-rolled JSON-Schema validator
  McpTransport.h               // transport interface (in/out)
  McpTransportStdio.{h,cpp}    // newline-delimited JSON over stdin/stdout
  McpTransportHttp.{h,cpp}     // QHttpServer, Streamable HTTP (single-shot)
  McpTransportLoopback.{h,cpp} // tests only: in-memory queues
```

A single `McpServer` keeps one `McpToolRegistry` (a `std::unordered_map<QString, McpToolDef>`). Both transports route through it. Adding a tool is purely additive: write a handler, register it.

### Process model, decided in `main.cpp`

In order:

1. **Parse CLI flags:**
   - `--mcp-stdio` — force headless stdio-server mode (override the TTY auto-detect).
   - `--no-mcp` — disable MCP entirely: no stdio auto-detect, no HTTP transport.
   - `--mcp-http-port=<N>` — override the HTTP port for this launch.
2. **Stdio auto-detect:** if `--mcp-stdio` is set, *or* `stdin`/`stdout` are **not** TTYs (`_isatty(_fileno(stdin/stdout))` on Windows, `isatty(fileno(stdin/stdout))` on Unix), enter **headless mode**:
   - Do **not** create a `QApplication` or `MainWindow`.
   - Construct `AudioEngine` (loads/uses the supplied project, or a default) and `McpServer` with the stdio transport.
   - Run the event loop. Exit cleanly when the client closes the pipes.
3. **GUI mode** (the default): normal `QApplication` + `MainWindow`. The stdio transport is **not** started (the GUI owns stdin/stdout). The HTTP transport is opt-in (see §7).

### Why this split

It matches the standard MCP client contract (clients launch the server as a child for stdio), avoids the stdin/stdout conflict a GUI would create, and leaves the existing interactive launch behavior unchanged. The HTTP transport is a deliberate, user-initiated opt-in.

## 2. Protocol & transport layer

### 2.1 JSON-RPC 2.0 (hand-rolled)

We implement the minimal subset MCP needs on top of `QJsonDocument`/`QJsonObject`:

- **Request:** `{ "jsonrpc": "2.0", "id": <any>, "method": <str>, "params": <obj|array|null> }`
- **Success response:** `{ "jsonrpc": "2.0", "id": <matching>, "result": <any> }`
- **Error response:** `{ "jsonrpc": "2.0", "id": <matching|null>, "error": { "code": <int>, "message": <str>, "data"?: <any> } }`
- **Notification:** like a request, no `id`, no reply.

Standard error codes used:

| Code | Meaning |
|------|---------|
| `-32700` | Parse error (invalid JSON) |
| `-32600` | Invalid request (shape wrong, missing method, etc.) |
| `-32601` | Method not found |
| `-32602` | Invalid params (schema validation failed) |
| `-32603` | Internal error (unexpected exception in handler/transport) |

**Tool-execution errors are not JSON-RPC errors.** Per the MCP contract, a tool that runs but fails (e.g. "track id not found," "source file missing") returns a normal result with `isError: true` and a `content` block describing the failure. This distinction is the one most hand-rolled implementations get wrong; we get it right.

### 2.2 MCP method set (v1)

| Method | Direction | Purpose |
|---|---|---|
| `initialize` | client → server | Handshake. Returns `protocolVersion`, `capabilities` (advertises `tools`), `serverInfo` (name, version). |
| `tools/list` | client → server | Returns registered tools with `name`, `description`, `inputSchema`. |
| `tools/call` | client → server | Invokes a tool by `name` with `arguments`; returns `{ content: [...], isError: bool }`. |
| `ping` | client → server | Liveness check; returns `{}`. |
| `notifications/cancelled` | client → server | Cancel an in-flight tool (sets a flag the handler polls). |
| `notifications/progress` | client ↔ server | Progress updates for long tools (we use it for `export_audio`). |

We do **not** ship `resources/*` or `prompts/*` in v1. The same registry pattern extends to them without protocol-layer changes.

### 2.3 Transports

Both transports implement a single `McpTransport` interface:

```cpp
class McpTransport {
public:
    virtual ~McpTransport() = default;
    // Called by McpServer. Implementations deliver parsed JSON-RPC requests
    // to McpServer::handleRequest(...) on the main thread (via a queued
    // connection), and write JSON-RPC responses/notifications to their sink.
    virtual void start(McpServer* server) = 0;
    virtual void stop() = 0;
};
```

The transport's only job is **framing** (bytes ↔ `McpRequest`/`McpResponse`). All dispatch and tool execution happens in `McpServer` on the main thread.

**Stdio transport** (headless mode only):
- Framing: **newline-delimited JSON**, one message per line, per the MCP stdio convention. No `Content-Length` headers.
- A **dedicated reader thread** reads stdin in a loop, parses each non-empty line, and posts a `McpRequest` to `McpServer` via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. The reader thread never touches the engine, model, or `AudioEngine`.
- `McpServer` writes responses to stdout from the main thread.
- On EOF or write error, the transport calls `McpServer::shutdown()` (which posts a `QCoreApplication::quit()`).

**Streamable HTTP transport** (GUI mode, opt-in):
- Implemented with **Qt 6.4+ `QHttpServer`** (new `Qt6::HttpServer` component; added to `CMakeLists.txt`).
- A single `POST /mcp` endpoint. Accepts `Content-Type: application/json`; returns `Content-Type: application/json` for single-shot responses (no SSE streaming of partial results in v1).
- The HTTP handler parses the request, then posts a `McpRequest` to `McpServer` via a queued call — so tool execution is serialised on the main thread.
- **Bound to `127.0.0.1` only** (see §8). The constructor **refuses** to bind to any other address.
- The handler also flushes responses asynchronously: `McpServer` calls `transport->send(response)`, which writes to the open socket; the transport holds the `QHttpServerResponse` lifetime until the write completes.

### 2.4 Request flow (both transports)

```
bytes
  └─► Transport (framing)        — stdio reader thread, or HTTP handler thread
        └─► QMetaObject::invokeMethod(server, McpRequest, QueuedConnection)
              └─► McpServer::handleRequest()         — main thread
                    ├─ dispatch to tool registry
                    ├─ validate(args) via McpSchema  — reject → -32602
                    └─ invoke handler → McpToolResult
                          └─► McpServer::sendResponse()
                                └─► Transport::send(bytes)
                                      └─► wire (stdout / HTTP response)
```

Because every tool runs on the main thread, engine/model access is **single-threaded by construction** — no locks for model state, no cross-thread `ProjectModel` corruption. The audio thread is not on this path at all (see §6).

## 3. Tool registry & v1 tool surface

### 3.1 Tool definition

```cpp
struct McpToolDef {
    QString        name;        // identifier the client calls
    QString        description; // shown to the LLM — author carefully
    QJsonObject    inputSchema; // JSON Schema subset (see §3.2)
    McpHandler     handler;     // (QJsonObject args) -> McpToolResult
};

struct McpToolResult {
    QJsonArray content;   // array of {type:"text", text:"..."} (and later audio/image)
    bool       isError = false;
};
```

### 3.2 Schema validation

A small, hand-rolled validator covering the subset MCP clients actually emit. The supported keywords:

- `type`: `"string" | "number" | "integer" | "boolean" | "array" | "object" | "null"`
- `required`: array of property names (object only)
- `properties`: nested schemas (object only)
- `items`: schema for array elements
- `enum`: exact-match set
- `minimum` / `maximum`: inclusive bounds (number/integer)
- `additionalProperties: false` (object): reject unknown properties

Validation rejects with JSON-RPC `-32602 invalid params` and a precise message indicating the failing path (`"trackId: expected integer, got string"`, `"pitches[2]: value 200 out of range [0, 127]"`, etc.). No third-party validator dependency.

### 3.3 v1 tool set (36 tools)

Grouped by domain. `id` fields use the model's existing `clipID` for clips and the track's index for tracks. Notes use a new `noteID` (§6.1). Every destructive tool takes an optional `dryRun: bool`.

| # | Domain | Tool | Key args | Notes |
|---|---|---|---|---|
| **Read (inspector)** | | | | |
| 1 | project | `get_project_summary` | — | name, tempo, time sig, counts, transport pos, isPlaying |
| 2 | project | `get_scale` | — | scaleRoot, scaleMode |
| 3 | transport | `get_transport` | — | position, isPlaying, isLooping, loopStart, loopEnd |
| 4 | tracks | `list_tracks` | — | id, name, color, vol, pan, mute, solo, clipCount |
| 5 | clips | `list_clips` | `trackId?` | id, trackId, name, start, duration, type, gain |
| 6 | clips | `get_clip` | `clipId` | full props + (midi) note list |
| 7 | fx | `list_fx` | `trackId` | per slot: type/pluginID, bypassed |
| 8 | automation | `list_automation_lanes` | `trackId` | per lane: name, paramID, enabled, pointCount |
| **Transport** | | | | |
| 9 | transport | `transport` | `action` (`play`/`stop`/`pause`/`rewind`/`toggleLoop`), `loopStart?`, `loopEnd?` | |
| 10 | transport | `seek` | `position` (beats) | |
| **Tracks** | | | | |
| 11 | tracks | `add_track` | `name`, `color?` (`#RRGGBB`), `parentBus?` | color defaults to `ProjectModel::trackColorForIndex(count)` |
| 12 | tracks | `remove_track` | `trackId`, `dryRun?` | destructive |
| 13 | tracks | `set_track` | `trackId`, `name?`/`volume?`/`pan?`/`mute?`/`solo?`/`color?` | partial updates |
| 14 | tracks | `move_track` | `trackId`, `newIndex` | |
| **Clips** | | | | |
| 15 | clips | `add_midi_clip` | `trackId`, `start`, `length`, `name?` | |
| 16 | clips | `add_audio_clip` | `trackId`, `start`, `sourceFile`, `name?` | |
| 17 | clips | `remove_clip` | `clipId`, `dryRun?` | destructive |
| 18 | clips | `move_clip` | `clipId`, `start?`, `trackId?` | re-parenting adopts the destination track's color (§6.2) |
| 19 | clips | `set_clip` | `clipId`, `name?`/`start?`/`duration?`/`gain?`/`fadeIn?`/`fadeOut?`/`looping?` | partial |
| 20 | clips | `duplicate_clip` | `clipId`, `start?`, `trackId?` | destructive (creates) |
| **MIDI notes** | | | | |
| 21 | notes | `add_note` | `clipId`, `pitch`, `start`, `duration`, `velocity` (0–127) | returns `noteId` |
| 22 | notes | `set_note` | `noteId`, `pitch?`/`start?`/`duration?`/`velocity?` | partial |
| 23 | notes | `remove_notes` | `clipId` + filter `{pitches?`/`startGte?`/`startLt?`} or `noteIds[]`, `dryRun?` | destructive |
| 24 | notes | `clear_notes` | `clipId`, `dryRun?` | destructive |
| **Composition (PhraseGenerator)** | | | | |
| 25 | comp | `set_scale` | `root` (0–11), `mode` (index) | |
| 26 | comp | `generate_phrase` | `trackId`, `style`, `length`, `density`, `scale?` | creates/fills a clip; returns clipId + note count |
| 27 | comp | `generate_chord` | `trackId`, `rootPitch`, `chordType`, `voicing?`, `inversion?`, `arpeggiate?`, `start?`, `length?` | |
| 28 | comp | `generate_progression` | `trackId`, `pattern`, `start?`, `beatsPerChord?`, `scale?` | |
| **FX** | | | | |
| 29 | fx | `add_fx` | `trackId`, `fxType` (`"eq"`/`"compressor"`/`"reverb"`/`"delay"`) **or** `pluginId`, `position?` | |
| 30 | fx | `remove_fx` | `trackId`, `slotIndex`, `dryRun?` | destructive |
| 31 | fx | `set_fx_bypass` | `trackId`, `slotIndex`, `bypassed` | |
| **Automation** | | | | |
| 32 | auto | `add_automation_point` | `trackId`, `lane` (`paramID` integer preferred; `name` accepted as a fallback lookup), `time`, `value` | |
| 33 | auto | `set_automation_enabled` | `trackId`, `lane`, `enabled` | |
| **Undo** | | | | |
| 34 | undo | `undo` | `count?` (default 1) | primary safety net (§3.4) |
| 35 | undo | `redo` | `count?` (default 1) | |
| **Export** | | | | |
| 36 | export | `export_audio` | `outputPath`, `format` (`"wav"`), `start?`, `end?`, `trackIds?` | long-running, cancellable (§4.3) |

Adding more tools later is purely additive: write a handler, register it.

### 3.4 Safety model

Three layers, in order of strength:

1. **`dryRun` on every destructive tool** (`remove_*`, `clear_notes`, `duplicate_clip`, `export_audio`). When `dryRun: true`, the tool validates args, computes the planned effect, and returns a text description ("would remove 2 tracks: Track 1, Synth") without mutating. The LLM (and a future "are you sure?" UI) can preview.
2. **Everything goes through the `UndoManager`.** Every mutation is wrapped in a single undoable action per tool call, so the user can `Ctrl+Z` (or call `undo` / `redo`) to roll back any LLM action in the GUI. The `undo`/`redo` tools (#34–35) are first-class — the LLM is its own safety net.
3. **Clear, action-verb naming** (`add_*` / `remove_*` / `set_*` / `clear_*` / `move_*`) so destructive intent is explicit in the LLM's prompt context. No silent overwrites.

## 4. Engine access, threading & safety

### 4.1 The single rule

*Every tool handler runs on the main thread.* The stdio reader thread and the HTTP handler both post to `McpServer` via `Qt::QueuedConnection`, and `McpServer` invokes handlers synchronously on its thread — the application's main thread (the only thread in headless mode; the Qt main thread in GUI mode). So tool handlers have direct, unsynchronised access to `ProjectModel`, `AudioEngine`, `TransportManager`. No `QMutex`, no `QMutexLocker`, no atomic gymnastics just to read the model.

### 4.2 MCP is "another main-thread mutator"

The GUI already mutates the model on the main thread from many places (track add, clip move, note edit, FX add, automation points, transport). The realtime-safety contract in `AGENTS.md` is about what runs *inside* `processBlock` on the audio thread — and that contract is satisfied because the audio render path reads pre-cached, lock-free state (transport atomics, automation cache, FX params via the SPSCBridge, clip/note data fetched between blocks). MCP tool handlers follow the **exact same access patterns** the UI already uses:

- **Model mutations** → `setProperty` / `addChild` / `removeChild` on `ProjectModel`, wrapped in the `UndoManager` (so `Ctrl+Z` and the `undo`/`redo` tools roll them back).
- **Transport** (play/stop/seek, loop region) → `TransportManager` atomics + `TRANSPORT` ValueTree properties.
- **FX parameter changes** → **`SPSCBridge`** (`ParamUpdate`), the same UI→audio path the existing faders use — never a direct audio-thread poke.
- **Plugin add/remove/bypass** → engine facade (`rebuildTrackFX` / `rebuildRoutingGraph`), main-thread, as today.
- **Phrase generation** → `PhraseGenerator` (pure CPU) → writes to a `MIDI_NOTE_LIST` on the main thread.
- **Clip→track re-parenting** (via `move_clip` with a new `trackId`): the clip is moved in the `ProjectModel` (`CLIP_LIST` parent changes). Because `ClipItem::getColor()` already resolves the parent track's color at paint time, the clip's rendered color updates automatically — no extra step.

The recent `FXSlotRow` lock-free ring (the audio→UI direction) is the *complement* of this design, not a conflict: it handles the audio thread notifying the UI of parameter changes, while MCP handles the UI (or a headless server) pushing parameter changes toward the audio thread via `SPSCBridge`. Together they keep both directions realtime-clean.

### 4.3 Long-running work: `export_audio` (#36)

Rendering offline can take seconds to minutes. `export_audio` runs on a **dedicated worker thread** (a `QThread` owned by `McpServer`, started/stopped with the server). To keep model access simple, the worker **posts "render next block" requests to the main thread** in a tight loop, the main thread drives one block's worth of offline render between event-loop iterations, and the worker accumulates the output. A `std::atomic<bool> cancelRequested` is set by the `notifications/cancelled` handler; the worker checks it each iteration. On completion (or cancellation) the worker posts the result back to `McpServer`, which returns it to the client. The export thread is the only place that does any heavy CPU; MCP stays responsive throughout.

*Note on the §4.1 rule:* the `export_audio` **handler** still runs on the main thread — it is the handler that starts the worker, arms the cancel flag, and collects the result. The handler does no heavy render work itself; the worker does. So "every tool handler runs on the main thread" holds in the strict sense (the handler is invoked on the main thread and has direct engine access), while the expensive CPU is correctly offloaded.

### 4.4 Headless mode threading

The trivial case: main thread runs the engine + server + (optional) export worker. No GUI, no Qt event-loop contention beyond the server's own I/O. The audio device is optional (see non-goals). Transport state still updates; phrase generation works; export works.

### 4.5 Hard rules for handler authors

1. **Never touch the audio thread.** No `processBlock`, no `MidiClipProcessor`, no `ClipSourceProcessor`, no `AutomationManager` writes from a callback.
2. **Never allocate or lock** on any path the audio thread could observe. (MCP is main-thread, so this is automatic — but if you ever offload work, do it on a worker thread that doesn't share locks with the render path.)
3. **Never call `MessageManager::callAsync`** from anywhere that isn't the main thread. (You're always on the main thread; just don't.)
4. **Never bypass the `UndoManager`** for mutations. Every state change is undoable.

## 5. Configuration & UX

### 5.1 CLI flags

| Flag | Effect |
|---|---|
| `--mcp-stdio` | Force headless stdio-server mode (override the TTY auto-detect). |
| `--no-mcp` | Disable MCP entirely: no stdio auto-detect, no HTTP transport. |
| `--mcp-http-port=<N>` | Override the HTTP port for this launch. |

### 5.2 `QSettings` keys

Under the existing `HDAW/HDAW` org/app:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `mcp/httpEnabled` | bool | `false` | Whether the HTTP server auto-starts on launch. |
| `mcp/httpHost` | string | `127.0.0.1` | Bind address. (Non-loopback is refused at startup.) |
| `mcp/httpPort` | int | `8765` | Bind port. |

Stdio is auto-detected; no setting needed.

### 5.3 Tools menu (GUI mode)

- **Tools → "MCP HTTP Server"** — a *checkable* `QAction` that starts/stops the HTTP transport. Label flips between "Start MCP HTTP Server" and "Stop MCP HTTP Server". On start, show a status-bar message like `MCP HTTP server listening on 127.0.0.1:8765`. On failure (port in use), show a `QMessageBox` with the error and leave the action unchecked.

### 5.4 Preferences

A new **Preferences → MCP** page with:
- Bind host (default `127.0.0.1`, with a note: "Any address other than loopback requires authentication, which HDAW v0.3.x does not provide.").
- Bind port (default `8765`).
- "Auto-start HTTP server on launch" checkbox (writes `mcp/httpEnabled`).

## 6. Prerequisites

### 6.1 Model change: stable `noteID` on `MIDI_NOTE`

The MIDI-note tools (#21–24) need to identify individual notes (`noteId` for `set_note`, `remove_notes`, and as the return of `add_note`). `MIDI_NOTE` ValueTrees currently have no id — they're identified by list index, which is fragile.

Changes:

- Add `IDs::noteID` to the `IDs` namespace.
- Add `ProjectModel::allocateNoteID()` (atomic counter, mirroring `allocateClipID`).
- Call `allocateNoteID()` in every existing note-creation path:
  - `ProjectModel::createMidiNote` (default project clips).
  - `PianoRollModel::addNote` (piano-roll click; existing route).
  - `NoteGridWidget` (chord-stamp).
  - `StepEditorWidget::commitNote` (step sequencer).
  - `PhraseGeneratorDialog` (phrase insertion).
  - `MainWindow` MIDI-import path.
  - The new MCP handlers.
- Add `ProjectModel::scanAndSyncNoteIDs()` (mirror of `scanAndSyncClipIDs`) called on project load: walks `MIDI_NOTE_LIST` children, assigns ids to any note missing one, without losing order.

This is the only model-layer change the MCP surface requires. It is mechanical and consistent with the existing ID discipline.

### 6.2 Clip color follows track color (already in main)

`ClipItem::getColor()` already resolves the parent track's color (so clips adopt a new track's color when moved), and `TimelineScene` repaints clips when a track's color changes. The `move_clip` tool's re-parenting path relies on this. No additional change needed beyond ensuring all *new* clip creation paths go through the same track-color resolution (they do, by virtue of `ClipItem::getColor()`).

## 7. Security posture

- **Stdio** is local-by-construction: the client launches us as a child. No network exposure.
- **HTTP** binds to `127.0.0.1` only. The transport's `start()` hard-codes a refusal to bind elsewhere (a `QHostAddress` check before `QHttpServer::listen`).
- **No authentication in v1.** Acceptable for a developer tool running on a single machine.
- **Documented non-goal:** exposing the HTTP port beyond loopback is explicitly out of scope and would require a token scheme (a follow-up).

## 8. Testing

This is the project's **first automated test suite** (per `AGENTS.md`, the codebase is pre-test). The MCP module is the natural pilot.

**Framework:** **GoogleTest** via `FetchContent` in a new `tests/CMakeLists.txt`, registered with CTest. Layout mirrors the source:

```
tests/
  CMakeLists.txt
  unit/
    mcp/
      json_rpc_test.cpp
      schema_test.cpp
      tool_registry_test.cpp
      dry_run_test.cpp
      note_id_test.cpp
      transport_stdio_test.cpp
  integration/
    mcp/
      mcp_server_test.cpp
```

### 8.1 Unit tests

| File | Coverage |
|---|---|
| `json_rpc_test` | Parse valid/invalid requests, notifications (no `id`), success/error responses, framing round-trips. |
| `schema_test` | `required` / `type` / `enum` / `minimum` / `maximum` / `additionalProperties`; nested objects/arrays; precise error paths. |
| `tool_registry_test` | Register, lookup, `tools/list` shape, unknown tool → `isError` result, handler exceptions caught and converted to error results. |
| `dry_run_test` | Destructive tool with `dryRun: true` reports plan and mutates nothing; `dryRun: false` mutates and is undoable. |
| `note_id_test` | `allocateNoteID` uniqueness; `scanAndSyncNoteIDs` assigns IDs to legacy notes without losing order; persistence round-trips through project save/load. |
| `transport_stdio_test` | Feed newline-delimited JSON into a fake stdin (in-memory `QBuffer`); assert parsed `McpRequest`s come out and writes are newline-delimited JSON. |

### 8.2 Integration tests

`tests/integration/mcp/mcp_server_test.cpp` constructs `AudioEngine` + `McpServer` with the **`McpTransportLoopback`** (a test-only transport using two `QByteArray` queues instead of real I/O). It drives a realistic client sequence:

- `initialize` → assert `protocolVersion`, `serverInfo`, `capabilities.tools`.
- `tools/list` → assert it contains the expected v1 tool names.
- `tools/call get_project_summary` → assert content matches the empty/default project.
- `tools/call add_track` → assert the project now has a track with the requested properties and the assigned palette color; assert the call is undoable via `undo`.
- `tools/call remove_track` with `dryRun: true` → assert no mutation; with `dryRun: false` → track gone.
- `tools/call generate_phrase` → assert a new clip with the expected note count; assert the clip's track color is the host track's.
- Assert every tool runs on the main thread (capture `std::this_thread::get_id()` in a handler and compare to the test's thread id).

The loopback transport is the single most valuable test artifact: it makes the whole server testable without a real MCP client, a real audio device, or a GUI, and it doubles as documentation of the transport contract. Any future transport (real stdio, HTTP, or otherwise) implements the same interface and is interchangeable in tests.

## 9. Rollout / sequencing

The layers are independent, so the design supports phased delivery without rework:

1. **Framework + stdio + noteID prerequisite.** `src/mcp/` core, stdio transport, headless mode in `main.cpp`, the `MIDI_NOTE` `noteID` model change, `HDAW_LOG` tags, the loopback transport, and the first batch of tests. Delivers a working (if thin) MCP server in headless mode. This is the "skeleton that proves the design."
2. **Broad tool registration.** The remaining ~30 tools, each as a small handler + schema + unit/integration test. The framework carries them with no further design changes.
3. **HTTP transport.** `McpTransportHttp` (`QHttpServer`), Tools-menu toggle, Preferences page, `QSettings` keys. GUI mode only.
4. **Polish.** `export_audio` worker thread + cancellation, headless audio-device handling, README section with sample Claude Desktop / opencode `mcp.json` showing how to launch HDAW in stdio mode, and a follow-up note on authentication for non-loopback HTTP.

Each phase is shippable on its own and doesn't block the next.

## 10. Open questions / future work

- **HTTP authentication** for non-loopback exposure (token-based, v0.4+).
- **`resources/*` and `prompts/*`** — extend the registry pattern; no protocol-layer change.
- **Headless audio playback with a real device** — out of scope for v1.
- **SSE streaming of partial results** for long tools — single-shot responses suffice today.
- **Remote operation** — explicitly out of scope; v1 is local-only.

---

*End of spec.*
