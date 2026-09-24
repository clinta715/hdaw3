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
- **`McpServer.HttpRoundTrip` no longer binds a FIXED port (fixed 2026-09-23).**
  The test now starts `TransportHttp t(0)` (mcp_server_test.cpp:118) — an
  OS-assigned **ephemeral** port — so it can never collide with a live engine
  holding 18765. `TransportHttp::port()` returns the bound port after a successful
  `start()` on port 0 (asserted by `HttpTransport.StartStopLifecycle`,
  transport_http_test.cpp:36), and the round-trip URL is built from that port.
- **`McpServer.EngineSettingsStartMcpHttp` no longer binds a FIXED port (fixed
  2026-09-23 evening).** It used to drive `mcp/httpPort = 18766` through the real
  Preferences path, so it failed whenever a live engine held that port. The engine
  config now supports the ephemeral form end to end: `AudioEngine::setMcpHttpConfig`
  accepts `port == 0` while ENABLING (0 is never persisted — the bound port is read
  back from `TransportHttp::port()` after listen and THAT is stored and reported), so
  the test asks for 0, reads the resolved port and round-trips on it. Nothing else
  changed: a non-zero port behaves exactly as before, and disabling with 0 is still an
  error. **No test in the suite drives a fixed port any more**, so "free the port
  first" is no longer part of any failure diagnosis here.
- **A sandboxed agent shell denies a child process' writes OUTSIDE the working tree — and that
  looks exactly like broken save/export/settings (measured 2026-09-24).** When `hdaw_tests.exe` is
  launched by a sandboxed agent shell, the child's writes to `%TEMP%`, `%APPDATA%` and (observed)
  `QSettings` are lost or refused while the *shell itself* and the project directory stay fine.
  Symptoms, all at once, on a build where the code is innocent:
  - `cmds.saveProject(...)` returns false and stderr shows a stray `HDAW: Failed to open debug log
    file` (the same denial hits the log). A probe inside `ProjectSerializer::save` proves it:
    `File::create()` on `%TEMP%` → **`Access is denied.`**, on the repo directory → `ok`, from the
    same process.
  - preset/template tools fail with `failed to write template file:
    C:\Users\…\AppData\Roaming\HDAW\section-templates\…` (`McpServer.ApplySongPlan`,
    `SongPlan.TemplateRoundTripDoesNotApply`, `McpCoverageTest.FxChainPresetRoundTrip`,
    `McpCoverageTest.FxChainPresetRoundTrip`).
  - QSettings-backed tests fail as if the persisted state were nailed down: the test's own
    `remove()`/`setValue()` have no effect, so it reads the real machine values
    (`RaveSettings.UnsetConfigReturnsEmptyValuesAndDefaultTimeout` fails while the repo's
    `rave/models` keys are present and passes the moment they are deleted by hand;
    `FrontendServer.SettingsNamespaceExposesMcpHttpConfig` sees the machine's `mcp/httpEnabled`).
  - The affected set in one 12.8-minute run: `ProjectMetadata.*` (5), `SongCells.CellsPersistAcrossSaveLoad`,
    `BusSendRpcTest.ListBusesMatchesMcpAndTheSavedProject`, `McpCoverageTest.ExportAudioTrackIdsFiltersTracks`,
    `MatrixRpcParityTest.*` (5), `RaveSettings.*` (6), `FrontendServer.SettingsNamespaceExposesMcpHttpConfig`,
    `McpServer.ApplySongPlan`, `SongPlan.TemplateRoundTripDoesNotApply` — plus the standard
    `PluginIsolation.LargeStateRoundTripThroughProxy` solo-pass flake.
  **Before blaming a change, re-run the suite with the temp area INSIDE the working tree**:
  `set TEMP=D:\…\hdaw3\.tmp_suite & set TMP=%TEMP% & powershell -File run-tests-sharded.ps1 -Shards 4`.
  That converts the `%TEMP%` class (save/export/render) to green immediately — the same focused set
  went from 15 failures to **172/172**. The `%APPDATA%` (preset/template) and QSettings classes stay
  red in a sandbox: they are environmental, independent of any session's diff, and the way to confirm
  is that the failing test passes once its path/keys are made available (clear the keys by hand, or
  run outside the sandbox) — never by editing the test to expect the sandbox.
- **`RespawnPath.RealPathPassesThrough` — expectation is now platform-gated**
  (`tests/unit/proxy/crash_recovery_test.cpp:655`). It asserts that
  `PluginManager::resolveRespawnPath("/usr/lib/MyPlugin.clap", …)` returns
  that Unix-style path unchanged. That was a deterministic red on Windows:
  JUCE's `File::isAbsolutePath` rejects Unix-style paths there, so the
  resolver correctly returns empty (refusing to spawn a child with a
  foreign-platform path) — the test now runs the `C:\...` passthrough
  assert under `#ifdef _WIN32` and the `/usr/...` passthrough assert
  under `#else`, so it is no longer a known red. It does not involve any
  command/model code. The "1 flake" baseline recorded in `AGENTS.md`
  (2026-09-21) is stale — a full run on 2026-09-22 showed 4 failures, of
  which 3 clear when the port is free / the tests are run in a filtered
  order (2 of them `McpServer.*`) and the 4th is the documented
  `PluginIsolation.LargeStateRoundTripThroughProxy` flake. **Two full runs on
  2026-09-22 pin this down: with a dev engine holding 18765 → 4 failures (the two
  `McpServer.*` above included — that half is now historical: `HttpRoundTrip` binds
  an ephemeral port since 2026-09-23, only `EngineSettingsStartMcpHttp`'s fixed
  18766 remains); with the port free → exactly 2, the two
  pre-existing ones listed here. So the "1 flake" baseline in `AGENTS.md`
  (2026-09-21) is stale, and the two `McpServer.*` failures are an artifact of
  running the suite alongside a live engine, not a regression.**
- **HTTP runtime path coverage**: `McpServer.EngineSettingsStartMcpHttp`
  enables `mcp/httpEnabled` in `QSettings`, starts `AudioEngine` with the
  persisted config, and verifies a real `POST /mcp` round-trip on the
  loopback transport.

### Async routing-rebuild drain seam

Clip/track add/remove listeners in `AudioEngine` call
`triggerAsyncUpdate()`; the message pump thread later dispatches
`handleAsyncUpdate()` → `MainAudioProcessor::rebuildRoutingGraph()`,
which swaps the whole `RoutingManager`. Engine tests that mutate the
`ValueTree` from the test thread race that coalesced rebuild: a
pump-side rebuild can destroy the very `RoutingManager`/`Track` objects
that the test's synchronous mutations and live-processor reads touch —
a use-after-free window that fixed sleeps only paper over.

The seam is `AudioEngine::drainPendingRoutingRebuild()`
(`src/engine/AudioEngine.cpp`): it marshals
`AsyncUpdater::handleUpdateNowIfNeeded()` onto the message thread (the
flush is documented main-thread-only), where the rebuild takes its
already-serialized no-park path and the graph's `prepareToPlay`
topology pass (the render-sequence bake that prepares the live
processors) also runs synchronously. Delivery is exactly-once (the
`shouldDeliver` atomic exchange) against the pump's own dispatch, a
no-op when nothing is pending, and it blocks until the rebuild
completes.

**Rule:** engine tests that need a settled routing graph before
touching live processors (`getMainProcessor()`) MUST call
`engine.drainPendingRoutingRebuild()` — never a fixed
`juce::Thread::sleep()`. Caller contract: any thread except the
message thread, and must not hold a `MessageManagerLock`
(`callFunctionOnMessageThread` jasserts).

### Render-suite bake starvation under heavy preceding suites (2026-09-23)

`export failed: Render graph bake timed out after 15000ms` is a **budget** symptom,
not a correctness one. In the 2026-09-23 full gate matrix (563 tests / 42 suites →
545 passed, 3 skipped) **14 render/verify tests failed with exactly that message and
ALL passed in isolation** — a heavy preceding CLAP-suite run exhausts the render
bake's fixed 15 s budget floor. Example: `VerifyPart.ComposedPartPasses` renders in
**373 ms solo vs ~17 s in the same full run**. Do NOT attribute these failures to a
code change without a **solo confirmation** first.

**Rule:** when diagnosing render/verify failures, run those suites in a **lighter
batch or solo** — preceding suites can starve the bake budget even though the suite
is green on its own.

**Related self-inflicted hazard (same session):** starting a **second
`build-fast.bat` while one is already running** collides in the shared build
directory — `LNK1104: cannot open file 'hdaw_tests.exe'` plus `LNK4076 invalid .ilk`
(the two linkers fight over the same PDB/ILK and output exe). NEVER start a second
build while one runs; wait for the first to exit.

## Frontend Tests (v0.12.0+) — DEPRECATED (2026-09-23)

**DEPRECATED (2026-09-23):** the Electron frontend is a separate project as of this
date — the repo no longer builds or tests it. Do NOT run these suites (`npm test`,
`npm run test:watch`, `npm run test:coverage`, `npm run test:e2e`,
`npm run test:e2e:ui`); the commands and listings below are retained for reference.
Engine-only verification: `build/hdaw_tests.exe` (gtest) + the MCP surface.

The React frontend has a comprehensive test suite using **Vitest** for
unit/component tests and **Playwright** for E2E tests.

### Unit & Component Tests (Vitest) — **DEPRECATED (2026-09-23)**

- **Run**: `cd frontend; npm test`
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

### E2E Tests (Playwright) — **DEPRECATED (2026-09-23)**

- **Run**: `cd frontend; npm run test:e2e`
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

#### Plugin-isolation E2E needs a warm plugin cache

`e2e/plugin-isolation.spec.ts` exercises the isolated plugin-host crash /
auto-recovery path and therefore needs at least one **scanned external
plugin** (internal FX run in-process and never spawn a host). Two gotchas:

- **The default-mode engine does not auto-scan.** `HDAW.exe` (the mode the
  Playwright `webServer` launches) only calls `PluginManager::loadCache()`
  at startup (`src/main.cpp`); the background scan runs only in `--headless`
  mode or when triggered via the `plugin.scanAll` RPC. So with an empty
  cache, `plugin.getPlugins` returns `[]` forever and the test skips.
- **Warm the cache once** by running the scan: `build\Debug\HDAW.exe --headless`
  (set `HDAW_NO_BROWSER=1`) and wait for `%APPDATA%\HDAW\plugin_cache.xml`
  to gain `<PLUGIN ...>` entries, then stop it. Subsequent engine starts load
  the cache and the test runs. On a machine with no plugins / empty cache the
  test skips gracefully.

Also note the recovery semantics the test asserts: crash auto-respawn is driven
by `PluginManager::timerCallback()` (a 250 ms timer calling
`CrashRecoveryManager::tick()`), with a 500 ms grace then respawn. So after the
host is killed the crash banner (`.fx-slot-crash`, rendered only inside the
FX Chain panel) appears and then clears **by itself** once the slot respawns —
the test asserts banner-shown → host-respawned → banner-cleared, not a manual
Restart click. (`PluginManager::tick()` is dead code; the live path is the
timer callback.)

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
  audio-thread notifications, as documented in the next section.
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
| HTTP (current OMP harness) | `hdaw-http` | repo-root `.mcp.json` → `http://127.0.0.1:18765/mcp` | per-server `timeout`, else **OMP's 30 s default** |
| stdio proxy (pi chain) | `hdaw` | `~/.config/lazy-mcp/servers.json` → `mcp-launch.bat` → engine | lazy-mcp `requestTimeout`, else **10 000 ms** |

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
