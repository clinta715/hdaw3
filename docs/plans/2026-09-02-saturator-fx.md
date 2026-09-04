# Standalone Saturator FX Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `saturator` internal FX type (drive → selectable transfer curve → DC block → dry/wet mix → output trim, 2× oversampled) so clean internal synths can be overdriven without third-party plugins.

**Architecture:** New header-only `SaturatorEngine` (transfer curves extracted from `GrowlBassEngine`, fixed for effect use: in-place stereo, DC blocker, output trim) owned by `TrackFXSlot` like all internal DSP; 2× `juce::dsp::Oversampling` around the shaper with latency plumbed into `Track::updateLatency`; params follow the `InternalParamDef` + `param_N` contract with clamps at all four sites.

**Tech Stack:** C++20, JUCE DSP (`Oversampling`, `AudioBlock`), existing `TrackFXSlot`/`Track`/router/MCP patterns.

**Effort:** Medium-High (~3-4 sessions; first oversampling integration in the codebase). **Risk:** High-blast-radius by definition (DSP chain, `processBlock` reachability, latency/PDC) — this plan is the pre-implementation discussion artifact required by the standing sound-engine rule. No `processBlock` signature changes; additive slot type only.

---

## Success Gates (all must pass to declare done)

- [ ] Gate 1: `Mix=0` renders bit-identical to bypass through a real `rebuildRoutingGraph` (neutral passthrough).
- [ ] Gate 2: Drive sweep 0→40 dB renders finite on sine + silence + full-scale square (no NaN/Inf; lesson-23 class).
- [ ] Gate 3: THD rises monotonically with drive on a 440 Hz sine (effect-present contrast, spectrum-measured in-test).
- [ ] Gate 4: Reported latency equals the oversampler's `getLatencyInSamples()`; `getTotalLatency()` before/after adding the slot differs by exactly that (lesson 7).
- [ ] Gate 5: Poison values (`Drive=900`, `Mix=-5`, `Output=1e6`) clamp to defs at all four sites and render finite (clamp-test pattern).
- [ ] Gate 6: `add_fx{fxType:"saturator"}` → params → rebuild survival via MCP; UI lists it; full suites green.

## Dependency Map

- Blast radius: `TrackFXSlot.h` (defs/ctor/prepare/process/reset/applyParams), new `SaturatorEngine.h`, `Track.cpp updateLatency`, MCP enums, `FXChain.tsx` list. Additive except `updateLatency` sum (one line + test).
- Upstream: `param_N` writes (existing generic path), automation/modulation (generic paramID scheme, no change).
- Downstream: `rebuildFXChain` generic branch (no special branch needed — no extra tree state beyond `param_N`), ReadModel (generic), PDC graph (latency contribution).
- God nodes: `TrackFXSlot` (hub — follow existing case patterns exactly, do not restructure).
- Projections: same dual coverage as all internal FX (listener push + rebuild restore).
- SPSC/threads: DSP state touched only via `setInternalParam` (stateLock-held) and `prepare` (message thread, tripwire-guarded).

## Pitfall Gates Triggered

- Gate 1/10 (rebuild restore): no new processor state beyond `internalParamValues` + oversampler (rebuilt in `prepare`); seam test asserts live processor.
- Gate 3 (audio thread): process path = stack + preallocated members only; no `Random`, no allocation, no locks, no I/O. Oversampler preallocated in `prepare`.
- Gate 7 (latency): measured before/after, PDC alignment asserted.
- Gate 8 (quality): A/B spectrum vs 1× (aliasing bounded by 2×), denormal check on silenced tail, DC offset check with asymmetry at extremes.
- Gate 9: `Type`/`Bits` enum params rounded like filter Mode (`TrackFXSlot.h:1375-1380` pattern).
- Lesson 13: param writes go through `setInternalParam` (clamp + apply); never touch DSP members from the listener directly.
- Lesson 23: defs with tight min/max + all four clamp sites from day one.

---

## Task 1: SaturatorEngine DSP + unit tests

**Files:**
- Create: `src/engine/SaturatorEngine.h`
- Test: `tests/unit/engine/saturator_engine_test.cpp`

- [ ] **Step 1: Write the failing test** — curves finite, small-signal identity, DC block, dry mix bit-identical:

```cpp
#include <gtest/gtest.h>
#include "engine/SaturatorEngine.h"

TEST(SaturatorEngine, SmallSignalNearIdentity) {
    SaturatorEngine sat;
    sat.setType(0); sat.setDriveDb(0.0f); sat.setAsymmetry(0.0f);
    float x = 0.001f;
    EXPECT_NEAR(sat.processSample(x), x, 1e-4f);  // tanh(x)~x, no bitcrush at neutral
}

TEST(SaturatorEngine, DriveSweepFinite) {
    SaturatorEngine sat;
    for (int type = 0; type <= 3; ++type) {
        sat.setType(type);
        for (float db : {0.f, 12.f, 24.f, 40.f}) {
            sat.setDriveDb(db);
            for (float x : {-1.f, -0.3f, 0.f, 0.3f, 1.f}) {
                float y = sat.processSample(x);
                EXPECT_TRUE(std::isfinite(y)) << "type=" << type << " db=" << db;
                EXPECT_LE(std::abs(y), 1.5f);
            }
        }
    }
}

TEST(SaturatorEngine, AsymmetryEmitsNoDC) {
    SaturatorEngine sat;
    sat.setType(0); sat.setDriveDb(24.f); sat.setAsymmetry(1.0f);
    float acc = 0.f; const int N = 48000;
    for (int i = 0; i < N; ++i)  // 1 kHz sine, drop the first second of state below
        acc += sat.processSampleDCBlocked(std::sin(2 * 3.14159265f * 1000 * i / 48000));
    EXPECT_LT(std::abs(acc / N), 1e-3f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmd /c build-fast.bat test`
Then run: `.\build\hdaw_tests.exe --gtest_filter=SaturatorEngine.*`
Expected: FAIL — `engine/SaturatorEngine.h` not found.

- [ ] **Step 3: Write minimal implementation** — curves extracted from `GrowlBassEngine.cpp:106-166`, adapted for effect use (in-place, no `buffer.clear()`, no MIDI, no double envelope). Deliberate deviations from the source, each covered by the tests above:

```cpp
// src/engine/SaturatorEngine.h (header-only, no JUCE dependency)
#pragma once
#include <cmath>

class SaturatorEngine {
public:
    enum Type { SoftTanh = 0, SoftAtan = 1, Hard = 2, Bitcrush = 3 };
    void setType(int t) { type_ = t < 0 ? 0 : (t > 3 ? 3 : t); }
    void setDriveDb(float db) { driveGain_ = std::pow(10.0f, db / 20.0f); }
    void setAsymmetry(float a) { asym_ = a < -1 ? -1 : (a > 1 ? 1 : a); }
    void setBits(float b) { bits_ = b < 2 ? 2 : (b > 16 ? 16 : (int)std::round(b)); }
    void reset() { dcX_ = dcY_ = 0.0f; }

    float shape(float input) const {
        const float driven = input * driveGain_;
        switch (type_) {
            case SoftTanh: {
                // sign-split asymmetric tanh (GrowlBassEngine.cpp:115-123 shape)
                const float pos = driven * (1.0f + 0.3f * asym_);
                const float neg = driven * (1.0f - 0.3f * asym_);
                return driven >= 0 ? std::tanh(pos) : std::tanh(neg);
            }
            case SoftAtan: {
                // NOTE: source reads k*atan(driven*k*...) (GrowlBassEngine.cpp:125-134)
                // which looks like a doubled k. Task 1b below A/Bs k vs 2k and locks one.
                constexpr float k = 0.63661977f;  // 2/pi
                const float g = driven * (1.0f + 0.3f * (driven >= 0 ? asym_ : -asym_));
                return std::atan(g * k) / std::atan(k);
            }
            case Hard: {
                const float p = 1.0f + 0.3f * asym_, n = 1.0f - 0.3f * asym_;
                if (driven > p) return p; if (driven < -n) return -n; return driven;
            }
            default: {  // Bitcrush + tanh safety net (:150-158 shape)
                const float levels = (float)(1 << bits_);
                return std::tanh(std::round(driven * levels) / levels);
            }
        }
    }

    float processSampleDCBlocked(float x) {
        float s = shape(x);
        // one-pole DC blocker, R=0.995 (asymmetry otherwise emits DC — growl has none)
        float y = s - dcX_ + 0.995f * dcY_;
        dcX_ = s; dcY_ = y;
        return y;
    }
    float processSample(float x) { return shape(x); }  // unblocked, for the small-signal test
private:
    int type_ = 0; float driveGain_ = 1.0f, asym_ = 0.0f; int bits_ = 8;
    float dcX_ = 0.0f, dcY_ = 0.0f;
};
```

- [ ] **Step 1b: A/B the SoftAtan `k` question** — render 1 s of 220 Hz sine +24 dB through both `atan(g*k)/atan(k)` and the literal source shape `k*atan(g*k)`, compare THD + peak. Keep the normalized variant (unity Cherie at drive≈0) unless the literal one measures strictly better-behaved; record the decision + numbers as a code comment. YAGNI forbids keeping both.
- [ ] **Step 4: Run test to verify it passes**

Run: `.\build\hdaw_tests.exe --gtest_filter=SaturatorEngine.*`
Expected: PASS (all three tests).

- [ ] **Step 5: Commit**

Run: `git add src/engine/SaturatorEngine.h tests/unit/engine/saturator_engine_test.cpp`
Run: `git commit -m "feat: SaturatorEngine transfer curves with DC blocker"`

---

## Task 2: Slot integration (defs → prepare → process → latency)

**Files:**
- Modify: `src/engine/TrackFXSlot.h` (defs `:36-241`, `ActiveType :1152`, ctor `:243-284`, prepare `:465-686`, process `:804-851`, reset `:854-873`, apply `:1274-1570`, members `:1167-1178`)
- Modify: `src/engine/Track.cpp` (`updateLatency :58-67`)
- Test: extend `tests/unit/engine/internal_fx_param_clamp_test.cpp` + `tests/unit/engine/psytrance_composition_stress_test.cpp` (`InternalFx.Saturator*` near `:1026`)

- [ ] **Step 1: Write the failing tests**

```cpp
// internal_fx_param_clamp_test.cpp — append:
TEST(InternalFxParamClamp, SaturatorPoisonClamped) {
    TrackFXSlot slot("saturator");
    slot.setInternalParam(0, 900.0f);   // Drive
    EXPECT_FLOAT_EQ(slot.getInternalParam(0), 40.0f);
    slot.setInternalParam(3, -5.0f);    // Mix
    EXPECT_FLOAT_EQ(slot.getInternalParam(3), 0.0f);
}

// psytrance_composition_stress_test.cpp — append:
TEST(InternalFx, SaturatorDryMixBitIdentical) {
    // fixture: track with saturator, Mix=0, Drive=40, real rebuildRoutingGraph(),
    // render sine with slot enabled vs bypassed; EXPECT bit-identical buffers.
}
TEST(InternalFx, SaturatorDriveAddsHarmonics) {
    // render 440 Hz sine Drive=0 vs Drive=24 (Mix=1); 2nd-5th harmonic energy strictly greater.
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `.\build\hdaw_tests.exe --gtest_filter=InternalFxParamClamp.SaturatorPoisonClamped:InternalFx.Saturator*`
Expected: FAIL — `"saturator"` falls to `None` (`TrackFXSlot.h:275-276`).

- [ ] **Step 3: Write minimal implementation** — follow the filter precedent exactly (`prepare :544-550`, `process :846`, `applyFilterParamsFromValues :1576-1591`, mode rounding `:1375-1380`):

```cpp
// 1. defs (getParamDefsForType, next to filter :103-108):
if (type == "saturator") return {
    {0, "Drive dB", 12.0f, 0.0f, 40.0f},
    {1, "Type", 0.0f, 0.0f, 3.0f},        // 0 SoftTanh 1 SoftAtan 2 Hard 3 Bitcrush
    {2, "Asymmetry", 0.0f, -1.0f, 1.0f},
    {3, "Mix", 1.0f, 0.0f, 1.0f},
    {4, "Output dB", 0.0f, -24.0f, 24.0f},
    {5, "Bits", 8.0f, 2.0f, 16.0f},      // Bitcrush only
};
// 2. ActiveType += Saturator (:1152); ctor mapping (:243-284).
// 3. members: SaturatorEngine sat_; std::unique_ptr<juce::dsp::Oversampling<float>> over_;
// 4. prepare(): sat_.reset(); over_ = std::make_unique<juce::dsp::Oversampling<float>>(
//        2 /*channels, re-sized in process if needed*/, 2 /*factor*/,
//        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
//    over_->initProcessing((size_t)expectedBlockSize); pushParamsToDsp();
//    (Verify constructor/flag names against JUCE-v9.0.1/modules/juce_dsp — no in-repo precedent.)
// 5. process(): up = over_->processSamplesUp(block); per channel/sample:
//        dry = s; wet = decibelsToGain(outDb) * sat(linked per-sample, see below);
//        s = dry + mix * (wet - dry);
//    down = over_->processSamplesDown(up); Bit-identical guarantee: if mix==0.0f skip the
//    oversampled path entirely and leave the block untouched.
//    DC-blocker state per channel: keep two SaturatorEngine instances (L/R) —
//    member array SaturatorEngine sat_[2]; reset both in reset() + prepare().
// 6. applyInternalParamToDsp(): round Type/Bits (filter :1375-1380 pattern),
//    sat_[0/1].setDriveDb/Type/Asymmetry/Bits; store mix/outDb as floats.
// 7. reset(): sat_ reset; over_->reset().
// 8. latency: int getLatencySamples() const { return activeType==Saturator && over_
//    ? (int)over_->getLatencyInSamples() : 0; }
//    Track::updateLatency (:58-67): total += slot->getLatencySamples() for internal slots
//    alongside the plugin sum.
```

Oversampling channel count: construct with 2 and use `AudioBlock` channel-safe processing (JUCE oversampler handles ≤ numChannels); mono content processes the second channel silently — document, do not special-case (YAGNI).

- [ ] **Step 4: Run tests to verify they pass**

Run: `.\build\hdaw_tests.exe --gtest_filter=InternalFxParamClamp.SaturatorPoisonClamped:InternalFx.Saturator*:SaturatorEngine.*`
Expected: PASS. Plus latency gate: extend `incremental_routing_spike_test`-style assertion — `getLatencySamples()` with and without the slot differs by exactly the oversampler latency; run it.

- [ ] **Step 5: Commit**

Run: `git add src/engine/TrackFXSlot.h src/engine/Track.cpp tests/unit/engine/internal_fx_param_clamp_test.cpp tests/unit/engine/psytrance_composition_stress_test.cpp`
Run: `git commit -m "feat: saturator internal FX with 2x oversampling"`

---

## Task 3: Reachability (MCP + RPC + UI) and quality evaluation

**Files:**
- Modify: `src/mcp/McpTools_FxSlot.cpp` (`add_fx` desc `:26` + enum `:29`; fix stale descs `:93,149,189,216` to include all 14 types)
- Modify: `src/mcp/McpTools_Track.cpp` (`add_track_with_fx` desc `:162` + enum `:165`)
- Modify: `frontend/src/components/FXChain.tsx` (`INTERNAL_FX :54-67` — add Saturator AND the missing `psy_fm`, a live parity gap)
- Test: `tests/unit/engine/rpc_surface_test.cpp:800-811`, `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Write the failing test** — extend `FxSurface.AddMultipleInternalFxTypes` with `"saturator"`, and MCP coverage `add_fx{saturator} → set_internal_fx_param{Drive 24} → get_internal_fx_param → rebuild → live assert` (copy `FilterFxParamsRealUnitsAndRebuildSurvival :1802-1856`).
- [ ] **Step 2: Run to verify it fails**

Run: `.\build\hdaw_tests.exe --gtest_filter=FxSurface.AddMultipleInternalFxTypes:McpCoverage.*Saturator*`
Expected: FAIL — enum rejects `"saturator"`.
- [ ] **Step 3: Write minimal implementation** — add `"saturator"` to all five enum/desc sites + `INTERNAL_FX` (+ `psy_fm` drive-by). RPC needs no change (`Router_Project.cpp:281-286` accepts any string; string overload `AudioEngineCommands_Fx.cpp:66-72` passes through). `read.getInternalFxParams` and `project.setFxSlotParam` are fully generic.
- [ ] **Step 4: Quality evaluation (lessons 7/8, record numbers in the commit message):**
  - Latency: `getTotalLatency()` identical except +oversampler samples; PDC alignment across two tracks (kick transient sample-accurate vs pre-change render).
  - Fidelity: 1 kHz sine at Drive 0/Mix 1 vs bypass ≤0.5 dB wideband difference; aliasing spot-check (10 kHz sine +24 dB Hard: strongest alias partial ≥40 dB below fundamental — 2× oversampling's whole job).
  - Denormals: 5 s decaying sine to digital silence, tail renders finite with no CPU spike.
  - Critical listen on the growl-bass render from song two (`exports/markov_dubpsy_5min.wav` piano_zone) with saturator inserted post-growl at three drive settings; keep the setting that adds teeth without narrowing the stereo image.
- [ ] **Step 5: Commit**

Run: `git add src/mcp/McpTools_FxSlot.cpp src/mcp/McpTools_Track.cpp frontend/src/components/FXChain.tsx tests/unit/engine/rpc_surface_test.cpp tests/integration/mcp/mcp_coverage_test.cpp`
Run: `git commit -m "feat: saturator reachability (MCP/RPC/UI) + quality gates"`

---

## Task 4: Docs, parity flips, graph refresh

**Files:**
- Modify: `feature_parity.md:68` (saturation ❌→✅), `roadmap.md:66`, `docs/psytrance-composition-guide.md` (§5 drive recipe: growl ClipType/Drive → saturator → compressor chain order), `docs/pitfalls-juce.md` (oversampling-first-precedent note if any gotcha found)
- Run: codebase-memory `index_repository` (mode `fast`)

- [ ] **Step 1:** Parity flips + a "driving clean synths" recipe with the exact chain order used in Step 4 listening (e.g. psy_fm feedback → saturator SoftTanh 18 dB → compressor 4:1).
- [ ] **Step 2:** Full gates — `cmd /c build-fast.bat all`, `.\build\hdaw_tests.exe` zero failures, `npm test` green.
- [ ] **Step 3:** Graph refresh.
- [ ] **Step 4: Commit**

Run: `git add feature_parity.md roadmap.md docs/psytrance-composition-guide.md docs/pitfalls-juce.md`
Run: `git commit -m "docs: saturator parity and drive recipe"`

---

## Self-Review

- [x] Spec coverage: drive/curves/mix/trim/bits + oversampling + latency + clamps + reachability + quality eval all have tasks. Import/export file transport correctly absent (internal FX need none).
- [x] Placeholder scan: no TBDs; JUCE API names flagged for verification against `JUCE-v9.0.1/` (explicit step, not a placeholder — the repo vendors the exact headers).
- [x] Type consistency: 6-param def table identical in Tasks 1–3; `getLatencySamples()` name consistent between slot and `updateLatency` call site.
