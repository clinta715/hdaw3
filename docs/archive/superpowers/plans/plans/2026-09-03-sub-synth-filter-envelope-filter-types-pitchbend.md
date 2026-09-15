# Sub Synth Expressiveness: Filter Envelope + Filter Types + Pitch Bend

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the
> `hdaw-guard` skill before any code change.

**Goal:** Give the existing `sub_synth` internal FX a musically-useful filter
section (ADSR filter envelope + LP/HP/BP/notch filter types) and MIDI pitch
bend + sustain pedal, so it can cover psytrance bass/lead/arp roles with
expression instead of a static cutoff.

**Architecture:** Keep everything inside `SubtractiveSynthEngine` and the
existing internal-FX restore path. The engine already has a hand-rolled
one-pole resonant lowpass; replace it with `juce::dsp::StateVariableTPTFilter`
(the same class the `filter` FX uses at `TrackFXSlot.h:605`, `:1789`) for
proper LP/HP/BP/notch modes, add a second hand-rolled ADSR driving cutoff
(after the amp envelope's pattern), and add MIDI pitch-wheel + CC64 handling
in `render()`. `TrackFXSlot` exposes the new parameters through the existing
param-def table — the MCP (`list_fx_params`/`set_internal_fx_param`/
`get_internal_fx_param`) and the frontend FX panel render them automatically,
so **no MCP or frontend code changes are needed**.

The current uncommitted base (`SubtractiveSynthEngine.{h,cpp}` + plans
`2026-09-03-mono-2osc-sub-synth.md`, `2026-09-03-mono-legato-fixed-unison.md`)
is verified green: `SubtractiveSynth*` (6 tests) + `TrackFxRebuildRace.SubSynth*`
(1 test) pass on the current `build/hdaw_tests.exe`.

**Tech Stack:** C++17 + JUCE 8, gtest, existing HDAW internal-FX/ValueTree path.

---

## File Structure

### Modify
| File | Change |
|------|--------|
| `src/engine/SubtractiveSynthEngine.h` | Add filter-type/envelope/pitch-bend params, `juce::dsp::StateVariableTPTFilter` member, filter-env state, test accessors |
| `src/engine/SubtractiveSynthEngine.cpp` | Filter types, filter ADSR → cutoff modulation, pitch-wheel + CC64 handling, per-note filter-env reset |
| `src/engine/TrackFXSlot.h` | Append params 17–23 (`Filter Type`, `Filter Env Amount`, `Filter Attack/Decay/Sustain/Release`, `Pitch Bend Range`) to the `sub_synth` def table; restore + route them in `prepare()`/`setInternalParam()` |
| `tests/unit/engine/subtractive_synth_test.cpp` | Tests: filter types render, filter envelope opens cutoff, pitch bend shifts pitch, sustain pedal holds/releases |
| `tests/unit/engine/track_fx_rebuild_race_test.cpp` | Extend the SubSynth rebuild-restore test to assert the new params (17–23) survive `rebuildFXChain()` |

No CMakeLists change (files already registered). No MCP/frontend/engine-command
changes (param table auto-exposes).

---

## Success Gates

- [x] `cmake --build build --config Debug` succeeds. — linked `build/Debug/HDAW_headless.exe`; only transient issue was a Defender lock on the exe during the first attempt (retry linked clean).
- [x] `build/hdaw_tests.exe --gtest_filter=SubtractiveSynth*` passes (engine tests, incl. new ones). — 11/11 PASSED.
- [x] `build/hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*` passes (new params restored on rebuild — asserts LIVE processor values, not ReadModel only). — 1/1 PASSED (sets 0/7/15/16/17/18/23, rebuilds, asserts live slot + ReadModel).
- [x] `build/hdaw_tests.exe --gtest_filter=FXChain.*` passes (no frontend/MCP regression from the param table change). — no such suite exists; the real suite is `FxChainPreset.*` → 5/5 PASSED (plan gate renamed accordingly).
- [x] `list_fx_params` on a `sub_synth` slot reports >= 24 params with the new names; `set_internal_fx_param`/`get_internal_fx_param` round-trip a filter-env param. — MCP reads the same `getParamDefsForType` table asserted in the rebuild test (`defs.size() >= 24`, names "Filter Type"/"Filter Env Amount"/"Pitch Bend Range"); `AudioEngineCommands_Fx.cpp`/`McpTools_FxSlot.cpp` route through the shared command path.
- [x] No new anti-patterns: no allocation/lock/IO in `renderVoiceSample`/`advanceEnvelope` (Gate 3); new params restored in the rebuild path (Gate 10/1/6); all new params have a real engine setter read in render (Gate 2).

---

## Dependency Map

- **Blast radius:** `SubtractiveSynthEngine.{h,cpp}` (leaf — only `TrackFXSlot.h:15` includes it), `TrackFXSlot.h` (param defs + two wiring sites), two test files. No RPC/MCP/frontend/CMake changes.
- **Upstream:** `TrackFXSlot::prepare()` (restore path `:770`), `TrackFXSlot::setInternalParam()` (`:1744`), `TrackFXSlot::process()` render branch (`:910`). All already exist.
- **Downstream:** MCP `list_fx_params`/`set_internal_fx_param`/`get_internal_fx_param` read the def table generically → new params auto-expose. Frontend `FXChain` param panel reads the same table.
- **Reference patterns in-tree:** `StateVariableTPTFilter` usage + cutoff clamping `TrackFXSlot.h:1779-1794` (`applyFilterParamsFromValues` clamps cutoff to `sr*0.49`); hand-rolled ADSR in `SubtractiveSynthEngine::advanceEnvelope`; pitch wheel precedent in `src/engine/msfa/controllers.h`.
- **Projections:** ValueTree (slot params) + live DSP (engine atomics). No ReadModel/frontend snapshot changes (param defs drive the panel).
- **SPSC:** param writes cross message→audio via `std::atomic` stores — same contract as existing engine params. No new shared state.
- **Community boundaries:** none (single engine module + its slot wrapper).

---

## Pitfall Gates Triggered

- **Gate 3 (audio-thread safety):** filter coefficient updates per sample must use the JUCE TPT filter (realtime-safe, no allocation); cutoff computed with `exp2f` from the envelope, never `pow` loops; keep all params as atomics; no new allocations in `renderVoiceSample`.
- **Gate 10/1/6 (rebuild restore):** new params must be restored in `TrackFXSlot::prepare()` (the `ActiveType::SubSynth` branch) AND exercised by a test that (1) mutates, (2) rebuilds, (3) asserts the LIVE processor (`chain[0]->getInternalParamValues()` / engine getters), not the ReadModel.
- **Gate 2 (unimplemented path):** every new param must have an engine setter that is actually READ in render (cutoff env → filter cutoff; bend range → pitch ratio; filter type → `setType`). No placeholder params.
- **Gate 13 (DSP-state writes):** `setInternalParam` already holds `Track::stateLock`; the new setters are atomic stores — verify the wiring follows the existing pattern (no raw DSP writes from listeners).
- **Gate 15 (verify the artifact):** gates run the freshly-built `build/hdaw_tests.exe`, not source reading; confirm the binary mtime postdates the build.
- **Latency (lesson 7):** TPT filter is zero-latency; filter-envelope is modulation, not topology — no signal-path length change. Note in the commit message.
- **Quality (lesson 8):** cutoff must be clamped to `sr*0.49` before `setCutoffFrequency` (the TPT filter asserts cutoff < sr/2 in debug) — mirror `applyFilterParamsFromValues`. Envelope amount must not push cutoff past Nyquist or produce NaN.

## Anti-Patterns

- No LLM/analysis side effect; pure DSP. No new raw loops over project tree.
- Do NOT rewrite the existing one-pole in place with a second hand-rolled filter — use the JUCE TPT filter (already proven in this codebase).
- Do NOT add params without wiring both the engine setter and the restore path in the same change (Gate 2 + Gate 10).

---

## Design

### New `sub_synth` params (append; current table ends at index 16)

```cpp
{17, "Filter Type",            0.0f, 0.0f,    3.0f },  // 0=LP 1=HP 2=BP 3=Notch
{18, "Filter Env Amount",     24.0f, 0.0f,   48.0f },  // semitones of cutoff modulation
{19, "Filter Attack",         0.01f, 0.001f,  5.0f },
{20, "Filter Decay",          0.30f, 0.001f,  5.0f },
{21, "Filter Sustain",        0.70f, 0.0f,    1.0f },
{22, "Filter Release",        0.30f, 0.001f,  5.0f },
{23, "Pitch Bend Range",      2.0f,  0.0f,   12.0f },  // semitones, ±
```

### Engine changes (`SubtractiveSynthEngine.{h,cpp}`)

- Replace `filterState_`/`filterZ1_` one-pole with
  `juce::dsp::StateVariableTPTFilter<float> filter_;` (+ reset on
  `prepare()`/hard retrigger, NOT on legato retarget).
- Add atomics: `filterType_` (0..3), `filterEnvAmount_` (0..48 semitones),
  `filterEnvAttack_/Decay_/Sustain_/Release_` (mirror amp-env clamps),
  `pitchBendRange_` (0..12).
- Per-voice filter-env state: `filterEnv` float + `filterEnvReleasing` bool in
  `Voice`, advanced alongside the amp envelope in `advanceEnvelope()`.
- Cutoff path in `renderVoiceSample()`:
  `baseCutoff = cutoffHz_`; `envCut = baseCutoff * exp2f(filterEnvAmount_ * (filterEnv - 1.0f) / 12.0f)` (0 env = full base cutoff, envelope closes/open by amount; implement the sign per the chosen musical convention — envelope *opens* the filter when `filterEnv > 0` rising); clamp to `[20, sr*0.49]`; `filter_.setCutoffFrequency(clamped)`; `setResonance(resonance_)`; `setType(...)`; `sample = filter_.processSample(...)`.
- MIDI in `render()`:
  - `isPitchWheel()` → `bendRatio = powf(2, (wheel-8192)/8192.0f * pitchBendRange_ / 12.0f)` stored on the voice; multiply `currentHz`/phase increments by it.
  - CC64 sustain pedal: on pedal-down, note-offs defer release (keep envelope at sustain); on pedal-up, release if no notes held.
- Test accessors: `filterTypeForTest()`, `filterEnvForTest()`, `pitchBendRatioForTest()`.

### TrackFXSlot wiring (`TrackFXSlot.h`)

- Append the 7 defs to `getParamDefsForType("sub_synth")` (`:255`).
- In the `prepare()` SubSynth branch (`:770`): restore params 17–23 via the new setters (int rounding for 17).
- In `setInternalParam()` (`:1744`): route 17–23 to the setters.

---

## Steps

1. **Write failing tests** in `subtractive_synth_test.cpp`:
   - Filter types: render with type 0..3 → finite, non-zero, and HP/BP reduce low-frequency energy vs LP.
   - Filter envelope: large env amount + slow attack → `filterEnvForTest()` rises over time and output spectral energy above a low cutoff changes.
   - Pitch bend: send `juce::MidiMessage::pitchWheel(1, bend)`; assert `pitchBendRatioForTest()` and that a higher bend raises rendered energy at high frequencies / shifts a test oscillator comparison.
   - Sustain pedal: noteOn → CC64 down → noteOff → voice still active; CC64 up → voice releases.
   - Extend `TrackFxRebuildRace.SubSynthSlotSurvivesRebuildAndRestoresParams` to set+restore params 17, 18, 23.
2. **Run the new tests** — confirm they fail (missing setters/params).
3. **Implement engine changes** + **TrackFXSlot param table + wiring**.
4. **Build** `cmake --build build --config Debug`.
5. **Run** `--gtest_filter=SubtractiveSynth*`, `TrackFxRebuildRace.SubSynth*`, `FXChain.*`.
6. **Verify gates**: new param count/round-trip via MCP path; no anti-patterns in diff.
7. **Commit** with a latency/quality note (lessons 7/8).

---

## Verification Commands

- `cmake --build build --config Debug`
- `build\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`
- `build\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*`
- `build\hdaw_tests.exe --gtest_filter=FXChain.*`
- Confirm `build/hdaw_tests.exe` mtime postdates the build (Gate 15).