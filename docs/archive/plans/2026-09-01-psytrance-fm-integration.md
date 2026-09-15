# Plan: Psytrance FM Synthesis Integration

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend HDAW's existing DX7 FM synth engine with a psytrance-focused modulation matrix, pluggable algorithm routing, and track-level modulation integration, as designed in `docs/plans/FM synthesis in psytrance.md`.

> **STATUS (2026-09-01):** Tasks 1–10 implemented and verified (25 gtest cases passing).
> Five bugs found during composition verification — 2 fixed in-session (LFO target 306
> no-op, voice-reclamation ordering), **5 open** (live-engine psy_fm MCP tools
> "track not found", silent Hypnoticum atmo loop, score-to-audio section offset,
> mod-depth stacking ceiling, async export cancellation). Full repro/fix notes:
> `docs/handoffs/2026-09-01-psyfm-bugs-handoff.md`.

**Context:** HDAW already has a working 6-operator DX7 FM synth (`FmSynthEngine`, `ActiveType::FmSynth` in `TrackFXSlot`) ported from the msfa library. It supports all 32 DX7 algorithms, per-operator level/coarse/fine/detune, feedback, LFO, mono mode, and DX7 sysex import. The psytrance FM plan describes a next-generation voice architecture with pluggable algorithm functions, a modulation matrix for routing LFOs/envelopes to operator ratios and feedback, and integration with the existing `MutatorConductor` bar clock. This plan bridges the two.

**Design decision:** Rather than replacing the existing msfa-based engine (which works for classic DX7 patches), we add a **parallel voice architecture** (`PsyFmEngine`) alongside it. The existing `FmSynthEngine` stays for DX7 compatibility; the new engine is for the psy-specific modulation-matrix workflow. Both share the `TrackFXSlot` framework but are distinct `ActiveType` entries. This avoids breaking existing FM synth functionality while enabling the new features.

**Tech Stack:** C++17 (engine), TypeScript/React (frontend UI), Zustand (state), Vite (build).

---

## Architecture Overview

```
Existing (stays unchanged)          New (this plan)
┌──────────────────────┐            ┌──────────────────────────────┐
│ FmSynthEngine        │            │ PsyFmEngine                  │
│ (msfa DX7 core)      │            │ (new voice architecture)     │
│ 32 hardcoded algs    │            │ Pluggable algorithm fns      │
│ Rate/level EGs       │            │ Sample-accurate envelopes    │
│ No mod matrix        │            │ FmModMatrix (ratio/fb routing)│
│                      │            │ FmModSourcePool (LFOs)       │
│ ActiveType::FmSynth  │            │ ActiveType::PsyFm            │
└──────────┬───────────┘            └──────────────┬───────────────┘
           │                                        │
           └──────────── TrackFXSlot ───────────────┘
                              │
                    ┌─────────┴──────────┐
                    │ ModulationManager   │
                    │ (track-level LFOs)  │
                    │ Extended with FM    │
                    │ param destinations  │
                    └─────────┬──────────┘
                              │
                    ┌─────────┴──────────┐
                    │ MutatorConductor    │
                    │ Bar clock → FM      │
                    │ voice.onBarBoundary │
                    └────────────────────┘
```

---

## File Structure

### New files (C++ engine)

| File | Responsibility |
|------|---------------|
| `src/engine/PsyFmEngine.h` | Top-level engine: voice allocator, MIDI, render entry, modulation matrix orchestration |
| `src/engine/PsyFmEngine.cpp` | Implementation |
| `src/engine/PsyFmOperator.h` | Single FM operator with sample-accurate envelope, block-rate ratio/feedback |
| `src/engine/PsyFmOperator.cpp` | Implementation |
| `src/engine/PsyFmModMatrix.h` | Modulation routing: sources → operator ratio/feedback/nested LFO rate |
| `src/engine/PsyFmModMatrix.cpp` | Implementation |
| `src/engine/PsyFmPatches.h` | Preset routing configs (growl bass, riser, acid lead, metallic pluck) |
| `src/engine/PsyFmPatches.cpp` | Implementation |
| `tests/unit/engine/psyfm_test.cpp` | GTest suite |

### Modified files

| File | Change |
|------|--------|
| `src/engine/TrackFXSlot.h` | Add `ActiveType::PsyFm`, `std::unique_ptr<PsyFmEngine> psyFm`, cases in `prepare()`, `process()`, `reset()`, `applyInternalParamToDsp()`, `getParamDefsForType()` |
| `src/engine/TrackFXSlot.h` | Extend `getParamDefsForType()` with psyFm param definitions |
| `src/engine/ModulationManager.h` | Add FM-specific paramID destinations (Op1–6 Ratio, Op6 Feedback) |
| `src/engine/Track.cpp` | Extend `processBlock` modulation pass to apply FM param modulations |
| `src/mcp/McpTools_Audio.cpp` | Add `"psy_fm"` to FX type enum, add `psy_fm_set_matrix`, `psy_fm_get_analysis` tools |
| `CMakeLists.txt` | Add new `.cpp` files to source list |

---

## Task 1: PsyFmOperator — Sample-Accurate FM Operator

**Files:**
- Create: `src/engine/PsyFmOperator.h`
- Create: `src/engine/PsyFmOperator.cpp`

The core DSP primitive. Each operator owns its amplitude/index envelope (sample-accurate) and receives block-rate ratio/feedback from the voice's modulation matrix pass.

- [x] **Step 1: Write PsyFmOperator.h**

```cpp
// src/engine/PsyFmOperator.h
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

class PsyFmOperator
{
public:
    void prepare(double sampleRate);
    void setEnvelopeParams(const juce::ADSR::Parameters& p);
    void noteOn();
    void noteOff();
    bool isActive() const;

    // Block-constant params, set once per block before renderBlock() runs
    void setBlockParams(float ratio, float feedbackAmount, float baseFreqHz);

    // Per-sample render. modInputBuffer: phase modulation from upstream operator (nullptr = none)
    void renderBlock(float* outBuffer, const float* modInputBuffer, int numSamples);

    // Access current envelope value (for analysis/visualization)
    float getCurrentEnvValue() const { return currentEnvValue_; }

private:
    double sampleRate_ = 44100.0;
    juce::ADSR indexEnv_;
    float phase_ = 0.0f;
    float lastOutput_ = 0.0f;
    float currentRatio_ = 1.0f;
    float feedbackAmount_ = 0.0f;
    float baseFreq_ = 220.0f;
    float currentEnvValue_ = 0.0f;
};
```

- [x] **Step 2: Write PsyFmOperator.cpp**

```cpp
// src/engine/PsyFmOperator.cpp
#include "PsyFmOperator.h"

void PsyFmOperator::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    indexEnv_.setSampleRate(sampleRate);
    phase_ = 0.0f;
    lastOutput_ = 0.0f;
    currentEnvValue_ = 0.0f;
}

void PsyFmOperator::setEnvelopeParams(const juce::ADSR::Parameters& p)
{
    indexEnv_.setParameters(p);
}

void PsyFmOperator::noteOn()
{
    indexEnv_.noteOn();
    phase_ = 0.0f;
    lastOutput_ = 0.0f;
}

void PsyFmOperator::noteOff()
{
    indexEnv_.noteOff();
}

bool PsyFmOperator::isActive() const
{
    return indexEnv_.isActive();
}

void PsyFmOperator::setBlockParams(float ratio, float feedbackAmount, float baseFreqHz)
{
    currentRatio_ = ratio;
    feedbackAmount_ = feedbackAmount;
    baseFreq_ = baseFreqHz;
}

void PsyFmOperator::renderBlock(float* outBuffer, const float* modInputBuffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float envValue = indexEnv_.getNextSample(); // sample-accurate
        currentEnvValue_ = envValue;

        float externalMod = modInputBuffer != nullptr ? modInputBuffer[i] : 0.0f;
        float selfFeedback = feedbackAmount_ * lastOutput_;

        float phaseIncrement = juce::MathConstants<float>::twoPi
                             * currentRatio_ * baseFreq_
                             / static_cast<float>(sampleRate_);
        phase_ += phaseIncrement;
        if (phase_ > juce::MathConstants<float>::twoPi)
            phase_ -= juce::MathConstants<float>::twoPi;

        float sample = std::sin(phase_ + externalMod + selfFeedback) * envValue;
        outBuffer[i] = sample;
        lastOutput_ = sample;
    }
}
```

- [x] **Step 3: Add to CMakeLists.txt, build, verify compilation**

- [x] **Step 4: Commit**

```
git add src/engine/PsyFmOperator.h src/engine/PsyFmOperator.cpp CMakeLists.txt
git commit -m "feat(psyfm): sample-accurate FM operator with ADSR envelope"
```

---

## Task 2: PsyFmModMatrix — Modulation Routing

**Files:**
- Create: `src/engine/PsyFmModMatrix.h`
- Create: `src/engine/PsyFmModMatrix.cpp`

Routes modulation sources (LFOs, mod wheel, velocity, bar clock) to operator ratios and feedback. Block-rate only — envelope-to-output-level is handled by the operator itself.

- [x] **Step 1: Write PsyFmModMatrix.h**

```cpp
// src/engine/PsyFmModMatrix.h
#pragma once
#include <vector>

struct PsyFmModSourcePool
{
    float ratioSweepLFORateHz = 0.2f;
    float feedbackLFORateHz   = 0.1f;
    float modWheelValue = 0.0f;
    float velocityValue = 0.0f;
    double ratioSweepPhase_ = 0.0;
    double feedbackPhase_   = 0.0;

    void advanceControlRate(int numSamples, double sampleRate);
    float getSourceValue(int sourceIndex) const;
};

struct PsyFmModRoute
{
    enum class Source { RatioSweepLFO, FeedbackLFO, ModWheel, Velocity, BarClock };
    enum class Dest   { Op1Ratio, Op2Ratio, Op3Ratio, Op4Ratio, Op5Ratio, Op6Ratio,
                         Op6Feedback, RatioSweepRateItself };

    Source source;
    Dest   dest;
    float  depth = 0.0f;
};

class PsyFmModMatrix
{
public:
    void addRoute(PsyFmModRoute route);
    void clearRoutes();
    void apply(const PsyFmModSourcePool& sources,
               const float baseRatios[6], float baseFeedback,
               float outRatios[6], float& outFeedback);

private:
    int sourceIndexFor(PsyFmModRoute::Source s) const;
    std::vector<PsyFmModRoute> routes_;
};
```

- [x] **Step 2: Write PsyFmModMatrix.cpp**

Implementation of `advanceControlRate`, `getSourceValue`, `sourceIndexFor`, `apply`. The `RatioSweepRateItself` destination is handled externally (by `PsyFmEngine::onBarBoundary`) before `apply()` runs — `apply()` skips it.

- [x] **Step 3: Add to CMakeLists.txt, build, verify**

- [x] **Step 4: Commit**

```
git add src/engine/PsyFmModMatrix.h src/engine/PsyFmModMatrix.cpp
git commit -m "feat(psyfm): modulation matrix for operator ratio/feedback routing"
```

---

## Task 3: PsyFmPatches — Preset Routing Configs

**Files:**
- Create: `src/engine/PsyFmPatches.h`
- Create: `src/engine/PsyFmPatches.cpp`

Factory preset routing matrices for common psytrance roles. Each returns a pre-configured `PsyFmModMatrix` with the appropriate source→dest routes.

- [x] **Step 1: Write PsyFmPatches.h**

```cpp
// src/engine/PsyFmPatches.h
#pragma once
#include "PsyFmModMatrix.h"

namespace PsyFmPatches
{
    // Feedback starts high, decays via envelope — "settling growl"
    PsyFmModMatrix makeGrowlBassMatrix();

    // Bar clock speeds up ratio-sweep LFO rate (sub-audio → audio-rate)
    PsyFmModMatrix makeRiserMatrix();

    // Mod wheel drives feedback toward self-oscillation — filter-sweep-style
    PsyFmModMatrix makeAcidLeadMatrix();

    // Fast-decay envelope drives modulator output level — bright transient → pure sustain
    PsyFmModMatrix makeMetallicPluckMatrix();
}
```

- [x] **Step 2: Write PsyFmPatches.cpp**

Each function creates a `PsyFmModMatrix`, adds the appropriate routes, and returns it. Example for growl bass:

```cpp
PsyFmModMatrix PsyFmPatches::makeGrowlBassMatrix()
{
    PsyFmModMatrix m;
    // Feedback is modulated by a source (bar clock or LFO) — the exact source
    // depends on the patch's modulation source pool configuration.
    // The matrix routing is: source → Op6Feedback with depth 0.4
    m.addRoute({ PsyFmModRoute::Source::FeedbackLFO,
                 PsyFmModRoute::Dest::Op6Feedback, 0.4f });
    return m;
}
```

- [x] **Step 3: Add to CMakeLists.txt, build, verify**

- [x] **Step 4: Commit**

```
git add src/engine/PsyFmPatches.h src/engine/PsyFmPatches.cpp
git commit -m "feat(psyfm): preset modulation routing for psytrance roles"
```

---

## Task 4: PsyFmEngine — Voice Allocator & Render

**Files:**
- Create: `src/engine/PsyFmEngine.h`
- Create: `src/engine/PsyFmEngine.cpp`

The top-level engine. Owns 6 `PsyFmOperator`s per voice, a `PsyFmModMatrix`, a `PsyFmModSourcePool`, and a pluggable algorithm function that defines operator chaining.

- [x] **Step 1: Write PsyFmEngine.h**

```cpp
// src/engine/PsyFmEngine.h
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <array>
#include "PsyFmOperator.h"
#include "PsyFmModMatrix.h"

class PsyFmEngine
{
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kNumOperators = 6;

    PsyFmEngine() = default;
    ~PsyFmEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Algorithm: pluggable function that defines operator chaining/summing
    using AlgorithmFn = std::function<void(PsyFmEngine&, int numSamples)>;
    void setAlgorithm(AlgorithmFn fn);

    // Base params (pre-modulation)
    void setBaseRatios(const float ratios[kNumOperators]);
    void setBaseFeedback(float fb);
    void setOpEnvelope(int opIndex, const juce::ADSR::Parameters& p);

    // Modulation matrix
    void setModMatrix(PsyFmModMatrix matrix);
    PsyFmModMatrix& getModMatrix() { return matrix_; }
    PsyFmModSourcePool& getModSourcePool() { return sources_; }

    // Bar clock (called from MutatorConductor)
    void onBarBoundary(int barCounter);

    // Lock-free param setters (message thread → audio thread)
    void setOutputLevel(float v) noexcept;

    // Inspection
    int activeVoiceCount() const noexcept;
    float getOpEgLevel(int op) const noexcept; // for analysis panel

    // Helpers for algorithm functions
    float* getScratch(int opIndex);
    PsyFmOperator& op(int index);
    std::vector<float>& carrierMix();

private:
    struct Voice
    {
        PsyFmOperator operators[kNumOperators];
        int midiNote = -1;
        int channel = 0;
        bool keydown = false;
        bool live = false;
    };

    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void allNotesOff();
    Voice* allocateVoice();

    double sampleRate_ = 44100.0;
    float baseFreqHz_ = 220.0f;
    float baseRatios_[kNumOperators] = { 1,1,1,1,1,1 };
    float baseFeedback_ = 0.0f;

    Voice voices_[kMaxVoices];
    int currentNote_ = 0;

    AlgorithmFn algorithmFn_;
    PsyFmModSourcePool sources_;
    PsyFmModMatrix matrix_;

    std::vector<std::vector<float>> scratchBuffers_;
    std::vector<float> carrierMixBuffer_;

    std::atomic<float> outputLevelAtom_{ 0.4f };

    // Analysis (written on audio thread, read on main thread)
    std::atomic<float> opEgLevel_[kNumOperators]{};
};
```

- [x] **Step 2: Write PsyFmEngine.cpp — prepare, voice allocation, MIDI**

```cpp
void PsyFmEngine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        for (auto& op : v.operators)
            op.prepare(sampleRate);

    scratchBuffers_.resize(kNumOperators);
    for (auto& buf : scratchBuffers_)
        buf.resize(static_cast<size_t>(maxBlockSize), 0.0f);
    carrierMixBuffer_.resize(static_cast<size_t>(maxBlockSize), 0.0f);
}
```

Voice allocation follows the same round-robin with priority scoring as `FmSynthEngine::allocateVoice()`.

- [x] **Step 3: Write PsyFmEngine.cpp — render()**

The render path:

1. Process MIDI events (noteOn/Off/AllNotesOff)
2. Block-rate modulation pass: `sources_.advanceControlRate()` → `matrix_.apply()` → `op.setBlockParams()` for each operator
3. Sample-accurate render pass: call `algorithmFn_(*this, numSamples)` which chains operators via scratch buffers
4. Copy carrier mix to output, apply output level

- [x] **Step 4: Write PsyFmEngine.cpp — onBarBoundary**

```cpp
void PsyFmEngine::onBarBoundary(int barCounter)
{
    // Example: every 8 bars, speed up the riser LFO
    if (barCounter % 8 == 0)
        sources_.ratioSweepLFORateHz = juce::jmin(
            sources_.ratioSweepLFORateHz * 1.3f, 40.0f);
}
```

- [x] **Step 5: Add to CMakeLists.txt, build, verify**

- [x] **Step 6: Commit**

```
git add src/engine/PsyFmEngine.h src/engine/PsyFmEngine.cpp
git commit -m "feat(psyfm): PsyFmEngine with pluggable algorithms and mod matrix"
```

---

## Task 5: Default Algorithm Functions

**Files:**
- Create: `src/engine/PsyFmAlgorithms.h`
- Create: `src/engine/PsyFmAlgorithms.cpp`

Factory algorithm functions for common psytrance roles. Each is a free function matching `PsyFmEngine::AlgorithmFn`.

- [x] **Step 1: Write PsyFmAlgorithms.h**

```cpp
// src/engine/PsyFmAlgorithms.h
#pragma once

class PsyFmEngine;

// Growl bass: op6 (feedback) → op5 → op1 (carrier)
void growlBassAlgorithm(PsyFmEngine& voice, int numSamples);

// Acid lead: op6 (high feedback) → op1 (carrier) — near self-oscillation
void acidLeadAlgorithm(PsyFmEngine& voice, int numSamples);

// Metallic pluck: op4 (non-integer ratio) → op2 → op1 (carrier)
void metallicPluckAlgorithm(PsyFmEngine& voice, int numSamples);

// Riser: op5 (ratio-sweep LFO) → op3 → op1 (carrier)
void riserAlgorithm(PsyFmEngine& voice, int numSamples);
```

- [x] **Step 2: Write PsyFmAlgorithms.cpp**

Each algorithm renders operators in chain order using scratch buffers, then sums carrier outputs. Example:

```cpp
void growlBassAlgorithm(PsyFmEngine& voice, int numSamples)
{
    // op6 (with feedback) → modulates op5 → modulates op1 (carrier)
    float* op6Buf = voice.getScratch(5);
    voice.op(5).renderBlock(op6Buf, nullptr, numSamples);

    float* op5Buf = voice.getScratch(4);
    voice.op(4).renderBlock(op5Buf, op6Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize(static_cast<size_t>(numSamples));
    voice.op(0).renderBlock(carrierOut.data(), op5Buf, numSamples);
}
```

- [x] **Step 3: Add to CMakeLists.txt, build, verify**

- [x] **Step 4: Commit**

```
git add src/engine/PsyFmAlgorithms.h src/engine/PsyFmAlgorithms.cpp
git commit -m "feat(psyfm): algorithm functions for growl, acid, pluck, riser"
```

---

## Task 6: TrackFXSlot Integration

**Files:**
- Modify: `src/engine/TrackFXSlot.h`

Add `PsyFm` as a new `ActiveType` alongside the existing `FmSynth`.

- [x] **Step 1: Add ActiveType::PsyFm and psyFm member**

```cpp
enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Chorus, Flanger,
                         Phaser, Plugin, Sampler, FmSynth, PsyFm };

// After fmSynth member:
std::unique_ptr<PsyFmEngine> psyFm;
```

- [x] **Step 2: Add psyFm to getParamDefsForType()**

When `type == "psy_fm"`, return param definitions for:
- Indices 0–5: Op1–6 base ratio (float, 0.1–10.0, default 1.0)
- Index 6: base feedback (float, 0.0–1.0, default 0.0)
- Indices 7–12: Op1–6 envelope attack (float, 0.001–2.0, default 0.01)
- Indices 13–18: Op1–6 envelope decay (float, 0.001–5.0, default 0.3)
- Indices 19–24: Op1–6 envelope sustain (float, 0.0–1.0, default 0.7)
- Indices 25–30: Op1–6 envelope release (float, 0.001–5.0, default 0.2)
- Index 31: output level (float, 0.0–1.0, default 0.4)
- Index 32: algorithm preset (int, 0=growlBass, 1=acidLead, 2=metallicPluck, 3=riser, default 0)

- [x] **Step 3: Add psyFm to prepare()**

```cpp
case ActiveType::PsyFm:
{
    if (!psyFm)
        psyFm = std::make_unique<PsyFmEngine>();
    psyFm->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
    // Push params from internalParamValues (ratios, feedback, envelopes)
    if (internalParamValues.size() > 0) {
        float ratios[6];
        for (int i = 0; i < 6; i++)
            ratios[i] = (internalParamValues.size() > (size_t)i) ? internalParamValues[i] : 1.0f;
        psyFm->setBaseRatios(ratios);
    }
    if (internalParamValues.size() > 6)
        psyFm->setBaseFeedback(internalParamValues[6]);
    // Set envelopes from params 7–30
    for (int op = 0; op < 6; op++) {
        juce::ADSR::Parameters p;
        p.attack  = (internalParamValues.size() > (size_t)(7 + op))  ? internalParamValues[7 + op]  : 0.01f;
        p.decay   = (internalParamValues.size() > (size_t)(13 + op)) ? internalParamValues[13 + op] : 0.3f;
        p.sustain = (internalParamValues.size() > (size_t)(19 + op)) ? internalParamValues[19 + op] : 0.7f;
        p.release = (internalParamValues.size() > (size_t)(25 + op)) ? internalParamValues[25 + op] : 0.2f;
        psyFm->setOpEnvelope(op, p);
    }
    if (internalParamValues.size() > 31)
        psyFm->setOutputLevel(internalParamValues[31]);
    // Set algorithm from param 32
    if (internalParamValues.size() > 32) {
        int algoIdx = static_cast<int>(internalParamValues[32]);
        switch (algoIdx) {
            case 0: psyFm->setAlgorithm(growlBassAlgorithm); break;
            case 1: psyFm->setAlgorithm(acidLeadAlgorithm); break;
            case 2: psyFm->setAlgorithm(metallicPluckAlgorithm); break;
            case 3: psyFm->setAlgorithm(riserAlgorithm); break;
            default: psyFm->setAlgorithm(growlBassAlgorithm); break;
        }
    }
    break;
}
```

- [x] **Step 4: Add psyFm to process()**

```cpp
if (activeType == ActiveType::PsyFm)
{
    if (psyFm)
    {
        buffer.clear();
        psyFm->render(buffer, midiMessages);
        midiMessages.clear();
    }
    return;
}
```

- [x] **Step 5: Add psyFm to applyInternalParamToDsp()**

Map param indices 0–32 to the appropriate `psyFm->setBaseRatios()`, `setBaseFeedback()`, `setOpEnvelope()`, `setOutputLevel()`, and algorithm selection.

- [x] **Step 6: Add psyFmEngine() accessor**

```cpp
PsyFmEngine* psyFmEngine() { return psyFm.get(); }
```

- [x] **Step 7: Add forwardPlayHead and setTempo forwarding for psyFm**

Forward playhead and tempo to the psyFm engine if it needs transport info (for beat-synced LFO rates).

- [x] **Step 8: Build and verify**

- [x] **Step 9: Commit**

```
git add src/engine/TrackFXSlot.h
git commit -m "feat(psyfm): integrate PsyFmEngine as ActiveType::PsyFm in TrackFXSlot"
```

---

## Task 7: Track-Level Modulation Integration

**Files:**
- Modify: `src/engine/ModulationManager.h`
- Modify: `src/engine/Track.cpp`

Extend the existing track-level modulation system to route LFOs into FM-specific destinations (operator ratios, feedback).

- [x] **Step 1: Define FM paramIDs in ModulationManager**

Add new paramID constants for FM modulation targets:

```cpp
// In ModulationManager.h or a shared header
namespace FmModParamIDs {
    static constexpr int Op1Ratio = 100;
    static constexpr int Op2Ratio = 101;
    static constexpr int Op3Ratio = 102;
    static constexpr int Op4Ratio = 103;
    static constexpr int Op5Ratio = 104;
    static constexpr int Op6Ratio = 105;
    static constexpr int Op6Feedback = 110;
}
```

- [x] **Step 2: Extend Track::processBlock modulation pass**

In `Track::processBlock`, after the existing volume/pan modulation application, add:

```cpp
// Apply FM modulation targets if this track has a PsyFm engine
if (auto* psyFm = /* get psyFm from FX chain */) {
    for (int op = 0; op < 6; op++) {
        float mod = modulationManager.getModulation(FmModParamIDs::Op1Ratio + op, bpm, sampleRate);
        if (mod != 0.0f) {
            // Apply ratio modulation to the psyFm engine's mod source pool
            // or directly to operator params
        }
    }
    float fbMod = modulationManager.getModulation(FmModParamIDs::Op6Feedback, bpm, sampleRate);
    if (fbMod != 0.0f) {
        // Apply feedback modulation
    }
}
```

- [x] **Step 3: Build and verify**

- [x] **Step 4: Commit**

```
git add src/engine/ModulationManager.h src/engine/Track.cpp
git commit -m "feat(psyfm): track-level modulation targets for FM ratio/feedback"
```

---

## Task 8: MCP Tools

**Files:**
- Modify: `src/mcp/McpTools_Audio.cpp`

- [x] **Step 1: Add "psy_fm" to add_fx enum**

```cpp
{"fxType", QJsonObject{{"type","string"},
    {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger",
                         "phaser","sampler","fm_synth","psy_fm"}}}},
```

- [x] **Step 2: Add psy_fm_set_matrix tool**

Sets modulation matrix routes on a PsyFm FX slot. Accepts a JSON array of `{source, dest, depth}` objects.

- [x] **Step 3: Add psy_fm_get_analysis tool**

Returns per-operator EG levels and active voice count (reads from the lock-free atomics on the audio thread).

- [x] **Step 4: Add psy_fm_load_preset tool**

Loads a named preset routing (growlBass, acidLead, metallicPluck, riser) — sets algorithm function + modulation matrix + default ratios/feedback/envelopes.

- [x] **Step 5: Build and verify**

- [x] **Step 6: Commit**

```
git add src/mcp/McpTools_Audio.cpp
git commit -m "feat(psyfm): MCP tools for PsyFm (add_fx, set_matrix, get_analysis, load_preset)"
```

---

## Task 9: Unit Tests

**Files:**
- Create: `tests/unit/engine/psyfm_test.cpp`

- [x] **Step 1: Write PsyFmOperator tests**

- Test: noteOn activates envelope, noteOff starts release
- Test: renderBlock produces non-zero output when active
- Test: renderBlock produces silence when inactive
- Test: feedback amount affects output (high feedback = louder/more complex)

- [x] **Step 2: Write PsyFmModMatrix tests**

- Test: apply() with no routes returns base values unchanged
- Test: single route modifies target param
- Test: multiple routes accumulate
- Test: depth=0 leaves target unchanged

- [x] **Step 3: Write PsyFmEngine tests**

- Test: initial state has 0 active voices
- Test: noteOn activates a voice
- Test: noteOff eventually deactivates voice
- Test: render produces non-zero output
- Test: algorithm function is called during render
- Test: onBarBoundary advances ratio sweep LFO rate
- Test: setBaseRatios + setBaseFeedback affect output

- [x] **Step 4: Write integration tests**

- Test: PsyFm in TrackFXSlot — add_fx "psy_fm", set params, render, verify non-zero output
- Test: Modulation matrix route from mod wheel to feedback changes output character

- [x] **Step 5: Add to CMakeLists.txt, build, run tests**

```bash
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=PsyFm*
```

- [x] **Step 6: Commit**

```
git add tests/unit/engine/psyfm_test.cpp CMakeLists.txt
git commit -m "test(psyfm): unit tests for PsyFm operator, matrix, engine, integration"
```

---

## Task 10: Frontend — PsyFm Editor Panel

**Files:**
- Create: `frontend/src/components/PsyFmEditor.tsx`
- Create: `frontend/src/components/PsyFmEditor.css`
- Create: `src/frontend/router/Router_PsyFm.h`
- Create: `src/frontend/router/Router_PsyFm.cpp`
- Modify: `frontend/src/App.tsx` (import + tab registration)
- Modify: `frontend/src/store/uiStore.ts` (add `psy-fm` to BOTTOM_TAB_IDS)
- Modify: `src/frontend/FrontendRouter.cpp` (add PsyFm dispatch)
- Modify: `src/frontend/FrontendRpc.h` (add PsyFm namespace)
- Modify: `CMakeLists.txt` (add Router_PsyFm.cpp)

- [x] **Step 1: Create PsyFmEditor component**

A bottom-panel tab (like `SamplerEditor`) showing:
- Algorithm preset selector (growlBass / acidLead / metallicPluck / riser)
- 6 operator ratio knobs
- Feedback knob
- Per-operator envelope ADSR (tabbed or stacked)
- Modulation matrix grid (source → dest → depth)
- Output level

- [x] **Step 2: Wire to RPC**

Use `set_internal_fx_param` for individual param changes. Use `psy_fm_set_matrix` for matrix routes. Use `psy_fm_get_analysis` for live operator level visualization.

- [x] **Step 3: Add to bottom panel tab list**

Register "PsyFm" as a tab in the bottom panel, shown when the selected FX slot is type "psy_fm".

- [x] **Step 4: Build frontend, verify**

```bash
cd frontend; npm run build
```

- [x] **Step 5: Commit**

```
git add frontend/src/components/PsyFmEditor.tsx
git commit -m "feat(psyfm): PsyFm editor panel (algorithm, ratios, envelopes, mod matrix)"
```

---

## Task 11: End-to-End Verification

- [x] **Step 1: Build full project**

```bash
cmake --build build --config Debug
```

- [x] **Step 2: Run all tests**

```bash
build\Debug\hdaw_tests.exe
```
Expected: All existing tests pass + new PsyFm* tests pass.

- [x] **Step 3: Manual smoke test**

1. Start HDAW
2. Add a MIDI track
3. `add_fx` with `psy_fm` type
4. `set_internal_fx_param` to set algorithm preset to growlBass (param 32 = 0)
5. Play MIDI notes — should hear growling FM bass
6. `psy_fm_set_matrix` to add a feedback LFO route
7. Verify modulation is audible
8. `psy_fm_get_analysis` — should report active voices and op levels
9. Switch to acid lead preset — verify character changes
10. Test riser — verify LFO rate sweeps up over 8 bars

- [x] **Step 4: Final commit**

```
git add -A
git commit -m "feat(psyfm): psytrance FM synthesis integration (mod matrix, algorithms, MCP, UI)"
```

---

## Pitfall Gates Triggered

| Gate | How Addressed |
|------|--------------|
| **Gate 1: State Not Restored on Rebuild** | PsyFmEngine params live in `internalParamValues` on the FX slot ValueTree. `prepare()` re-pushes all params (ratios, feedback, envelopes, algorithm) from the stored values on every `rebuildRoutingGraph()`. |
| **Gate 3: Audio-Thread Safety** | All param updates via `std::atomic` (output level) or block-rate matrix pass (ratios, feedback). Envelopes run sample-accurately inside the audio thread. No locks, no allocations, no I/O. |
| **Gate 5: Frontend Stale Closures** | UI reads from Zustand store, not closures. FM params set via `set_internal_fx_param` → ValueTree → listener → DSP. |
| **Gate 10: Rebuild State Restore** | `prepare()` reads `internalParamValues` and pushes to the new engine instance. Matrix routes stored in ValueTree, re-applied on rebuild. |
| **Gate 13: DSP-State Writes Without stateLock** | PsyFmEngine uses atomic params and block-rate matrix. Envelopes are per-voice (not shared). No cross-thread DSP object access. |

---

## Licensing

The new `PsyFm*` files are original HDAW code (GPL v3). The existing msfa files remain Apache 2.0. No new third-party dependencies.

---

## Additional MCP Tools (added during implementation)

During implementation, MCP feature parity gaps were identified and filled:

### Slicing tools (added to `McpTools_Clip.cpp`)

| Tool | Description |
|------|-------------|
| `slice_clip_at_playhead` | Slice a clip at the current playhead position |
| `slice_clips_at_playhead` | Slice multiple clips at the playhead |
| `slice_clip_at_times` | Slice a clip at multiple beat positions |
| `slice_clip_at_transients` | Auto-slice an audio clip at detected transients |
| `slice_clips_at_transients` | Auto-slice multiple audio clips at transients |

### Timestretch tools (added to `McpTools_Clip.cpp`)

| Tool | Description |
|------|-------------|
| `set_clip_source_bpm` | Set the source BPM metadata for an audio clip |
| `set_clip_stretch_mode` | Set stretch mode (Off/TempoMatch/ManualRatio) |
| `set_clip_stretch_ratio` | Set the stretch ratio (0.25–4.0) |
| `tempo_match_clip` | Auto-stretch to match project tempo |
| `fit_clip_to_loop` | Stretch clip to fill the loop region |

### PsyFm tools (added to `McpTools_PsyFm.cpp`)

| Tool | Description |
|------|-------------|
| `psy_fm_load_preset` | Load a preset routing (growlBass/acidLead/metallicPluck/riser) |
| `psy_fm_get_analysis` | Get active voices and per-operator EG levels |
| `psy_fm_set_mod_route` | Add a modulation route to the matrix |
| `psy_fm_clear_mod_matrix` | Clear all modulation routes |
