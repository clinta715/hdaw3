# HANDOFF: Fix "only one synth audible" polyrhythm export + resolve FxAddRemoveBypass failure

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, MSVC/VS generator, JUCE 8 via CMake FetchContent.
> **MANDATORY:** before ANY code change, load the `hdaw-guard` skill (AGENTS.md) — plan-first, success gates, subagent execution, lesson-15 stale-binary discipline.

## Mission (remaining work)

1. **#1 — Resolve the `McpServer.FxAddRemoveBypass` test failure + hang** (blocking). Details below — diagnosis is mid-flight, strong leads exist.
2. **#2 — Finish verification gates for the VST3 in-process resolution fix** (already implemented, uncommitted in working tree). Gates G1–G4 below.
3. **#3 — Re-render the user's project and confirm all 10 tracks audible**, then report. Do NOT commit unless the user asks.

## Original bug (diagnosed — evidence complete)

User report: "only one synth sound is audible in the output from rendering
`polyrhythm_aminor_120bpm.hdaw3`" (10-track / 475-clip generative test project:
5× VST3 "Identity" synths on Lead/Bass/Pad/Sub Bass/Piano Keys + Toxic + WD Echo
on Lead, 5× CLAP JE8086 drums on Kick/Snare/CH/OH/Clap).

**Root cause (verified):** the user's WAV
(`D:\pdf\roo projects\hdaw3\polyrhythm_aminor_120bpm.wav`, 240 s, 48 kHz/24-bit
stereo) was rendered with `HDAW_NO_PLUGIN_ISOLATION=1` (in-process mode). It is
**bit-identical** (first 16 s) to an in-process repro render AND bit-identical to a
drums-only variant render → all 7 VST3 slots rendered silent; only the 5 JE8086
CLAP drum tracks are audible. Debug-log evidence from in-process renders:
`createPluginInstance result: NULL error=Unable to load VST-3 plug-in file` ×7
(every VST3 slot); isolated mode logs 12× `ok`.

**Why:** `PluginManager::resolveIdentifierToPath`
(`src/engine/PluginManager.cpp:736-768`) returned a stitched copy of the input
`PluginDescription` with only `fileOrIdentifier`/`name` patched — `uniqueId`/
`deprecatedUid` stayed 0. JUCE's `VST3ModuleHandle::open()`
(`build\_deps\juce-src\modules\juce_audio_processors\format_types\juce_VST3PluginFormat.cpp:1407-1436`)
only loads a module when `description.name` matches the class name AND `uniqueId`
(or `deprecatedUid`) matches the class hash → every in-process VST3 instantiation
failed. The isolated child path is unaffected (`PluginHost::loadPluginByPath` in
`src/proxy/host/PluginHost.cpp:1537` uses `findAllTypesForFile` → full
descriptions). CLAP in-process unaffected (matches by path/name only).

**Isolated mode (default) verified correct:** solo-kick, solo-pad, drums-only,
synths-only, and full-project isolated renders all contain correct content
(narrowband spectral checks: pad chord A3/C4/E4, sub F1 43.6 Hz, hats 6–10 kHz
all present in the full isolated render).

## The fix (ALREADY APPLIED — uncommitted, in working tree, binary current)

`git status` shows exactly 3 modified files:

- `src/engine/PluginManager.cpp` — `resolveIdentifierToPath` now returns the full
  known-list entry `kd` in both match branches (identifier-match + format+name
  fallback) instead of the stitched copy.
- `src/engine/PluginManager.h` — doc comment updated to match.
- `tests/unit/engine/plugin_identifier_resolution_test.cpp` — 3 new tests:
  `PluginIdentifierResolution.PreservesUniqueIdFromKnownEntry`,
  `PluginIdentifierResolution.FallbackMatchPreservesUniqueId`,
  `PluginManagerInProcessVst3.InstantiatesRealVst3ByIdentifier` (real-plugin gate,
  skips if no VST3 in cache; sets `pm.isolationEnabled = false`).

Binary freshness: `build\Debug\hdaw_tests.exe` rebuilt 8/12 9:47:02 AM, sources
edited 9:40 — the fix IS in the binary (verified by timestamp).

## OPEN ISSUE #1 — McpServer.FxAddRemoveBypass fails + hangs (blocking)

Symptom (user-reported and reproduced 4×):
```
hdaw_tests.exe --gtest_filter=McpServer.FxAddRemoveBypass
[ RUN      ] McpServer.FxAddRemoveBypass
mcp_server_test.cpp(223): error: Expected: (tr0) != (nullptr), actual: NULL vs (nullptr)
```
…then the process HANGS indefinitely after the assertion (killed at 90–150 s).

Established facts:

- `getTrack(0)` returns `routingManager ? routingManager->getTrackNode(index) : nullptr`
  (`src/engine/MainAudioProcessor.cpp:419-422`). `routingManager` is only created in
  `MainAudioProcessor::prepareToPlay` (`:32-100`, rebuild at `:65-68`).
- `prepareToPlay` is triggered via `processorPlayer.setProcessor(...)` in
  `AudioEngine::initialize()` (`src/engine/AudioEngine.cpp:158`) — JUCE's
  `AudioProcessorPlayer::setProcessor` only calls `prepareToPlay` when its
  sampleRate/blockSize are set, which happens once the audio DEVICE runs. So the
  failure means: **the audio device never started in the test process.**
- Device init is `deviceManager.initialiseWithDefaultDevices(2, 2)` at
  `AudioEngine.cpp:116`; errors are logged via `juce::Logger::writeToLog` (`:117-118`)
  and saved-device restore from QSettings follows (`:120-155`).
- The fix above CANNOT be the direct cause: the default test project's FX chains are
  EMPTY (`src/model/ProjectModel.cpp:20-24` `createFXChain()` returns an empty chain;
  tracks created at `:355-406`) → `resolveIdentifierToPath` is never exercised by
  this test.
- A stray `HDAW_headless_mcp.exe` (opencode's MCP server for the `hdaw_*` tools,
  `C:\Users\hapbt\AppData\Local\Temp\HDAW_headless_mcp.exe`, started 8:47:40 AM) was
  holding `%TEMP%\hdaw_debug.log` AND had an engine/audio device open. It was KILLED
  (PID 17936) → log unlocked → **test STILL fails and hangs**. So that process is not
  the (sole) cause. NOTE: the `hdaw_*` MCP tools are dead until that server restarts.
- `%TEMP%\hdaw_debug.log` (JSON lines, UTC timestamps; local = UTC−5) has NOT grown
  (2,370,564 bytes) since 8/12 09:29:58 local — none of the failing test runs wrote
  a single HDAW_LOG entry. DebugLog opens the file in append mode on first use and
  prints `HDAW: Failed to open debug log file` to **stderr** on failure
  (`src/common/DebugLog.h:38-40`). **NOT YET CHECKED: the captured stderr files**
  `C:\Users\hapbt\AppData\Local\Temp\opencode\fxtest_err2.txt` / `fxtest_err3.txt`
  — look there first for device-init errors and log-open failures.

Next diagnostic steps (in order):

1. Read `fxtest_err2.txt` / `fxtest_err3.txt` (stderr of the hung runs).
2. If device error found → fix the environment or the device-restore path; if none,
   add a temporary probe (or run under a debugger) to see where
   `AudioEngine::initialize()` stops and why the device never starts.
3. Determine pre-existing vs. new: `git stash` the 3 modified files →
   `cmake --build build --config Debug` (big timeout!) →
   run the single test → observe → `git stash pop` → rebuild. If it fails on the
   clean tree, it is pre-existing/environmental (unrelated to the fix).
4. Explain/fix the post-assertion hang (teardown waits on something — audio device,
   `fileLibraryManager` scan thread, or MessagePumpThread).
5. Suspect list for the device failure: QSettings saved-device restore
   (`AudioEngine.cpp:120-155`, keys `SettingsKeys::kKeyAudioDriver` etc. — inspect
   the registry `HKCU\Software\HDAW\HDAW`), exclusive-mode device state left by the
   many engine instances spawned this morning, or Windows audio service state.

## OPEN ISSUE #2 — verification gates for the VST3 fix (after #1 is unblocked)

- **G1 (unit):** `hdaw_tests.exe --gtest_filter=PluginIdentifierResolution.*:PluginManagerInProcessVst3.*` — all pass/skip-clean.
- **G2 (regression):** the 5 pre-existing PluginIdentifierResolution tests still pass.
- **G3 (render repro, the user's actual bug):** using the driver script below,
  render `synths_only.hdaw3` with `inproc` → must now be NON-silent (was RMS=0
  pre-fix); render the full `polyrhythm_aminor_120bpm.hdaw3` with `inproc` → spectrum
  must show synth content (mid band ≫ drums-only baseline `render_inproc.wav` old
  values: mid≈0.011 relative) and no longer be bit-equal to drums-only.
- **G4 (full suite):** `hdaw_tests.exe` — report totals; pre-existing failures: note,
  don't fix (handoff `2026-08-11-isolation-triage-multiport-test.md` documents 4
  known PluginIsolation failures as of 8/10; re-verify status).
- **G5 (user acceptance):** re-render the full 240 s project in DEFAULT (isolated)
  mode and verify drums+synths both present (attack detection on a 3 kHz high-pass +
  narrowband pad/sub checks), so the user gets a correct `polyrhythm_aminor_120bpm.wav`.

## Repro assets (in `C:\Users\hapbt\AppData\Local\Temp\opencode\`)

- `py_repro2.py` — MCP-stdio render driver:
  `python py_repro2.py <isolated|inproc> <end_seconds> <project_path>`; launches
  `build\Debug\HDAW_headless.exe --mcp-stdio`, loads the project via `load_project`,
  exports via `export_audio` (48 kHz/24-bit WAV), waits for `exportComplete`.
  Output: `render_<projecttag>_<mode>.wav` + log `repro_<projecttag>_<mode>.log`.
  In `inproc` mode it sets env `HDAW_NO_PLUGIN_ISOLATION=1`.
- Project variants (built from the real project by deleting tracks):
  `solo_kick.hdaw3`, `solo_pad.hdaw3`, `drums_only.hdaw3`, `synths_only.hdaw3`.
- Reference renders: `render_inproc.wav` (full 16 s in-process, bit-equal to user's
  WAV — drums only), `polyrhythm_render.wav` (full 16 s isolated — synths+drums OK),
  `render_{drums,synths}_only_{isolated,inproc}.wav`, `synths_run1/2.wav`
  (determinism check: isolated renders are NOT bit-deterministic — do not use
  sample-exact subtraction across separate isolated renders).
- 24-bit WAV loader pattern (used in all analyses): parse RIFF chunks to `data`,
  decode 3-byte little-endian samples, sign-extend at 0x800000.

## Build / ops notes (learned the hard way)

- Bash-tool default timeout is 120 s; Debug builds take minutes — ALWAYS pass a big
  timeout. Killed builds orphan `cl.exe`/`MSBuild.exe` that lock source files.
- `HDAW_headless_mcp.exe` in `%TEMP%` is opencode's MCP server (`hdaw_*` tools). It
  opens the audio device and locks `%TEMP%\hdaw_debug.log` — engine tests and it do
  not coexist peacefully. It was killed this session; if `hdaw_*` tools are needed,
  it must be restarted (opencode may do so automatically on next tool call).
- `%TEMP%\hdaw_debug.log` is the shared engine log (append, UTC). If a process holds
  it, later engines' log entries are lost — check stderr for the open-failure message.
- Plugin cache: `%APPDATA%\hdaw\plugin_cache.xml` (Identity/Toxic/WD Echo VST3,
  JE8086 CLAP all present). Blacklist: FM8 only (32-bit). `HDAW_NO_PLUGIN_ISOLATION`
  is NOT set persistently (user/machine env checked) — the user's in-process render
  came from a session that set it explicitly (log session 8/12 03:36 UTC shows it).
- Lesson 15 discipline: after any edit, verify the BINARY timestamp/behavior, not the
  source. `build/Release` is stale — never use it.

## Key residual knowledge

- The user's render was an in-process render; default isolated mode was already correct
  for this project. The fix makes the in-process path (diagnostic knob
  `HDAW_NO_PLUGIN_ISOLATION=1`, also used by the in-process export regression gate in
  `docs/plans/2026-08-11-export-isolation-wedge.md` Gate 4) work for VST3.
- `resolveIdentifierToPath` callers: only `PluginManager::createPluginInstance`
  (isolated branch uses just `fileOrIdentifier` → unaffected; in-process branch is the
  fix target); sole external caller is `Track::rebuildFXChain` (`src/engine/Track.cpp:177`).
  `createOfflineCopy` (`src/engine/PluginManager.cpp:107-118`) seeds the full
  known-list, so the export domain is covered.
- Secondary observation (not yet actionable): the export of the full 12-plugin
  project via MCP stdio reached 100% progress but `exportComplete` never fired and
  the 12 plugin hosts stayed alive (teardown hang after render, isolated mode,
  8/12 ~08:53 repro with `py_repro.py` predecessor script). Solo/variant projects
  complete cleanly. May share a root cause with OPEN ISSUE #1's teardown hang —
  investigate `~PluginProxySlot` / render-graph teardown ordering
  (`src/engine/ExportManager.cpp:384-394`) if time permits.

Related docs: `docs/plans/2026-08-11-export-isolation-wedge.md`,
`docs/handoffs/2026-08-11-isolation-triage-multiport-test.md`,
`docs/postmortem-silent-clap-export.md`, AGENTS.md lessons 11–16.

---

# RESOLUTION (same-day session, 2026-08-12 ~11:30–13:45)

All three missions done. Everything below is in the working tree, UNCOMMITTED.

## OPEN ISSUE #1 — RESOLVED (two root causes, both fixed)

**1a. Test failure (`getTrack(0)` null): no audio capture device kills device init.**
`initialiseWithDefaultDevices(2, 2)` fails the WHOLE open when no capture device
exists (RDP session with render-only "Remote Audio"): `Error opening Primary Sound
Capture Driver: "No driver"` (captured live via cdb breakpoint on
`juce::Logger::writeToLog` — juce::Logger writes to OutputDebugString only, which is
why stderr/hdaw_debug.log captures were empty). **Fix:** `AudioEngine::initialize()`
(`src/engine/AudioEngine.cpp`) now uses an `initDefaultDevice` lambda: try (2,2); on
error log (juce::Logger + HDAW_LOG) and retry output-only (0,2). Applied at both init
sites (default init + saved-device-restore fallback).

**1b. Post-assertion hang: destruction-order hazard in the tests.** Tests declared
`TransportLoopback tp` AFTER `McpServer s`; on early ASSERT, `tp` is destroyed first,
then `~McpServer()` → `stop()` → `transport_->stop()` touches the destroyed transport
(Qt6 QMutex frees its d-pointer in its dtor) → waits on a garbage futex forever
(proven by thread dump of a live hung process: main thread in
`TransportLoopback::stop` via `~McpServer`, no other thread holds the lock).
**Fix:** all 18 sites in `tests/integration/mcp/mcp_server_test.cpp` now declare the
transport BEFORE the server (destruction order server-first); NOTE comment documents
the rule.

**1c. Side discovery:** the MIDI assertion spam in test output is a pre-existing
`JUCE_ASSERT_MESSAGE_THREAD` violation — `AudioDeviceManager` constructs a
`MidiDeviceListConnection` in its ctor, and tests construct `AudioEngine` on the main
thread while JUCE's message thread is the MessagePumpThread. Debug-only, logs to
OutputDebugString, continues — harmless, not fixed (documented here).

## NEW BUG #2 — in-process rebuild deadlock (unmasked by fix 1a) — FIXED

With a device present, `load_project` in in-process mode deadlocked permanently
(thread dump: MCP/Qt-main thread in `createInstanceFromDescription` waiting on a
WaitableEvent; pump thread parked in `MessageManager::Lock::BlockingMessage`).
Root cause: `rebuildRoutingGraph` parks the pump (lesson 12) and JUCE plugin
instantiation off the message thread dispatches TO the message thread — the park
blocks its own dispatch. (Previously invisible: with no device `routingManager` was
null and `rebuildRoutingGraph` early-returned.) **Fix — two-phase rebuild:**
`RoutingManager::prebuildTracks()` (+ private `buildTrackProcessor()` extraction)
constructs Track processors and their plugin FX instances BEFORE the park;
`addTrack` adopts prebuilt tracks; `MainAudioProcessor::rebuildRoutingGraph` builds
the replacement RoutingManager pre-park and calls `prebuildTracks()` only when
`needsPark && !pluginManager->isolationEnabled`. Isolated mode, `prepareToPlay`, and
ExportManager keep their exact previous paths.

## NEW BUG #3 — CLAP host audio-thread misclassification → process termination — FIXED

Full-project inproc export aborted with rc=3 (Debug CRT `abort` via
`std::terminate`; caught with cdb attached live + breakpoints on `ucrtbased!abort`).
Stack: JE8086.clap window proc on the Qt main thread → `clap_params_request_flush`
(spec: `[thread-safe,!audio-thread]` — legal) → clap-helpers
`ensureAudioThread("params.request_flush", false)` → `pluginMisbehaving` →
`terminate` (CLAPHost uses `MisbehaviourHandler::Terminate, CheckingLevel::Maximal`).
Root cause: `CLAPHost::threadCheckIsAudioThread()` returned
`!isThisTheMessageThread()` — "anything that's not the message thread is the audio
thread" — which marks the Qt main thread (JUCE's message thread is the pump!) as
audio. **Fix:** `CLAPHost::audioThreadId` (atomic) recorded in
`CLAPPluginInstance::processBlock`; `threadCheckIsAudioThread` compares against it
(false until first processBlock). `threadCheckIsMainThread` untouched.

## Verification results

- **G1/G2 (unit+regression):** `PluginIdentifierResolution.*` +
  `PluginManagerInProcessVst3.*` — 8/8 pass; the real-plugin in-process VST3 gate
  actually ran (not skipped).
- **G3 (inproc repro):** `synths_only` inproc: RMS 0.1135 (was 0.0). Full project
  inproc: completes, 13/13 `createPluginInstance result: ok`, RMS 0.1348; spectrum
  matches the known-good isolated reference in every band (sub F1 470 vs 464,
  hats 6–10 kHz 0.1437 vs 0.1447, mid 200–2k 19.8 vs 19.7). No longer bit-equal to
  drums-only.
- **G5 (user acceptance):** full 240 s project re-rendered in DEFAULT isolated mode:
  `exportComplete success=true`, 69,120,104 bytes (exactly 240 s @ 48 kHz/24-bit),
  438 drum attacks on a 3 kHz high-pass, sub F1 + pad A3/C4/E4 + hats present in all
  nine sampled sections (2 s…234 s). Delivered to
  `D:\pdf\roo projects\hdaw3\polyrhythm_aminor_120bpm.wav` (replaced the broken
  drums-only file).
- **G4 (full suite):** 746 tests, 737 passed, 9 failed — **all 9 environmental**:
  the user's RDP session DISCONNECTED ~13:32 (session state Disc), which removes the
  "Remote Audio" endpoint (Get-PnpDevice shows zero AudioEndpoint devices since).
  Every failure is the same "device never started" class (`getTrack`/`routingManager`
  null) with logged `Error opening Primary Sound Driver: "No driver"` for both (2,2)
  and (0,2) attempts. Same binary passed McpServer.* 17/17 earlier with the device
  present. **Action: re-run `hdaw_tests.exe` once the RDP session is Active again.**
  (The 4 PluginIsolation failures documented in the 8/11 handoff did NOT appear.)

## Residual notes

- Intermittent `Render graph bake timed out after 15s` in isolated export seen once
  (subagent's first drums_only run; retry clean). Pre-existing race documented at
  `ExportManager.cpp:214-218`; the stall began ~7 s BEFORE the bake started
  processing (pump stall, no child failure). Watch it.
- The old secondary observation (12-plugin isolated export: 100% progress but no
  `exportComplete`, hosts left alive) did NOT reproduce today — both isolated renders
  (16 s and 240 s) completed with `exportComplete success=true`.
- `HDAW_headless` QSettings contains a saved audio device ("Primary Sound Driver",
  DirectSound) whose restore fails harmlessly ("No such device … using defaults").
- Diagnostic technique that cracked this session: cdb with
  `eb <mod>!juce::juce_isRunningUnderDebugger 33 c0 c3` (silences JUCE int3 asserts
  by making the debugger invisible), breakpoints on `juce::Logger::writeToLog`
  (`da poi(@rcx)`) and `ucrtbased!abort` (`~* k 50; q`), procdump `-ma` + `~* k` for
  live hangs. JUCE assertion output goes to OutputDebugString, never stderr.
- Untracked repro assets remain in `C:\Users\hapbt\AppData\Local\Temp\opencode\`
  (py_repro2.py, variant projects, renders, dumps, cdb logs).
