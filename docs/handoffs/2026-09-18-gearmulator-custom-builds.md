# Handoff: Path 2 — custom gearmulator builds (patch-state delivery + offline fidelity)

Written 2026-09-18 to be executed from a FRESH context. Self-contained: goal, evidence,
sources, deliverables, gates, traps, first-session plan are all below.

## Goal
Modify the five gearmulator emulation CLAPs (build our own from source) so that:
1. Delivered patch data actually reaches the emulated synths (per-device: SysEx dumps,
   program change, or parameter writes).
2. Applied presets persist into OFFLINE renders (today they render as init audio).
3. The per-device ear passes (real corpus patches, kits in compositions/earpass/real/)
   become possible, closing the matrix-preset pipeline's final gate.

## Why (all measured 2026-09-16..18; full trail in docs/plans/2026-09-16-matrix-preset-engine-fixes.md)
- HDAW's matrix-preset pipeline is COMPLETE and committed: per-device sheets (40
  presets each, je8086/xenia/nodalred2x/vavra/virus), morph chains, SysEx/patch writers
  (timbre-lib/{xenia,nord,virus,vavra}_dump.py, virus_fx_pages.py), R3 MCP tools
  (list_matrix_presets/apply_matrix_preset), engine capture/replay + ensureLiveRouting
  + LiveClockDiag.
- The human ear pass over real-corpus-patch kits: NO engine's delivered patches were
  audible. Per device:
  * Xenia: 14 real single dumps -> edit buffer (bank 0x20) via SysEx: renders identical.
  * NodalRed2x: 14 real .syx patches via load_nord_bank: renders identical by ear.
  * JE8086: 44 params applied via setParam (live DSP affected, verified audible live),
    but getStateInformation returns BIT-IDENTICAL boot state -> offline renders init.
  * Vavra: dumps queued, captureStatus=unchanged, render identical.
  * Osirus (model C): offline renders digital silence under ALL programs.
- ROOT CAUSE (unified): HDAW's proxy delivers MIDI via the clap-juce-extensions bridge
  (MidiBuffer -> CLAP events). CLAP 1.x has no SysEx event type -> SysEx dumps are
  dropped at the bridge. CC0/program-change events cross, but ALL five wrappers
  advertise program-list=false (ext-probe) -> program change is a structural no-op.
  Notes and CCs cross fine (phrases play audibly). Additionally JE8086's
  getStateInformation does not serialize param-driven state.
- The engine side of HDAW is FIXED and verified: ensureLiveRouting (deviceless seam),
  captureFxSlotState (deferred + scratch-drive capture), replayAppliedParamOverrides
  (offline param-override replay), LiveClockDiag (processBlock counter). The blocker is
  inside the gearmulator wrappers/emulations.
- The deviceless live graph DOES clock (~100 blocks/s, LiveClockDiag measured) — the
  earlier 'never clocks' hypothesis is refuted; do not re-derive it.

## Sources inventory (both trees complete and buildable)
- /mnt/d/pdf/gearmulator-2.2.9/ — CMakeLists.txt with CLAP targets
  (clap-juce-extensions submodule), base.cmake, CMakePresets.json,
  build_win64.bat/build_win64_vs19.bat, scripts/deploy*.cmake.
  Emulation cores: source/virusLib + source/dsp56300 (Virus TI/C), source/jeLib
  (JE8086 H8S), source/mqLib (microQ), source/xtLib (Microwave), source/nord/n2x
  (Nord Lead 2x). JUCE wrappers: source/{osirusJucePlugin,osTIrusJucePlugin,
  je8086JucePlugin,mqJucePlugin,xtJucePlugin,n2xJucePlugin}.
- /mnt/d/pdf/retromulator-main/ — second source tree (newer in parts; diff before citing).
- Installed CLAPs: C:\Program Files\Common Files\CLAP\{JE8086,NodalRed2x,Osirus,
  OsTIrus,Vavra,Xenia}.clap — built from these sources. ext-probe (all five):
  params=461..6939, program-list=false, preset-load=false, preset-discovery=false.
- HDAW proxy: child = build/hdaw_plugin_host.exe (HDAW's own host; 3 threads,
  READY-before-construct, PREPARE marshaled to the message thread, renderMode).
- ear-pass kits + methodology: compositions/earpass/real/{xenia,nodalred2x}/
  (14 real patches each, index.md with Keep? columns), driver
  compositions/earpass/.scratch/earpass_engine.py (stateless MCP HTTP client with
  track discovery + resumable rendering).

## Path 2 deliverables (priority order)
1. **JE8086 state serialization** (S/M, unblocks the most): jeLib/je8086 wrapper —
   investigate why getStateInformation misses param-driven changes (params write the
   emu per parameterDescriptions_je mapping; serialize likely reads the ROM patch
   buffer instead of live memory). Fix so presets persist (tree capture becomes
   non-boot -> offline renders + save/load both work).
2. **Program exposure, all wrappers** (M): expose the emulations' internal banks as
   JUCE programs (virusLib Microcontroller holds m_singles/m_multis = complete banks;
   n2x has banks; mq/xt have patch memories) so CC0+PC and load_virus_preset /
   load_nord_bank genuinely switch patches, offline included. This alone revives the
   Nord + Virus ear passes with REAL patches.
3. **Osirus model-C offline silence** (M): C-path OS bring-up advances only with
   processed audio; offline children get notes at block 0 of a fresh child. Options:
   pre-warm pump after PREPARE in the offline child, or reuse-warm-instance for
   unchanged projects (careful: contradicts fresh-domain design; see fixes doc).
4. **Vavra/Xenia external dump delivery** (M-L): HDAW-side delivery DONE 2026-09-18 -
   `apply_preset` gained a **WaldorfSysex** route (`src/mcp/PresetRoute.h` + `PresetFileParser.h`):
   F0 3E 0E/10 dumps are header/F7/size-validated, split, and queued via `send_fx_midi`
   into Xenia/Vavra plugin slots (tests: `ApplyPresetResolver.WaldorfSyxIntoXeniaAndVavra`,
   `PresetFileParser.SplitsConcatenatedWaldorfXeniaDumps`, `..WaldorfValidationRejectsWrongMachineAndTruncatedDump`;
   apply_preset help text updated). **LIVE-APPLICATION GATE PASSED 2026-09-18**:
   `FxMidiInjection.XeniaEditBufferDumpChangesOfflineRender` (real Xenia.clap,
   dumps built by xenia_dump.py from the real cobalt.\u00b5sb bank) rendered
   fresh-boot rms=0.0357 vs dumpA(Cobalt Blue)=0.0542 vs dumpB(Analog Bass)=0.0362
   — deterministic seed, only the injected dump varies, so the 265 B edit-buffer
   dumps DO retarget the live Xenia child. **DURABILITY FIXED 2026-09-18 via
   preset-sysex replay**: the plugin-state capture route is a dead end (the XT
   single cache is editor-request-driven; `getStateInformation` stays a 279 B
   boot stub -- measured `containsPatchName=no` even with gearmulator mirror
   hooks in xtState/xtDevice). Instead, `sendFxMidi` now persists the RAW dumps
   on the slot (`IDs::presetSysex`, length-prefixed base64) and Track.cpp replays
   them into every fresh child at restore. Gate numbers (real Xenia.clap md5
   71687993…, installed 2026-09-18; original backup
   gearmulator-git/Xenia.clap.bak-2026-09-18 md5 75302c8d…): live boot->dumpA
   Δ0.0058 / dumpA->dumpB Δ0.0057; presetSysex written (368 B); rebuilt-from-tree
   FRESH child Δ0.0067 vs factory (replay heard!); offline export Δ0.0013.
   Regression: 57 tests green (TrackFX rebuild, PluginStateSaveLoad, FxChainPreset,
   ApplyPreset, PresetFileParser). Gearmulator hooks (xtState::noteIncomingSingle
   + xtDevice::process mirror) remain in gearmulator-git as harmless upstream
   improvements. Artifacts: `compositions/xenia-ab/{dumpA,dumpB}.syx`.
   **VAVRA FIXED 2026-09-19** - the "mq side ignores external dumps" assumption
   was WRONG: mqLib/mqDevice forwards injected sysex into the microQ OS
   (sendMidi -> m_mq.sendMidiEvent). The 2026-09-16 "NOT APPLYING" was buffer
   TARGETING: real bank dumps carry 0x30 (multi-edit) / 0x40+ (bank) buffer bytes
   while the microQ OS only plays the single-mode edit buffer (0x20); as-is
   injection loaded a buffer the current sound never reads. Fix: runWaldorfSysexFile
   (PresetRoute.h) retargets 392-byte microQ dumps to 0x20/0x00 + recomputes the
   Waldorf checksum (sum [4..390) & 0x7f) before queuing - identical to what
   mqController::sendSingle does for the editor. Gate
   `FxMidiInjection.VavraEditBufferDumpChangesOfflineRender` (real Vavra.clap,
   real rhythm-lab dump 'Arp/Acid bender   CJ Arp.syx'): live rms 0.00945 ->
   0.01512 (D0.0057), presetSysexLen=538, rebuilt-from-tree replay D0.0054 vs
   fresh boot - save/load/rebuild/export all reproduce the patch. No gearmulator
   change required; mqLib/xtLib dump RX stays untouched.


   **OSIRUS MODEL-C OFFLINE SILENCE — NEGATIVE RESULTS 2026-09-19 (do not
   repeat these experiments)**: implemented the doc's option (a) child-local
   warmup pump (PluginHost.cpp PREPARE handler, Virus-family scoped, SEH
   guarded, real-time paced; env knobs HDAW_NO_CHILD_WARMUP /
   HDAW_CHILD_WARMUP_SECONDS; parent kPrepareTimeoutMs 5000 -> 45000). Also
   tested the init-silent-boot-patch theory: the Virus C boots with
   'Ch 1 Channel Volume' def=0 (probe report); setParam to 1.0 reaches the
   child's param cache (0 -> 1 verified) but the solo render stays at exact
   floating-point dust (3.09e-06) before and after. Warmup variants tested:
   0.64 s, 12 s at CPU speed (310 ms wall), 12 s REAL-TIME paced (exact 12 s
   wall, sleep-until-target) — all render identical dust. Conclusion: the
   model-C OS post-boot state in the headless/offline child never reaches
   'plays notes', independent of warmup, patch volume, or pacing. This needs
   the interactive LCD investigation (open the Osirus editor, watch the OS
   display during a live session vs an offline child) or dsp56300 OS-level
   reverse engineering. The warmup pump + env knobs stay in the tree (harmless,
   enable future experiments without rebuilds); Osirus offline silence remains
   EMULATOR-BLOCKED.

   **NODALRED2X PARAMS EXPOSED 2026-09-19**: same recipe (33 curated sound
   params public in parameterDescriptions_n2x.json + rebuilt NodalRed2x.clap
   md5 a85221e2...). Gate
   `FxMidiInjection.NodalRed2xHostParamsChangeRender`: exposed=362 (33 x 11
   parts), Cutoff set audible (same-child render D0.00039). NL2x has no
   onboard FX but filter/env/LFO/mod-env automation is now live via
   set_fx_param. Backup: gearmulator-git/NodalRed2x.clap.bak-2026-09-19
   (md5 0007b703...).

   **HOST PARAMETERS EXPOSED 2026-09-19 (both devices; kills limitation
   #1 'no params to automate')**: the zero-param symptom was three layers:
   (1) `parameterDescriptions_xt.json` had only 5 `isPublic:true` params and
   `parameterDescriptions_mq.json` had 1 (Version) - host exposure is gated
   purely by `isPublic` (controller.cpp registerParams). Curated sets made
   public: XT 66 sound/FX params (filters, envelopes, EffectType/ParamA-C,
   MixRingMod, LFOs, arp, glide, DelayTime, Pan...), microQ 96 (filters +
   FilterEnv, FX1Type/Mix + chorus/flanger/OD/phaser/reverb subparams,
   FX2Type/Mix, amp/env, osc tuning). (2) WSL->Windows drvfs staleness: the
   first mq rebuild embedded the PRE-curation json (BinaryData regen read old
   bytes); fixed via Windows-side copy + mtime bump + forced BinaryData regen
   (AGENTS sync recipe). (3) HDAW proxy cap: PluginProxySlot::
   fetchParamMetadata refused n > 4096 - Vavra exposes 7557 (96 x 16 parts) -
   raised to kMaxProxyParams 16384 (staged arrays are heap-sized to n).
   Gates (real CLAPs, HDAW_REAL_PLUGIN_TESTS=1):
   `FxMidiInjection.XeniaHostParamsChangeRender` exposed=2151, F1Cutoff 1.0 ->
   0.1 via PluginParamService::setParam (OS applied), same-child render
   D0.0049; `FxMidiInjection.VavraHostParamsChangeRender` exposed=7557,
   F1Cutoff set + same-child render D>1e-5. Binaries: Xenia.clap md5
   237ab777..., Vavra.clap md5 10849d42... (backups
   gearmulator-git/{Xenia,Vavra}.clap.bak-2026-09-19).
5. **Build provenance**: build our CLAPs via the gearmulator CMake (Windows/MSVC;
   build_win64.bat or cmake preset + CLAP targets), install to
   C:\Program Files\Common Files\CLAP\ (backup originals first), record per-binary
   md5 + source revision HERE and in the va-suite doc.

## Gates (per device, je8086 first)
- G1: offline render of an APPLIED preset audibly differs from the init render
  (by ear + rms/md5); the ear-pass kit flow re-runs end to end.
- G2: HDAW suites green after swapping CLAPs: MatrixPresetsTest, McpCoverageTest,
  proxy suites (children must stay compatible with HDAW's proxy: param count/order
  ideally unchanged; re-probe after build).
- G3: save/load fidelity: applied preset survives save -> load -> offline render.
- G4: no DSP/CPU-math changes (state/MIDI plumbing only); per-device diffs documented.
- G5: provenance (md5 + source rev) recorded; originals backed up.

## Traps
- The emulated cores are timing-sensitive (dsp56300, H8S): DO NOT touch DSP/CPU math
  or timing; all changes at the MIDI/state-serialization boundary.
- Lesson 16: CLAP lifecycle on the reported main thread; the proxy host's 3-thread
  structure (PluginHost.cpp) — preserve it.
- clap-juce-extensions: advertised capabilities (program-list, preset-load) require
  BRIDGE implementation, not just plugin-side intent — adding them means implementing
  the clap extension in the bridge layer.
- The D-lite guard (stateLooksUnchangedSinceBoot) intentionally blocks persisting
  boot-identical captures; with wrapper fixes, real captures flow naturally.
- HDAW proxies isolate every instance (unique namespace prefix); offline renders use
  a fresh domain per render (fresh children) — warm-state approaches contradict that
  design; prefer making fresh children receive state (patch delivery + param replay).
- Earlier 'verified to change the render' claims for nord/virus loaders came from
  gtest harness renders; by ear they did not (2026-09-18). Trust by-ear + metrics.

## First-session plan (new context)
1. Baseline-build the gearmulator tree (build_win64.bat or cmake preset; confirm CLAP
   targets + where outputs land). Back up the installed CLAPs.
2. JE8086: locate je8086 wrapper's getStateInformation/setStateInformation + the
   param->emu write path (parameterDescriptions_je mapping); answer WHY serialized
   state misses param writes; implement the fix; validate: HDAW apply -> offline
   render changes (use the ear-pass kit flow).
3. Then programs exposure (deliverable 2) -> Nord/Virus ear passes with real patches.
4. Re-run the matrix-preset ear passes per device (kits + methodology above);
   curate; commit.

## Pointers
- docs/plans/2026-09-16-matrix-presets.md, docs/plans/2026-09-16-matrix-preset-engine-fixes.md
  (HDAW side complete, all findings).
- docs/plans/2026-09-17-offline-param-replay.md (the replay design this builds on).
- timbre-lib/{xenia_dump,nord_dump,virus_dump,vavra_dump}.py — the per-device delivery
  formats (writers).
- compositions/earpass/real/ + .scratch/earpass_engine.py — ear-pass kits + driver.
- src/proxy/ (PluginHost.cpp, PluginProxySlot.cpp) — HDAW's child/proxy internals.
- tests: MatrixPresetsTest (MCP seam), FxMidiInjection.NordBankLoadChangesNodalRed2xRender
  (the historical render-change assertion — re-verify by EAR, not just bytes).

## Execution note — JE8086 JPAR build installed (2026-09-18)
- Source tree actually built: `/mnt/d/pdf/gearmulator-git` (commit `6ff5ef3b`; existing untracked probe files left untouched). The nominal `/mnt/d/pdf/gearmulator-2.2.9` tree had empty submodule dirs and could not configure.
- Patch: JE8086 wrapper writes/reads a `JPAR` v1 plugin-state chunk containing exposed JUCE parameter values, replayed with `Origin::PresetChange`; no DSP/CPU/processBlock changes.
- Build: VS 18 bundled CMake, configured tree `temp/cmake_vs2026`, target `jeJucePlugin_CLAP`, Release.
- Installed: `C:\Program Files\Common Files\CLAP\JE8086.clap`.
- Installed md5: `15001c1fe9139f5fa83f4edcff5d7750` (size 36,156,416 bytes); binary contains `JPAR` marker.
- Backup of previous installed CLAP: `C:\Program Files\Common Files\CLAP\backup-hdaw-20260918-je8086\JE8086.clap.pre-jpar.20260918-183511`, md5 `f4a19cd63f0963a30238829a69fc80dc` (size 34,491,904 bytes).
- Validation gate PASSED (2026-09-18, HDAW MCP HTTP): `add_instrument_part` with `CLAP-JE8086-3429505-0` exposed 461 params; `apply_matrix_preset` b44052f76c82a7a7 applied 46 params and captured `status=ok stateBytes=5419`; offline 10s renders differed from init (`00_init.wav` md5 `1dc838f6cbaed823bd4822865f3f009a`, rms 0.03215 -> `01_after_apply.wav` md5 `d2615e255f45f1c65f25cb94c30451a0`, rms 0.01587); save/load persisted the applied sound (`02_after_load.wav` md5 `1de2d97ac2e5c5764a356bf81a7fa783`, rms 0.01890). A second preset 47e01d5cf2091704 rendered distinct (`03_second_preset.wav` md5 `d425fefe09b269b604e5864fe48ab924`, rms 0.009998). Artifacts: `compositions/je8086-jpar/`.

## Osirus (Virus C) offline silence — ROOT-CAUSED + FIXED (2026-09-19)

The dsp56300-level investigation resolved the Osirus silence that blocked the
Virus apply/ear pass. Full writeup: `docs/hardware-va-suite.md` §9.

- **Symptom:** Osirus (C) rendered exact digital silence offline (`rms == 0`, or
  `3.09e-06` float dust) for every note/patch/param write, while OsTIrus (TI)
  rendered `rms 0.042` through the identical wrapper and proxy path — which
  exonerated the host-side injection/proxy/param bridges.
- **Root cause (emulator boot state, not the host):**
  `virusLib/microcontroller.cpp::createDefaultState()` writes the boot patch by
  dumping `m_singleEditBuffer` verbatim to the OS edit buffer, and that member is
  value-initialized (`TPreset m_singleEditBuffer{}`, `microcontroller.h:118`) =
  **512 zero bytes**. The OS booted on an all-zeros patch — every oscillator
  level, envelope level, filter cutoff and channel volume at 0 — so the DSP ran
  correctly and produced silence. The earlier "parameter awakening" result
  (Channel Volume cache 0 -> 1 with zero render delta) is thus explained:
  unmuting one channel cannot make sound from oscillators at level 0.
- **Why only the Virus:** cross-lib audit — `n2xLib` builds real defaults
  (`State::createDefaultSingle()` copies `g_singleDefault`), `xtLib` (Xenia) and
  `mqLib` (Vavra) keep no value-initialized edit buffer. virusLib was the sole
  lib booting from zeroed state.
- **Fix:** `gearmulator-git/source/virusLib/device.cpp` (Device ctor, after
  `createDefaultState()`): `m_rom.getSingle(0, 0, romPatch)` then
  `m_mc->writeSingle(BankNumber::EditBuffer, SINGLE, romPatch)` — load ROM
  factory patch A-0 as the boot patch. Guarded with `if (!m_rom.isTIFamily())`
  so the TI path stays byte-identical (TI already boots audible).
- **Evidence (A/B):** pre-fix `rms` = `3.09492e-06` (gates 1-2) / `0` (gates
  3-6); post-fix `rms` = **`0.0471329`** (gate 7 + final). Built via
  `build_osirus_clap.bat` (target `osirusJucePlugin_CLAP`, Release); installed
  `C:\Program Files\Common Files\CLAP\Osirus.clap` md5
  `60ad7cf8c3bf50d867d60dd37194f437`. Gates
  `FxMidiInjection.OsirusBootPatchAwakening` and
  `FxMidiInjection.BootStateBaselineGuard` both **OK**.
- **Blast radius / regression check:** OsTIrus measured before and after — the
  same binary reproduced `rms 0.042`, and its two flaky preset tests
  (`OsTIrusPresetChangeReflectsInChildParams`,
  `OsTIrusInjectionCapturesToTreeAndSurvivesRebuild`) fail on the **untouched**
  pre-change binary too, at shifting lines (186 `a.ok` vs 311 delta-0) — that is
  pre-existing flakiness plus the documented F-A injection gap, not a regression.
- **Superseded conclusion:** finding F-A (`load_virus_preset` queues but does not
  change renders) was measured against the silent slot — every comparison was
  0-vs-0. F-A is now **unproven** and must be re-measured on the audible build.
- **RESOLVED (2026-09-20)**: the decisive fix was the **`stateSet` SHM ring** —
  plugin state previously travelled over the control pipe and timed out (the
  child's control thread is blocked by the 12 s Virus OS warmup), so the restored
  state never reached the plugin. The wrapper `OBST`/`PRGS`/`JPAR` chunks, the
  `addMidiEvent` CC0+PC tap, the HDAW capture fixes and the stopped-transport
  MIDI flush are all still required, but the round-trip only works with the ring
  (`SET_STATE published to shm ring` → `state ring applied` in ~30 ms; gate
  deltas ~0.019, deterministic 3/3). Full writeup + evidence:
  `docs/hardware-va-suite.md` §9 CORRECTION.
- **Follow-up (2026-09-20)**: the render/export budget now accounts for the
  virus OS warmup (`ExportManager::computeBakeWaitMs` adds
  `HDAW_CHILD_WARMUP_SECONDS` × virus-slot count, after the 120 s cap) — the
  OsTIrus audibility test failed with `render timed out` because bake+window+5 s
  is shorter than one 12 s warmup. `OsTIrusRenderAudibility` now passes.
- **Original (superseded) note**: wrapper `OBST` (OS arrangement) +
  `PRGS` (ROM program selection, from a new `Processor::addMidiEvent` CC0+PC tap)
  + `JPAR` (host params) chunks; HDAW capture/save self-baselining fix
  (`hasBootBaseline`, boot baseline seeded at `TrackFXSlot::prepare`, warm-clock
  capture retries, empty-state tolerance); stopped-transport MIDI flush in
  `sendFxMidi` (scratch blocks → SHM midiIn ring — the buzz-guard had stopped the
  only ring writer). Gate now prints `VERDICT: preset load CHANGED the offline
  render` + `PHASE2 VERDICT: host-param write ROUND-TRIPS`; OsirusBootPatchAwakening
  still green. Full writeup: `docs/hardware-va-suite.md` §9.
- **Session close (2026-09-20) — the FxMidiInjection suite is green.** Two more
  test-instrument retargets closed the last reds:
  `OsTIrusPresetChangeReflectsInChildParams` now asserts the child's serialized
  **state** (the TI's OS does not echo PC-loaded patch parameters into the host
  cache: `paramCacheChanged=0 stateChanged=1`, state 263387 -> 263406 B, the +19 B
  being the `PRGS` chunk), and `NordBankLoadChangesNodalRed2xRender` asserts the
  persisted dumps + a settled receipt instead of a `pluginState` blob
  (`captureStatus=unchanged pluginStateLen=0 presetSysexLen=33114` — the n2x
  serialized state does not reflect its volatile patch RAM; the bank round-trips
  via the `presetSysex` replay path). Every test in the suite has now passed; the
  last full run was 18/19 with the single red a marginal-threshold flake
  (`NodalRed2xHostParamsChangeRender`: delta 4.81e-06 vs the 1e-05 threshold,
  passes 2/2 standalone). **Known systematic weakness:** several render-delta
  thresholds sit at or below run-to-run variation (table in
  `docs/hardware-va-suite.md` §9) — a shared noise-floor-based delta helper would
  remove the remaining flakes. Environment note: the RDP audio endpoint dropped
  for ~30 min during the session (`devState=none`, `"Error opening Primary Sound
  Driver: 'No driver'"`), failing every live-graph test with
  `sendFxMidi failed: track not found: 0`; it recovered on its own. Two full-suite
  runs also lost the process mid-test (Xenia, then Vavra) while the same tests
  passed in other runs — load-related, not logic.
