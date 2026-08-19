# Handoff — Plugin preset audition shipped; remaining composer items (2026-08-19)

## Purpose

The previous session shipped handoff #1 (plugin preset selection / audition) AND
its prerequisite #5 (shared render helper). This file is the briefing for a
**fresh context** continuing the REMAINING items of the composer agenda IN
PRIORITY ORDER. Each item below is actionable on its own; pick up #2 first.
The plan that shipped is `docs/plans/2026-08-19-plugin-preset-audition.md`; the
prior agenda lives in `docs/handoffs/2026-08-19-composer-remaining-items.md`.

## Baseline: committed and verified (all on `main`)

- **Commit `2350423`** — `feat(engine): plugin preset audition + programIndex in
  instrument-part composer`. 11 files, +1230/−111 (incl. plan + test file).
- Full engine suite **928/928 green** (171 suites, exit 0) — was 917 before.
- Run with `HDAW_REAL_PLUGIN_TESTS=1` so the 3 env-guarded real-plugin cases
  (TyrellN6 at `C:\Program Files\Common Files\VST3\TyrellN6(x64).vst3`) RAN and
  passed, not skipped.
- Fresh `build/Debug/HDAW.exe` built; binary string-probe confirms
  `auditionPlugin` / `audition_plugin` / new error strings present.
- Knowledge graph refreshed (`codebase-memory`, 9788 nodes).
- No stale HDAW/plugin-host processes left running; `projects/` kept out of
  commits (untracked).

### What shipped

```
composition.auditionPlugin {
  pluginId?, programIndex? (-1=current), trackIndex? (<0 = temp probe track),
  slotIndex?, style?, lengthBeats?, density?, noteDuration?, lowNote?,
  highNote?, minVelocity?, maxVelocity?, seed?, windowSeconds?, keepTrack?
}
→ { ok, trackIndex (=-1 when probe removed), slotIndex, programIndex, programName,
    numPrograms, rms, peak, durationSeconds, audible, error? }
```

- **Engine command:** `ProjectCommands::auditionPlugin` (`AudioEngineCommands_Composition.cpp`).
  Probe mode = one "Audition probe" undo unit, reverted (`undo()`+rebuild) unless
  `keepTrack` or on any failure — a failed/complete probe leaves the project
  untouched. Existing-slot mode sets the program + persists its state snapshot
  (same semantics as `load_plugin_preset`) and does not roll back.
- **`programIndex` in `addInstrumentPart`** (`InstrumentPartParams`, default −1):
  validated at the boundary (`programIndex >= 0 && pluginId.empty()` → error);
  applied AFTER the in-transaction `rebuildRoutingGraph()` via
  `applyPluginProgram()` — sets the program on the LIVE slot and snapshots
  `getStateInformation` into the slot's `pluginState` (nullptr undo, mirrors
  `Track::rebuildFXChain`). A bad pick (`plugin instance unavailable` /
  out-of-range / empty state) rolls the WHOLE composite back (undo + close
  transaction) — no dead track left. Rides the same undo unit as the part; gain
  staging remains a separate unit.
- **Shared render helper (#5):** file-local `renderTrackWindow(engine,
  trackIndex, windowSeconds, fader, applyFader) → {wavPath, rms, peak, error}` in
  `AudioEngineCommands_Composition.cpp` — solo tree copy, `startExport`
  (48 kHz WAV 24-bit), block-wait, `measureWav`. `autoGainToTarget` now calls it
  twice (raw + verify). **All future render loops (verifyPart, audition,
  gain-stage) MUST use this — one implementation.**
- **RPC:** `composition.auditionPlugin` + `programIndex` in
  `composition.addInstrumentPart` (`Router_Composition.cpp`, dispatch cases).
- **MCP:** `audition_plugin` + `programIndex` in `add_instrument_part`
  (`McpTools_Project.cpp` `registerCompositionTools`). Both call the SAME engine
  commands (parity rule).
- **Tests:** `tests/unit/engine/audition_test.cpp` (`Audition.*` ×5),
  `instrument_part_test.cpp` (`InstrumentPart.ProgramIndex*` ×3), MCP tool tests
  (`McpServer.AuditionPluginTool`, `McpServer.AddInstrumentPartProgramIndex`),
  RPC round-trips (`FrontendServer.AuditionPluginRpc`, extended
  `AddInstrumentPartRpc`).

### ⚠️ Verified finding — drives every future real-plugin test expectation

**JUCE's `setCurrentProgram` is COSMETIC for the installed VST3 synths.**
Live probes (2026-08-19) on TyrellN6 / TripleCheese / Podolski / Zebralette3
established:

- `setCurrentProgram(i)` on a fresh live instance DOES change the reported index
  (`getCurrentProgram()==i`) and the captured `getStateInformation` blob (all
  pairwise state comparisons differ).
- The state round-trip is LOSSLESS: `setStateInformation` + `prepareToPlay` into
  a fresh instance reproduces the captured bytes exactly (MATCH).
- Yet the RENDERED AUDIO is byte-identical across programs (exact-0 peak
  differences for every pair) — both in the tree-copy render path AND when
  processing the live instance directly with a held note.

Conclusion: for these plugins the state blob does not carry the selected
program's audio params, so offline renders cannot reproduce program-specific
audio. **Do NOT write tests asserting per-program audio differences with these
plugins.** Assert the verifiable contract instead: live index changes, snapshot
written, audibility signal. A plugin whose program switching actually changes
audio would still work fully through this mechanism — none found among the
installed VST3s; CLAP is not wired (see below).

## The remaining agenda (priority order)

### 1. Global-scale + post-scale gain staging (FM clipping at fader 1.0)

FM instruments clip at fader 1.0 (preL up to 8). `autoGainToTarget` clamps at
fader 1.0 and reports `clamped=true`, but has no master-gain fallback.

- **Gap:** when `clamped` AND a full-mix render shows the master clipping, add an
  optional `globalScale` step — lower the master bus gain, raise the target fader
  proportionally, re-render to verify. Wire a `setMasterGain` command if none
  exists (check `MasterBusProcessor` / `AudioEngineCommands` FIRST — verify
  before assuming).
- **Calibration lesson (already documented):** measure RMS at a
  moderately-dense section, NOT the densest — RMS adds in quadrature, a
  gain-staged plugin moves full-mix RMS only ~1–3% at the climax. Use
  plugin-solo-render evidence for audibility (the `auditionPlugin` tool is now
  the way to get that evidence).
- Reuse `renderTrackWindow` for the re-render/verify; do NOT fork a new loop.

### 2. `compose.verifyPart` companion

A composer tool should self-verify: solo peak > threshold, full-mix
non-clipping, band-presence, plugin on/off RMS delta. `verify` on
`autoGainToTarget` only re-renders the solo window; it does not verify the part
in the mix.

- **Proposed shape:** `composition.verifyPart {trackIndex, windowSeconds?}` →
  renders the solo window AND the full mix at the same window, returns
  `{soloPeak, soloRms, mixPeak, mixRms, nonClipping, audible, bandsPresent}`.
  Reuses `renderTrackWindow`. Full-mix render = a tree copy WITHOUT the
  solo-muting (a new tiny helper or an `applyFader`-style flag on
  `renderTrackWindow` — extend the helper, don't fork it).
- Success gate: a part composed by `addInstrumentPart` with gain staging passes
  `verifyPart` (nonClipping, audible). Per the VST3 finding, "audible" means the
  DEFAULT program is audible — do not assert per-program audio.
- MCP twin `verify_part` (parity rule) + RPC + gtest + MCP test in the same
  change.

### 3. Part templates + typed track presets (design Q6 + Q8)

`addTrack` creates a generic track. Typed presets (instrument track → default
MIDI channel/note-range/velocity) and saving a composed part as a reusable
recipe (plugin, style, placement, targetRms, fader) would let composition become
"assemble building blocks". Track add = fullSync, clip add = delta; a template
instantiation is a fullSync like any track add.

- **Proposed shape:** a `TRACK_TEMPLATE` store (project- or app-level),
  instantiated by the same composite. Defer the store; the cheapest first step
  is a `role → defaults` map (Bass/Lead/Chords → fm_synth + default ranges,
  Drums → percussive patch) inside `addInstrumentPart`, exposed as a `role`
  param.
- Success gate: `addInstrumentPart {role:"Bass"}` produces the same slot/
  note-range defaults as a hand-configured Bass part. No new processor state.

### 4. Minor API gap — `pluginFormat` in the RPC snapshot (handoff bug #3)

`read.snapshot` FX-slot JSON has `pluginId` + `name` but not `pluginFormat` (the
XML persists it). Tooling must read the XML to assert VST3-vs-CLAP. Expose it as
a snapshot field (`ReadModelImpl.cpp:481` area, `FxSlotSnapshot` struct in
`ReadModel.h`). One-line change + one gtest. Cheap, do it before any audit
tooling.

### 5. CLAP program wiring (NEW — surfaced by the VST3 finding)

HDAW's `CLAPPluginInstance` stubs the JUCE program API
(`CLAPPluginInstance.h:203-206`: `getNumPrograms()==1`, `setCurrentProgram`
no-op). CLAP HAS a real program API (`clap_plugin_t` `get_program_count` /
`set_program` / `get_program_name` + the `clap-ext/sound-programs` extension).
Wiring it would make program selection work end-to-end for CLAP synths (Dexed,
Vital, Odin2, JC303, etc. are installed). This is the ONE path that could make
per-program audio differences real. Scope: implement the three accessors +
`setCurrentProgram` on `CLAPPluginInstance` (host-side, uses `clap_plugin_t`),
then re-run the env-guarded real-plugin tests against a CLAP synth. Medium
effort, high payoff for the program-selection story. Watch Gate 16 (thread
checks) — `set_program`/state calls from the command thread must match CLAP's
`[main-thread]` annotation; the existing `CLAPHost::runLifecycleOnMessageThread`
marshaling is the reference.

### 6. Beats-vs-seconds ergonomics (design Q9)

Mostly solved by `wholeSong` placement. Remaining: a `paintToProjectEnd` helper /
uniform bars-beats acceptance so callers don't hand-convert. Low priority — the
composer already accepts beats at the RPC boundary and converts internally.

## Standing technical debt (from AGENTS.md lessons)

- **Lesson 20 guard (permanent fix):** per-run pipe/shm namespace (or a
  held-name skip) so a stale engine's orphaned `hdaw_plugin_host.exe` children
  can't collide with proxy-slot tests (`\\.\pipe\hdaw_plugin_<n>` /
  `hdaw_plugin_shm_<n>`, slot counter restarts at 1 per instance). Cheapest,
  highest test-suite-value item — the "known to fail five" CrashRecovery tests
  fail on slot collision when a stale engine is alive. Also note `mcp-launch.bat`
  kills stale engines/plugin-hosts before copying the binary.
- **Qt 6.11.1:** the disconnect-during-export fix (`2e37eb6`) is a workaround for
  a genuine Qt bug (`QWebSocketPrivate::processData` NULL-deref on re-entrant
  free); re-verify if the engine ever moves off Qt 6.11.1.

## Operational context a fresh session MUST know

- **Audio-device environment:** a machine-wide audio failure appears
  intermittently (empty WASAPI scan → `routingManager` null → ~29 device-dependent
  tests fail with `getTrack(0)==nullptr` + FrontendServer bind failures). It is
  ENVIRONMENTAL, not code. Recovery: elevated `Restart-Service Audiosrv,
  AudioEndpointBuilder` (or reboot). Diagnosis rule: a cluster of fast
  `getTrack(0)==nullptr` failures = audio stack down, not a regression.
- **Message pump:** every non-GUI process starts `MessagePumpThread` before any
  JUCE construction. The block-wait-on-`isExporting()` pattern works BECAUSE the
  pump is a separate thread — never "help" it by pumping on the calling thread.
- **Probe hygiene:** never leave HDAW/plugin-host processes running (kills don't
  propagate to children on Windows; a stale engine breaks proxy tests). Probe
  scripts must restart the engine and resume on failure. Always attribute a log
  line to a pid (`date`+`pid` fields) before concluding a fix failed.
- **Real-plugin tests:** env-guard with `HDAW_REAL_PLUGIN_TESTS=1` + file-exists
  check, else `GTEST_SKIP()`. Pattern:
  `tests/unit/engine/audition_test.cpp` `tyrellN6Available()`. Real-plugin tests
  are SLOW (each TyrellN6 probe ≈ 2.5 s, render ≈ 1–2 s) — budget runtime.
  Plugin isolation is ON by default; in-process VST3 loading crashes (SEH) — do
  NOT set `HDAW_NO_PLUGIN_ISOLATION` for real VST3 tests.
- **Beats vs seconds:** RPC/notes/clips are beats, processors/export are seconds.
  The composer converts at the boundary; new code must do the same.
- **The default project ships 3 EMPTY tracks** ("Track 1", "Synth", "Vocals") —
  never hard-code absolute clip counts; scope count assertions to the track you
  control.
- **Stale binary trap:** `build/Release/HDAW.exe` is stale — never test against
  it. After any C++ change verify the Debug binary's timestamp/string-probe, and
  remember the packaged Electron app ships its engine from `build/RelWithDebInfo/`
  — repackage via `frontend\build.bat` if the packaged app is the target.
- **Plugin isolation default ON** spawns child processes per plugin FX slot. New
  tests that touch real plugins must account for spawn cost and the crash-recovery
  slot-namespace collision (lesson 20).

## Where to look

- Plan that shipped: `docs/plans/2026-08-19-plugin-preset-audition.md` (includes
  the verified VST3 finding + the "programs differ" gate removal rationale).
- Prior agenda: `docs/handoffs/2026-08-19-composer-remaining-items.md`.
- Composer implementation: `src/engine/AudioEngineCommands_Composition.cpp`
  (`renderTrackWindow`, `applyPluginProgram`, `addInstrumentPart`,
  `autoGainToTarget`, `auditionPlugin`).
- Composition RPC: `src/frontend/router/Router_Composition.cpp`
  (`dispatchComposition`; `auditionPlugin` case ~line 347).
- MCP composition tools: `src/mcp/McpTools_Project.cpp`
  (`registerCompositionTools`; `audition_plugin` ~line 1117).
- Plugin program APIs: `src/engine/PluginParamServiceImpl.cpp` (service),
  `src/engine/TrackFXSlot.h:753-778` (slot accessors), `src/engine/CLAPPluginInstance.h:203-206`
  (STUBBED — item #5), `src/proxy/PluginProxySlot.cpp:272-322` (isolated path).
- Gain-staging / render loop: `renderTrackWindow` (the ONE render implementation
  to reuse) + `measureWav` at the top of the composition cpp.
- Fader restore on rebuild: `Track::restoreMixerState` +
  `track_mixer_state_test.cpp`.
- Export render/bake: `src/engine/ExportManager.cpp` (`startExport`,
  `computeBakeWaitMs`), `tests/unit/engine/export_bake_timeout_test.cpp`.
- Learnings: `docs/realtime-safety.md`, `docs/pitfalls-juce.md`,
  `AGENTS.md` lessons 1–22.

## Suggested execution order for the fresh session

1. `pluginFormat` in the snapshot (#4) — cheap, one-line + test, unblocks audit tooling.
2. `verifyPart` (#2) — reuses `renderTrackWindow` (extend it for full-mix, don't fork).
3. Global-scale / post-scale gain staging (#1) — the FM-clipping edge.
4. CLAP program wiring (#5) — the only path to real per-program audio; medium effort.
5. Role→defaults map / templates (#3) — extensibility.
6. Lesson-20 namespace guard — standing debt, high test-suite value.
7. Beats-vs-seconds ergonomics (#6) — low priority.

Per hdaw-guard: every code change gets a plan with success gates first, dependency
analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`), pitfall-gate scan, and subagent dispatch with
verification. MCP parity for every new user-facing capability. Tests for every
change (engine → gtest, UI → Vitest/Playwright). Real-plugin tests are
env-guarded and never assert per-program audio deltas for VST3.