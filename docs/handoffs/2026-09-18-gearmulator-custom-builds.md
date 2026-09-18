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
4. **Vavra/Xenia external dump delivery** (M-L): route external single-dump handling
   into mqLib/xtLib state (receive paths exist in the libs), or expose a custom CLAP
   extension/param for dump delivery. Vavra's mq side ignores external dumps today.
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
