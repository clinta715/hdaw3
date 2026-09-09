# Plan: Master Bus FX Chain (v0.31.x) — ✅ COMPLETE 2026-09-08

## Result
Loudness goal achieved through dynamics, not faders: RMS **-20.6 → -12.7 dBFS**
(+7.9 dB) with peak -5.1 → -4.7 dBFS (headroom intact, no clipping).
Render: `compositions/FINAL_v9_louder.wav` (limiter threshold -12, master EQ +2 dB @ 1.2 kHz).

## Gate results (all pass)
- [x] Gate 1 (rebuild restore): `MasterBusFx.RestoredAfterRoutingGraphRebuild`,
      `RebuildRestoresBypassedDefaults` — LIVE processor asserted.
- [x] Gate 2 (full-path trace): `LimiterCapsPeakWhenEnabled` (JUCE Limiter
      ceiling = 0 dBFS via compressor+makeup+hard-clip; see lesson below),
      `EqBoost/CutRaisesRms` + live render RMS observable.
- [x] Gate 3 (audio-thread safety): atomics + per-block coefficient recompute;
      no alloc/lock in the master processBlock.
- [x] Gate 4 (lesson-23 clamp): `ParamWriteClampsToDefs`,
      `OutOfRangeParamIndexIsRejected` (Gate 9 parity in the command itself).
- [x] Gate 5: 10/10 MasterBusFx + 79/79 McpCoverageTest + TrackMixerState,
      ExportVolumeBypass green. FrontendServer has PRE-EXISTING order-dependent
      flakes (different failures per run; every test passes in isolation) —
      not caused by this change.

## Bonus fixes surfaced
1. **MCP `load_project` bypassed the command layer** — called
   ProjectSerializer::load directly, skipping ALL load migrations (trackType,
   MASTER_FX) and load-progress broadcast. Now routes through
   `ProjectCommands::loadProject` (AGENTS.md MCP-parity rule).
2. **Backward-compat migration** — `ProjectModel::ensureMasterFxNode()` stamps
   the default chain into pre-master-FX projects on load (idempotent, tested:
   `EnsureMasterFxNodeIsIdempotent`).

## JUCE Limiter semantics (lesson, verified against juce_Limiter.cpp)
`juce::dsp::Limiter` = 2 compressor stages + threshold-derived MAKEUP gain
(`+(-threshold) dB`) + hard clip at ±1.0. The output CEILING is always 0 dBFS;
the threshold parameter controls loudness drive (compression + makeup), NOT
the ceiling. Test ceiling contracts accordingly: input >1.0 → output ≤1.0
enabled; passthrough bypassed.

## Design
- MASTER_FX ValueTree node (project-root child). Children = FX_SLOT-style
  nodes: fxType in {eq, compressor, limiter}, param_0..N, bypassed, slotIndex.
- Default project stamps MASTER_FX with 2 slots (eq, limiter), both bypassed.
- MasterBusProcessor owns the DSP: params via std::atomic<float>; per-block
  coefficient recomputation (no locks). juce::dsp::Compressor/Limiter members
  set off-thread (existing TrackFXSlot practice).
- Listener: MASTER_FX property changes forward to live masterBus; child
  add/remove escalates to rebuildRoutingGraph (fullSync fallback automatic).
- RoutingManager rebuild: re-apply master FX from tree after setGain (Gate 1).
- Commands: setMasterFxParam(slot, param, value) [clamped, undoable,
  returns clamped value], setMasterFxBypassed(slot, bool).
- MCP: set_master_fx_param / set_master_fx_bypassed / get_master_fx_params.

## Param defs (common/MasterFxDefs.h — shared model/engine/MCP)
- eq: Frequency 20-20000Hz (1000), Q 0.1-10 (0.7), Gain -24..+24dB (0)
- compressor: Threshold -60..0dB (-18), Ratio 1-20 (4), Attack 0.1-100ms (5),
  Release 10-1000ms (120)
- limiter: Threshold -24..0dB (-3), Release 1-500ms (80)

## Files
- src/common/MasterFxDefs.h (new) — defs + clamp helper
- src/engine/MasterBusProcessor.h — FX chain (lock-free)
- src/model/ProjectModel.{h,cpp} — IDs::MASTER_FX + ensureMasterFxNode
- src/engine/AudioEngine.cpp — listener branches (property/child add/remove)
- src/engine/RoutingManager.cpp — Gate-1 restore in rebuild
- src/engine/AudioEngineCommands.{h,Tracks.cpp} — commands
- src/mcp/McpTools_FxSlot.cpp — 3 master FX tools
- src/mcp/McpTools_ProjectSaveLoad.cpp — load routed through command layer
- tests/unit/engine/master_bus_fx_test.cpp (new, 10 tests)
- tests/integration/mcp/mcp_coverage_test.cpp — MasterFxToolsRoundTrip
- tests/CMakeLists.txt — suite registered
