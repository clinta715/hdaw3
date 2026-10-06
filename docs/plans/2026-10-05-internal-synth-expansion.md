# Plan — internal synth expansion: psyarp multimode filter + osc shapes, psy_fm filter

Drafted 2026-10-05. Status: **LANDED 2026-10-05** (slices A + B). Derived from the
engine survey (palette reality on Linux: no VA CLAPs, so the six internal synths
ARE the palette).

## Close-out (2026-10-05)

**A. psyarp** — `OscShape` +Pulse(3)/Noise(4); `processSVF` now LP/HP/BP; params
21 `Filter Type`, 22 `Pulse Width`, 23 `Noise Level` appended (`Osc Shape` max
2→4). 23 psyarp tests pass. Back-compat proven by FNV-1a golden hashes captured
pre-change and re-run post-change: `0x236ff2cd` (engine default), `0x04d484a5`
(slot default) — **bit-identical**.

**B. psy_fm** — per-voice `HDAW::InternalFilter` on the carrier mix, bypassed
unless engaged (`cutoff<19999 || keyTrack || envAmount`); params 33 `Filter
Cutoff`, 34 `Filter Resonance`, 35 `Filter Type`, 36 `Filter Key Track`,
37 `Filter Env Amount` appended. 47 psy_fm tests pass. Back-compat proven against
a **true pre-change golden** (engine reverted to HEAD, hashes captured, restored):
`0x87f32b95` / `0xba3c4b19` — identical.

**Verification** — live `param_verity` on the running engine:
`psyarp Filter Type` (LP→HP) and `Osc Shape` (→Pulse/Noise) `anyAudible:true`;
`psy_fm Filter Cutoff` close attenuates mid −301 / high −1724 `anyAudible:true`.
Device maps regenerated + `--check` green (psyarp 21→24, psy_fm 33→38; index 266).
Suites at the documented baseline: engine 1407 / 13 env failures, mcp 418/421 —
no new failures.

**Landing order**: A (psyarp) → B (psy_fm) → C (anti-aliasing, non-additive).
**Still open**: D extra psy_fm algorithm presets.

## C. Oscillator anti-aliasing (PolyBLEP) — NON-ADDITIVE — LANDED 2026-10-05

Changes EXISTING renders: the naive shapes are replaced IN PLACE, no opt-out param
(naive aliasing is a defect, not a feature; an escape-hatch param would keep a
dead legacy branch). Applies to discontinuity-bearing shapes ONLY:
- **psyarp**: Saw, Square, Pulse, SuperSaw (7 saws; the detune is a phase OFFSET,
  so all 7 share one `dt`).
- **sub_synth**: Saw, Square (and the internal sub, which uses Square).
- Untouched: Sine, Triangle, Noise (and psy_fm — pure-sine operators, no discontinuity).

PolyBLEP residual for phase `t` in [0,1) and increment `dt`:
`t<dt → t/=dt; 2t−t²−1` · `t>1−dt → t=(t−1)/dt; t²+2t+1` · else 0.
Saw: `2t−1 − polyBlep(t,dt)`. Square: `(t<0.5?1:−1) + polyBlep(t,dt) − polyBlep(frac(t+0.5),dt)`.
Pulse(d): `(t<d?1:−1) + polyBlep(t,dt) − polyBlep(frac(t+1−d),dt)`.
Both engines thread `dt` (phase increment / sampleRate) into the oscillator fn.

Gates:
- [x] **Spectral A/B** MEASURED (stash-and-build naive vs AA, 702.3 Hz folded partial
      of a 4978 Hz saw): psyarp saw −17.4 dB → −88.7 dB; supersaw −30.6 → −98.3 dB;
      sub_synth saw −25.0 → −139.8 dB; square −141.9 dB. High-note alias fraction
      0.039→0.000003 (psyarp), 0.034→0.000001 (sub_synth).
- [x] **In-band integrity**: low note h2/h3 ratios match naive to <1%
      (psyarp 48: 0.4999 vs 0.5000; sub_synth 36: rms −1.1% / −0.6%).
- [x] Back-compat hash tests re-pinned to the AA render (psyarp only; psy_fm
      unchanged, documented) with a comment naming C as non-additive.
- [x] RT-safe; suites at the documented baseline (engine/mcp/frontend/platform —
      zero new failures).

## D. psy_fm preset + algorithm expansion — ADDITIVE — LANDED 2026-10-06

Purpose: psy_fm has only 4 presets (growlBass/acidLead/metallicPluck/riser) and 4
algorithm routings. Expand the vocabulary so the slot is a first-class lead/pad/
pluck engine, not just a growl machine. ADDITIVE: existing preset names, existing
algorithm indices 0..3 and their renders stay UNCHANGED.

Interfaces (all read ONE source of truth, verified):
- `HDAW::PsyFmState::presetTable()` — `PresetDef {name, ratios[6], feedback,
  algorithm, env[6][4], outputLevel, sweepRateHz, matrix}`.
  `presetNames()` / `presetNameList()` / `findPreset()` / `unknownPresetError()`
  all read it, so a new row auto-extends the MCP `psy_fm_load_preset` enum
  (`psyFmPresetEnum()`), the RPC twin, and the refusal text.
- Algorithms: `PsyFmAlgorithms.{h,cpp}` `*Algorithm(PsyFmEngine&, int)`; applied
  by `TrackFXSlot` param 32 via a switch at TWO sites (~L1183 prepare, ~L2357
  param-change); param 32 def `{32,"Algorithm Preset",0,0,3}` widened to 0..5.

Deliverable:
1. New algorithms (additive; index 4+): `padAlgorithm` (index 4) is a
   **dual-carrier** routing — op2 (idx 1) + op1 (idx 0) both summed into
   `carrierMix()`, both phase-modulated by op5 (idx 4), which op4 (idx 3)
   modulates; `bellAlgorithm` (index 5) is a **deep feedback chain**
   op6→op5→op3→op1 (idx 5→4→2→0).
2. New preset rows — `pad`, `bell`, `pluck`, `drone`, `stab` — appended after
   `riser`. `pad`/`drone` use algorithm 4; `bell` uses 5; `pluck` (2) and `stab`
   (1) reuse existing routings with distinct ratios/envelopes. `PsyFmPatches`
   gained `makePadMatrix` / `makeBellMatrix` / `makePluckMatrix` /
   `makeDroneMatrix` / `makeStabMatrix`; a test pins each helper's encoded
   routing to its preset row.
3. `TrackFXSlot` param 32 max 3 → 5; BOTH switch sites got `case 4`/`case 5`;
   device-map enum/note regenerated.

Gates:
- [x] Each new preset loads via `psy_fm_load_preset` and the RPC twin (slot
      trees concat-equal on the `param_*` + `PsyFmMatrix`/`PsyFmSweepRate`
      payload); unknown-name refusal names the whole 9-name set on both surfaces.
- [x] Each new preset renders NON-SILENT and SPECTRALLY DISTINCT from the others
      (measured 3-band profile + DFT centroid; `tests/unit/engine/psyfm_presets_test.cpp`).
- [x] **Back-compat**: the 4 existing presets and a default slot render
      hash-identical (FNV-1a goldens `0xcad4acfd` / `0x9e5b9db9` / `0xe7c245e9`
      / `0xe52b8255`, default slot `0xba3c4b19` — captured by building the engine
      with slice D reverted, slices A/B/C present).
- [x] Device map regenerated + `--check` green (psy_fm param 32 enum 0..5, max 5).

Note: at the time of slice D the preset rows set params 0..32 only — the slot's
post-carrier filter (33..37, slice B) was NOT written by a preset load and stayed
at its neutral defaults (bypassed). Slice E closed that gap.

## E. psy_fm presets carry filter settings — ADDITIVE — LANDED 2026-10-06

Gap left by D: `psy_fm_load_preset` writes params 0..32 + matrix, so a preset's
sound does not include the slice-B filter (33..37) — `pad`/`bell` ship with the
filter at defaults. E makes the preset the whole sound.

- `PsyFmState::PresetDef` gains `float filter[5]`
  `{Cutoff, Resonance, Type, KeyTrack, EnvAmount}` (param 33..37 order).
- `AudioEngineCommands::setFxSlotPsyFmPreset` writes params 33..37 from the row
  (one undo unit with the rest).
- Existing 4 presets (`growlBass`/`acidLead`/`metallicPluck`/`riser`) use the
  NEUTRAL filter `{20000, 0.7, 0, 0, 0}` — identical to the engine default and
  the slice-B bypass condition, so their renders stay hash-identical.
- New 5 presets get role-appropriate filters: pad `{1200, 0.7, 0, 0.3, 0.4}` ·
  bell `{6000, 1.2, 0, 1.0, 0.0}` · pluck `{5000, 1.0, 0, 0.5, 0.7}` ·
  drone `{800, 0.5, 0, 0.0, 0.0}` · stab `{1200, 2.0, 0, 0.3, 0.4}` (pluck/stab
  darkened/brightened from the first draft so their spectra stay distinct —
  both are percussive envelopes, so the filter is the separating axis).
- Tool description (`psy_fm_load_preset`) updated to say it also sets the
  post-carrier filter (33..37).

Gates:
- [x] Existing 4 preset + default-slot hashes UNCHANGED (the D goldens re-run:
      `0xcad4acfd` / `0x9e5b9db9` / `0xe7c245e9` / `0xe52b8255`, default
      `0xba3c4b19` — `PsyFmPresetBackCompat.*`).
- [x] Each new preset, after load, reports the expected 33..37 via
      `get_internal_fx_param` (`PsyFmPresetFilter.LoadWritesEveryNewRowsFilterParams`);
      the load itself goes through both surfaces in
      `PsyFmPresetSurfaces.NewPresetsLoadIdenticallyViaToolAndRpc`.
- [x] A new preset's render differs from the SAME preset with a neutral filter
      (`PsyFmPresetFilter.NewPresetRendersDifferentlyFromTheSamePresetWithANeutralFilter`,
      all five rows).
- [x] Device map unchanged (no param added); suites at the documented baseline
      (engine + frontend psy_fm suites re-run).

## Goal

Close the two coverage holes found by reading the engines:

- **A. `psyarp`** — its filter is `FilterMode { LowPass = 0, NumModes }` (one mode)
  and its oscillators are `Saw|Square|SuperSaw` (no pulse, no noise). Arps are the
  psytrance lead idiom; HP/BP sweeps are the rolling Astral Projection character.
- **B. `psy_fm`** — has **no filter at all** (its own patch comment: mod-wheel→
  feedback is a "filter-sweep-style performance control **without a filter**").
  That makes it an acid/growl machine that needs an external filter slot for
  leads/pads, and that slot cannot key-track.

Both are **additive** (new params appended; existing indices and ranges only
widened, never re-pointed), so existing projects re-render identically and no
MCP/RPC parity change is required (engine params flow through the existing
`set_internal_fx_param` / `list_fx_params` / device-map surface).

## Non-goals (this pass)

- **Osc anti-aliasing (PolyBLEP)** — changes existing renders (not additive);
  separate ticket, needs an A/B. The new Pulse/Noise oscs ship naive + documented.
- Filter envelopes with per-voice ADSR in psy_fm — start with the filter block
  (cutoff/res/type/key-track); add env-amount only if the audibility gate asks.
- No new MCP tools (params are addressed by the existing param tools).

## Shared facts (verified)

- `HDAW::InternalFilter` (`src/engine/InternalFilter.h`) is the **verified** TPT
  SVF (LP/HP/BP, mode-rounded, clamp-at-entry, allocation-free) — REUSE it
  rather than writing a third SVF. `PsyArpEngine::processSVF` is a separate
  hand-rolled SVF (returns `v2` = LP only) — extend it to HP/BP.
- Param tables: `TrackFXSlot::getParamDefsForType` — `psyarp` 0..20, `psy_fm` 0..32.
  Two wiring sites each: the prepare/apply path (~L1128, ~L1183) and the
  param-change path (~L2357).
- Device map: `timbre-lib/build_device_map.py` (regenerate + `--check` determinism).
- RT safety: Gate 3 — the render path may not allocate/lock/format strings.

## A. psyarp (`PsyArpEngine.{h,cpp}` + `TrackFXSlot.h`)

- `OscShape`: `Saw, Square, SuperSaw, Pulse, Noise` (append 3,4 — existing 0..2
  unchanged); `generateOscillator` handles Pulse (`phase<duty?+1:-1`) and Noise
  (per-sample xorshift white). Add `setPulseWidth(float)`, `setNoiseLevel(float)`.
- Filter mode: extend `processSVF` to return LP/HP/BP by mode (`hp = in - k*bp - lp`,
  `bp`), add `setFilterMode(int)`.
- New params (append after 20):
  `21 Filter Type` (0..2, def 0), `22 Pulse Width` (0.05..0.95, def 0.5),
  `23 Noise Level` (0..1, def 0); widen `0 Osc Shape` max 2 → 4.
- Sum-noise headroom: if Noise Level can add on top of unison voices, apply the
  same memoryless soft-ceiling discipline as lesson 45 (knee above normal levels).

## B. psy_fm (`PsyFmEngine.{h,cpp}` + `TrackFXSlot.h`)

**LANDED 2026-10-05** (slice B). Implementation notes: the per-voice filter is
`HDAW::InternalFilter` (8 instances, prepared once in `PsyFmEngine::prepare`),
applied to `carrierMixBuffer_` per voice BEFORE the voice-sum. The amp envelope for
env-amount is the CARRIER's `PsyFmOperator::getCurrentEnvValue()`. Bypass rule
implemented as `isFilterEngaged()`
(`cutoff < 19999 || keyTrack != 0 || envAmount != 0`); the effective cutoff is
clamped after key-track and again after the env offset. Tests:
`tests/unit/engine/psyfm_filter_test.cpp` (3 back-compat hashes captured from the
pre-change engine + audibility/params/wiring).

- Per-voice `InternalFilter` (8 voices; prepared once, no allocation) applied to
  each voice's carrier output, BEFORE the voice-sum. Add
  `setFilterParam(index,value)` in the engine; key-track maps the voice's MIDI
  note to cutoff; `Filter Env Amount` = cutoff offset scaled by the voice's
  amp-envelope level (read from the carrier op's existing env, no new ADSR).
- New params (append after 32):
  `33 Filter Cutoff` (20..20000, def 20000 = neutral/off), `34 Filter Resonance`
  (0.1..10, def 0.7), `35 Filter Type` (0..2, def 0), `36 Filter Key Track`
  (0..1, def 0), `37 Filter Env Amount` (0..1, def 0).
  Default cutoff 20000 ⇒ a default patch renders as before (audible no-op).

## Pitfall gates

- Gate 3: no alloc/lock/String in `render()`; noise + pulse are per-sample math;
  `InternalFilter` is allocation-free after `prepare`.
- Lesson 23: clamp new params at their def ranges in the engine setters.
- Lesson 45: soft ceiling on any summed voice/noise path before output level.
- Device map determinism: `python timbre-lib/build_device_map.py` then `--check`.
- Back-compat: assert a PRE-CHANGE patch (no new param_N) renders bit-identically.

## Success gates

- [x] Unit tests: psyarp HP/BP change the render vs LP; Pulse/Noise shapes render
      non-silent and differ from Saw; psy_fm filter cutoff sweeps attenuate the
      high band; both engines' new params round-trip via `set_internal_fx_param`
      and read back. (`tests/unit/engine/psyarp_filter_test.cpp`,
      `tests/unit/engine/psyfm_filter_test.cpp`; psy_fm also covers key-track +
      env-amount direction and HP/BP.)
- [x] Back-compat: an existing project with no new `param_N` renders unchanged
      (A/B or analytic). (`PsyFmBackCompat` pins the PRE-change FNV-1a hash of
      the engine / slot / existing-tree default scenarios — captured by building
      the reverted engine; the neutral defaults take the explicit filter bypass.)
- [ ] `param_verity`/`tone_verity` evidence: the new filter mode + cutoff are
      AUDIBLE on a rendered probe (the gate the plan requires). [orchestrator —
      live-engine gate, not run by the slice subagent]
- [x] Device map regenerated + `--check` green; `list_device_params {engine}` shows
      the new rows with units. (psy_fm 33 → 38, psyarp 21 → 24.)
- [ ] Four `hdaw_tests_*` suites at the documented baseline (no new failures);
      `build-fast.sh all` clean. [orchestrator — full sweep]
- [ ] Live smoke: a psyarp arp with BP filter renders audibly different from LP.
      [orchestrator — live-engine gate]

## Execution

Serialized by the shared `TrackFXSlot.h` param table: **A first, then B**
(one subagent each, plan-first). C (anti-aliasing) and D (presets) remain open.
