# HDAW Testing & MCP Server Reference

Domain-specific documentation split from AGENTS.md.
For the original combined file, see `../AGENTS.md`.

Sections: GTest Suite, TransportLoopback Test Seam, MCP Server Architecture,
MCP Tool Safety, File Browser Audio Preview.

## Testing

The project has a gtest suite (added in v0.3.x via the `hdaw_lib`
static library split; see `tests/CMakeLists.txt` and the `HDAW_BUILD_TESTS`
option in the top-level `CMakeLists.txt`). The MCP module is the pilot —
its tests live under `tests/unit/mcp/` and `tests/integration/mcp/`.

- **Run all tests**: `ctest --test-dir build -C Debug --output-on-failure`
  (registers the `hdaw_tests` aggregate) — or run the binary directly:
  `build/Debug/hdaw_tests.exe`. Filter a single gtest sub-suite with
  the binary's `--gtest_filter=SuiteName.*` (e.g. `--gtest_filter=JsonRpc.*`).
  The project's CTest setup registers only the aggregate `hdaw_tests`
  target, not individual gtest sub-suites, so `ctest -R JsonRpc` does
  **not** work — use the gtest binary's filter instead.
- **Layout** mirrors the source path: `tests/unit/mcp/json_rpc_test.cpp`
  tests `src/mcp/McpJsonRpc.h`, `tests/integration/mcp/mcp_server_test.cpp`
  exercises the full server end-to-end. Filenames end in `_test.cpp`.
- **The `TransportLoopback` is the test seam** for the MCP server. It
  uses in-memory `QByteArray` queues and a `pumpIncoming` /
  `waitForOutgoing` API, so the full JSON-RPC protocol can be exercised
  without real stdin/stdout, sockets, or an audio device. Any future
  transport implements the same `Transport` interface and is
  interchangeable in tests.
- **Determinism**: never `sleep()` or use timed waits for
  synchronization — use `QSignalSpy::wait()`, the loopback's
  `waitForOutgoing` (bounded timeout), or condition variables.
- **Temp directories**: keep unique per test (e.g. via `QStandardPaths::TempLocation` + a UUID), clean up in teardown.
- **Known test-infra note**: `McpServer.HttpRoundTrip` is order-sensitive
  in the test binary — it must run **first** in the `McpServer` suite
  because the JUCE WASAPI audio-device teardown from earlier tests
  leaves process-wide COM/WinHTTP state that a subsequent HTTP test
  cannot recover from. Single-run is stable; `--gtest_repeat` is
  flaky at the start of iteration 2. A comment in the test file
  documents this. A future fix is to make the test order-independent
  (e.g. by isolating the audio device).

## Frontend Tests (v0.12.0+)

The React frontend has a comprehensive test suite using **Vitest** for
unit/component tests and **Playwright** for E2E tests.

### Unit & Component Tests (Vitest)

- **Run**: `cd frontend && npm test`
- **Watch mode**: `npm run test:watch`
- **Coverage**: `npm run test:coverage`
- **Config**: `frontend/vitest.config.ts` (jsdom environment)
- **Setup**: `frontend/src/test/setup.ts` (localStorage mock, jest-dom matchers)

**Store tests** (`src/store/*.test.ts`):
- `transportStore.test.ts` — transport state defaults, updates
- `uiStore.test.ts` — clip selection, clipboard, snap, tabs (9 tests)
- `projectStore.test.ts` — snapshot, track/clip lookup, file path (7 tests)
- `notifyStore.test.ts` — toast push/dismiss/clear, auto-dismiss timers,
  `reportRpcError` helper (14 tests)
- `meterStore.test.ts` — master/track meter updates (3 tests)
- `browserStore.test.ts` — folders, favorites, localStorage persistence,
  expanded paths, search (16 tests)

**Component tests** (`src/components/*.test.tsx`):
- `StatusBar.test.tsx` — renders BPM, sample rate, selection count,
  recording indicator, track name (6 tests)
- `Toaster.test.tsx` — toast rendering, level classes, dismiss button,
  auto-dismiss timer (9 tests)
- `BottomTabs.test.tsx` — tab switching, active class, controlled/uncontrolled
  mode, onTabChange callback (11 tests)

Total: **~78 frontend tests** covering all Zustand stores and key UI components.

### E2E Tests (Playwright)

- **Run**: `cd frontend && npm run test:e2e`
- **Interactive UI**: `npm run test:e2e:ui`
- **Config**: `frontend/playwright.config.ts`
- **Tests**: `frontend/e2e/*.spec.ts`

E2E tests require a running `HDAW.exe` instance (the engine serves the
frontend on port 8765). The Playwright config includes a `webServer` block
that can auto-start the engine, but typically you'll run the engine
separately and use `reuseExistingServer: true`.

Sample E2E tests (`e2e/app.spec.ts`):
- App loads at `http://127.0.0.1:8765`
- Transport bar, timeline, track headers, status bar render
- BPM and sample rate display
- Play/stop buttons visible

## MCP server (v0.3.x)

A new `src/mcp/` module exposes HDAW as an **MCP** (Model Context
Protocol) server so an LLM client (Claude Desktop, opencode, etc.)
can drive the DAW. 36 tools cover transport, tracks, clips, MIDI notes,
composition (`PhraseGenerator`), FX, automation, undo, and audio export.

- **Two transports**, both behind the `Transport` interface
  (`src/mcp/McpTransport.h`): `McpTransportStdio` (newline-delimited
  JSON over `stdin`/`stdout`, with a dedicated reader thread that
  posts requests to the server via `Qt::QueuedConnection`) and
  `McpTransportHttp` (Streamable HTTP, configurable host via Preferences
  `mcp/httpHost`, defaults to `127.0.0.1`, no auth). `McpTransportLoopback` is the in-memory test transport.
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
  audio-thread notifications, as documented in the next section.
- **Spec / plan** documents: `docs/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md`
  and `docs/superpowers/plans/2026-06-29-hdaw-mcp-server-phase{1,2}.md`.

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
