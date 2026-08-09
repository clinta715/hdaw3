# Handoff: Fix silent WAV exports (isolated CLAP) — STATUS: RESOLVED (2026-08-08)

> This handoff is now CLOSED OUT: the export-silence fix is complete and the full suite is
> green except the documented pre-existing `PluginIsolation.*` failures. Remaining items below
> are optional follow-ups, not blockers.

## What was accomplished (2026-08-08 session)

1. **Restored + actually linked the message pump in `tests/test_main.cpp`.**
   - The pump version was already on disk (12:58 PM) but `test_main.obj` (5:27 PM) was compiled
     from the NO-pump baseline; MSBuild skipped recompiling (source older than obj) and the
     6:09 PM link produced a no-pump `main()`. **This stale binary was the "hang" root cause:**
     with no pump, the export thread became the first `MessageManager` owner, its hidden window
     died with the thread, and `AudioEngine::shutdown()`'s `MessageManagerLock` posted a
     `BlockingMessage` that nobody could ever dispatch → infinite wait.
   - Proven via cdb breakpoints (`main → start → pumpLoop` chain absent; `MM ctor` on the test
     thread) and thread/window inventory. Fix: `(Get-Item tests\test_main.cpp).LastWriteTime =
     Get-Date` → recompile + relink. **Always delete `build/Debug/hdaw_tests.exe` (or touch the
     source) when the dependency graph may miss a change.**
2. **Pump + ExportManager fix verified green**: `McpServer.*` 11/11 (incl.
   `ExportAudioRendersDefaultProject`, `ExportAudioWithClapPluginDoesNotHang` 9 s, 5/5 export
   pair) — multiple runs.
3. **Fixed the "WITH pump crashes" family** (teardown/mutation races vs the pump; the shutdown()
   fix alone was NOT sufficient):
   - `MainAudioProcessor::rebuildRoutingGraph` — conditional `MessageManagerLock` pump-park
     (skip when on the message thread — self-deadlock guard) around the graph clear+rebuild.
     Closes: pump dispatching `AudioProcessorGraph`'s internal `LockingAsyncUpdater`
     (`Pimpl::handleAsyncUpdate` iterating the live node list) while the command thread frees
     those nodes → 0xC0000005 in `Node::getProcessor()`.
   - `Track::prepareToPlay` — guard `fxChain`/`automationManagers`/`modulationManager`
     iteration with `stateLock.tryEnter()` + skip (existing processBlock idiom). Closes: pump's
     graph applySettings path iterating vectors the command thread clears under `stateLock` →
     UAF / debug-CRT heap corruption (even a modal MessageBox hang).
   - Verified: 14 consecutive green runs of the 36-test crasher group
     (`StretchCommands.FitToLoopComputesRatio:InsertSilence.*:DuplicateRegion.*:ProjectLifecycle.*:ClipSlicing.*:FxSurface.*:ProjectMetadata.*:Commands.SetAutomationPointValue:Commands.SetFxSlotPlugin:TrackProperties.*`).
4. **Full suite**: `631/635` passed — 2 consecutive runs. Only failures:
   `PluginIsolation.SpawnWithBadPluginExits`, `CheckAllChildrenFiresCallback`,
   `CrashDetectionViaSelfExit`, `PerSlotCrashCallback` — **pre-existing** (fail with AND without
   the pump; stem from the older WIP `PluginHost`/`PluginManager` changes; A/B-verified by
   building with the fix edits reverted). `2 DISABLED tests` present as intended.
5. **Cleanup done**: debug logging removed (`MidiClipProcessor.h` MidiClipPB fprintf + twin,
   `TrackFXSlot.h` SlotProc process logs + dead counter, `PluginProxySlot.cpp` ClapProbe logs,
   `ExportManager.cpp` ClapProbe/ExportDiag logs incl. probe counters + unused include);
   `tests/unit/common/graph_bake_probe_test.cpp` deleted + deregistered from
   `tests/CMakeLists.txt`; re-verified McpServer 11/11, MessagePumpThread 2/2, 4-test rebuild
   sample. Left (intentional throttled WIP diagnostics): `Track.cpp` TrackPB,
   `CLAPPluginInstance.cpp` CLAPHost/ClapProbe/ClapDiag, `TrackFXSlot.h` FXSlotCtor/Dtor,
   `PluginProxySlot.cpp` "proxy" tag.
6. **Plan doc updated**: `.opencode/plans/2026-08-07-fix-clap-isolated-export-silence.md` →
   "Phase 4 status update (2026-08-08) - RESOLVED" with gate-by-gate status.
7. **THIRD race fix (2026-08-08 late session, user-captured AV):** `TrackFXSlot::prepare`
   recreating the `eq` (`ProcessorDuplicator<IIR::Filter>`) on the pump thread under
   `stateLock` while `AudioEngine::valueTreePropertyChanged` (command/MCP thread) wrote
   `*eq->state` via `setInternalParam → applyInternalParamToDsp` WITHOUT any lock →
   write-after-free → corrupt `OwnedArray` → AV in `ArrayBase::size` ←
   `~ProcessorDuplicator` ← `TrackFXSlot::prepare` ← `Track::prepareToPlay` ←
   `NodeStates::applySettings` (pump thread). Fix: `Track::setFxSlotInternalParam`
   (holds `stateLock`, mirrors `setFXBypassed`), used by the FX_SLOT param_N listener
   branch in `AudioEngine.cpp`. Verified: 8× race-stress runs green
   (`FxSurface.*:Commands.SetFxSlotParam:McpServer.FxAddRemoveBypass`), McpServer 11/11,
   crasher group 36/36 ×2, full suite 634 ran / 630 passed / 4 FAILED (pre-existing
   PluginIsolation only — count delta vs earlier 635 is the deleted graph_bake_probe_test).
8. **MANUAL GATE — original repro project rendered NON-SILENT (2026-08-08):** rebuilt
   `HDAW_headless` + `hdaw_plugin_host` (both 22:13), launched headless WS mode, drove
   `project.loadProject` + `export.audio` over WS (driver: `render_techno_fresh.js`,
   NODE_PATH not needed — `ws` resolves from root `node_modules`):
   `test_techno_64bars.hdaw` (Vital + Dexed + JE8086 CLAP instruments) → exported 128 s
   @48 kHz 16-bit in 129.4 s → `render_fresh_techno_64bars.wav`:
   **peak=1.00000, 1,971,877 non-zero frames of 6,144,000** (non-silent ✓).
   Context: every historical export of this project (8/6–8/7) measured peak=0.00000;
   only the 8/3 `hdaw_vital_patterns.wav` was non-silent (peak 0.97). Note: peak hits
   full scale (1.0) — the techno mix is hot/clips at full scale; audible loudness, not
   a correctness regression, but worth a mix-gain look when polishing.
   Historical peak inventory + the peak-measure tool: `%TEMP%\opencode\wavpeak.py`.

## Current state (as of session end)

- HEAD `c05ec4b` + large uncommitted WIP (engine + frontend + tests). NOT committed (user has
  not asked).
- Working-tree files modified this session: `tests/test_main.cpp` (pump main — content was
  already the pump version; forced recompile), `src/engine/MainAudioProcessor.cpp` (pump-park in
  `rebuildRoutingGraph`), `src/engine/Track.cpp` (stateLock guard in `prepareToPlay`; rest of the
  Track.cpp diff is pre-existing WIP: TrackPB log, MIDI-FX automation, deferred reset),
  `src/engine/MidiClipProcessor.h`, `src/engine/TrackFXSlot.h`, `src/proxy/PluginProxySlot.cpp`,
  `src/engine/ExportManager.cpp`, `tests/CMakeLists.txt` (debug-log removal + probe-test
  deregistration), `.opencode/plans/2026-08-07-fix-clap-isolated-export-silence.md`,
  `.opencode/handoffs/2026-08-08-clap-export-fix.md` (this file).
- `git stash list` shows ONLY `stash@{0}` = older unrelated UI WIP (MainWindow/NoteGridWidget/
  ProjectModel — NOT the current engine work; the handoff's earlier `stash@{1}` reference no
  longer exists and was wrong). The current engine WIP lives uncommitted in the working tree.
- 3 unkillable hung `hdaw_tests.exe` processes (pids 5728, 19172, 24780) from crash reproduction
  — idle, hold debug ports; **require a reboot to clear**. Delete `build\Debug\hdaw_tests_stuck*.exe`
  after reboot if present.

## Remaining follow-ups (optional, not blockers)

1. **Commit scope decision** (when the user asks): the WIP contains this task's engine fix
   (MessagePumpThread, shutdown() park, rebuild park, Track guard, ExportManager ordering,
   McpServer export tests) + the older proxy-fidelity WIP (CLAPPluginFormat/CLAPPluginInstance/
   PluginProxySlot/PluginHost/Track/MidiFx/ReadModelImpl) + unrelated root debris
   (test_*.wav, diag *.js, .tmp_probe/, auto-backups/, docs/plans/). Suggest splitting, or at
   minimum exclude the root debris. Do NOT touch `frontend/src/components/ModulationPanel.tsx`
   (unrelated WIP). Check `git diff --stat` before committing.
2. **`PluginIsolation.*` 4 failures** — pre-existing; triage separately (likely related to the
   WIP `PluginHost`/`PluginManager` changes: crash-callback/self-exit detection timing).
3. **Docs**: realtime-safety.md / architecture.md note — headless and test processes REQUIRE the
   message pump before engine init; export graphs bake on the pump thread; graph mutations from
   non-message threads must park the pump (`MainAudioProcessor::rebuildRoutingGraph` pattern).
4. **Diagnostic leftovers** in `%TEMP%`: `hdaw_debug.log`, `test_main_pump.cpp`,
   `opencode\diag_pump.ps1`, `opencode\pump_bp*.txt`, `exp_*.log`, `crash*.log`, `full_suite*.log`,
   `find_crashes.ps1` — delete once the reboot has cleared the hung processes and the follow-ups
   are done.
5. **Knowledge graph refresh**: `graphify . --update` currently FAILS with
   `ValueError: deduplicate_entities: nodes span multiple repos ['frontend', 'src']` (graphify
   0.9.32 tooling constraint; graph.json is stale from 2026-08-02). Needs per-repo dedup or a
   config change (`.graphify.json`) before `graphify-out/graph.json` can be updated.

## Key technical facts (for future sessions)

- JUCE 8 `AudioProcessorGraph` bakes its render sequence via `LockingAsyncUpdater` (message
  queue). With no dispatched queue, `processBlock` clears audio (silence). `setNonRealtime(true)`
  makes processBlock spin-wait for the bake.
- `MessageManager::getInstance()` fixes `messageThreadId` + the hidden window to the FIRST caller.
  In a pump-less process the export thread can become the message thread; when it exits, the
  hidden window is orphaned and every subsequent `MessageManagerLock` waits forever (BlockingMessage
  never dispatched). Also: `Lock::tryAcquire(false)` has NO timeout on the condvar wait — a
  never-dispatched BlockingMessage hangs the caller forever, mandatory or not.
- `MessageManagerLock` on the message thread self-deadlocks (posts a BlockingMessage and waits
  for its own dispatch) — any pump-park must check `!isThisTheMessageThread()` first.
- cdb recipes: `-c "sxd bpe; sxd wos; sxe av; g; kp L40; q"` for AV stacks; breakpoint command
  files (`-cf`) avoid PowerShell quoting; EnumWindows does NOT list message-only windows, so a
  "missing" JUCE hidden window is not evidence of its absence (use thread/window ownership via
  toolhelp instead).
- `tests/test_main.cpp` restore copy: `%TEMP%\test_main_pump.cpp`.
- Build: `cmake --build build --config Debug --target hdaw_tests`; test exe
  `build/Debug/hdaw_tests.exe`. Do NOT run `build/Release/HDAW.exe` (stale per AGENTS.md).
