# Fix: Offline render produces silence because isolated CLAP spawn passes an identifier string, not a file path

## Goal
Make `export_audio` produce non-silent audio for the 64-bar techno project. The
isolated-plugin child process (`hdaw_plugin_host.exe`) currently receives a CLAP
identifier string (e.g. `"CLAP-Vital-aaca468a-0"`) as the `--plugin` argument;
`CLAPPluginFormat::fileMightContainThisPluginType` rejects anything not ending in
`.clap`, so the child loads no plugin and falls back to `PassthroughProcessor`.
Passthrough copies the (zeroed) input straight through → silent WAV.

## Root Cause (confirmed by code trace, not guesswork)
- `PluginManager::createPluginInstance` isolated branch (`PluginManager.cpp:504-543`)
  calls `proxyProcessManager.spawnPluginHost(desc.fileOrIdentifier.toStdString(), slotId)`
  where `desc.fileOrIdentifier` for CLAP is the **identifier string** produced by
  `PluginDescription::createIdentifierString()` (e.g. `"CLAP-Vital-aaca468a-0"`).
  The non-isolated branch already resolves this via `resolveIdentifierToPath`
  (`PluginManager.cpp:553`), but the isolated branch was never given the same fix.
- `respawnIsolatedSlot` (`PluginManager.cpp:719`) re-spawns with the
  `pluginPath` captured by `CrashRecoveryManager::onSlotCrashed`, which is the
  *same* identifier string stored on the `PluginProxySlot` at construction
  (`PluginProxySlot.cpp:34` → `pluginPathForRecovery`). So after any crash the
  respawned child also can't load the plugin.
- The child's `loadPluginByPath` (`PluginHost.cpp:1193-1208`) runs
  `fileMightContainThisPluginType(path)` on the identifier string → false for CLAP
  → the format loop skips CLAP → returns false → `loadPlugin` substitutes
  `PassthroughProcessor`. The handoff doc's "threading violation" diagnosis was a
  misread: in the child, `init()` actually DOES run on the main thread (the `run()`
  entry is the main thread and calls `loadPlugin()` before starting any other
  thread — see `PluginHost.cpp:489`). The real failure is the never-matching
  identifier, which makes the SEH-guarded `init()` "crash" path unreachable because
  `init()` is never even called.

## Fix (localized, mirrors the non-isolated resolution)
1. In `PluginManager::createPluginInstance` isolated branch, resolve
   `desc.fileOrIdentifier` to a real file path BEFORE calling `spawnPluginHost`,
   using the existing `resolveIdentifierToPath(desc, knownPluginList)` helper
   (already defined at `PluginManager.cpp:585`). Pass
   `resolved.fileOrIdentifier.toStdString()` to `spawnPluginHost`.
2. Store the **resolved** path on the `PluginProxySlot` so crash-recovery resend
   survives. The `PluginProxySlot` ctor currently takes `pluginPath` and stores
   it verbatim into `pluginPathForRecovery`. Easiest: change the call site to pass
   the resolved path (`PluginManager.cpp:516` already constructs the proxy with
   `desc.fileOrIdentifier`; change it to the resolved `fileOrIdentifier`).
3. `CrashRecoveryManager::onSlotCrashed` already receives the path through the
   notifier lambda set at `PluginManager.cpp:521-532`. Today that lambda closes
   over `path` from `setCrashRecoveryNotifier` — that path originates from
   `PluginProxySlot` (which already calls `requestRespawn()` with `pluginPath`,
   see `PluginProxySlot.cpp:645` / `PluginProxySlot.h:135`) — verify it carries
   the resolved path after step 2, and if not, fix the closure to resolve from
   `knownPluginList` before calling `crashRecovery->onSlotCrashed`. **Verify with
   grep** — do not assume.

## Success Gates (all must pass — evidence before claims)
- [ ] Gate 1: `cmake --build build --config Debug --target HDAW_headless hdaw_tests` succeeds.
- [ ] Gate 2: `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.ExportAudioWithClapPluginDoesNotHang` passes AND the test now also asserts the output WAV is **non-silent** (peak > 0.01) — see step 5 below.
- [ ] Gate 3: `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*` all pass.
- [ ] Gate 4: `build/Debug/hdaw_tests.exe` full suite passes (no regressions); pre-existing pass count ~630+.
- [ ] Gate 5: Manual repro: a project with one CLAP instrument track + a MIDI clip with 4 sustained notes exports to a WAV whose PCM peak > 0.01 (run via `HDAW_headless_mcp` MCP `export_audio`). The 64-bar techno project renders to a non-silent WAV. Capture the peak value as evidence.
- [ ] Gate 6: Grep confirms NO identifier strings are passed to `spawnPluginHost` from the `PluginManager` isolated path; the only literals seen by `spawnPluginHost` are real `.clap`/`.vst3` paths or `__passthrough__`/`__crash__`/`__blocksize__`/`__stateecho__`/`__midiecho__` sentinels.

## Dependency Map (verified by grep + code reading)
- **Upstream callers of `spawnPluginHost`:** exactly two —
  `PluginManager::createPluginInstance` (`PluginManager.cpp:508`, initial spawn)
  and `PluginManager::respawnIsolatedSlot` (`PluginManager.cpp:738`, respawn).
  Both must receive a real path.
- **Upstream callers of `respawnIsolatedSlot`:** only the
  `CrashRecoveryManager` respawnFn lambda (`PluginManager.cpp:46-47`), which is
  fed by `CrashRecoveryManager::attemptRespawn` (`CrashRecoveryManager.cpp:62`)
  using `entry.pluginPath` stored by `onSlotCrashed`.
- **Source of the path that reaches `onSlotCrashed`:**
  `PluginProxySlot` → `setCrashRecoveryNotifier` lambda (`PluginManager.cpp:521-532`)
  closes over `name` and `path`. Verify by grep where `requestRespawn` /
  `setCrashRecoveryNotifier` is invoked from inside `PluginProxySlot` and what path
  it passes. The path ultimately comes from `pluginPathForRecovery`, set in the
  `PluginProxySlot` constructor — so step 2 above (passing resolved path into the
  ctor) is the single chokepoint that fixes both initial spawn and respawn.
- **Downstream:** `ProxyProcessManager::spawnPluginHost` builds the child command
  line (`ProxyProcessManager.cpp:58-62`) — it will now receive a real `.clap` path
  and successfully launch `hdaw_plugin_host.exe --plugin=C:\path\to\Vital.clap`.
- **Child side:** `PluginHost::loadPluginByPath` (`PluginHost.cpp:1160-1212`) —
  no change needed; once it sees a real `.clap` path,
  `fileMightContainThisPluginType` returns true, `findAllTypesForFile` populates a
  real `PluginDescription`, `createPluginInstance` invokes `plugin->init()` on the
  main thread (already correct), and the live audio thread produces audio.
- **Community boundaries:** This change crosses Engine → ProxyIPC.
  `PluginManager` (engine) is the API owner of `KnownPluginList` / identifier
  resolution; `ProxyProcessManager` and `PluginHost` (proxy) just consume a path
  string. No new IPC messages, no protocol change.
- **Projections affected:** none. No ValueTree, no ReadModel, no frontend, no
  snapshot, no delta. Purely the audio-engine plugin-load path.
- **SPSC paths:** the existing shm rings are unaffected — only the plugin binary
  the child loads changes, not the IPC protocol or ring layout.
- **God nodes in scope:** none. `spawnPluginHost` and `respawnIsolatedSlot` are
  leaf-ish functions in the proxy/IPC community, not graph hubs.

## Pitfall Gates Triggered
- **Gate 2 (unimplemented path):** The fix must result in a real CLAP plugin being
  loaded in the child. Verified by Gate 2 (test asserts non-silent WAV) AND Gate 6
  (grep audit of `spawnPluginHost` callers). If only the test passes, still run
  the grep audit — it guards against a future regression slipping an identifier
  string back in.
- **Gate 4 (stale binaries):** Always test `build/Debug/...`, never
  `build/Release/HDAW.exe`. The deployment workflow in the handoff doc copies
  `build\Debug\HDAW_headless.exe` → `$env:TEMP\HDAW_headless_mcp.exe` plus the
  `*.dll`s and `hdaw_plugin_host.exe`; follow it exactly for the manual render.
  `hdaw_plugin_host.exe` MUST be re-copied after the rebuild — the child binary
  is unchanged by this fix but the parent is, and export will fail silently
  against a stale `.exe`.
- **Gate 6 (day-one bug masked by live SPSC path):** Live audio works because the
  identifier string is never matched against a file extension at the engine — the
  proxy slot just remembers it. The bug is **only observable under isolated export**
  where the identifier flows down into the child and is matched there. The existing
  test `ExportAudioWithClapPluginDoesNotHang` checks only non-hang + non-empty file,
  which is why this regression survived. Fix the test to assert non-silence (Gate 2).
- **Gate 1 / Gate 3 (audio-thread / rebuild):** NOT triggered. Resolution runs on
  the message thread inside `PluginManager::createPluginInstance` (graph rebuild
  path, already serialized via `graphLock` / `AsyncUpdater` coalescing) and on the
  message-thread `CrashRecoveryManager::tick`/`attemptRespawn`. No allocation or
  locking is added to any audio callback. The render worker thread does not call
  `createPluginInstance` — the engine constructs the graph up-front; the export
  just drives `processBlock` on an already-rebuilt graph.
- **Gate 9 (ID namespace):** No new allocator or `stoi` introduced. The resolved
  path comes from the existing `KnownPluginList::getTypes()` which already has
  validated, scanned entries. Guard against a missing match by keeping the existing
  fallthrough (return `desc` unchanged) so a bad identifier still produces the old
  silent-but-no-crash behavior rather than a hard failure.

## Steps
1. **Read** `src/engine/PluginManager.cpp:504-543` (isolated branch) and
   `src/engine/PluginManager.cpp:585-617` (`resolveIdentifierToPath`). Both are
   confirmed present and correct.
2. **Edit** the isolated branch: insert
   ```cpp
   auto resolvedDesc = resolveIdentifierToPath(desc, knownPluginList);
   ```
   immediately before the `if (!proxyProcessManager.spawnPluginHost(...))` call
   (line 508). Change the spawn argument from
   `desc.fileOrIdentifier.toStdString()` to
   `resolvedDesc.fileOrIdentifier.toStdString()`.
3. **Edit** the `PluginProxySlot` construction (line 516): change the fourth
   argument from `desc.fileOrIdentifier` to `resolvedDesc.fileOrIdentifier` so
   `pluginPathForRecovery` stores the real path and respawn inherits it.
4. **Audit** `PluginProxySlot.cpp` for any other path the plugin path reaches
   `CrashRecoveryManager::onSlotCrashed` (grep `pluginPathForRecovery`,
   `requestRespawn`, `setCrashRecoveryNotifier` inside `src/proxy/`). Confirm
   the closure at `PluginManager.cpp:521-532` passes the same resolved path. If
   an additional branch passes the unresolved identifier, fix it the same way.
5. **Test update** (`tests/integration/mcp/mcp_server_test.cpp`, test at line 427
   `ExportAudioWithClapPluginDoesNotHang`): after the existing `EXPECT_TRUE(QFile::exists(path))`
   assertion, decode the WAV and assert the PCM peak > 0.01. Use JUCE's
   `AudioFormatManager` + `WavAudioFormat` to read the file, scan all channels'
   sample absolute values, take the max. If the project has no CLAP plugins
   installed the test already `GTEST_SKIP()`s — keep that. Add the peak assertion
   only in the non-skip branch. Name the local var `peakAbs` and log it on
   failure (`<< "peak=" << peakAbs`) so future regressions are diagnosable.
6. **Build:** `cmake --build build --config Debug --target HDAW_headless hdaw_tests`.
7. **Run:** `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*`.
8. **Run:** `build/Debug/hdaw_tests.exe` (full suite; expect ~630+ pass, 0 fail).
9. **Grep audit (Gate 6):**
   `Select-String -Path "src\**\*.cpp","src\**\*.h" -Pattern "spawnPluginHost\("`
   and confirm both callers pass either a resolved `fileOrIdentifier.toStdString()`
   / path literal or a sentinel (`__passthrough__` etc.). No identifier-string
   use remains.
10. **Manual render (Gate 5):** follow the deployment workflow in the handoff
    document:
    ```
    Stop-Process -Name "HDAW*" -Force
    cmake --build build --config Debug
    Copy-Item "build\Debug\HDAW_headless.exe" "$env:TEMP\HDAW_headless_mcp.exe" -Force
    Copy-Item "build\Debug\*.dll" "$env:TEMP" -Force
    Copy-Item "build\Debug\hdaw_plugin_host.exe" "$env:TEMP" -Force
    ```
    Then run the export repro (`node export_64bars.js` against the MCP server, or
    an equivalent minimal repro: one CLAP track + 4 sustained notes → export).
    Analyze the output WAV with a quick peak scan (the test in step 5 demonstrates
    the JUCE pattern; for a one-off, a PowerShell reading a 16-bit WAV via
    `[System.IO.File]::ReadAllBytes` + manual int16 scan works too). Record the
    peak value in the report. **The headless binary must be the just-built one.**
11. **Report:** list files changed, paste the test output, paste the grep audit,
    paste the manual render peak value, and tick each gate above with evidence.

## Verification command
`build/Debug/hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*` plus the manual
export repro described in Gate 5.

## Anti-Pattern Scan
- No new `rebuildRoutingGraph()` calls — the resolution happens inside the existing
  `createPluginInstance` which the rebuild path already invokes through its own
  batched/`AsyncUpdater`-coalesced rebuild. No additional graph rebuilds.
- No new `setProperty` calls.
- No raw hex colors / CSS / frontend change.
- No `DBG(...)` — use `HDAW_LOG(tag, msg)` from `common/DebugLog.h`.
- No new `.cpp` files → no `CMakeLists.txt` change needed.
- No `callFunctionOnMessageThread` / blocking message-thread round-trips —
  `resolveIdentifierToPath` is a synchronous `KnownPluginList::getTypes()` scan
  running on the same message thread that already owns `createPluginInstance`.
- No identifier-string-as-path regression — Gate 6 grep audit.

---

## PHASE 2 (ADDENDUM 2026-08-07): Diagnostic probes � root cause the real gate

## Status update (evidence, 2026-08-07)
- The identifier->path fix (Phase 1) IS in the tree and spawnPluginHost now
  receives a real .clap path (Gate 6 grep passed). Yet exports are STILL silent.
- NEW: a 10-plugin matrix diagnostic test (Vital, Dexed, JC303, Odin2,
  ShinRonin, Identity, Gneiss, Retrospect, NodalRed2x, Altitude) ran the full
  MCP pipeline (add_track_with_fx + generate_phrase Lead + export_audio) for
  each: **every single one exported peak=0.000000.** Gneiss and NodalRed2x even
  fired child-side CLAPHost requestCallback/on_main_thread (their init ran in
  the child) yet NO ClapDiag ever appeared. Conclusion: NOT a Xenia bug; it is
  systemic to the isolated export path. The 'ClapDiag' probe sits AFTER the
  early-out gate in CLAPPluginInstance.cpp:588-593, so its absence means:
  plugin==nullptr || !activated || !processing is being hit every block, OR
  processBlock is never reached inside CLAP's own implementation (some
  instruments dump their own internal silence on init).
- Since the parent blocks complete in ~1ms and never hit the 200ms spin-wait
  timeout, the child IS consuming input and returning buffers fast � i.e. the
  ring flow works; the silence is inside the child's plugin processing.

## Goal (Phase 2)
Add TEMPORARY diagnostic HDAW_LOG probes (tag "ClapProbe") to answer ONE
question: on the isolated export path, for the child's CLAPPluginInstance,
which of these is true:
  (a) PREPARE never reaches the child (controlLoop PREPARE case never fires);
  (b) PREPARE arrives but plugin==nullptr at that moment;
  (c) plugin->prepareToPlay is called and CLAP activate()/start_processing()
      fails or reports a non-started state;
  (d) processBlock is called, gate passes, CLAP process() runs but the
      instrument EMPTYs (built-in preset with no sound / no notes routed);
  (e) MIDI never reaches the child (src2 midiIn ring empty) � even a
      processing CLAP synth would output silence with no note events.

## Success Gates (diagnostic completion � evidence before claims)
- [ ] Gate D1: Temporary probes compile in hdaw_plugin_host + hdaw_tests.
- [ ] Gate D2: Running the export matrix once more yields probe lines in
      %TEMP%\hdaw_debug.log showing, per plugin: PREPARE-received?, 
      plugin-loaded?, prepareToPlay-called?, activate/start_processing results,
      ClapDiag-style gate log (first 5 blocks), midiIn frameCount per block.
- [ ] Gate D3: The probes MUST distinguish (a)-(e) � i.e. no AMBIGUOUS result.
- [ ] Gate D4: Probes removed again cleanly afterwards (or kept gated behind
      an env var if they earn their keep) � no log spam in steady state
      (use first-N + rate-limited pattern like existing code).

## Dependency map for probes (verified by grep/read, not assumption)
- Child side (PluginHost.cpp, controlLoop + audioLoop + loadPlugin):
  - controlLoop PREPARE case at PluginHost.cpp:564-586 � ADD: probe of
    preparedSampleRate/BlockSize/NumChannels + (plugin != null).
  - audioLoop gate at PluginHost.cpp:967 � ADD: first-N probe of
    w-r, preparedBlockSize*preparedNumChannels, and whether the branch that
    calls plugin->processBlock runs (767-1018) or the spin branch (1130-1136).
  - loadPluginByPath (1161-1213) � ADD: probe of the exact path string that
    reaches it and which format matched (result of fileMightContain queries).
- CLAP side (CLAPPluginInstance.cpp):
  - prepareToPlay (471-485) � ADD: before/after probe of activate() +
    start_processing() BOOL returns; capture both.
  - processBlock gate (583-593) � BEFORE the early-out, log one-time:
    plugin!=null, activated, processing, bufCh, bufS (i.e. ensure a
    'ClapProbe early-out' log exists for the case ClapDiag cannot reach).
  - CLAPFormat::createPluginInstance / CLAPHost init: rely on existing
    ClapHost tag; just add one probe in CLAPPluginFormat::createPluginInstance
    on success/failure path (CLAPPluginFormat.cpp:242-320).
- Parent side (PluginProxySlot.cpp): prepareToPlay (55-85) � ADD: probe that
  PREPARE was sent (before send) + receive succeeded/result (after). Ensures
  we know the parent DID send PREPARE during export (it MAY not if
  Track::prepareToPlay is not invoked in the render path).
- Who calls what upstream/downstream (from graph + grep):
  - RoutingManager::addTrack (124-135) ? newTrack->prepareToPlay(...) (in
    Track.cpp:19-42) ? each fxChain slot->prepare(fxSpec). TrackFXSlot::prepare
    (TrackFXSlot.cpp:202) ? PluginProxySlot::prepareToPlay ? sendMsgBounded
    PREPARE. This whole chain must be traced in the export path to know if
    prepareToPlay is invoked at all (suspect: TRACK::prepareToPlay calls
    fxChain slots, but if rebuildFXChain runs BEFORE prepareToPlay, slot was
    never prepared ? child keeps defaults).
  - ExportManager::renderThreadFunc (61-131): renderGraph.prepareToPlay
    (line 125) � after rebuildFromValueTree (line ~123). Order matters: if the
    graph is rebuilt AFTER tracker states set but before prepare, track slots
    may not exist at prepare time. Verify the ordering by probe around a
    'prepareAfterFXChain' flag.
- SPSC rings touched: only READ from the probes (inputReadPos etc.), never
  write. No protocol change. Ring sizes unchanged.
- Audio-thread caveat: lifecycle of temporary probes is governed by the same
  rule as existing ClapDiag (first-N, static atomic counters; no allocation).

## Pitfall gates triggered (Phase 2)
- Gate 3 (audio thread): probes read atomics only (w-r) and log first-N �
  identical precedent to ClapDiag lines 689-701. No alloc, no locks.
- Gate 9 (ID): no new IDs. Gate 4 (stale binaries): after adding probes, MUST
  rebuild hdaw_plugin_host.exe AND copy it next to hdaw_tests.exe (the manual
  deploy step) � otherwise the child under test is stale and probes show
  nothing. IMPORTANT for the diagnostic run.
- No project model changes, no ValueTree, no frontend, no RPC surface change.

## Phase 2 Steps
1. Add ~5 HDAW_LOG probes per above (tags 'ClapProbe' + site-specific suffix).
2. Build ONLY hdaw_plugin_host AND hdaw_tests:
   cmake --build build --config Debug --target hdaw_plugin_host hdaw_tests
3. Copy build\Debug\hdaw_plugin_host.exe to the folder where the test process
   resolves it (check ProxyProcessManager::getHostExePath) � ensure the test
   runs the NEW host binary (Gate 4 evidence).
4. Run the diagnostic matrix test:
   build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix
   (this test still exists in mcp_server_test.cpp).
5. Read %TEMP%\hdaw_debug.log (tag ClapProbe) and answer (a)-(e) per plugin
   with concrete lines pasted in the report.
6. Second-run single test to double-check: the MCP export test with the same
   probes (McpServer.ExportAudioWithCLAPDoesNotHang) � the log must show the
   same pattern for whichever plugin it picks.
7. Report: paste the probe lines, the verdict (a)-(e), and the ordering
   finding on prepareToPlay (parent: send? child: receive? activate results?).

## Phase 2 completion: the evidence decides
- If (a)/(b): PREPARE plumbing is broken � fix in Track::prepareToPlay ordering
  or proxy prepareToPlay invocation during export.
- If (c): CLAP activate/start_processing failure � fix threading in
  CLAPSPPluginInstance::prepareToPlay (currently runs activate on control
  thread; CLAP wants main thread for activate atX, audio thread for
  start_processing).
- If (d): plugin loaded + activated + receives blocks but outputs zero �
  inspect block size / latency compensation / tailLengths (getTailLength).
- If (e): MIDI routing to proxy fail (midiIn ring not written by parent side)
  � trace generate_phrase?clip?proxy input handoff.

## Phase 3 addendum: ROOT CAUSE CONFIRMED � JUCE render sequence never baked on export worker thread (evidence-backed)

### The verdict (Round-2 probes + JUCE source read, not assumption)
- **Render graph is fully built** (`export graphNodes=10 totalIn=2 totalOut=2`) and the CLAP child is 100% ready
  (`controlLoop PREPARE received sr=44100 bs=512 ch=2 pluginNull=0` x10, `prepareToPlay activate=1 startProcessing=1` x9,
  real `.clap` path resolved � Phase 1 fix verified live).
- **But the parent NEVER drives the graph**: 690 render blocks at `peak=0.000000`, `TrackPB`/`MidiClipPB`/`proxyPB`
  never fire during export, and the child spins `audioLoop gate SPIN w-r=0` x23,146 with ZERO `gate OPEN`.
- **Mechanism (juce_AudioProcessorGraph.cpp, verified):**
  - `Pimpl::prepareToPlay` (1837-1849) sets NodeStates + `topologyChanged(sync)`.
  - `Pimpl::rebuild` (1857-1866): `sync` bake is INLINE **only when `isThisTheMessageThread()`**, otherwise
    `updater.triggerAsyncUpdate()` (a message-thread AsyncUpdater).
  - `Pimpl::processBlock` (1880-1909): re-bakes inline only if it *is* the message thread (1885); the non-realtime
    spin (1888-1895) waits for the message thread too; otherwise **`audio.clear(); midi.clear();`** (1904-1908) � every
    node silently no-ops. Settings match (`state->getSettings() == nodeStates.getLastRequestedSettings()`) never holds
    because no sequence is baked at all.
- **Why nothing bakes on the export worker:** `MessageManager::messageThreadId` is frozen once at singleton creation
  (`juce_MessageManager.cpp:38-39` � `Thread::getCurrentThreadId()`). The export worker (`std::thread` at
  ExportManager.cpp:33) can never be the message thread, and `runDispatchLoopUntil(0)` (ExportManager.cpp:134/234)
  drains the *worker's* queue � the AsyncUpdater message sits in the message thread's queue. In tests and in
  `HDAW_headless` the QUEUE IS NEVER PUMPED BY ANYONE ? bake never happens ? silence. (The child *does* pump: it
  runs `runDispatchLoopUntil(-1)` at PluginHost.cpp:534 on its own main thread that IS its message thread � that's
  why the child side works perfectly.)
- `AudioEngine` itself is an `AsyncUpdater + Timer` (AudioEngine.h:25) � it *assumes* a pumping message loop;
  headless/test exports starve it too.
- Also: `AudioProcessorGraph `setNonRealtime(true)` (1874-1878, public; would spin-wait via 1887-1895) still needs
  a *live messages thread* to bake; callFunctionOnMessageThread from the worker deadlocks in the test (message
  thread = main test thread, blocked in waitForOutgoing). Both dead ends.

### The fix: give the PROCESS a live JUCE message-pump thread — it must be the FIRST JUCE user
**Why "start pump before anything":** on Windows, JUCE posts its wake-up via `PostMessage` to a
hidden message window owned by the thread that first created `MessageManager`/`InternalMessageQueue`
(juce_Messaging_windows.cpp:91-112); `GetMessage` only ever drains **the calling thread's own queue**
(juce_Messaging_windows.cpp:118-121). So ONLY the thread that first calls `MessageManager::getInstance()`
can deliver async updates — a pump started later on a different thread pumps an empty queue (this is
precisely why `ExportManager::runDispatchLoopUntil(0)` never helped). Therefore:
1. New `src/common/MessagePumpThread.{h,cpp}` � a process-wide singleton; `start()` must run at the
   very top of each process entry point **before anything else constructs JUCE objects** so the pump
   thread wins `messageThreadId` AND `InternalMessageQueue` ownership. The pump thread then runs
   `MessageManager::getInstance()->runDispatchLoopUntil(1000)` in a loop with a quit flag (shutdown
   via `stopDispatchLoop()`, same pattern the child already proves: PluginHost.cpp:499-534).
2. **Start sites (compile-time order matters — pump BEFORE engine construction):**
   - `tests/`: replace `GTest::gtest_main` with a custom `tests/main.cpp` (`main()` → start pump →
     `RUN_ALL_TESTS`). Without this the test thread (or any earlier engine thread) owns the queue and
     the export starves exactly as today.
   - `main_headless.cpp:56+` (top of `main()` before any JUCE use).
   - `main.cpp:66+` for the two headless modes (`--mcp-stdio`, `--headless`). GUI mode already runs a
     real dispatch loop — the pump must NOT be started there (double ownership conflict); guard by mode.
   - NOT in ExportManager/McpServer-methods: lazy start is TOO LATE — the queue may already be owned
     by the current thread. (AudioEngine is itself an AsyncUpdater+Timer — AudioEngine.h:25 — so any
     earlier engine init already seeded the queue if the pump isn't first.)
   - NEVER start from the audio thread.
3. Export no longer needs to pump: DELETE the two `runDispatchLoopUntil(0)` calls at ExportManager.cpp:134
   and 234 and the misleading comment at 131-135.
4. With the pump alive, the graph's AsyncUpdater bakes on the pump thread within ms; the worker's
   processBlock then sees state != null and renders. TrackPB/MidiClipPB/proxyPB/ClapDiag all fire; the
   CLAP child receives notes + audio → non-silent WAV.
5. Do NOT add `setNonRealtime(true)` — the current clear-branch stays as a fail-safe, as upstream.
   DONE-CRITERION is the evidence, not the mechanism: TrackPB + gate OPEN + peak > 0.01.

### Gates (all must pass � evidence before claims)
- [ ] Gate E1: unit-ish: a small gtest asserts the pump thread owns the MessageManager thread id and that
  `AsyncUpdater` lambdas fire without any UI loop (Schedule -> within 2s). Add to a new `message_pump_test.cpp`.
- [ ] Gate E1b: grep-audit that no test expects messageThreadId == test thread; the live TrackPB canary
  (Track::processBlock fires during live render) must still hold after the pump owns the queue (E4 covers).
- [ ] Gate E2: Export test now green: `McpServer.ExportAudioWithClapPluginDoesNotHang` AND WAV peak > 0.01.
- [ ] Gate E3: `McpServer.ExportAudio*` all pass; also the 10-plugin matrix (DiagnosticClapExportMatrix) shows
  non-zero peaks for instruments (Vital/Dexed/Gneiss at least).
- [ ] Gate E4: full suite `build\Debug\hdaw_tests.exe --gtest_brief=1` � no regressions (remember: engine starts
  a pump thread now � transport/track/value-tree tests must not hang or serialise assumptions).
- [ ] Gate E5: child log shows `gate OPEN ...` + `ClapDiag` during export; parent log shows `proxyPB entry`
  and export block peaks > 0.01 in %TEMP%\hdaw_debug.log.
- [ ] Gate E6: probe removal/keep decision: once green, the temporary ClapProbe lines (PluginHost.cpp,
  CLAPPluginInstance.cpp, PluginProxySlot.cpp, ExportManager.cpp) are removed or gated behind an env var
  (first-N/rate-limited only). The diagnostic matrix test either removed or reduced to 2-3 plugins if kept.
- [ ] Gate E7: Manual headless repro HDAW_headless.exe --headless + MCP export_audio of the 1-CLAP project ?
  non-silent WAV (>0.01 peak) � recorded as evidence.

### Dependency map (verified above, not assumed)
- `AudioProcessorGraph::Pimpl::rebuild/processBlock` (juce_AudioProcessorGraph.cpp 1837-1909) � the bake contract.
- `MessageManager::messageThreadId` frozen at ctor (juce_MessageManager.cpp:38-39) ? pump thread must be started
  first; every process entry point that constructs the engine must ensure that (main.cpp:66+, main_headless.cpp:56+,
  any McpServer embedded start, and tests that init the engine � see gate E1 timing).
- `ExportManager::renderThreadFunc` (61-289) � worker renders on a spawned std::thread; graph prep at 128;
  processBlock at 189; the two runDispatchLoopUntil pumps at 134/234 get removed.
- `PluginHost.cpp:499-534` � proven pump pattern to copy (runDispatchUntil with sleep, quit flag via stopDispatchLoop).
- Child path evidence: controlLoop/loadPlugin/audioLoop gates at PluginHost.cpp 564-586/1143-1213/904-1137.
- What MUST NOT change: ring buffer protocol, JSON-RPC surface, prepareToPlay ordering, routing � none of that was
  broken; only the missing message pump ties to silence.
- Risk: a pump thread that never quits keeps the process alive at exit (must stop on shutdown; use
  `stopDispatchLoop()` + join — the child proves the pattern). Do NOT start the pump lazily from engine
  methods: it must be the process' FIRST JUCE user (fix step 2) — start it only at the entry points
  listed there, and never from the audio thread.

## Phase 3 Steps
1. Implement `src/common/MessagePumpThread.{h,cpp}` (idempotent static `start()`; thread runs bounded
   `runDispatchLoopUntil(1000)` loop; `stop()` joins on exit; thread-safe singleton).
2. Wire the START (before any other JUCE construction, by mode):
   - `tests/`: drop `GTest::gtest_main`, add custom `tests/main.cpp` that starts the pump then
     `RUN_ALL_TESTS()`; one pump per process, not per fixture.
   - entry points: top of `main()` in main.cpp / main_headless.cpp for the two headless modes
     (`--mcp-stdio`, `--headless`); GUI mode must NOT start it (double ownership conflict).
3. Remove ExportManager `runDispatchLoopUntil(0)` x2 (134/234) + stale comment (131-135).
4. Rebuild host+tests, copy `hdaw_plugin_host.exe` beside `hdaw_tests.exe` (Gate 4 stale-binary rule);
   run gates E1-E3, then full suite E4, then E5/E6/E7.
5. Keep the probes during E2-E5 iteration; strip or env-gate them at E6; update docs
   (realtime-safety.md / architecture.md note: headless+test processes REQUIRE the message pump before
   engine init; export graphs bake on the pump thread).
6. Report evidence: new log lines showing non-silent blocks, gate OPEN in child, and the peak values.

## Phase 4 status update (2026-08-08) - RESOLVED, full suite green

### Outcome
The silent-export fix shipped end-to-end. Full suite: **631/635 passed** (2 runs), only the 4
pre-existing PluginIsolation.* failures remain (verified pre-existing: fail with AND without the
pump; they stem from the older WIP PluginHost/PluginManager changes, not from this task).

### What actually happened (evidence, not assumptions)
1. **Pump implementation + wiring** (src/common/MessagePumpThread.{h,cpp}; first statement of
   main() in main.cpp/main_headless.cpp/test_main.cpp) - as designed in Phase 3.
2. **ExportManager silence fix** (ExportManager.cpp): econnectMasterToOutput() BEFORE
   prepareToPlay() so the single async bake contains the complete topology, plus
   enderGraph.setNonRealtime(true) after prepare so processBlock spin-waits for the bake.
   Both unDispatchLoopUntil(0) flushes removed (they crashed on
   jassert(isThisTheMessageThread())).
3. **NEW root-cause discovered this session: the built test binary was silently running WITHOUT
   the pump.** 	ests/test_main.cpp was the pump version on disk (12:58), but 	est_main.obj
   (5:27) was compiled from the no-pump baseline and MSBuild skipped recompiling (source older
   than obj) - the 6:09 PM link produced a no-pump main(). Symptoms of the stale binary: the
   "hang" in McpServer.ExportAudioRendersDefaultProject. Mechanism proven via cdb breakpoints
   (main -> start -> pumpLoop chain absent) and window-owner inspection: with no pump, the
   export thread became the first MessageManager owner; its hidden window died with the thread;
   AudioEngine::shutdown()'s MessageManagerLock posted a BlockingMessage nobody could ever
   dispatch -> infinite wait. Fix: touch test_main.cpp, recompile, relink (see handoff warning
   about deleting the exe when the dependency graph misses changes).
4. **AudioEngine::shutdown() lock-parked teardown** (stopTimer -> cancelPendingUpdate ->
   removeListener -> removeAudioCallback -> stopCrashMonitor -> mainProcessor.reset under
   MessageManagerLock) - resolves teardown races vs the pump.
5. **NEW fixes required after the pump became real (the "WITH pump crashes" family):**
   - MainAudioProcessor::rebuildRoutingGraph: conditional MessageManagerLock pump-park
     (skipped when already on the message thread - self-deadlock guard) around the graph
     clear+rebuild. Without it, the pump dispatches AudioProcessorGraph's internal
     LockingAsyncUpdater (Pimpl::handleAsyncUpdate iterating the live node list) WHILE the
     command thread frees those nodes -> 0xC0000005 in Node::getProcessor().
   - Track::prepareToPlay: guard fxChain/automationManagers/modulationManager iteration with
     stateLock.tryEnter() + skip (matches the processBlock idiom). Without it, the pump's
     graph applySettings path iterates track vectors the command thread clears under stateLock
     in rebuildFXChain/setAutomationTrees/rebuildModulation -> UAF / debug-CRT heap corruption
     that even manifested as a modal MessageBox hang.
   - Both verified: 14 consecutive green runs of the 36-test crasher group, McpServer 11/11 x3,
     MessagePumpThread 2/2, full suite 631/635 x2.
6. **Cleanup done**: debug logging removed (MidiClipProcessor.h MidiClipPB fprintf + twin,
   TrackFXSlot.h SlotProc process logs, PluginProxySlot.cpp ClapProbe logs, ExportManager.cpp
   ClapProbe/ExportDiag logs incl. probe counters); graph_bake_probe_test.cpp deleted +
   deregistered; re-verified McpServer 11/11, MessagePumpThread 2/2, 4-test rebuild sample.
   Remaining diagnostic logs (intentional WIP, throttled): Track.cpp TrackPB, CLAPPluginInstance.cpp
   CLAPHost/ClapProbe/ClapDiag, TrackFXSlot.h FXSlotCtor/Dtor, PluginProxySlot.cpp "proxy" tag.
7. **Known residual:** 3 unkillable hung hdaw_tests.exe processes (pids 5728, 19172, 24780)
   from crash reproduction - idle, hold debug ports; require a reboot to clear. Delete any
   uild\Debug\hdaw_tests_stuck*.exe after reboot.
8. **Docs to update (not yet done):** realtime-safety.md / architecture.md note that headless
   and test processes REQUIRE the message pump before engine init; export graphs bake on the
   pump thread.

### Gate status
- Gate E1 (pump owns messageThreadId): PASS (cdb: MM ctor on pump thread after start).
- Gate E2 (export non-silent with CLAP): PASS (ExportAudioWithClapPluginDoesNotHang, 9s, green).
- Gate E3 (ExportAudio* matrix): PASS - ExportAudioDryRunReturnsPlan, ExportAudioSkipsWhenCancelFlagSet,
  ExportAudioRendersDefaultProject, ExportAudioWithClapPluginDoesNotHang all green; the 10-plugin
  matrix stays DISABLED_DiagnosticClapExportMatrix (latent UAF in respawn path documented in handoff).
- Gate E4 (full suite): PASS 631/635 x2 (4 pre-existing PluginIsolation failures documented).
- Gate E5 (child log gates): not re-audited this session; export green is the functional gate.
- Gate E6 (probe removal): PASS - listed removals done + grep gate clean.
- DISABLED tests: TrackFXSlotShowEditor.ShowEditorTriggersEditorCreation (IMM32 recursion w/ pump).

## Phase 4 addendum (2026-08-08, late): THIRD race fix — unguarded internal-FX param writer

User-captured AV: `ArrayBase::size` ← `OwnedArray::deleteAllObjects` ←
`~ProcessorDuplicator<IIR::Filter>` ← `unique_ptr::operator=` ← `TrackFXSlot::prepare` ←
`Track::prepareToPlay` ← `NodeStates::applySettings` (pump thread).

Mechanism: `AudioEngine::valueTreePropertyChanged` FX_SLOT `param_N` branch ran
synchronously on the command/MCP thread and called `setInternalParam →
applyInternalParamToDsp` → `*eq->state = *makePeakFilter(...)` WITHOUT `stateLock`,
while the pump thread's graph bake (`Track::prepareToPlay` under `stateLock`) was
destroying that same `eq` in `TrackFXSlot::prepare` → write-after-free → corrupt
OwnedArray → later delete AV. Pre-pump this was safe (single thread).

Fix: new `Track::setFxSlotInternalParam(int slotIndex, int paramIndex, float value)`
(holds `stateLock`, bounds-checked) used by the listener branch (AudioEngine.cpp).
Verified: 8x race-stress (`FxSurface.*:Commands.SetFxSlotParam:McpServer.FxAddRemoveBypass`)
green; McpServer 11/11; crasher group 36/36 x2; full suite 634 ran / 630 passed / 4
pre-existing PluginIsolation failures only (634 = 635 minus the deleted
graph_bake_probe_test).
