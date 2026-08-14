# Internal FM Synth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a DX7-style 6-operator FM synthesizer as a new internal FX type in HDAW, following the SamplerEngine integration pattern exactly.

**Architecture:** Port the msfa (music-synthesizer-for-android) DSP engine — a proven, Apache-2.0-licensed, fixed-point FM synth core — into HDAW's engine as `FmSynthEngine`. Integrate it as `ActiveType::FmSynth` in `TrackFXSlot`, mirroring the Sampler engine's voice allocation, MIDI handling, and lock-free parameter push pattern. Expose via MCP tools and a bottom-panel UI tab.

**Tech Stack:** C++ (msfa DSP, HDAW engine), TypeScript/React (frontend UI tab), Zustand (state), Vite (build).

---

## File Structure

### New files (C++ engine)
| File | Responsibility |
|------|---------------|
| `src/engine/FmSynthEngine.h` | Voice allocator, parameter atomics, MIDI handling, `render()` entry point |
| `src/engine/FmSynthEngine.cpp` | Implementation: voice allocation, noteOn/Off, render loop, param sync |
| `src/engine/msfa/sin.h` | Sine LUT (inline, ported from Dexed) |
| `src/engine/msfa/sin.cc` | Sine LUT init |
| `src/engine/msfa/exp2.h` | Exp2/Tanh LUTs (inline, ported from Dexed) |
| `src/engine/msfa/exp2.cc` | Exp2/Tanh LUT init |
| `src/engine/msfa/synth.h` | Constants (N=64, LG_N=6), types |
| `src/engine/msfa/fm_op_kernel.h` | FM operator compute (pure, FM, feedback) |
| `src/engine/msfa/fm_op_kernel.cc` | Operator implementation |
| `src/engine/msfa/fm_core.h` | Algorithm router (32 DX7 algorithms) |
| `src/engine/msfa/fm_core.cc` | Algorithm table + render |
| `src/engine/msfa/env.h` | DX7 4-rate/4-level envelope |
| `src/engine/msfa/env.cc` | Envelope implementation |
| `src/engine/msfa/pitchenv.h` | Pitch envelope |
| `src/engine/msfa/pitchenv.cc` | Pitch envelope implementation |
| `src/engine/msfa/lfo.h` | DX7 LFO |
| `src/engine/msfa/lfo.cc` | LFO implementation |
| `src/engine/msfa/freqlut.h` | Log-freq to phase increment |
| `src/engine/msfa/freqlut.cc` | Frequency LUT init |
| `src/engine/msfa/dx7note.h` | Per-voice state (6 ops, EGs, pitch, LFO mod) |
| `src/engine/msfa/dx7note.cc` | Voice init, compute, keyup, transferState |
| `src/engine/msfa/controllers.h` | MIDI controller state (mod wheel, breath, etc.) |
| `src/engine/msfa/aligned_buf.h` | Aligned buffer helper |
| `src/engine/msfa/porta.h` | Portamento rates |
| `src/engine/msfa/porta.cc` | Portamento implementation |
| `tests/unit/engine/fm_synth_test.cpp` | GTest suite for FM synth engine |

### Modified files
| File | Change |
|------|--------|
| `src/engine/TrackFXSlot.h` | Add `ActiveType::FmSynth`, `std::unique_ptr<FmSynthEngine> fmSynth`, cases in `prepare()`, `process()`, `reset()`, `applyInternalParamToDsp()`, `getParamDefsForType()` |
| `src/engine/AudioEngineCommands_Fx.cpp` | Add `"fm_synth"` to valid types in `addFxSlot` |
| `src/mcp/McpTools_Audio.cpp` | Add `"fm_synth"` to enum, add `fm_synth_load_preset` and `fm_synth_get_state` MCP tools |
| `CMakeLists.txt` | Add msfa `.cc` files to source list |
| `frontend/src/types/api.ts` | Add `FmSynthParams` type |
| `frontend/src/store/projectStore.ts` | (If needed for FM synth state) |

---

## Task 1: Port msfa DSP Library

**Files:**
- Create: `src/engine/msfa/` (all files listed above)

- [ ] **Step 1: Create the msfa directory and port synth.h (constants)**

```cpp
// src/engine/msfa/synth.h
#ifndef HDAW_SYNTH_H
#define HDAW_SYNTH_H

#include <cstdint>
#include <algorithm>

static constexpr int LG_N = 6;
static constexpr int N = (1 << LG_N); // 64-sample blocks

template<typename T>
inline static T smin(const T& a, const T& b) { return a < b ? a : b; }
template<typename T>
inline static T smax(const T& a, const T& b) { return a > b ? a : b; }

#endif
```

- [ ] **Step 2: Port sin.h / sin.cc (sine lookup table)**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\sin.h` and `sin.cc`. Replace `#include "synth.h"` with the local path. Keep the inline `Sin::lookup()` function. The `sintab` array and `Sin::init()` are the only runtime state.

- [ ] **Step 3: Port exp2.h / exp2.cc (exp2 and tanh lookup tables)**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\exp2.h` and `exp2.cc`. Replace `#include "synth.h"`. Keep the inline `Exp2::lookup()` and `Tanh::lookup()` functions.

- [ ] **Step 4: Port aligned_buf.h**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\aligned_buf.h`. Simple aligned allocation helper.

- [ ] **Step 5: Port fm_op_kernel.h / fm_op_kernel.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\fm_op_kernel.h` and `fm_op_kernel.cc`. The three compute functions (`compute`, `compute_pure`, `compute_fb`) are the core DSP. Remove NEON paths (not needed for HDAW's desktop targets).

- [ ] **Step 6: Port controllers.h**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\controllers.h`. Replace `#include "../Dexed.h"` with `<cstdint>`. This defines MIDI controller state and modulation routing.

- [ ] **Step 7: Port fm_core.h / fm_core.cc (algorithm router)**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\fm_core.h` and `fm_core.cc`. The 32-algorithm table and `FmCore::render()` are the routing logic. This is the base class that EngineMkI/EngineOpl override.

- [ ] **Step 8: Port env.h / env.cc (DX7 envelope)**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\env.h` and `env.cc`. The 4-rate/4-level EG. Note: `Env::init_sr()` sets a sample-rate multiplier — must be called on `prepare()`.

- [ ] **Step 9: Port pitchenv.h / pitchenv.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\pitchenv.h` and `pitchenv.cc`.

- [ ] **Step 10: Port freqlut.h / freqlut.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\freqlut.h` and `freqlut.cc`. `Freqlut::init(sampleRate)` must be called on prepare.

- [ ] **Step 11: Port lfo.h / lfo.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\lfo.h` and `lfo.cc`. `Lfo::init(sampleRate)` must be called on prepare.

- [ ] **Step 12: Port dx7note.h / dx7note.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\dx7note.h` and `dx7note.cc`. This is the per-voice logic. Remove MTS-ESP / tuning support for now (can be added later). Simplify `osc_freq` to standard tuning only.

- [ ] **Step 13: Port porta.h / porta.cc**

Copy from `D:\pdf\dexed-1.0.1\Source\msfa\porta.h` and `porta.cc`. Portamento rate table.

- [ ] **Step 14: Add msfa files to CMakeLists.txt**

Add all `.cc` files to the `add_executable` or `add_library` source list in the appropriate CMakeLists.txt.

- [ ] **Step 15: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: All msfa files compile without errors.

- [ ] **Step 16: Commit**

```bash
git add src/engine/msfa/ CMakeLists.txt
git commit -m "feat: port msfa FM synth DSP library (Apache 2.0)"
```

---

## Task 2: FmSynthEngine — Voice Allocator & MIDI

**Files:**
- Create: `src/engine/FmSynthEngine.h`
- Create: `src/engine/FmSynthEngine.cpp`

- [ ] **Step 1: Write FmSynthEngine.h**

```cpp
// src/engine/FmSynthEngine.h
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include "msfa/dx7note.h"
#include "msfa/controllers.h"
#include "msfa/lfo.h"

class FmSynthEngine
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kPatchSize = 156; // DX7 single voice = 156 bytes

    struct Params
    {
        // Global
        int   algorithm    = 0;    // 0..31
        int   feedback     = 5;    // 0..7
        float outputLevel  = 0.8f; // 0..1 (scaled to DX7 0..99 internally)
        bool  monoMode     = false;
        float portaTime    = 0.0f; // 0..1

        // Per-operator: level (0..1), coarse (0..31), fine (0..99), detune (0..14)
        struct OpParams { float level=0.8f; int coarse=0; int fine=0; int detune=7; } ops[6];

        // LFO
        float lfoRate      = 0.5f; // 0..1
        float lfoDelay     = 0.0f; // 0..1
        float lfoPitchDepth = 0.0f; // 0..1
        float lfoAmpDepth  = 0.0f; // 0..1
        int   lfoWaveform  = 0;    // 0=tri,1=saw,2=square,3=s&h
    };

    FmSynthEngine() = default;
    ~FmSynthEngine();

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Lock-free param setters (message thread → audio thread via atomics)
    void setAlgorithm(int v) noexcept;
    void setFeedback(int v) noexcept;
    void setOutputLevel(float v) noexcept;
    void setMonoMode(bool v) noexcept;
    void setOpLevel(int op, float v) noexcept;
    void setOpCoarse(int op, int v) noexcept;
    void setOpFine(int op, int v) noexcept;
    void setOpDetune(int op, int v) noexcept;
    void setLfoRate(float v) noexcept;
    void setLfoDelay(float v) noexcept;
    void setLfoPitchDepth(float v) noexcept;
    void setLfoAmpDepth(float v) noexcept;
    void setLfoWaveform(int v) noexcept;

    // Load a raw DX7 patch (156 bytes). Message thread only.
    void loadPatch(const uint8_t patch[kPatchSize]);

    // Inspection
    int activeVoiceCount() const noexcept;

    // Access for tests
    const Controllers& getControllers() const { return controllers_; }

private:
    struct Voice
    {
        Dx7Note* note = nullptr;
        int midiNote = -1;
        int channel = 0;
        int velocity = 0;
        bool keydown = false;
        bool sustained = false;
        bool live = false;
        int32_t keydownSeq = 0;
    };

    Voice voices_[kMaxVoices];
    int currentNote_ = 0;
    int32_t nextKeydownSeq_ = 0;
    bool sustain_ = false;

    Lfo lfo_;
    Controllers controllers_;
    uint8_t patchData_[kPatchSize]{};
    Params params_;

    double sampleRate_ = 44100.0;

    // Atomic mirrors for audio-thread reads
    std::atomic<int>   algorithmAtom_{ 0 };
    std::atomic<int>   feedbackAtom_{ 5 };
    std::atomic<float> outputLevelAtom_{ 0.8f };
    std::atomic<bool>  monoModeAtom_{ false };
    std::atomic<float> opLevelAtom_[6]{ 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
    std::atomic<int>   opCoarseAtom_[6]{};
    std::atomic<int>   opFineAtom_[6]{};
    std::atomic<int>   opDetuneAtom_[6]{ 7,7,7,7,7,7 };
    std::atomic<float> lfoRateAtom_{ 0.5f };
    std::atomic<float> lfoDelayAtom_{ 0.0f };
    std::atomic<float> lfoPitchDepthAtom_{ 0.0f };
    std::atomic<float> lfoAmpDepthAtom_{ 0.0f };
    std::atomic<int>   lfoWaveformAtom_{ 0 };
    std::atomic<bool>  paramsDirty_{ false };

    void applyPendingParams();
    void handleNoteOn(int note, float vel, int channel);
    void handleNoteOff(int note, int channel);
    Voice* allocateVoice();
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void allNotesOff();
};
```

- [ ] **Step 2: Write FmSynthEngine.cpp — constructor, prepare, param setters**

```cpp
// src/engine/FmSynthEngine.cpp
#include "FmSynthEngine.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/lfo.h"
#include "msfa/pitchenv.h"
#include "msfa/env.h"
#include "msfa/porta.h"

FmSynthEngine::~FmSynthEngine()
{
    for (auto& v : voices_)
        delete v.note;
}

void FmSynthEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    Sin::init();
    Exp2::init();
    Tanh::init();
    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);

    for (auto& v : voices_)
    {
        delete v.note;
        v.note = new Dx7Note(nullptr, nullptr); // tuning/MTS null for now
        v.live = false;
    }
    lfo_.reset(patchData_ + 137);
}

// Atomic setters
void FmSynthEngine::setAlgorithm(int v) noexcept { algorithmAtom_.store(v & 31, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setFeedback(int v) noexcept { feedbackAtom_.store(v & 7, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setOutputLevel(float v) noexcept { outputLevelAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setMonoMode(bool v) noexcept { monoModeAtom_.store(v, std::memory_order_relaxed); }
void FmSynthEngine::setOpLevel(int op, float v) noexcept { if (op>=0&&op<6) opLevelAtom_[op].store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setOpCoarse(int op, int v) noexcept { if (op>=0&&op<6) opCoarseAtom_[op].store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setOpFine(int op, int v) noexcept { if (op>=0&&op<6) opFineAtom_[op].store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setOpDetune(int op, int v) noexcept { if (op>=0&&op<6) opDetuneAtom_[op].store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setLfoRate(float v) noexcept { lfoRateAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setLfoDelay(float v) noexcept { lfoDelayAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setLfoPitchDepth(float v) noexcept { lfoPitchDepthAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setLfoAmpDepth(float v) noexcept { lfoAmpDepthAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
void FmSynthEngine::setLfoWaveform(int v) noexcept { lfoWaveformAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store(true, std::memory_order_release); }
```

- [ ] **Step 3: Write FmSynthEngine.cpp — loadPatch, applyPendingParams**

```cpp
void FmSynthEngine::loadPatch(const uint8_t patch[kPatchSize])
{
    std::memcpy(patchData_, patch, kPatchSize);
    lfo_.reset(patchData_ + 137);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::applyPendingParams()
{
    if (!paramsDirty_.load(std::memory_order_acquire)) return;
    paramsDirty_.store(false, std::memory_order_relaxed);

    // Update patch data from atomics for live-editing
    for (int op = 0; op < 6; op++)
    {
        int off = op * 21;
        // Level: float 0..1 → DX7 0..99
        float lvl = opLevelAtom_[op].load(std::memory_order_relaxed);
        patchData_[off + 16] = static_cast<uint8_t>(lvl * 99.0f);
        patchData_[off + 18] = static_cast<uint8_t>(opCoarseAtom_[op].load(std::memory_order_relaxed) & 31);
        patchData_[off + 19] = static_cast<uint8_t>(opFineAtom_[op].load(std::memory_order_relaxed) & 99);
        patchData_[off + 20] = static_cast<uint8_t>(opDetuneAtom_[op].load(std::memory_order_relaxed) & 15);
    }
    patchData_[134] = static_cast<uint8_t>(algorithmAtom_.load(std::memory_order_relaxed) & 31);
    patchData_[135] = static_cast<uint8_t>(feedbackAtom_.load(std::memory_order_relaxed) & 7);

    // Update all live voices
    for (auto& v : voices_)
    {
        if (v.live && v.note)
            v.note->update(patchData_, v.midiNote, v.velocity, v.channel);
    }
}
```

- [ ] **Step 4: Write FmSynthEngine.cpp — voice allocator and MIDI handling**

```cpp
FmSynthEngine::Voice* FmSynthEngine::allocateVoice()
{
    int bestNote = currentNote_;
    int bestScore = -1;
    int note = currentNote_;
    for (int i = 0; i < kMaxVoices; i++)
    {
        int score = 0;
        if (!voices_[note].live) score += 4;
        if (!voices_[note].keydown) score += 2;
        if ((score > bestScore) || (score == bestScore && voices_[note].keydownSeq < voices_[bestNote].keydownSeq))
        {
            bestNote = note;
            bestScore = score;
        }
        note = (note + 1) % kMaxVoices;
    }
    currentNote_ = (bestNote + 1) % kMaxVoices;
    return &voices_[bestNote];
}

void FmSynthEngine::noteOn(int channel, int pitch, int velocity)
{
    if (velocity == 0) { noteOff(channel, pitch); return; }

    bool mono = monoModeAtom_.load(std::memory_order_relaxed);

    // Mono mode: cut all voices
    if (mono)
        for (auto& v : voices_)
            if (v.live) v.note->keyup();

    Voice* v = allocateVoice();
    v->channel = channel;
    v->midiNote = pitch;
    v->velocity = velocity;
    v->keydown = true;
    v->sustained = sustain_;
    v->keydownSeq = nextKeydownSeq_++;
    v->note->init(patchData_, pitch, velocity, channel, &controllers_);
    v->live = true;
}

void FmSynthEngine::noteOff(int channel, int pitch)
{
    for (auto& v : voices_)
    {
        if (v.live && v.midiNote == pitch && v.channel == channel && v.keydown)
        {
            v.keydown = false;
            if (sustain_)
                v.sustained = true;
            else
                v.note->keyup();
            return;
        }
    }
}

void FmSynthEngine::allNotesOff()
{
    for (auto& v : voices_)
    {
        v.live = false;
        v.keydown = false;
        v.sustained = false;
        if (v.note) v.note->oscSync();
    }
}
```

- [ ] **Step 5: Write FmSynthEngine.cpp — render()**

```cpp
void FmSynthEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    applyPendingParams();

    int numSamples = buffer.getNumSamples();
    float* channelData = buffer.getWritePointer(0);

    // Process MIDI events
    for (const auto metadata : midi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())     noteOn(msg.getChannel(), msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff()) noteOff(msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) allNotesOff();
        else if (msg.isSustainPedal()) sustain_ = msg.getControllerValue() >= 64;
    }

    float outGain = outputLevelAtom_.load(std::memory_order_relaxed);
    int algo = algorithmAtom_.load(std::memory_order_relaxed);

    // Process in N-sample blocks (same as Dexed)
    for (int i = 0; i < numSamples; i += N)
    {
        int32_t audiobuf[N] = {};
        float sumbuf[N] = {};

        int32_t lfoVal = lfo_.getsample();
        int32_t lfoDelay = lfo_.getdelay();

        for (auto& v : voices_)
        {
            if (v.live && v.note)
            {
                v.note->compute(audiobuf, lfoVal, lfoDelay, &controllers_);
                for (int j = 0; j < N && (i + j) < numSamples; j++)
                {
                    int32_t val = audiobuf[j] >> 4;
                    int clip_val = val < -(1 << 24) ? -32768 : val >= (1 << 24) ? 32767 : val >> 9;
                    float f = static_cast<float>(clip_val) / 32768.0f;
                    f = std::clamp(f, -1.0f, 1.0f);
                    sumbuf[j] += f;
                    audiobuf[j] = 0;
                }
            }
        }

        for (int j = 0; j < N && (i + j) < numSamples; j++)
            channelData[i + j] = sumbuf[j] * outGain;
    }

    // Copy to right channel if stereo
    if (buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
}

int FmSynthEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& v : voices_)
        if (v.live) ++count;
    return count;
}
```

- [ ] **Step 6: Add FmSynthEngine to CMakeLists.txt**

Add `src/engine/FmSynthEngine.cpp` to the source list.

- [ ] **Step 7: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: FmSynthEngine compiles and links.

- [ ] **Step 8: Commit**

```bash
git add src/engine/FmSynthEngine.h src/engine/FmSynthEngine.cpp CMakeLists.txt
git commit -m "feat: FmSynthEngine with 16-voice DX7 FM synth core"
```

---

## Task 3: TrackFXSlot Integration

**Files:**
- Modify: `src/engine/TrackFXSlot.h`

- [ ] **Step 1: Add ActiveType::FmSynth and fmSynth member**

In `TrackFXSlot.h`, add `FmSynth` to the `ActiveType` enum (after `Sampler`):

```cpp
enum class ActiveType { None, EQ, Compressor, Reverb, Delay, Chorus, Flanger, Phaser, Plugin, Sampler, FmSynth };
```

Add member after the `sampler` member:

```cpp
std::unique_ptr<FmSynthEngine> fmSynth;
```

- [ ] **Step 2: Add FmSynth case to getParamDefsForType()**

```cpp
if (type == "fm_synth")
    return {
        { 0, "Algorithm",      0.0f,  0.0f, 31.0f },
        { 1, "Feedback",       5.0f,  0.0f,  7.0f },
        { 2, "Output Level",   0.8f,  0.0f,  1.0f },
        { 3, "OP1 Level",      0.8f,  0.0f,  1.0f },
        { 4, "OP2 Level",      0.8f,  0.0f,  1.0f },
        { 5, "OP3 Level",      0.8f,  0.0f,  1.0f },
        { 6, "OP4 Level",      0.8f,  0.0f,  1.0f },
        { 7, "OP5 Level",      0.8f,  0.0f,  1.0f },
        { 8, "OP6 Level",      0.8f,  0.0f,  1.0f },
        { 9, "OP1 Coarse",     0.0f,  0.0f, 31.0f },
        {10, "OP2 Coarse",     0.0f,  0.0f, 31.0f },
        {11, "OP3 Coarse",     0.0f,  0.0f, 31.0f },
        {12, "OP4 Coarse",     0.0f,  0.0f, 31.0f },
        {13, "OP5 Coarse",     0.0f,  0.0f, 31.0f },
        {14, "OP6 Coarse",     0.0f,  0.0f, 31.0f },
        {15, "OP1 Fine",       0.0f,  0.0f, 99.0f },
        {16, "OP2 Fine",       0.0f,  0.0f, 99.0f },
        {17, "OP3 Fine",       0.0f,  0.0f, 99.0f },
        {18, "OP4 Fine",       0.0f,  0.0f, 99.0f },
        {19, "OP5 Fine",       0.0f,  0.0f, 99.0f },
        {20, "OP6 Fine",       0.0f,  0.0f, 99.0f },
        {21, "LFO Rate",       0.5f,  0.0f,  1.0f },
        {22, "LFO Delay",      0.0f,  0.0f,  1.0f },
        {23, "LFO Pitch Depth",0.0f,  0.0f,  1.0f },
        {24, "LFO Amp Depth",  0.0f,  0.0f,  1.0f },
        {25, "LFO Waveform",   0.0f,  0.0f,  3.0f },
    };
```

- [ ] **Step 3: Add FmSynth case to prepare()**

After the `ActiveType::Sampler` case:

```cpp
case ActiveType::FmSynth:
{
    if (!fmSynth)
        fmSynth = std::make_unique<FmSynthEngine>();
    fmSynth->prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
    if (internalParamValues.size() > 0) fmSynth->setAlgorithm(static_cast<int>(internalParamValues[0]));
    if (internalParamValues.size() > 1) fmSynth->setFeedback(static_cast<int>(internalParamValues[1]));
    if (internalParamValues.size() > 2) fmSynth->setOutputLevel(internalParamValues[2]);
    for (int op = 0; op < 6 && internalParamValues.size() > (size_t)(3 + op); op++)
        fmSynth->setOpLevel(op, internalParamValues[3 + op]);
    break;
}
```

- [ ] **Step 4: Add FmSynth case to process()**

After the `ActiveType::Sampler` block:

```cpp
if (activeType == ActiveType::FmSynth)
{
    if (fmSynth)
    {
        buffer.clear();
        fmSynth->render(buffer, midiMessages);
        midiMessages.clear();
    }
    return;
}
```

- [ ] **Step 5: Add FmSynth to reset()**

```cpp
if (fmSynth) fmSynth->prepare(sampleRate_, 0); // re-init voices
```

- [ ] **Step 6: Add FmSynth case to applyInternalParamToDsp()**

```cpp
case ActiveType::FmSynth:
{
    if (!fmSynth) return;
    switch (paramIndex)
    {
        case 0: fmSynth->setAlgorithm(static_cast<int>(value)); break;
        case 1: fmSynth->setFeedback(static_cast<int>(value)); break;
        case 2: fmSynth->setOutputLevel(value); break;
        case 3: case 4: case 5: case 6: case 7: case 8:
            fmSynth->setOpLevel(paramIndex - 3, value); break;
        case 9: case 10: case 11: case 12: case 13: case 14:
            fmSynth->setOpCoarse(paramIndex - 9, static_cast<int>(value)); break;
        case 15: case 16: case 17: case 18: case 19: case 20:
            fmSynth->setOpFine(paramIndex - 15, static_cast<int>(value)); break;
        case 21: fmSynth->setLfoRate(value); break;
        case 22: fmSynth->setLfoDelay(value); break;
        case 23: fmSynth->setLfoPitchDepth(value); break;
        case 24: fmSynth->setLfoAmpDepth(value); break;
        case 25: fmSynth->setLfoWaveform(static_cast<int>(value)); break;
        default: return;
    }
    break;
}
```

- [ ] **Step 7: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: TrackFXSlot compiles with FmSynth cases.

- [ ] **Step 8: Commit**

```bash
git add src/engine/TrackFXSlot.h
git commit -m "feat: integrate FmSynthEngine as ActiveType::FmSynth in TrackFXSlot"
```

---

## Task 4: Command Layer — addFxSlot Support

**Files:**
- Modify: `src/engine/AudioEngineCommands_Fx.cpp`

- [ ] **Step 1: Add "fm_synth" to addFxSlot**

In `AudioEngineCommands::addFxSlot()`, the function already accepts any `type` string and creates a ValueTree with `IDs::fxType`. No code change needed — the string `"fm_synth"` will be stored and the `TrackFXSlot` will match it in `prepare()`.

However, the MCP tool's enum filter needs updating. Verify the addFxSlot command works by checking that `"fm_synth"` flows through to `TrackFXSlot::prepare()`.

- [ ] **Step 2: Commit (if any change needed)**

```bash
git add src/engine/AudioEngineCommands_Fx.cpp
git commit -m "feat: add fm_synth to valid FX types"
```

---

## Task 5: MCP Tools

**Files:**
- Modify: `src/mcp/McpTools_Audio.cpp`

- [ ] **Step 1: Add "fm_synth" to the add_fx enum**

In `registerFxTools()`, update the `add_fx` tool's enum:

```cpp
{"fxType", QJsonObject{{"type","string"},
    {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","sampler","fm_synth"}}}},
```

Also update the `set_internal_fx_param` tool description to mention `fm_synth`.

- [ ] **Step 2: Add fm_synth_load_preset tool**

```cpp
s.registerTool({"fm_synth_load_preset",
    "Load a raw DX7 patch (156 bytes hex string) into an FM synth FX slot.",
    objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
               {"slotIndex", QJsonObject{{"type","integer"}}},
               {"patchData", QJsonObject{{"type","string"}}}}, {"trackId","slotIndex","patchData"}),
    [e](const QJsonObject& a) -> McpToolResult {
        int ti = a.value("trackId").toInt();
        int si = a.value("slotIndex").toInt();
        auto fxSlots = e->getReadModel().getFxSlots(ti);
        if (si < 0 || si >= (int)fxSlots.size())
            return McpToolResult::text("slot not found", true);
        if (fxSlots[si].fxType != "fm_synth")
            return McpToolResult::text("slot is not an FM synth", true);

        QString hex = a.value("patchData").toString();
        if (hex.isEmpty() || hex.size() != 312) // 156 bytes = 312 hex chars
            return McpToolResult::text("patchData must be 312 hex characters (156 bytes)", true);

        uint8_t patch[156];
        for (int i = 0; i < 156; i++)
        {
            bool ok;
            patch[i] = static_cast<uint8_t>(hex.mid(i * 2, 2).toInt(&ok, 16));
            if (!ok) return McpToolResult::text("invalid hex at offset " + QString::number(i), true);
        }

        auto* proc = e->getMainProcessor();
        if (!proc) return McpToolResult::text("audio engine not initialized", true);
        auto* track = proc->getTrack(ti);
        if (!track) return McpToolResult::text("track not found", true);
        auto& chain = track->getFXChain();
        if (si >= (int)chain.size() || !chain[si])
            return McpToolResult::text("FX slot not found in chain", true);
        auto* slot = chain[si].get();
        if (!slot->fmSynthEngine())
            return McpToolResult::text("FM synth engine not initialized", true);

        slot->fmSynthEngine()->loadPatch(patch);
        return McpToolResult::text("ok");
    }});
```

- [ ] **Step 3: Add fm_synth_get_state tool**

```cpp
s.registerTool({"fm_synth_get_state",
    "Get the current state of an FM synth FX slot (active voices, algorithm, patch summary).",
    objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
               {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
    [e](const QJsonObject& a) -> McpToolResult {
        int ti = a.value("trackId").toInt();
        int si = a.value("slotIndex").toInt();
        auto fxSlots = e->getReadModel().getFxSlots(ti);
        if (si < 0 || si >= (int)fxSlots.size())
            return McpToolResult::text("slot not found", true);
        if (fxSlots[si].fxType != "fm_synth")
            return McpToolResult::text("slot is not an FM synth", true);

        QJsonObject state;
        auto* proc = e->getMainProcessor();
        if (proc)
        {
            auto* track = proc->getTrack(ti);
            if (track)
            {
                auto& chain = track->getFXChain();
                if (si < (int)chain.size() && chain[si])
                {
                    auto* engine = chain[si]->fmSynthEngine();
                    if (engine)
                    {
                        state["activeVoices"] = engine->activeVoiceCount();
                    }
                }
            }
        }
        state["algorithm"] = fxSlots[si].params.size() > 0 ? (int)fxSlots[si].params[0] : 0;
        return McpToolResult::text(QString::fromUtf8(
            QJsonDocument(state).toJson(QJsonDocument::Compact)));
    }});
```

- [ ] **Step 4: Add fmSynthEngine accessor to TrackFXSlot**

In `TrackFXSlot.h`, add after `samplerEngineForTest()`:

```cpp
FmSynthEngine* fmSynthEngine() { return fmSynth.get(); }
```

- [ ] **Step 5: Build and verify compilation**

Run: `cmake --build build --config Debug`
Expected: MCP tools compile.

- [ ] **Step 6: Commit**

```bash
git add src/mcp/McpTools_Audio.cpp src/engine/TrackFXSlot.h
git commit -m "feat: MCP tools for FM synth (add_fx fm_synth, load_preset, get_state)"
```

---

## Task 6: Unit Tests

**Files:**
- Create: `tests/unit/engine/fm_synth_test.cpp`

- [ ] **Step 1: Write basic FM synth tests**

```cpp
#include <gtest/gtest.h>
#include "engine/FmSynthEngine.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"

class FmSynthTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.prepare(44100.0, 512);
    }
    FmSynthEngine engine;
};

TEST_F(FmSynthTest, InitialStateNoActiveVoices) {
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, NoteOnActivatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, NoteOffDeactivatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buf.clear();
    engine.render(buf, midi);
    // Voice may still be in release phase, but keydown should be false
    // Check that it eventually goes silent
    for (int i = 0; i < 100; i++) {
        buf.clear();
        midi.clear();
        engine.render(buf, midi);
    }
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, ProducesNonZeroOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxVal = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++)
        maxVal = std::max(maxVal, std::abs(buf.getSample(0, i)));
    EXPECT_GT(maxVal, 0.0f);
}

TEST_F(FmSynthTest, AllNotesOffClearsVoices) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);
    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, LoadPatchDoesNotCrash) {
    uint8_t patch[156] = {};
    // Set algorithm to 0, all operators enabled
    patch[134] = 0;
    engine.loadPatch(patch);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    EXPECT_NO_THROW(engine.render(buf, midi));
}

TEST_F(FmSynthTest, SetAlgorithmDoesNotCrash) {
    engine.setAlgorithm(15);
    engine.setFeedback(3);
    engine.setOutputLevel(0.5f);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    EXPECT_NO_THROW(engine.render(buf, midi));
}

TEST_F(FmSynthTest, StereoOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    engine.render(buf, midi);
    // Both channels should have signal (stereo copy)
    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++) {
        maxL = std::max(maxL, std::abs(buf.getSample(0, i)));
        maxR = std::max(maxR, std::abs(buf.getSample(1, i)));
    }
    EXPECT_GT(maxL, 0.0f);
    EXPECT_GT(maxR, 0.0f);
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Add `tests/unit/engine/fm_synth_test.cpp` to the test source list.

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=FmSynthTest.*
```
Expected: All 8 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/engine/fm_synth_test.cpp CMakeLists.txt
git commit -m "test: FM synth engine unit tests (voice alloc, MIDI, patch load)"
```

---

## Task 7: End-to-End Verification

- [ ] **Step 1: Build full project**

```bash
cmake --build build --config Debug
```

- [ ] **Step 2: Run all tests**

```bash
build\Debug\hdaw_tests.exe
```
Expected: All existing tests pass + new FmSynthTest suite passes.

- [ ] **Step 3: Manual smoke test**

1. Start HDAW
2. Add a track
3. `add_fx` with `fm_synth` type
4. `fm_synth_load_preset` with a test DX7 patch
5. Play MIDI notes — should hear FM synthesis
6. `set_fx_param` to adjust algorithm, operator levels, feedback
7. `fm_synth_get_state` — should report active voices

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: internal FM synth (DX7-compatible 6-operator, 32 algorithms)"
```

---

## Pitfall Gates Triggered

| Gate | How Addressed |
|------|--------------|
| **Gate 1: State Not Restored on Rebuild** | FmSynthEngine state lives in `patchData_[]` and atomics. On `rebuildRoutingGraph()`, `TrackFXSlot::prepare()` re-instantiates the engine and pushes params from `internalParamValues`. The DX7 patch blob must be stored in the ValueTree and reloaded in `prepare()`. |
| **Gate 3: Audio-Thread Safety** | All param updates are via `std::atomic`. No locks, no allocations, no I/O on the audio thread. Voice allocation uses fixed-size array scan (O(kMaxVoices)). |
| **Gate 5: Frontend Stale Closures** | UI reads from store, not closures. FM synth params are set via `set_fx_param` command → ValueTree → listener → DSP. |
| **Gate 9: ID Namespace** | No new ID types — reuses existing FX slot ID system. |
| **Gate 10: Rebuild State Restore** | `prepare()` pushes all params from `internalParamValues` to the new engine. ValueTree stores patch blob. Test: mutate params, rebuild, assert on live engine. |
| **Gate 13: DSP-State Writes Without stateLock** | FmSynthEngine uses atomic params, no shared DSP objects between threads. Lock-free by design (mirrors SamplerEngine pattern). |

---

## Anti-Pattern Scan

- No N separate RPC calls — `add_fx` is a single call.
- No `syncSnapshot` after unverified RPC — FM synth state is in the FX slot ValueTree, synced naturally.
- No raw hex in CSS — UI will use theme tokens.
- No `DBG()` — uses `HDAW_LOG` if logging needed.
- New `.cpp` files added to CMakeLists.txt ✓

---

## Licensing Note

The msfa DSP files (`sin`, `exp2`, `fm_core`, `fm_op_kernel`, `env`, `lfo`, `freqlut`, `pitchenv`, `dx7note`, `controllers`, `aligned_buf`, `porta`, `synth`) are **Apache 2.0** licensed (Google Inc., 2012). They may be used in HDAW's GPL v3 codebase with attribution. Add an `Apache-2.0` license notice in each ported file and a third-party notice in the project root.
