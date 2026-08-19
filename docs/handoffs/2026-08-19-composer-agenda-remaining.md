# Handoff — composer agenda items #4/#2/#1 shipped; remaining items (2026-08-19, session 3)

## Purpose

This session shipped the top three items of the composer agenda from handoff #2
(`docs/handoffs/2026-08-19-plugin-audition-next-steps.md`): **#4 pluginFormat
snapshot field**, **#2 composition.verifyPart**, and **#1 master gain +
global-scale gain staging** (the FM-clipping fix). This file briefs a **fresh
context** on the REMAINING items, in priority order. Pick up **#5 CLAP program
wiring** first — it is the only path to real per-program audio.

## Baseline: committed and verified (all on `main`)

| Commit | Change |
|--------|--------|
| `a8e7b5e` | **#4** `pluginFormat` in `FxSlotSnapshot` → `read.getFxSlots` JSON + MCP `list_fx` + TS types |
| `925c55b` | **#2** `composition.verifyPart` — solo + full-mix render, levels, nonClipping, audible, FFT band presence; RPC + MCP `verify_part` |
| `5a10e6f` | **#1-A** master bus gain — `IDs::masterGain` root property, realtime-safe `MasterBusProcessor` gain, rebuild restore, live listener, `setMasterGain` command/RPC/MCP, snapshot field, wired the (previously dead) frontend master mixer fader |
| `b9d1b60` | **#1-B** global-scale gain staging — `allowGlobalScale` on `autoGainToTarget`/`addInstrumentPart`: attenuated full-mix probe → master scale `1/truePeak` + fader raised into the headroom, one undo unit, verify re-render |

- Full engine suite **946 tests: 943 passed, 3 env-guarded real-plugin skips
  (TyrellN6), 0 failures**. Frontend build + **351 Vitest** green.
- Knowledge graph refreshed (`codebase-memory` project
  `D-pdf-roo-projects-hdaw3`, 9825 nodes).
- `projects/` kept untracked; no new files left out of commits.
- Plans (with success gates + measured evidence):
  `docs/plans/2026-08-19-pluginformat-snapshot-field.md`,
  `docs/plans/2026-08-19-composition-verify-part.md`,
  `docs/plans/2026-08-19-master-gain-global-scale.md`.

### What shipped (API surface)

```
composition.verifyPart { trackIndex, windowSeconds? }
→ { ok, soloRms, soloPeak, mixRms, mixPeak, nonClipping, audible,
    bandsPresent, bandLow, bandMid, bandHigh, windowStart, durationSeconds, error? }

project.setMasterGain { gain }        MCP: set_master_gain
read.snapshot now carries masterGain  (root property, default 1.0, undoable,
                                       persisted, restored on every rebuild)

composition.autoGainToTarget / addInstrumentPart  + allowGlobalScale (default false)
GainStageResult + { globalScale (<1 = applied), masterGain, mixPeak }
```

- `renderTrackWindow` (the ONE render loop) grew three defaulted params, all
  backward-compatible: `soloMuteOthers=true` (false = full-mix tree copy),
  `BandPresence* outBands` (FFT order 12, Hann, ≤8 frames, bands
  20–250/250–4000/4000–min(20000,Nyquist), threshold 0.005×total), and
  `masterScale=1.0` (tree-copy-only masterGain multiplier — the attenuated probe).
- Master gain is realtime-safe (atomic write off-thread + multiplicative
  `SmoothedValue` on the audio thread, ClipSourceProcessor idiom; meter
  post-gain) and survives rebuilds (`RoutingManager::rebuildFromValueTree`
  restores it on the fresh `MasterBusProcessor` — Gate 1/10 test
  `MasterGain.SurvivesRebuild`).

## ⚠️ Verified findings — drive future test expectations

1. **VST3 `setCurrentProgram` is COSMETIC** (handoff #2, unchanged): state blob
   changes and round-trips losslessly, rendered audio is byte-identical across
   programs for TyrellN6/TripleCheese/Podolski/Zebralette3. Never assert
   per-program audio for VST3.
2. **24-bit WAV clamps at full scale:** any sample ≥ 1.0 writes as full scale
   and reads back **exactly 1.0**. A unity render of a clipping mix therefore
   cannot reveal HOW FAR over 1.0 it is. Any future code that needs true peaks
   > 1.0 must render an attenuated probe (pattern: `renderTrackWindow`
   `masterScale=0.125` recovers true peaks up to 8.0 = preL headroom;
   `autoGainToTarget` global-scale branch).
3. **Post-global-scale mix peak lands AT 1.0, not below** — the fader raise
   exactly cancels the master scale for the target track (single-track case:
   exactly 1.0; multi-track: ≤ 1.0 unless the target dominates). The
   no-clipping contract is `peak <= 1.0`. `GlobalScale.ClippedMixScaledDown`
   asserts `mixPeak == 1.0f` for this reason.
4. **The default fm_synth patch (DX7 E.PIANO) is velocity-insensitive** —
   identical RMS at velocity 10–25 vs 60–110 (noteGain multiplies velocity,
   equally ineffective). Tests needing quiet fm_synth content must use a
   different instrument or an audio clip (the non-clipping global-scale test
   uses a 0.5-amp sine WAV).
5. **The frontend master mixer fader was DEAD UI** — `Mixer.tsx` fabricates a
   Master strip that called `project.setTrackVolume` with trackIndex −1 (silent
   no-op). Now wired: commits `project.setMasterGain`, displays
   `snapshot.masterGain`, pan hidden for master. If mixer UI work resumes, the
   master strip is real.
6. **Master gain travels with tree copies** — every render/export path applies
   it (export rebuilds its RoutingManager from the copy, which restores the
   root property). Render-measurement tests must control or account for it
   (`MasterGain.RenderAttenuation` is the reference: peak halves at master 0.5).

## The remaining agenda (priority order)

### 1. CLAP program wiring (handoff #2 item #5 — the big one)

HDAW's `CLAPPluginInstance` stubs the JUCE program API
(`src/engine/CLAPPluginInstance.h:203-206`: `getNumPrograms()==1`,
`setCurrentProgram` no-op). CLAP HAS a real program API (`clap_plugin_t`
`get_program_count` / `set_program` / `get_program_name` + the
`clap-ext/sound-programs` extension). Installed CLAP synths (carried from
handoff #2, unverified this session): Dexed, Vital, Odin2, JC303.

- All downstream machinery ALREADY EXISTS and works for VST3: `applyPluginProgram`
  (composition cpp), `auditionPlugin` program reporting, `addInstrumentPart`
  `programIndex`, `PluginParamServiceImpl`, `TrackFXSlot` accessors
  (`TrackFXSlot.h:753-778`), isolated proxy path (`PluginProxySlot.cpp:272-322`).
  Wiring the three CLAP accessors + `setCurrentProgram` on the host side makes
  the whole story work end-to-end for CLAP — and is the ONE path that could
  make per-program audio differences real.
- **Gate 16 (thread checks):** `set_program` / state calls from the command
  thread must match CLAP's `[main-thread]` annotations;
  `CLAPHost::runLifecycleOnMessageThread` marshaling is the reference. Lesson 19:
  thread-check predicates must report recorded thread identities, never "not X"
  complements — verify any new host callback against `clap/ext/*.h` `[thread]`
  annotations under `CheckingLevel::Maximal` (terminate semantics).
- Verify the isolated path proxies program calls for CLAP the same way it does
  for VST3 (it should — the proxy is format-agnostic — confirm, don't assume).
- Tests: env-guarded real-plugin cases against a CLAP synth (pattern:
  `tests/unit/engine/audition_test.cpp` `tyrellN6Available()`); assert the
  verifiable contract (live index changes, snapshot written, audibility) — and
  FOR CLAP ONLY, per-program audio differences become assertable if the synth
  truly differs per program (measure first; do not assume).
- Medium effort, highest payoff of the remaining items.

### 2. Part templates + typed track presets (handoff #2 item #3)

Cheapest first step: a `role → defaults` map inside `addInstrumentPart`
(Bass/Lead/Chords → fm_synth + default note ranges/styles, Drums → percussive
patch), exposed as a `role` param. Now that global-scale exists, role defaults
can also carry sensible `targetRms`/`allowGlobalScale` presets. Success gate:
`addInstrumentPart {role:"Bass"}` produces the same defaults as a
hand-configured Bass part. No new processor state. Defer the full
`TRACK_TEMPLATE` store.

### 3. Lesson-20 namespace guard (standing debt — high test-suite value)

Per-run pipe/shm namespace (or a held-name skip) so a stale engine's orphaned
`hdaw_plugin_host.exe` children can't collide with proxy-slot tests
(`\\.\pipe\hdaw_plugin_<n>` / `hdaw_plugin_shm_<n>`; slot counter restarts at 1
per instance). The "known to fail five" CrashRecovery tests fail on slot
collision when a stale engine is alive, and this session also observed
`PluginIsolation.LiveDropDrainsStaleOutput` fail in the full suite while
passing in isolation (a stale `HDAW_headless_mcp.exe`, PID 20396, running since
8/18, was alive throughout — left running as it is likely another session's MCP
backend). `mcp-launch.bat` already kills stale engines before copying.

### 4. Beats-vs-seconds ergonomics (handoff #2 item #6 — low priority)

Mostly solved by `wholeSong` placement. Remaining: a `paintToProjectEnd` helper
/ uniform bars-beats acceptance so callers don't hand-convert.

## Standing technical debt (carried)

- **Qt 6.11.1:** the disconnect-during-export fix (`2e37eb6`) works around a
  genuine Qt bug (`QWebSocketPrivate::processData` NULL-deref on re-entrant
  free); re-verify if the engine ever moves off Qt 6.11.1.
- **Stale engine hygiene:** killing `HDAW_headless_mcp.exe` kills that
  session's hdaw MCP backend; killing a parent does NOT kill its
  `hdaw_plugin_host.exe` children on Windows.

## Operational context a fresh session MUST know

- **Audio-device environment:** intermittent machine-wide failure (empty WASAPI
  scan → `routingManager` null → ~29 device-dependent tests fail fast with
  `getTrack(0)==nullptr` + FrontendServer bind failures). ENVIRONMENTAL.
  Recovery: elevated `Restart-Service Audiosrv, AudioEndpointBuilder` (or
  reboot). A cluster of fast `getTrack(0)==nullptr` failures = audio stack
  down, not a regression.
- **Message pump:** every non-GUI process starts `MessagePumpThread` before any
  JUCE construction; block-wait-on-`isExporting()` works BECAUSE the pump is a
  separate thread — never pump on the calling thread.
- **Probe hygiene:** never leave HDAW/plugin-host processes running; attribute
  every log line to a pid (`date`+`pid` fields) before concluding a fix failed.
- **Real-plugin tests:** env-guard `HDAW_REAL_PLUGIN_TESTS=1` + file-exists,
  else `GTEST_SKIP()`. SLOW (~2.5 s per TyrellN6 probe). Plugin isolation ON by
  default; in-process VST3 loading crashes (SEH) — never set
  `HDAW_NO_PLUGIN_ISOLATION` for real VST3 tests.
- **Beats vs seconds:** RPC/notes/clips beats; processors/export seconds;
  convert at boundaries (the composer does; `windowSeconds` is seconds
  everywhere in the composition RPCs).
- **Default project ships 3 EMPTY tracks** ("Track 1", "Synth", "Vocals") —
  scope count assertions to tracks you control; never hard-code absolute clip
  counts.
- **Stale binary trap:** never test against `build/Release/HDAW.exe`; verify
  Debug binary timestamps after C++ changes; the packaged Electron app ships
  its engine from `build/RelWithDebInfo/` (repackage via `frontend\build.bat`).
- **Master gain in tests:** it defaults to 1.0 and is undoable; render-level
  assertions must pin or account for it (finding #6 above).

## Where to look

- Composer implementation: `src/engine/AudioEngineCommands_Composition.cpp`
  (`renderTrackWindow` + `measureWav`/`BandPresence`, `applyPluginProgram`,
  `addInstrumentPart`, `autoGainToTarget` (global-scale branch),
  `auditionPlugin`, `verifyPart`).
- Master gain: `src/engine/MasterBusProcessor.h` (gain stage),
  `src/engine/RoutingManager.cpp:~95` (rebuild restore),
  `src/engine/AudioEngine.cpp:~836` (live listener branch),
  `src/model/ProjectModel.cpp` (`getMasterGain`),
  `src/engine/AudioEngineCommands_Tracks.cpp` (`setMasterGain`),
  `tests/unit/engine/master_gain_test.cpp` (Gate 1/10 + attenuation patterns).
- verifyPart wiring: `Router_Composition.cpp` (`verifyPart` case),
  `McpTools_Project.cpp` (`verify_part`), `tests/unit/engine/verify_part_test.cpp`.
- pluginFormat: `ReadModel.h` `FxSlotSnapshot`, `ReadModelImpl.cpp:~425`,
  `FrontendRpc.h` `toJson(FxSlotSnapshot)`, `McpTools_Audio.cpp` `list_fx`.
- CLAP stubs (item #1 target): `src/engine/CLAPPluginInstance.h:203-206`;
  CLAP host: `src/engine/CLAPHost.*` (`runLifecycleOnMessageThread`,
  `threadCheckIsMainThread`, `audioThreadId`); proxy: `PluginProxySlot.cpp:272-322`.
- Program machinery: `src/engine/PluginParamServiceImpl.cpp`,
  `src/engine/TrackFXSlot.h:753-778`.
- Learnings: `docs/realtime-safety.md`, `docs/pitfalls-juce.md`, AGENTS.md
  lessons 1–22.

## Suggested execution order for the fresh session

1. **CLAP program wiring** (agenda #1) — medium effort, high payoff; Gate 16
   discipline; env-guarded real-CLAP tests.
2. **Role→defaults map** (agenda #2) — small, self-contained composer extension.
3. **Lesson-20 namespace guard** (agenda #3) — standing debt, high test-suite
   value; do it before any future session that runs the proxy tests near a
   long-lived engine.
4. **Beats-vs-seconds ergonomics** (agenda #4) — low priority.

Per hdaw-guard: every code change gets a plan with success gates first,
dependency analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`), pitfall-gate scan, and subagent dispatch with
verification. MCP parity for every new user-facing capability. Tests for every
change. Real-plugin tests env-guarded; never assert per-program audio for VST3
(item #1 may change that for CLAP synths — measure first).
