# HDAW Testing Reference (gtest suite)

Domain-specific documentation split from AGENTS.md.
For the original combined file, see `../AGENTS.md`.

The MCP-server half of this file moved verbatim to
[`mcp-server-ops.md`](mcp-server-ops.md) on 2026-09-24: MCP server architecture
and tool safety, engine binary update flow, file browser audio preview, the
live-transport timeout table, and the lazy-mcp lifecycle knobs. This file keeps
the gtest suite, the environmental-failure catalog, and the deprecated frontend tests.

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
  **Before blaming a change, re-run the suite with the temp area INSIDE the working tree** — and set
  BOTH vars to the literal path:
  `cmd /c "set TEMP=D:\…\hdaw3\.tmp_suite&& set TMP=D:\…\hdaw3\.tmp_suite&& powershell -File run-tests-sharded.ps1 -Shards 4"`.
  **The harness now does this itself (2026-09-25).** `tests/test_main.cpp` probes the default temp
  dir once before anything else in `main`; when the probe (write+read+delete) fails it redirects
  `TMP`/`TEMP` to `<repo>/.tmp_tests` in both the CRT and Win32 process environments and prints
  `[test_main] temp dir redirected to … (default unwritable: …)`. So this class no longer needs a
  per-run override — `HDAW_TEST_TMP` overrides the choice if you want a specific dir. Measured:
  `./build/hdaw_tests.exe --gtest_filter='VerifyPart.*'` went from **9/13 failing** with
  `export failed: Could not create output file` to **13/13 PASSED** with no external env at all.
  `set TMP=%TEMP%` inside the same cmd line is a **trap** (measured 2026-09-24): cmd expands `%TEMP%` at
  parse time, so TMP keeps the sandbox-denied path and the save/load class stays red — 8 unrelated
  failures (`SongCells.CellsPersistAcrossSaveLoad`, 6× `ProjectMetadata.*`,
  `BusSendRpcTest.ListBusesMatchesMcpAndTheSavedProject`), all green once TMP is set explicitly.
  That converts the `%TEMP%` class (save/export/render) to green immediately — the same focused set
  went from 15 failures to **172/172**. The `%APPDATA%` (preset/template) and QSettings classes were
  the remaining two; **the harness now covers them too (2026-09-25)**:
  - `%APPDATA%` = `File::getSpecialLocation(userApplicationDataDirectory)` =
    `SHGetSpecialFolderPathW(CSIDL_APPDATA)` (registry `%USERPROFILE%\AppData\Roaming`, NOT the
    `APPDATA` env var) — `build/_deps/juce-src/modules/juce_core/native/juce_Files_windows.cpp:721`
    → `:146`. shell32 resolves that folder set on FIRST use and never re-reads the env afterwards, so
    the harness rewrites `USERPROFILE`/`APPDATA`/`LOCALAPPDATA` to `<repo>/.tmp_tests/userdata` as the
    very first statements of `main` (before `ScopedComInit`/the pump thread/`QCoreApplication`) and
    prints `[test_main] user data dir redirected to …`. `HDAW_TEST_USERDATA` overrides the profile root.
  - QSettings is NativeFormat (registry `HKCU\Software\HDAW\HDAW`) because the engine never sets a
    format/path (`src/engine/AudioEngine.cpp:198,345,445`, `src/engine/RaveService.cpp:46`,
    `src/engine/PluginManager.cpp:78`, `src/frontend/router/Router_Audio.cpp:293`, …). The harness
    sets `QSettings::IniFormat` + `QSettings::setPath(…)` = `<repo>/.tmp_tests/settings/<pid>`
    (User- and SystemScope) for the TEST PROCESS before any `QSettings` exists and prints
    `[test_main] settings store = …`. Production entry points (`src/main.cpp`,
    `src/main_headless.cpp`) are untouched; the switch is process-wide, so engine and tests still
    share one settings mechanism.
  Measured 2026-09-25: the settings filter
  (`RaveSettings.*:FrontendServer.SettingsNamespaceExposesMcpHttpConfig:McpServer.EngineSettingsStartMcpHttp`)
  is **13/13 PASSED** (was 8 red) and
  `SongPlan.TemplateRoundTripDoesNotApply:McpServer.ApplySongPlan:McpCoverageTest.FxChainPresetRoundTrip`
  is **3/3 PASSED** (was 3 red). `TransportSurface.StartStopRecording` also passes now (3/3 solo):
  its recorder targets `@userApplicationDataDirectory/HDAW/recordings`
  (`src/engine/MainAudioProcessor.cpp:376`), so the first classification below ("no capture endpoint")
  was really this path denial on this box. `VerifyPart.*` stays 13/13.
- **Two more environmental classes measured on 2026-09-24 full runs (do not chase either):**
  1. **No capture endpoint → `TransportSurface.StartStopRecording` fails deterministically.**
     This box exposes exactly one audio endpoint (RDP "Remote Audio", playback only). `beginActualRecording`
     passes `getTotalNumInputChannels()` (0) to the recorder → `juce::WavAudioFormat::createWriterFor`
     returns null → `isRecording()` stays false. Solo-fails consistently (~270 ms). It needs a real
     input device, like the deviceless pattern above. **(2026-09-25 update: with the harness
     user-data redirect this test PASSES (3/3 solo) — `beginActualRecording` writes
     `@userApplicationDataDirectory/HDAW/recordings` (`src/engine/MainAudioProcessor.cpp:376`), so on
     this box the denial was the path, not the endpoint. The no-endpoint failure remains possible on a
     box with no route at all.)**
  2. **A `FrontendServer.*` shard can cascade on "server failed to bind".** The shard's first
     FrontendServer test that fails to bind port 0's listener (or loses the WS handshake under load)
     turns every later `client.connect(...)` in that shard red (`server failed to bind` /
     `client.connect → false`), which can look like 16+ unrelated failures. Signature: failures cluster
     in one shard, all show `frontend_server_test.cpp:158 server->start(0)` false or a connect failure,
     and the same tests pass solo or in a different shard split. Re-run the FrontendServer tests solo
     before believing any of them.
  With both classes accounted for, a clean reference run (2026-09-24, post ledger-close) is:
  **1971 tests / 285 suites — 1952 passed, 39 skipped, 11 failures**, all environmental:
  6× `RaveSettings.*` + `FrontendServer.SettingsNamespaceExposesMcpHttpConfig` (QSettings/machine state),
  `McpServer.ApplySongPlan` + `SongPlan.TemplateRoundTripDoesNotApply` + `McpCoverageTest.FxChainPresetRoundTrip`
  (`%APPDATA%` writes), and `TransportSurface.StartStopRecording` (no capture endpoint).
  `PluginIsolation.LargeStateRoundTripThroughProxy` did NOT fire in that run (it remains a known flake).
- **Clean reference baseline (2026-09-24, post ledger-close): 1971 tests / 285 suites —
  1952 passed, 39 skipped, 11 failures, all environmental** (exact failure list in the
  bullet above; `docs/build-and-testing.md` defers its baseline counts here). Compare any
  later full run against this.
- **Full-suite 4-shard measurement (2026-09-25, pre-widened-harness):**
  `powershell -File run-tests-sharded.ps1 -Shards 4` over the WHOLE suite took
  **1398 s (23.3 min)** and reported **1957 passed, 12 unique failures**. Sharding is measured
  **NOT** to multiply failures — all 12 reproduce SOLO on this box, so they are environmental, not
  contention. Census by class: **3 persistence** (`%APPDATA%` preset/template writes:
  `SongPlan.TemplateRoundTripDoesNotApply`, `McpServer.ApplySongPlan`,
  `McpCoverageTest.FxChainPresetRoundTrip`) + **8 settings-backed** (6× `RaveSettings.*`,
  `FrontendServer.SettingsNamespaceExposesMcpHttpConfig`, `McpServer.EngineSettingsStartMcpHttp`) +
  **1 deviceless/env** (`TransportSurface.StartStopRecording`). The widened harness isolation (temp +
  user-data root + isolated per-process QSettings INI store, bullets above) makes classes 1 and 2
  (11 tests) green.
- **Intermittent CLAP bake-bed dropout (measured 3× on 2026-09-25 during
  PsyDub tail-polish iteration).** In a windowed render, one 4-beat window
  occasionally comes out at −89..−96 dBFS — the smooth exponential decay is
  missing and the pad bed is absent — while the identical binary and
  arrangement rerun clean (one observed rerun even half-recovered at −44.49).
  Seen in candidate-A run 1 (beats 628-632 at −89.65) and once in the freeze
  full run (−95.8 in a single window); it sits outside all gates. **Before
  blaming a mix/arrangement change, re-render just the affected window.**
  Root cause unknown; suspects are the render-sequence bake race or an
  isolated-CLAP child dropout under load.
- **Shard runner counts UNIQUE failures (fixed 2026-09-25).** gtest prints each failing test twice
  (timed `[  FAILED  ] Name (123 ms)` plus a bare `[  FAILED  ] Name` in the summary list) and the
  `[  FAILED  ] N test(s), listed below:` counter is not a test name; summing raw lines reported the
  12 unique failures above as "28 failed". `run-tests-sharded.ps1` now totals the unique timed names
  and prints `TOTAL: <n> passed, <n> unique failed (raw FAILED lines: <n>)`; the per-shard detail lines
  keep the raw counts. Measured: `-Shards 2 -Filter "BusSendRpcTest.*"` prints
  `TOTAL: 32 passed, 0 unique failed (raw FAILED lines: 0)`.
- **Twin parity suites (landed with the 2026-09-24 parity + B3 waves)** — each drives the
  SAME scenario through the MCP tool AND the frontend JSON-RPC route and asserts identical
  payloads / failure texts:
  - `FmLibraryParityTest` (`tests/unit/frontend/fm_library_parity_test.cpp`) — FM sysex
    import / FM patch load persists exactly like the tool through the
    `audio.fm_synthImportSysex` / `audio.fmSynthLoadPreset` routes (cartridge and
    4097-byte VMEM banks, identical failure texts), plus `add_library` patch-type parity.
  - `CapabilityRouteParityTest` (`tests/unit/frontend/capability_route_parity_test.cpp`) —
    the shared-`src/common/` surfaces (master FX params, clip takes, FM synth state,
    sub-synth sysex import, preset apply/audition, plugin preset files) answer on the
    route with the tool's payload.
  - `MissingRouteParityTest` (`tests/unit/frontend/missing_route_parity_test.cpp`) —
    routes that had NO implementation before the wave (automation preset, movement plan,
    master-FX writes, place_patterns, scale_note, session clip states) now return the
    tool's own payload/failure text.
  - `DurableRefMigration` (`tests/unit/engine/durable_ref_migration_test.cpp`) — B3
    stable ids: legacy string-ref projects migrate through the REAL save/load path,
    re-save is byte-stable, send-target PIDs survive, and loading an already-migrated
    file is idempotent.
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
- **`PluginIsolation.LargeStateRoundTripThroughProxy` — ROOT-CAUSED AND FIXED (2026-09-24,
  commit e632738).** It was never load flakiness: a `runLifecycleOnMessageThread` marshal
  timeout in the plugin host's GET_STATE handler was answered as `result=1, size=0`
  (indistinguishable from a legitimately empty state), and the parent's
  `getStateInformation` had no retry. Fix: honest failure signaling (`result=0` on marshal
  timeout, GET and SET paths), a 3-attempt parent handshake, plus two latent safety bugs found
  on the way (a use-after-free in the marshal-lambda lifetime, a dangling `&block` capture, and
  `ProxyPipe` poisoning `connected=false` on a bounded-receive timeout). 3 deterministic
  failure-path tests added (`SlowStateTimeoutSignalsFailure`, `GetStateRetriesAfterWrongTypeResponse`,
  `GetStateRetriesWhileChildBusyInSetStateMarshal`); verified 5× solo + 5× under parallel CPU load
  (10/10 each round) and `PluginIsolation.*` 49/49.
- **`HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900` test-side UAF — ROOT-CAUSED AND FIXED
  (2026-09-26); this is the cause of the intermittent shard-death class.** Symptom: SEH
  `0xc0000005` in `HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900`
  (`tests/unit/mcp/transport_http_test.cpp`), ~1/17 idle, which under load cascaded into killing a
  whole 855-test shard (an earlier full run lost a shard and showed 2 failures — both this same
  UAF). Root cause: test declaration order — `QTcpSocket` was declared before the `QEventLoop` it
  captured, so `~QTcpSocket` → `disconnected` → `loop.quit()` ran on a destroyed loop. Fix:
  declaration order plus an explicit `QObject::disconnect` teardown. Evidence: 25/25 repeat green,
  and both later full runs completed every shard. The intermittent shard-death class (a whole test
  shard lost mid-suite, `INCOMPLETE SHARDS … (ran <x> of <y> intended tests)`, no crash artifact —
  previously unclassified in `docs/build-and-testing.md`) has been tracked down to this test-side
  UAF and has not recurred since the fix.
- **DISPROVEN (2026-09-25) — "exports 3+ in one session ignore live tree changes" was an audit
  artifact, not an export bug.** The 2026-09-24 stem audit soloed tracks with
  `cmds.setTrackVolume(t, 0)` (a static fader write to `IDs::volume`), but the project has
  ENABLED paramID-1 `Volume` automation on `bass/growl/stab/lead/hat/pad`, and an enabled lane
  rewrites the parameter EVERY BLOCK in the offline render (`src/engine/Track.cpp:557-561`,
  lanes installed by `RoutingManager` during the offline rebuild). Every enabled lane
  evaluates to exactly 1.0 across the audit window (beats 208-240) by construction:
  `getValueAtTime` returns `points.front().second` for any time before a lane's first point
  (`src/engine/AutomationManager.h:59-77`), and these Volume lanes' first point is
  (beat 672, 1.0). So each such lane re-opened a track the audit had tried to silence,
  and the audited stems are overlapping, highly correlated mixes
  (measured 0.94-0.97 zero-lag correlation). `kick` carries no Volume lane, so it alone honoured
  the fader. Automation-wins-over-fader is the intended semantics; no stale export-graph reuse
  was reproduced — `ExportManager::startExport` deep-copies the tree per call and builds a
  fresh graph, and the probe/recipe runs below all re-read the live tree (the audit run's own
  log shows 18 × "render finished success=1 message=Export complete."). Verified 2026-09-25 by
  three controlled multi-export probes that all isolated correctly — mute soloing ×5 exports,
  `setTrackVolume` ×5, `setTrackVolume` + a real isolated
  `__passthrough__` child ×4 — plus the historical audit recipe restored in the current
  v3 PsyDub project, which produced six DISTINCT stems: kick rms 0.3195 vs the five automated
  synth stems 0.1855-0.1856, mutually correlated 0.94-0.97).
  **Audit rule:** isolate with mute (`setTrackMuted`) or disable the track's Volume automation
  on the offline copy — never `setTrackVolume` on a track with an enabled Volume lane.
  Regression pins (passing 2026-09-25): `ExportVolumeBypass.VolumeAutomationOverridesTreeFader`
  (automation on vs off = 162.7 dB apart on rendered RMS) and
  `ExportVolumeBypass.MultiExportRereadsLiveTree` (exports #1 and #3 match to <1% RMS while the
  muted #2 sits 12.5 dB down).
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

### Parallel agent slices: build/test ownership

Working rule (measured 2026-09-24): parallel slices **EDIT ONLY** — no slice runs
cmake/ninja/tests in the shared `build/` tree. Concurrent ninja/cmake invocations
corrupt each other's outputs: RC1109 `manifest.res` lock, transient C1083
`Permission denied` on `.obj`s, a corrupt `HDAW_lib.lib` (LNK1136 — fixed by
deleting it and relinking), plus LNK1168 on a locked `hdaw_tests.exe`. The
orchestrator owns exactly ONE build + ONE focused test pass after all slices land,
and the full suite once at finalize. Announce the rule in the slice brief, not
mid-flight. (This is the slice-level version of the second-build collision above.)

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

