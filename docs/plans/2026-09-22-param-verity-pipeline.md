# Plan: ParamVerity — audio analysis + synth verification pipeline

Date: 2026-09-22
Status: Phase 1 implementation (this document's §Phase 1)
Lessons feeding this: AGENTS.md lesson 27 (audit renders are tree-copy exports; live-only
writes are not inputs; only multi-x separations count as audibility proof),
docs/va-suite-status-log.md (measured same-input spreads 4.1e-07 (Vavra) up to 0.0056 on
~0.05 RMS (Xenia, ~11%); one Xenia render moved ~17% between runs).

## Goal

A deterministic, machine-readable probe pipeline that answers: **does this parameter do
what we think it does — audibly — through a real offline render?** Extends the
lesson-27 discipline from one-off gate tests into a reusable per-(device, param) corpus,
plus tone-level checks (envelope/pitch/LFO) in later phases.

Decisions (user, 2026-09-22): full corpus eventually; plan doc then implement;
deterministic verdicts first — local LLMs are NOT in the verdict path (optional prose
summarization later at most).

## Existing assets (reuse, do not rebuild)

| Asset | Location | Role in pipeline |
| --- | --- | --- |
| `renderTrackWindow` | `AudioEngineCommands_Composition.cpp:360` | offline solo render of a tree copy window (the lesson-27 input) |
| `set_fx_param` | `AudioEngineCommands_Fx.cpp` + MCP `set_fx_param` | durable param write (internal: `param_N` ValueTree; plugin: `appliedParamOverrides` ledger replayed into fresh children) |
| `MixReportAnalyzer` | `src/engine/MixReport.h` | RMS/peak/4-band energy measurement of a rendered window |
| `verify_part` | MCP `McpTools_CompositionInstrument.cpp:288` | single-window probe precedent (payload shape + audibility fields) |
| `mix_diff` | `McpTools_AudioRead.cpp:602` | per-band A/B comparison precedent |
| `list_fx_params` | `DeviceParamMap`/FxSlot | param defs + durability tags (pipeline adds *audibility* evidence) |
| `FxMidiInjection.*` gtest gates | tests | the per-device gate precedent this generalizes |

## Success gates (Phase 1) — STATUS 2026-09-22: ALL PASS

- [x] G1: `param_verity` MCP tool + `composition.verifyParamSweep` RPC route exist and
      return identical payload shapes (shared builder in `src/common/ParamVerity.cpp`).
- [x] G2: Saturator Drive sweep (0 dB vs 40 dB) verdict `audible:true` at >= 3x the
      baseline spread — `ParamVerity.SaturatorDriveSweepIsAudibleAndRestored`.
- [x] G3: Bypassed-slot path reports `anyAudible:false` with an audible dry baseline;
      a silent baseline reports `inconclusive` (lesson 25 encoded in math + engine).
- [x] G4: Probe restores the original param value (internal: ValueTree property assert;
      plugin: ledger restore/remove) — `restored:true` asserted in tests and live.
- [x] G5: Verdict math unit tests incl. the Xenia-style spread>separation case
      (`ParamVerityMath.*`, 6 tests).
- [x] G6: Parity ledger regenerated (293 tools / 381 RPC; `param_verity` mapped to
      `composition.verifyParamSweep` via the ALIASES table); ratchet passes.
- [x] G7: Targeted suites green: ParamVerity* + ParamVerityMath + RpcParityRatchet.*
      + SongPlanRpcTest.* + MovementPlan.* = 28 passed.

## Live evidence (deployed 0.37.0 engine, remix project)

- psy_fm `Feedback` sweep on the hook track: spread 0, max-step delta -2.9e-3 →
  audible:true; restored:true.
- psy_fm `OP5 Release`: no audible separation — honestly reported not-audible
  (the patch carries no OP5 energy in the probed window).

## Implementation note (deviation from §Phase 1 step 2)

`verifyParamSweep` lives in `AudioEngineCommands_Composition.cpp` (next to `verifyPart`)
instead of a new `_Verity.cpp` TU: `renderTrackWindow` is file-local (anonymous
namespace), and moving it was higher-risk than co-locating. Math + payload builder are
still shared in `src/common/ParamVerity.{h,cpp}` per the MixReportJson contract.

## Dependency map

- New shared shaping: `src/common/ParamVerity.{h,cpp}` (verdict math + payload builder;
  single builder behind MCP + RPC per the MixReportJson precedent).
- Engine command: `ProjectCommands::verifyParamSweep` (virtual, like
  `auditionPlugin`) implemented in `AudioEngineCommands` — calls `renderTrackWindow`
  (Composition.cpp) + the MixReport analyzer; param writes via the SAME path as
  `set_fx_param` (durable both flavors).
- MCP tool: `McpTools_FxSlot.cpp` (param domain lives there) `param_verity`.
- RPC: `composition.verifyParamSweep` in `Router_Composition.cpp` (auditionPlugin lives
  there — same community seam).
- Blast radius: no DSP, no processBlock, no graph rebuilds (renders use the existing
  offline export path). Projections: ValueTree param writes transient, restored by G4;
  no ReadModel/audio-graph new state.
- God nodes in scope: none new touched (AudioEngineCommands is a hub — additive virtual
  only; verified additive by the `auditionPlugin` precedent).

## Pitfall gates triggered

- Gate 2 (silent path): verdict math + write path + render path all asserted by tests;
  the tool errors (not silently empty) when the track has no clip content (G3).
- Gate 3 (audio-thread): probe runs on the command thread; renders reuse the existing
  offline export machinery (message-pump-safe, lessons 11/12 already handled there).
  No new audio-thread work.
- Gate 5/6 (lesson 25 silence trap): baseline renders are asserted non-silent; a silent
  baseline yields `inconclusive`, never `audible:false` presented as evidence.
- Lesson 27: verdict threshold is `separation >= kSeparationFactor * spread` with
  `kSeparationFactor = 3.0` (NodalRed2x 3.1-8.2x precedent) and an absolute floor
  (`kMinSeparation = 1e-4`); per-step results carry raw numbers so the agent can apply
  stricter factors (e.g. 8x) when a device is known jittery.
- Lesson 23 (clamping): param writes go through the existing clamped `set_fx_param`
  path — no raw values reach DSP.

## Phase 2 — tone verification (`tone_verity`, 2026-09-22)

Goal: verify that tones behave as intended — envelope shape, modulation rate, pitch,
spectral trajectory — from the SAME offline render path as Phase 1.

`src/common/ToneVerity.{h,cpp}` analyzes one rendered WAV and reports measured facts:
- **Envelope**: binned RMS trace (default 10 ms bins), `attackMs` (first 10%→90% of peak
  crossing), `sustainRatio` (mean of last-25% bins / peak), `trailingSilenceSeconds`.
- **AM rate**: FFT of the mean-removed envelope trace → `modRateHz` + `modProminence`
  (peak/median in 0.05–25 Hz) — verifies LFO/tremolo/pump rates end-to-end.
- **Spectral trajectory**: frame-level spectral centroid; `centroidStart`/`centroidEnd`
  (first/last quarter means) — verifies filter sweeps.
- **Pitch**: harmonic-product-spectrum f0 estimate at the loudest frame → `f0Hz`,
  `f0Confidence`, nearest MIDI note + cents.

Optional expectations in the request (NaN sentinel = absent, VerifyPart convention):
`attackMsMin/Max`, `sustainRatioMin`, `modRateHz` ± `modRateTolPct` (default 10%),
`centroidRiseMin` (end/start), `f0Hz` ± `f0CentsMax` (default 50 cents). Response carries
`expectations:[{name, pass, measured, expected}]` + overall `pass` (vacuously true when
no expectations are supplied; `expectationsChecked` counts).

### Phase 2 success gates — STATUS 2026-09-22: ALL PASS

- [x] H1: `tone_verity` MCP tool + `audio.verifyTone` RPC route, identical payloads via
      the shared builder; parity ledger regenerated (294 tools, `tone_verity` mapped) and
      ratchet green.
- [x] H2: Pure analyzer tests on synthesized WAVs: 220 Hz sine → f0 within 1 Hz (MIDI 57);
      3 Hz AM → modRate within 5% + depth; two-tone file → centroid ratio > 4;
      ramped tone → attackMs in range; trailing silence measured.
- [x] H3: Engine test — fm_synth instrument renders a measurable envelope
      (`ToneVerityEngine.FmSynthEnvelopeIsMeasurable`).
- [x] H4: Engine test end-to-end modulation: track volume LFO (unsynced 3 Hz sine,
      depth 0.6, targetParamID 1) detected at 3 Hz ± 15%
      (`ToneVerityEngine.VolumeLfoRateIsDetectedEndToEnd`).
- [x] H5: Live probe on the deployed engine (remix project): bass sub_synth f0 = 55.0 Hz
      (A1, MIDI 33, -2 cents) expectation PASS; pad breakdown sustain 0.4 PASS;
      hook measured D5/587 Hz, attack 10 ms, amDepth 1.3; riser-track finding below.

### Live finding (riser = downlifter)

The Riser/Down FX track's centroid FALLS across build2 (4302 → 596 Hz, rise 0.1): the
"Buildup" psy_fm phrase cell is behaving as a downlifter, not a riser. The tool caught a
real composition-intent mismatch on its first live run — exactly its purpose. Fix is
content-level (reroll/re-recipe the riser cells or add a cutoff-rise automation lane).

### Implementation notes

- JUCE `performRealOnlyForwardTransform` requires a 2×fftSize float buffer — F+2 was a
  live crash caught by the analyzer tests before deployment.
- HPS pitch estimation ties on harmonic-free tones (all 220/k subharmonics score
  identically); f0 uses lowest-strong-peak (within 3 dB of max) instead, with confidence.
- `modProminence` is gated on a new `amDepth` metric ((max-min)/mean over the central
  80% of envelope bins) — a hard onset/end is not modulation.

## Phase 3 — batch corpus runner (`param_verity_corpus`, 2026-09-22)

Goal: sweep MANY parameters of ONE slot in a single call, aggregating Phase-1 verdicts
into a machine-readable audibility sidecar — the corpus a per-device audit loop reads.

`verifyParamCorpus(trackIndex, slotIndex, paramIndexes?, maxParams, sweep args, outPath?)`:
- param enumeration: explicit `paramIndexes`, else ALL internal defs, else the first
  `maxParams` (default 16, hard cap 128) of the live plugin param cache.
- runs `verifyParamSweep` per param (baseline renders reused per param), aggregates
  `{ran, audibleCount, results:[{paramIndex, paramName, anyAudible, maxAbsRmsDelta,
  baselineRms, spread}]}`.
- `outPath` (optional) writes the FULL per-step payload as a sidecar JSON
  (`hdaw.param.verity.corpus.v1`); agents feed audibility evidence into composition
  decisions and `list_device_params` review (tag integration = Phase 3b).

### Phase 3 success gates — STATUS 2026-09-22: ALL PASS

- [x] P1: `param_verity_corpus` MCP tool + `composition.verifyParamCorpus` RPC route,
      shared payload; parity ledger regenerated, ratchet green.
- [x] P2: Engine test — saturator corpus over ALL 6 defs: every param gets a verdict,
      strong params (Drive/Mix) read audible, sidecar file written and re-readable.
- [x] P3: Bounded enumeration proven (maxParams respected; unknown indexes reported,
      not silently dropped).
- [x] P4: Deployed-engine live corpus run on the remix project (psy_fm hook slot 0),
      sidecar written under `compositions/verity/`.
- [x] P5: Riser intent mismatch FIXED at content level and re-verified with
      `tone_verity` (centroidRise >= 1.2 across build2 on the riser track).

## Non-goals (Phase 1, unchanged)

- Batch corpus runner over all devices — Phase 3 (builds on Phase 1 primitives).
- Async job wrapping (`poll_job`) — only if a probe with real-plugin warmups exceeds the
  10 s bridge timeout in practice; Phase 1 keeps windows small (default 2 s) and counts
  low (2 baseline + 3 steps).

## Phase 1 steps

1. `src/common/ParamVerity.h/.cpp`: verdict math (pure) + JSON payload builder.
2. Engine: `verifyParamSweep` in AudioEngineCommands (new `_Verity.cpp` translation unit;
   add to CMake source list) + `ProjectCommands.h` structs + virtual.
3. MCP `param_verity` tool + RPC `composition.verifyParamSweep` route.
4. Regenerate parity ledger; add twin-test for arg-name parity if a mapped route probe
   applies.
5. Tests: `tests/unit/common/param_verity_math_test.cpp` (pure) and
   `tests/unit/engine/param_verity_test.cpp` (engine, internal FX).
6. Build (`build-fast.bat test`), run targeted suites, fix until green.
7. Live dogfood one probe through the deployed engine (restart first), then close out
   the plan's Phase 1 gates.

## Phase 3 live evidence (2026-09-22, deployed engine)

- Corpus run on the Psy FM Hook (track 6, slot 0, psy_fm): 14 params swept, **5 audible**
  (OP1 Ratio, Feedback, OP1 Attack/Decay/Sustain), 9 inaudible (OP2-6 silent in this
  patch — explains the earlier OP5 Release finding). Sidecar:
  `compositions/verity/psyfm-hook-audibility.json` (hdaw.param.verity.corpus.v1).
- Riser fix: rerolled the build2 riser cell (seed +1) + riser volume arc
  (0.2→0.75 across beats 384–472). `tone_verity centroidRiseMin 1.2` now PASSES:
  centroid 781 → 3173 Hz (**4.06x**). Project saved.
- Test totals across Phases 1-3: **39 tests green** (ParamVerity 3, ParamVerityMath 6,
  ToneVerityEngine 3, ToneVerityAnalyzer 5, ParamCorpus 3, RpcParityRatchet 5,
  SongPlanRpcTest 9, MovementPlan 5).
