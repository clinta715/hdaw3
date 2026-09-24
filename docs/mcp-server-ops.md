# HDAW MCP Server Operations

Operational MCP-server reference, moved verbatim from
[`testing-mcp.md`](testing-mcp.md) on 2026-09-24. The testing half
(gtest suite, environmental-failure catalog, deprecated frontend tests)
stayed in `testing-mcp.md`.

## MCP server (v0.3.x)

A new `src/mcp/` module exposes HDAW as an **MCP** (Model Context
Protocol) server so an LLM client (Claude Desktop, opencode, etc.)
can drive the DAW. 36 tools cover transport, tracks, clips, MIDI notes,
composition (`PhraseGenerator`), FX, automation, undo, and audio export.

### Engine binary update flow (`engine_info` / `engine_restart`)

Rebuilding `HDAW_headless.exe` does not update a **running** engine — the
process keeps executing the old image (lesson 21: a stale binary looks
healthy and answers every RPC, but contains none of the fixes). The update
flow is: rebuild → `engine_info` with `buildBinaryPath` set to the fresh
binary (returns `stale: true` when the build tree is newer than the running
process) → `engine_restart`, which refuses while an export renders (never
silently cancels a long render; override with `force: true`) and then
schedules `QCoreApplication::exit(42)` 300 ms after the tool response is
flushed — exit code 42 means *intentional restart*, and `mcp-launch.bat`
propagates it, re-copies the fresh binary, and size-verifies the copy before
relaunching. The MCP client must reconnect afterwards (`mcp.reload` or a
launcher relaunch). Both tools are read-only/restart-only by contract:
`engine_info` never mutates, and `engine_restart` performs no engine-side
cleanup — the normal shutdown path owns AudioEngine teardown.

- **Two transports**, both behind the `Transport` interface
  (`src/mcp/McpTransport.h`): `McpTransportStdio` (newline-delimited
  JSON over `stdin`/`stdout`, with a dedicated reader thread that
  posts requests to the server via `Qt::QueuedConnection`) and
  `McpTransportHttp` (Streamable HTTP, configurable host/port via Preferences
  `mcp/httpHost` / `mcp/httpPort`, defaults to `127.0.0.1:18765`, no auth).
  `McpTransportLoopback` is the in-memory test transport.
- **Tool safety**: every destructive tool (`remove_*`, `clear_notes`,
  `duplicate_clip`, `export_audio`) accepts `dryRun: true` and reports
  what it would do without mutating. Every mutation goes through the
  `UndoManager` so `undo` / `redo` tools (or the GUI's `Ctrl+Z`) can
  roll it back. `notifications/cancelled` sets a `std::atomic<bool>`
  cancel flag (`McpServer::isCancelRequested()`); the spec's
  worker-thread follow-up will poll this for cancellable exports.
- **Tool-execution errors are not JSON-RPC errors.** Per the MCP
  contract, a tool that runs but fails returns
  `{isError: true, content: [{type:"text", text:"…"}]}` in a SUCCESSFUL
  JSON-RPC response. JSON-RPC errors (`{code, message}`) are reserved
  for parse/validation/method-not-found failures. `McpServer::dispatchRequest`
  in `src/mcp/McpServer.cpp` is the single dispatch path used by both
  the stdio transport (via the `handleRequest` slot) and the HTTP
  transport (directly, synchronously).
- **Every tool runs on the main thread.** This is the same
  single-thread rule as the rest of the project: tools access the
  engine/model directly without locks, and the audio thread is
  never touched. Audio-thread concerns (e.g. plugin parameter
  changes) are the tool handler's responsibility — use `SPSCBridge` for
  audio-thread notifications, as documented in `docs/realtime-safety.md`
  (the SPSC paramID contract).
- **Spec / plan** documents: `docs/archive/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md`
  and `docs/archive/superpowers/plans/2026-06-29-hdaw-mcp-server-phase{1,2}.md`.

## File Browser Audio Preview (v0.9.2)

The file browser (`frontend/src/components/FileBrowser.tsx`) supports
audio preview at project tempo. The preview uses the engine's
`AudioPreviewPlayer` via the `preview.*` RPC namespace.

**RPC methods** (defined in `src/frontend/FrontendRouter.cpp`):
- `preview.load` — load an audio file for preview
- `preview.play` / `preview.stop` — playback control
- `preview.setVolume` — volume (0–1)
- `preview.setTempoMatch` — enable/disable with source BPM
- `preview.setProjectBpm` — set the target project tempo
- `preview.isPlaying` — poll playback state

**UI**: Each audio file row shows a ▶ button on hover. Clicking it
loads and plays the file. The preview bar at the bottom of the browser
has play/stop, volume slider, "Tempo Match" checkbox (enabled by
default), and a source BPM input. The file plays at the project tempo
when tempo match is on.

**Architecture**: `AudioEngine` owns an `AudioPreviewPlayer` instance
(lazy-initialized in `initialize()`). The player uses the same
`AudioDeviceManager` as the main engine but routes through its own
`AudioSourcePlayer` to avoid interfering with the main audio graph.
The player does not apply time-stretching — tempo matching adjusts
playback rate (pitch changes with speed).

## Which MCP transport is live — check this before blaming a timeout

There are **two** independent ways hdaw's tools reach an agent, with **different
timeouts**. Diagnose with the error the harness prints (`server:` / `transport:`),
never by assumption:

| Transport | Server name | Config that owns it | Client timeout |
| --- | --- | --- | --- |
| **stdio direct (preferred, 2026-09-24)** | `hdaw` | repo-root `.mcp.json` → `mcp-launch.bat` → engine (pi spawns + owns the process) | per-server `timeout: 900000`; **OMP `settings.json` sets `mcp.startupTimeoutMs: 0`** (wait until connections settle — the copy-on-launch + tool enumeration exceeds the 250 ms default discovery window) |
| HTTP (fallback, engine must already be running) | `hdaw-http` | repo-root `.mcp.json` → `http://127.0.0.1:18765/mcp` | per-server `timeout`, else **OMP's 30 s default** |
| stdio proxy (pi chain, legacy) | `hdaw` | `~/.config/lazy-mcp/servers.json` → `mcp-launch.bat` → engine | lazy-mcp `requestTimeout`, else **10 000 ms** |

- **Why direct stdio (2026-09-24):** the HTTP entry needs an engine ALREADY serving on 18765
  (`mcp/httpEnabled=true` in QSettings) *and* the agent session must start after that engine — pi
  reads `.mcp.json` once at startup with a ~250 ms discovery window (`mcp.startupTimeoutMs`), so a
  cold `mcp-launch.bat` spawn can never make the HTTP deadline and the tools silently never appear.
  The direct stdio entry makes pi the engine's parent: `mcp-launch.bat`'s copy-on-launch guarantees
  fresh binaries, the engine's lifetime is the session's lifetime, and no pre-running HTTP server is
  needed. `mcp.startupTimeoutMs: 0` (OMP `settings.json`, `~/.omp/agent/settings.json`) makes the
  discovery wait until connections settle. Both entries can coexist in `.mcp.json` — the stdio one is
  the one that works without a pre-running engine. Takes effect on the NEXT agent session (pi reads
  the config at startup).

- The HTTP path does **not** go through lazy-mcp at all — editing
  `~/.config/lazy-mcp/servers.json` cannot change its behaviour. The stdio path is
  what `mcp-launch.bat`, `%TEMP%\hdaw_crash_captures\...\procdump.log`, and the
  exit-code forensics below are about.
- **Applied 2026-09-22:** `.mcp.json` now sets `"type": "http"` + `"timeout": 900000`
  on `hdaw-http`, so a long but healthy call (full render, plugin warmup) is not cut
  at 30 s; and `~/.config/lazy-mcp/servers.json` regained the documented
  `requestTimeout: 900000` + `healthMonitor.idleTimeout: 0` (it had been lost — the
  file's mtime predated the 2026-09-15 fix).
- **Never set `timeout: 0`.** It disables the client-side deadline completely, and
  HTTP/SSE carry **no socket-idle timeout** — an engine that accepts the connection
  and then stalls would block the agent indefinitely. Prefer a bounded value.
- `OMP_MCP_TIMEOUT_MS` overrides every per-server `timeout` process-wide (set it in
  the launching environment when you need a different budget without editing a
  committed file).
- After editing either file: `/mcp reload` (or a new session) — a config change is
  not picked up mid-flight. Verify with `jq` **and** by watching whether a
  deliberately long call is cut, since a config the running client never re-read is
  indistinguishable from one that was ignored.

### Long calls: the engine keeps working after the socket drops

Observed 2026-09-22 while driving the engine directly over HTTP (no harness in the
path): calls that run for minutes return `RemoteDisconnected` — the client's response
socket is closed — **while the engine is unaffected** (same pid, port still open).
Three examples: `export_audio {wait:true}` on a 303 s render (the WAV was written in
full and the export job completed), and two `auto_gain_tracks` batches (the faders
were staged; only the reply was lost). So:

- Prefer the async form where one exists (`export_audio`, `mix_report`,
  `analyze_tuning` accept `wait:false` → poll `poll_job`).
- After a drop, **re-read state before retrying** — the mutation very likely applied.
  A blind retry double-applies it (the same reason lesson 29 says never blind-retry a
  timed-out call). (The *server-side* dropped-response mechanism behind the 2026-09-22
  observations is fixed — `TransportHttp::start` now sets a 900 s keep-alive instead of
  Qt's 15 s default, so a long synchronous rebuild no longer discards its buffered
  response; see `docs/composition-toolkit.md` and
  `HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900`. The re-read rule stays as
  general client hygiene — a socket can still drop for reasons outside the server.)
- Do not run `save_project` concurrently with an export: on 2026-09-22 a render that
  had reported "export complete" was gone from disk when the save ran alongside it.
  Save between mutation groups, after the export job reports finished.

### ~~`export_audio` reports success and writes NOTHING if the output directory is missing~~ — FIXED 2026-09-23

**Fixed in two halves.** (1) `ExportManager` now creates the output **directory** before opening
the stream (the house pattern `AudioRecorder.cpp:21` already used), and a stream that cannot be
opened for *any* reason (permissions, path-is-a-directory, locked file) now sets `success = false`
instead of relying on a function-scope initializer 300 lines away. (2) The **actual root cause of
the silent `success: true`**: `McpExportTool` reported success **unconditionally** after
`waitForIdle()` — both the `wait:true` path and the async McpJobs path — regardless of how the
export went. Both now read `em.getLastExportMessage()` (the same check
`AudioEngineCommands::renderTrackWindow` uses) and surface a failure through the tool's existing
`isError` mechanism.

Regression tests: `McpCoverageTest.ExportAudioCreatesMissingOutputDirectory` (exports into a
guaranteed-nonexistent temp subdirectory; fails pre-fix) and
`McpCoverageTest.ExportAudioStreamOpenFailureIsToolError` (the output path IS an existing
directory, so `createDirectory` passes but the stream cannot open it; asserts `isError == true`
and that the target is not clobbered).

Historical record of the bug as it stood — two full 300 s renders were lost to it before the
cause was found:

- `dub_embers`: the first full render reported success; `ls` showed only `brief.json`.
  Re-submitting later (after the folder existed) worked.
- `aether_dub`: a stem export to a not-yet-created `compositions/aether_dub/` produced no
  file; the **identical** export after the folder existed wrote 7,891,298 bytes (27.4 s).

Practical rules: **create the song folder before the first export** (writing the brief
first is enough), and treat "success + no file" as this bug rather than re-timing the
render. This also costs real time — two full 300 s renders were lost to it before the
cause was found.

## The engine "crashes" during MCP sessions — lazy-mcp lifecycle knobs

The **stdio** path runs through **lazy-mcp** (`~/.pi/agent/mcp.json` →
npx lazy-mcp → `~/.config/lazy-mcp/servers.json` → mcp-launch.bat → the
engine). lazy-mcp has two lifecycle defaults that silently kill the
engine process, and each relaunch starts a FRESH EMPTY project:

- **`requestTimeout` default 10 000 ms** — any tool call longer than 10 s
  (`export_audio` with `wait:true`, a full-length `mix_report`) makes
  lazy-mcp discard the connection; the engine keeps rendering on its
  worker thread and exits abnormally (exit code 1) when the response can
  no longer be delivered, or is killed outright. Symptom: the adapter
  reports `Server timeout (10s)` and the next call relaunches an empty
  engine (a "silent export" from a fresh engine is this exact bug).
- **`healthMonitor.idleTimeout` default 300 000 ms (5 min)** — the engine
  is put to sleep (clean exit 0 on stdin EOF — correct stdio behavior)
  after 5 minutes without activity. Any pause longer than 5 minutes
  during a composition session loses the live project.

**Fix (applied 2026-09-15) in `~/.config/lazy-mcp/servers.json`**:

```json
{
  "requestTimeout": 900000,
  "healthMonitor": { "idleTimeout": 0 },
  "servers": [ { "name": "hdaw", "requestTimeout": 900000, ... } ]
}
```

**Verify the fix is actually present before trusting it.** On 2026-09-22 the live
`~/.config/lazy-mcp/servers.json` (mtime Sep 10, *before* this fix) contained only
the `servers` array — **neither `requestTimeout` nor `healthMonitor`** — so the
10 000 ms default was live again and every long call could still discard the
connection and relaunch onto an empty project:

```powershell
jq '{requestTimeout, healthMonitor}' "$env:USERPROFILE\.config\lazy-mcp\servers.json"
```

A `null` means the override is missing; re-apply the block above (it takes effect
on the next lazy-mcp start).

`idleTimeout: 0` is lazy-mcp's documented "legacy never-sleep mode" — the
echo-friendly default is right for most servers but wrong for a DAW
engine that holds live session state. Config changes take effect on the
next lazy-mcp start (a new pi session); procdump exit-code forensics live
in `%TEMP%\hdaw_crash_captures\engine_*\procdump.log` — exit 0x00000000
= the idle/EOF path (lazy-mcp lifecycle, not an engine bug), exit 0x1
after a long render = the response pipe died mid-render. Also relevant:
the saved `.hdaw` is the source of truth — after ANY engine relaunch,
`load_project` before doing anything else.
