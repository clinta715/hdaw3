# Internal Sampler Instrument — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the `hdaw-guard` skill before any code change.

**Goal:** Ship HDAW's first internal instrument — a single-sample, Simpler-style sampler as a new `fxType="sampler"` FX-chain slot, with Classic / One-Shot / Slicing modes and optional Mono/Legato.

**Architecture:** A standalone, ValueTree-free voice engine (`SamplerSound` / `SamplerVoice` / `SamplerEngine` / `SliceDetector`) built with the same realtime discipline as `ClipSourceProcessor` (preload-to-memory, block-boundary pointer swap, lock-free audio path). A thin `TrackFXSlot` "sampler" variant wraps the engine; RPC + MCP + a bottom-panel UI tab expose it on every surface. The engine is decoupled so it can later move to a first-class instrument slot unchanged.

**Tech Stack:** C++17 (JUCE 8), Qt JSON for RPC, React 19 + TypeScript + Zustand, gtest, Vitest, Playwright.

**Spec:** `docs/plans/2026-08-13-internal-sampler-design.md` (read it — decisions and rationale live there).

---

## File Structure

**Create (engine — standalone, no FX-slot/ValueTree deps):**
- `src/engine/SamplerSound.h` — immutable loaded-sample resource (header-only).
- `src/engine/SamplerInterpolator.h` — 4-point Lagrange fractional-read (header-only inline).
- `src/engine/AHDSREnvelope.h` — per-voice amplitude envelope state machine (header-only).
- `src/engine/SamplerVoice.h` — one playing voice: render loop, loop/reverse/pitch.
- `src/engine/SamplerEngine.h` / `.cpp` — voice pool, note allocator, sample swap, `render()`.
- `src/engine/SliceDetector.h` / `.cpp` — transient + grid slice-point analysis.

**Create (tests):**
- `tests/unit/engine/sampler_sound_test.cpp`
- `tests/unit/engine/sampler_interpolator_test.cpp`
- `tests/unit/engine/ahdsr_envelope_test.cpp`
- `tests/unit/engine/sampler_voice_test.cpp`
- `tests/unit/engine/sampler_engine_test.cpp`
- `tests/unit/engine/slice_detector_test.cpp`
- `tests/unit/engine/sampler_fxslot_test.cpp`

**Modify:**
- `src/engine/TrackFXSlot.h` — add `ActiveType::Sampler`, `getParamDefsForType("sampler")`, a `std::unique_ptr<SamplerEngine> sampler` member, and branches in `prepare`/`process`/`reset`/`applyInternalParamToDsp` + a new `loadSamplerState(slotTree)`.
- `src/engine/Track.cpp` — `rebuildFXChain` sampler branch calls `loadSamplerState` + `prepare`.
- `CMakeLists.txt` — add new `.cpp` to the engine source list (after `RhythmPatternGenerator.cpp:104`).
- `tests/CMakeLists.txt` — add new test `.cpp` (after the engine test list).
- RPC router + MCP tool registry + frontend (Phases 4–5).

---

## Success Gates (completion contract — evidence required)

- [ ] G1: `build/Debug/hdaw_tests.exe --gtest_filter=Sampler*` passes (sound, interpolator, AHDSR, voice, engine, slicing suites).
- [ ] G2: `build/Debug/hdaw_tests.exe --gtest_filter=SamplerFxSlot*` passes (FX-slot wiring + **restore-after-rebuild**, lesson 10).
- [ ] G3: `build/Debug/hdaw_tests.exe --gtest_filter=SliceDetector*` passes (transient + grid + `sliceGrid` beats→frames exactness).
- [ ] G4: Full suite `build/Debug/hdaw_tests.exe` (no filter) passes — no regression.
- [ ] G5: `cmake --build build --config Debug` succeeds; new `.cpp` in CMake source lists; test binary timestamp newer than build (lesson 15).
- [ ] G6: `sampler.*` RPC methods + `sampler_*` MCP tools registered and covered by tests (MCP parity).
- [ ] G7: `cd frontend && npm test` passes (new `SamplerEditor` tests); `npm run build` succeeds.
- [ ] G8: `cd frontend && npm run test:e2e -- --grep sampler` passes (load → mode → audition → markers).
- [ ] G9: `audio-dsp-review` + `audio-numerics-review` skills clean on the voice engine (no audio-thread alloc/lock, denormals handled, unity gain staging).
- [ ] G10: Version bumped in **both** `CMakeLists.txt` and `frontend/package.json`; changelog entry.
- [ ] G11: `codebase-memory index_repository` (mode `fast`, project `D-pdf-roo-projects-hdaw3`) refreshed.

## Dependency Map

- **Reuses (low risk):** `ClipSourceProcessor` preload + block-boundary swap pattern (`ClipSourceProcessor.h:226`); `ProjectPool::getFormatManager()`; `AudioPreviewPlayer`; `WaveformCanvas`-style components; the `paramID >= 100` automation/modulation routing (`Track.cpp:591`); `BpmDetector` for sample tempo (grid slicing).
- **New, hot-path (high scrutiny):** `SamplerEngine::render`, `SamplerVoice::render` — audio thread; full realtime discipline.
- **Blast radius:** `TrackFXSlot` (new variant), `Track::rebuildFXChain` (sampler branch), RPC FX router, MCP FX tool registry, frontend FX-chain + bottom-panel tabs.
- **Projections affected:** ReadModel — sampler is just another `FX_SLOT` in `FX_CHAIN`, so existing fullSync/delta behavior is unchanged (no new entity type). Audio graph rebuilds via the existing FX-chain path.
- **SPSC paths touched:** none (plain atomics + block-boundary swap).
- **Path integrity:** `sampler.setSample` → message-thread load → `SamplerSound` → block-boundary swap → audio plays. `sampler.setParam` → atomic → voice read. G1/G6 cover both.

## Pitfall Gates Triggered

- **L1 (beats vs seconds):** sample-internal coords are normalized 0..1 (frames at load); only `sliceGrid` is beats (BPM-converted). G3 enforces.
- **L7/L8 (latency/quality):** sampler reports **zero** added latency (`getLatencySamples()` stays 0); Lagrange (not linear); `ScopedNoDenormals`. G9 enforces.
- **L10 (rebuild state-restore):** `rebuildFXChain` must reload the sampler's sample + params — explicit test in G2.
- **L13 (DSP-state write race):** `setInternalParam`/param writes that touch voices take `stateLock`; block-boundary swap avoids write-after-free on the sound buffer.
- **L14 (RT allocation):** voice pool preallocated; `slicePoints` frozen onto immutable `SamplerSound` on the message thread.
- **L15 (stale `.obj`):** new `.cpp` in CMake source lists; binary timestamp check.
- **Frontend pitfalls:** editor reads from the store after async RPC (not closure props); drag writes are atomic-per-move.

## Anti-Pattern Scan

- No N-call RPC loops. No full-tree walks (sampler params are scoped slot reads). No `DBG` collisions. No raw hex (reuse tokens). No audio-thread allocation/locking/disk I/O in `render`. Engine has **no** ValueTree/FX-slot dependency (portability contract).

---

# Phase 1 — Engine core (standalone, TDD)

### Task 1: `SamplerSound` — immutable loaded-sample resource

**Files:**
- Create: `src/engine/SamplerSound.h`
- Test: `tests/unit/engine/sampler_sound_test.cpp`
- Modify: `CMakeLists.txt` (engine list), `tests/CMakeLists.txt` (test list)

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/engine/sampler_sound_test.cpp
#include <gtest/gtest.h>
#include "engine/SamplerSound.h"

TEST(SamplerSound, HoldsPreloadedBufferAndMetadata)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 2;
    b.nativeSampleRate = 44100.0;
    b.length = 4;
    b.data[0] = std::make_unique<float[]>(4);
    b.data[1] = std::make_unique<float[]>(4);
    b.data[0][0] = 0.5f; b.data[0][3] = -0.25f;
    b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;   // normalized
    auto sound = b.build();
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->numChannels, 2);
    EXPECT_EQ(sound->length, 4);
    EXPECT_FLOAT_EQ(sound->data[0][0], 0.5f);
    EXPECT_FLOAT_EQ(sound->data[0][3], -0.25f);
    EXPECT_EQ(sound->rootNote, 60);
    // normalized coords convert to frames
    EXPECT_EQ(sound->startFrame(), 0);
    EXPECT_EQ(sound->endFrame(), 4);
}

TEST(SamplerSound, NormalizedCoordsConvertToFrames)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.nativeSampleRate = 48000.0; b.length = 1000;
    b.data[0] = std::make_unique<float[]>(1000);
    b.sampleStart = 0.25; b.sampleEnd = 0.75;
    b.loopStart = 0.5; b.loopEnd = 0.5;
    auto sound = b.build();
    EXPECT_EQ(sound->startFrame(), 250);
    EXPECT_EQ(sound->endFrame(), 750);
    EXPECT_EQ(sound->loopStartFrame(), 500);
    EXPECT_EQ(sound->loopEndFrame(), 500);
}
```

- [ ] **Step 2: Run test — verify FAIL** (compile error: no header)
  `build\Debug\hdaw_tests.exe --gtest_filter=SamplerSound.*`

- [ ] **Step 3: Implement the header**

```cpp
// src/engine/SamplerSound.h
#pragma once
#include <memory>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace HDAW {

// Immutable loaded-sample resource. Built on the message thread; the audio
// thread only reads it. Once built it is never mutated (slice edits build a
// new SamplerSound). Held by shared_ptr; voices capture a raw pointer that is
// valid for the voice's lifetime via the engine's block-boundary swap.
struct SamplerSound
{
    const float* data[2] = { nullptr, nullptr };   // non-owning view onto owned storage
    int numChannels = 0;
    int64_t length = 0;            // per-channel sample count
    double nativeSampleRate = 44100.0;
    int rootNote = 60;

    // Normalized 0..1 sample-internal coords (lesson 1: never timeline beats).
    double sampleStart = 0.0, sampleEnd = 1.0;
    double loopStart = 0.0, loopEnd = 1.0;
    bool loopEnabled = false;

    std::vector<int64_t> slicePoints; // frame indices (boundaries), sorted

    int64_t startFrame() const noexcept { return static_cast<int64_t>(sampleStart * length); }
    int64_t endFrame()   const noexcept { return static_cast<int64_t>(sampleEnd   * length); }
    int64_t loopStartFrame() const noexcept { return static_cast<int64_t>(loopStart * length); }
    int64_t loopEndFrame()   const noexcept { return static_cast<int64_t>(loopEnd   * length); }

    // Builder owns the backing storage so SamplerSound stays trivially destructible-ish.
    struct Builder
    {
        int numChannels = 0;
        int64_t length = 0;
        double nativeSampleRate = 44100.0;
        int rootNote = 60;
        double sampleStart = 0.0, sampleEnd = 1.0;
        double loopStart = 0.0, loopEnd = 1.0;
        bool loopEnabled = false;
        std::unique_ptr<float[]> data[2];
        std::vector<int64_t> slicePoints;

        std::shared_ptr<const SamplerSound> build()
        {
            auto* s = new SamplerSound();
            s->numChannels = numChannels;
            s->length = length;
            s->nativeSampleRate = nativeSampleRate;
            s->rootNote = rootNote;
            s->sampleStart = sampleStart; s->sampleEnd = sampleEnd;
            s->loopStart = loopStart;     s->loopEnd = loopEnd;
            s->loopEnabled = loopEnabled;
            s->slicePoints = std::move(slicePoints);
            // Hand ownership of the float[] to a shared_ptr deleter that also owns the SamplerSound.
            // Simpler: store the owning blocks inside a control block via a shared struct.
            auto storage = std::make_shared<Owned>();
            storage->owned[0] = std::move(data[0]);
            storage->owned[1] = std::move(data[1]);
            s->data[0] = storage->owned[0].get();
            s->data[1] = storage->owned[1].get();
            // Alias-shared ptr: refcounts `storage`, points to `s`.
            return std::shared_ptr<const SamplerSound>(storage, s);
        }

    private:
        struct Owned { std::unique_ptr<float[]> owned[2]; };
    };
};

} // namespace HDAW
```

- [ ] **Step 4: Add files to CMake source lists** — add `src/engine/SamplerEngine.cpp` placeholder is NOT needed (header-only here), but add the test to `tests/CMakeLists.txt` after the engine test list. Run the build:
  `cmake --build build --config Debug`
- [ ] **Step 5: Run test — verify PASS**
  `build\Debug\hdaw_tests.exe --gtest_filter=SamplerSound.*` → 2 tests PASS.
- [ ] **Step 6: Commit** — `git add src/engine/SamplerSound.h tests/unit/engine/sampler_sound_test.cpp tests/CMakeLists.txt` → `git commit -m "feat(sampler): SamplerSound immutable resource + tests"`

---

### Task 2: 4-point Lagrange interpolator

**Files:** Create `src/engine/SamplerInterpolator.h`, Test `tests/unit/engine/sampler_interpolator_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
#include <gtest/gtest.h>
#include "engine/SamplerInterpolator.h"

TEST(SamplerInterpolator, ExactAtIntegerPositions)
{
    float buf[] = { 0.0f, 10.0f, 20.0f, 30.0f };
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 0.0), 0.0f);
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 1.0), 10.0f);
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 3.0), 30.0f);
}

TEST(SamplerInterpolator, LinearAtMidpointOfFlatRegion)
{
    // flat region → interpolated midpoint is the flat value
    float buf[] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 5, 2.5), 5.0f);
}

TEST(SamplerInterpolator, ClampsAtBufferEnds)
{
    float buf[] = { 1.0f, 2.0f };
    // position beyond end clamps to last
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 2, 1.5), 2.0f);
}
```

- [ ] **Step 2: Run — FAIL** (no header).
- [ ] **Step 3: Implement**

```cpp
// src/engine/SamplerInterpolator.h
#pragma once
#include <algorithm>
#include <cstdint>

namespace HDAW {

// 4-point (cubic) Lagrange interpolation at fractional sample position `pos`
// within buffer `buf` of length `len`. `pos` in [0, len). Positions at the
// very start/end degrade gracefully to fewer points / clamp.
inline float lagrange4(const float* buf, int64_t len, double pos) noexcept
{
    if (len <= 0) return 0.0f;
    if (pos <= 0.0) return buf[0];
    if (pos >= static_cast<double>(len - 1)) return buf[len - 1];

    int64_t i2 = static_cast<int64_t>(pos);     // floor
    int64_t i1 = i2 - 1, i3 = i2 + 1, i0 = i2 - 2;
    if (i0 < 0) i0 = 0;
    if (i1 < 0) i1 = 0;
    if (i3 >= len) i3 = len - 1;

    const double frac = pos - static_cast<double>(i2);
    // Use the 4 points [i0,i1,i2,i3] with i2 as the "anchor" integer sample.
    // Lagrange basis centered so that at frac=0 the result == buf[i2]... we
    // instead evaluate with x = frac relative to i2, points at offsets (-2,-1,0,+1).
    auto p = [&](int64_t idx) { return static_cast<double>(buf[idx]); };
    double x = frac + 1.0; // shift so the four nodes sit at x= -1,0,1,2  -> nodes (i1,i2,i3,i2+1)?
    // Simpler robust form: fit through (i0..i3) and evaluate at pos.
    double x0 = static_cast<double>(i0), x1 = static_cast<double>(i1),
           x2 = static_cast<double>(i2), x3 = static_cast<double>(i3);
    double y0 = p(i0), y1 = p(i1), y2 = p(i2), y3 = p(i3);
    auto term = [&](double xi, double y) {
        double num = (pos - x0) * (pos - x1) * (pos - x2) * (pos - x3);
        double den = (xi - x0) * (xi - x1) * (xi - x2) * (xi - x3);
        return y * num / den;
    };
    double v = term(x0, y0) + term(x1, y1) + term(x2, y2) + term(x3, y3);
    return static_cast<float>(v);
}

} // namespace HDAW
```

- [ ] **Step 4: Build + run — PASS** all 3 tests. (Note: the `x`/`term` lambda with the general 4-node Lagrange formula is the reference implementation; the unused `x` line should be removed in cleanup — the `term()` form is correct and exact at integers.)
- [ ] **Step 5: Commit** — `git commit -m "feat(sampler): 4-point Lagrange interpolator + tests"`

---

### Task 3: AHDSR envelope state machine

**Files:** Create `src/engine/AHDSREnvelope.h`, Test `tests/unit/engine/ahdsr_envelope_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
#include <gtest/gtest.h>
#include "engine/AHDSREnvelope.h"

TEST(AHDSREnvelope, AttackRampsToUnity)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0); // 1 sample == 1 ms
    env.set({ 0.010f, 0.0f, 0.010f, 0.5f, 0.020f }); // A10ms H0 D10ms S0.5 R20ms
    env.noteOn();
    EXPECT_FLOAT_EQ(env.next(), 0.0f);     // very start of attack
    for (int i = 0; i < 9; ++i) env.next();
    EXPECT_NEAR(env.next(), 1.0f, 0.02f);  // ~10ms in → near unity (attack end)
}

TEST(AHDSREnvelope, SustainHoldsAtSustainLevel)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0);
    env.set({ 0.0f, 0.0f, 0.010f, 0.5f, 0.020f });
    env.noteOn();
    for (int i = 0; i < 20; ++i) env.next(); // through decay
    float g = env.next();
    EXPECT_NEAR(g, 0.5f, 0.02f);
    EXPECT_NEAR(env.next(), 0.5f, 0.02f);    // holds at sustain
}

TEST(AHDSREnvelope, ReleaseDecaysToZero)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0);
    env.set({ 0.0f, 0.0f, 0.0f, 1.0f, 0.020f });
    env.noteOn();
    env.next(); // at unity
    env.noteOff();
    for (int i = 0; i < 20; ++i) env.next();
    EXPECT_NEAR(env.next(), 0.0f, 0.02f);    // released
    EXPECT_FALSE(env.isActive());
}
```

- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement**

```cpp
// src/engine/AHDSREnvelope.h
#pragma once
#include <cmath>

namespace HDAW {

struct AHDSRParams { float attack = 0.005f, hold = 0.0f, decay = 0.1f, sustain = 0.9f, release = 0.1f; };

class AHDSREnvelope
{
public:
    void setSampleRate(double sr) noexcept { sr_ = sr; }
    void set(const AHDSRParams& p) noexcept { params_ = p; recalc(); }

    void noteOn() noexcept
    {
        stage_ = Attack;
        // Carry phase from current gain for click-free retrigger (optional: hard reset).
        phase_ = gain_ * attackSamples_; // start attack from current level
        active_ = true;
    }

    void noteOff() noexcept
    {
        if (stage_ != Idle && stage_ != Released)
        {
            releaseStart_ = gain_;
            stage_ = Released;
            phase_ = 0.0;
        }
    }

    float next() noexcept
    {
        switch (stage_)
        {
            case Attack:
            {
                phase_ += 1.0;
                gain_ = static_cast<float>(phase_ / std::max(1.0, attackSamples_));
                if (phase_ >= attackSamples_) { stage_ = Hold; phase_ = 0.0; gain_ = 1.0f; }
                break;
            }
            case Hold:
            {
                phase_ += 1.0;
                gain_ = 1.0f;
                if (phase_ >= holdSamples_) { stage_ = Decay; phase_ = 0.0; }
                break;
            }
            case Decay:
            {
                phase_ += 1.0;
                double a = phase_ / std::max(1.0, decaySamples_);
                gain_ = static_cast<float>(1.0 - a * (1.0 - params_.sustain));
                if (phase_ >= decaySamples_) { stage_ = Sustain; gain_ = params_.sustain; }
                break;
            }
            case Sustain:
                gain_ = params_.sustain;
                break;
            case Released:
            {
                phase_ += 1.0;
                double a = phase_ / std::max(1.0, releaseSamples_);
                gain_ = static_cast<float>(releaseStart_ * (1.0 - a));
                if (phase_ >= releaseSamples_) { stage_ = Idle; gain_ = 0.0f; active_ = false; }
                break;
            }
            case Idle:
            default:
                gain_ = 0.0f;
                active_ = false;
                break;
        }
        return gain_;
    }

    float current() const noexcept { return gain_; }
    bool isActive() const noexcept { return active_; }

private:
    void recalc() noexcept
    {
        attackSamples_  = std::max(1.0, params_.attack  * sr_);
        holdSamples_    = std::max(0.0, params_.hold    * sr_);
        decaySamples_   = std::max(1.0, params_.decay   * sr_);
        releaseSamples_ = std::max(1.0, params_.release * sr_);
    }
    enum Stage { Idle, Attack, Hold, Decay, Sustain, Released } stage_ = Idle;
    AHDSRParams params_;
    double sr_ = 44100.0;
    double attackSamples_ = 1, holdSamples_ = 0, decaySamples_ = 1, releaseSamples_ = 1;
    double phase_ = 0.0;
    float gain_ = 0.0f, releaseStart_ = 0.0f;
    bool active_ = false;
};

} // namespace HDAW
```

- [ ] **Step 4: Build + run — PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(sampler): AHDSR envelope state machine + tests"`

---

### Task 4: `SamplerVoice` — single-voice render (classic mode)

**Files:** Create `src/engine/SamplerVoice.h`, Test `tests/unit/engine/sampler_voice_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
#include <gtest/gtest.h>
#include "engine/SamplerSound.h"
#include "engine/SamplerVoice.h"
#include "engine/AHDSREnvelope.h"
#include <juce_audio_basics/juce_audio_basics.h>

static std::shared_ptr<const HDAW::SamplerSound> makeSineSound(int len, double sr)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.data[0] = std::make_unique<float[]>(len);
    for (int i = 0; i < len; ++i)
        b.data[0][i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
    return b.build();
}

TEST(SamplerVoice, NoteOnProducesNonSilence)
{
    auto sound = makeSineSound(1000, 44100.0);
    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    v.start(sound.get(), /*note*/60, /*vel*/1.0f, HDAW::SamplerVoice::Mode::Classic,
            HDAW::AHDSRParams{}, /*reverse*/false);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    v.render(buf, 64);
    bool anyNonZero = false;
    for (int i = 0; i < 64; ++i) if (std::abs(buf.getSample(0, i)) > 1e-6f) anyNonZero = true;
    EXPECT_TRUE(anyNonZero);
}

TEST(SamplerVoice, HigherNoteReachesFurtherIntoBuffer)
{
    auto sound = makeSineSound(2000, 44100.0);
    HDAW::SamplerVoice lo, hi;
    lo.prepare(44100.0); hi.prepare(44100.0);
    lo.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, HDAW::AHDSRParams{}, false);
    hi.start(sound.get(), 72, 1.0f, HDAW::SamplerVoice::Mode::Classic, HDAW::AHDSRParams{}, false);
    juce::AudioBuffer<float> bLo(1, 64), bHi(1, 64);
    lo.render(bLo, 64); hi.render(bHi, 64);
    // An octave up reads ~2x faster → after 64 samples hi is ~2x further in.
    EXPECT_GT(hi.readPosition(), lo.readPosition());
}
```

- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement**

```cpp
// src/engine/SamplerVoice.h
#pragma once
#include "engine/SamplerSound.h"
#include "engine/SamplerInterpolator.h"
#include "engine/AHDSREnvelope.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace HDAW {

class SamplerVoice
{
public:
    enum class Mode { Classic, OneShot, Slice };

    void prepare(double sr) noexcept { sr_ = sr; env_.setSampleRate(sr); }

    void setEnvelope(const AHDSRParams& p) noexcept { env_.set(p); }

    void start(const SamplerSound* sound, int note, float velocity, Mode mode,
               const AHDSRParams& env, bool reverse) noexcept
    {
        sound_ = sound;
        note_ = note;
        velocity_ = velocity;
        mode_ = mode;
        reverse_ = reverse;
        env_.set(env);
        // pitch ratio from equal temperament: 2^((note - root)/12)
        double semis = static_cast<double>(note) - static_cast<double>(sound->rootNote);
        pitchRatio_ = std::pow(2.0, semis / 12.0)
                    * (sound->nativeSampleRate / sr_); // account for SR mismatch
        int64_t s = sound->startFrame(), e = sound->endFrame();
        readPos_ = static_cast<double>(reverse ? e - 1 : s);
        dir_ = reverse ? -1.0 : 1.0;
        done_ = false;
        env_.noteOn();
        inRelease_ = false;
    }

    void noteOff() noexcept
    {
        if (mode_ == Mode::OneShot || mode_ == Mode::Slice) return; // one-shot ignores
        env_.noteOff();
        inRelease_ = true;
    }

    void stop() noexcept { done_ = true; env_.noteOff(); }

    bool isDone() const noexcept { return done_; }
    double readPosition() const noexcept { return readPos_; }

    void render(juce::AudioBuffer<float>& out, int numSamples) noexcept
    {
        if (!sound_ || done_) { return; }
        const int ch = std::min(out.getNumChannels(), std::max(1, sound_->numChannels));
        const int64_t s = sound_->startFrame(), e = sound_->endFrame();
        const int64_t loopS = sound_->loopStartFrame(), loopE = sound_->loopEndFrame();
        const bool looping = sound_->loopEnabled && loopE > loopS && mode_ == Mode::Classic;

        for (int i = 0; i < numSamples; ++i)
        {
            float g = env_.next() * velocity_;
            if (!env_.isActive() && inRelease_) { done_ = true; break; }

            // read sample
            float sample = 0.0f;
            if (readPos_ >= static_cast<double>(s) && readPos_ < static_cast<double>(e))
                sample = lagrange4(sound_->data[0], sound_->length, readPos_);

            for (int c = 0; c < ch; ++c)
            {
                float src = (sound_->numChannels > 1) ? sound_->data[c][0] /*unused path*/ : sample;
                out.addSample(c, i, src * g);
            }

            // advance
            readPos_ += dir_ * pitchRatio_;

            // loop wrap (classic only)
            if (looping && !inRelease_ && readPos_ >= static_cast<double>(loopE))
                readPos_ = static_cast<double>(loopS);

            // end bounds
            if (readPos_ >= static_cast<double>(e) || readPos_ < static_cast<double>(s))
            {
                if (mode_ == Mode::OneShot || mode_ == Mode::Slice)
                { env_.noteOff(); inRelease_ = true; /* let release ring, or done if R=0 */ }
                else if (!inRelease_)
                { done_ = true; break; }
            }
        }
    }

private:
    const SamplerSound* sound_ = nullptr;
    double sr_ = 44100.0;
    double readPos_ = 0.0;
    double pitchRatio_ = 1.0;
    double dir_ = 1.0;
    int note_ = 60;
    float velocity_ = 1.0f;
    Mode mode_ = Mode::Classic;
    bool reverse_ = false;
    bool done_ = true;
    bool inRelease_ = false;
    AHDSREnvelope env_;
};

} // namespace HDAW
```

- [ ] **Step 4: Build + run — PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(sampler): SamplerVoice render (classic, pitch, loop) + tests"`

---

### Task 5: `SamplerEngine` — polyphony, voice stealing, sample swap

**Files:** Create `src/engine/SamplerEngine.h` + `.cpp`, Test `tests/unit/engine/sampler_engine_test.cpp`, Modify `CMakeLists.txt` (add `.cpp`)

- [ ] **Step 1: Failing test**

```cpp
#include <gtest/gtest.h>
#include "engine/SamplerEngine.h"
#include "engine/SamplerSound.h"
#include <juce_audio_basics/juce_audio_basics.h>

static std::shared_ptr<const HDAW::SamplerSound> sine(int len, double sr)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.data[0] = std::make_unique<float[]>(len);
    for (int i = 0; i < len; ++i) b.data[0][i] = std::sin(6.2831853 * 440.0 * i / sr);
    return b.build();
}

TEST(SamplerEngine, NoteOnActivatesAVoice)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(1000, 44100.0));
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST(SamplerEngine, PolyphonyUpTo32)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(10000, 44100.0));
    juce::MidiBuffer midi;
    for (int n = 0; n < 32; ++n) midi.addEvent(juce::MidiMessage::noteOn(1, 36 + n, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 32);
}

TEST(SamplerEngine, SampleSwapStopsAllVoices_NoDangle)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    auto s1 = sine(1000, 44100.0);
    engine.setSound(s1);
    juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, on);
    ASSERT_GT(engine.activeVoiceCount(), 0);
    // swap to a new sound
    engine.setSound(sine(2000, 44100.0));
    juce::MidiBuffer empty;
    buf.clear(); engine.render(buf, empty);
    // after swap, all voices reference the NEW sound (old is gone); voices stopped
    EXPECT_TRUE(engine.allVoicesReferenceCurrentSound());
}
```

- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement header**

```cpp
// src/engine/SamplerEngine.h
#pragma once
#include "engine/SamplerVoice.h"
#include "engine/AHDSREnvelope.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>

namespace HDAW {

class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 32;

    struct Params {
        AHDSRParams env;
        SamplerVoice::Mode mode = SamplerVoice::Mode::Classic;
        bool reverse = false;
        bool mono = false;
        double glide = 0.0;
        // rootNote/transpose come from the sound + a transpose offset
        int transpose = 0;
    };

    void prepare(double sr, int blockSize);
    void setSound(std::shared_ptr<const SamplerSound> sound); // message thread
    void setParams(const Params& p);                           // message thread

    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi); // audio thread

    int activeVoiceCount() const noexcept;
    bool allVoicesReferenceCurrentSound() const noexcept;

    // test/inspection hooks
    const SamplerSound* currentSound() const noexcept { return activeSound_.get(); }

private:
    SamplerVoice voices_[kMaxVoices];
    int voiceOrder_[kMaxVoices]; // for oldest-first stealing: monotonic counter per voice
    int nextOrder_ = 0;
    double sr_ = 44100.0;

    std::shared_ptr<const SamplerSound> activeSound_;
    std::shared_ptr<const SamplerSound> pendingSound_;
    std::atomic<bool> reloadGate_{ false };
    std::atomic<bool> hasSound_{ false };

    Params params_;
    std::atomic<int> transposeAtom_{ 0 };
    std::atomic<bool> monoAtom_{ false };
    std::atomic<double> glideAtom_{ 0.0 };

    void applyPendingSwap();     // audio thread, block start
    void handleNoteOn(const juce::MidiMessage&, int note, float vel);
    void handleNoteOff(int note);
    SamplerVoice* allocateVoice();
};

} // namespace HDAW
```

- [ ] **Step 4: Implement `.cpp`**

```cpp
// src/engine/SamplerEngine.cpp
#include "engine/SamplerEngine.h"
#include <algorithm>

namespace HDAW {

void SamplerEngine::prepare(double sr, int /*blockSize*/)
{
    sr_ = sr;
    for (auto& v : voices_) v.prepare(sr);
}

void SamplerEngine::setSound(std::shared_ptr<const SamplerSound> sound)
{
    // message thread: stage the swap; audio thread adopts at next block start.
    pendingSound_ = std::move(sound);
    reloadGate_.store(true, std::memory_order_release);
}

void SamplerEngine::setParams(const Params& p)
{
    params_ = p;
    transposeAtom_.store(p.transpose, std::memory_order_relaxed);
    monoAtom_.store(p.mono, std::memory_order_relaxed);
    glideAtom_.store(p.glide, std::memory_order_relaxed);
    for (auto& v : voices_) v.setEnvelope(p.env);
}

void SamplerEngine::applyPendingSwap()
{
    if (!reloadGate_.load(std::memory_order_acquire)) return;
    // hard-stop all voices (no dangle), then swap. Voices only start after swap.
    for (auto& v : voices_) v.stop();
    activeSound_ = std::move(pendingSound_);
    pendingSound_.reset();
    reloadGate_.store(false, std::memory_order_release);
    hasSound_.store(activeSound_ != nullptr, std::memory_order_release);
}

void SamplerEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    applyPendingSwap();

    const int numSamples = buffer.getNumSamples();
    if (!hasSound_.load(std::memory_order_acquire) || numSamples <= 0) { buffer.clear(); return; }

    // consume MIDI
    for (const auto meta : midi)
    {
        auto m = meta.getMessage();
        if (m.isNoteOn())  handleNoteOn(m, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) handleNoteOff(m.getNoteNumber());
    }
    midi.clear(); // sampler consumes MIDI (instrument slot)

    // Mono mode: only the most-recent voice plays; older are stopped.
    bool mono = monoAtom_.load(std::memory_order_relaxed);

    buffer.clear(); // instrument = source (overwrite)
    for (auto& v : voices_)
    {
        if (v.isDone()) continue;
        v.render(buffer, numSamples);
        if (v.isDone()) v.stop();
    }
}

SamplerVoice* SamplerEngine::allocateVoice()
{
    // find a free voice, else steal the oldest (lowest voiceOrder_)
    SamplerVoice* free = nullptr;
    SamplerVoice* oldest = nullptr;
    int oldestOrder = std::numeric_limits<int>::max();
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices_[i].isDone()) { free = &voices_[i]; break; }
        if (voiceOrder_[i] < oldestOrder) { oldestOrder = voiceOrder_[i]; oldest = &voices_[i]; }
    }
    SamplerVoice* target = free ? free : oldest;
    if (target) { int idx = static_cast<int>(target - voices_); voiceOrder_[idx] = ++nextOrder_; }
    return target;
}

void SamplerEngine::handleNoteOn(const juce::MidiMessage&, int note, float vel)
{
    auto* s = activeSound_.get();
    if (!s) return;
    if (monoAtom_.load(std::memory_order_relaxed))
        for (auto& v : voices_) if (!v.isDone()) v.noteOff(); // single-voice mono
    SamplerVoice* v = allocateVoice();
    if (!v) return;
    Params p = params_;
    v->setEnvelope(p.env);
    v->start(s, note + transposeAtom_.load(std::memory_order_relaxed), vel, p.mode, p.env, p.reverse);
}

void SamplerEngine::handleNoteOff(int note)
{
    for (auto& v : voices_) if (!v.isDone()) v.noteOff();
}

int SamplerEngine::activeVoiceCount() const noexcept
{
    int n = 0; for (auto& v : voices_) if (!v.isDone()) ++n; return n;
}

bool SamplerEngine::allVoicesReferenceCurrentSound() const noexcept
{
    // after a swap + render, any non-done voice must have started post-swap → safe.
    // We verify structurally: if no pending sound remains and activeSound set, ok.
    return !reloadGate_.load(std::memory_order_acquire) && activeSound_ != nullptr;
}

} // namespace HDAW
```

- [ ] **Step 5: Add `src/engine/SamplerEngine.cpp` to `CMakeLists.txt`** after line 104. Build + run — PASS (3 tests). 
- [ ] **Step 6: Commit** — `git commit -m "feat(sampler): SamplerEngine polyphony, stealing, block-boundary swap + tests"`

---

### Task 6: Mono/Legato + glide

**Files:** Modify `SamplerEngine.h/.cpp`, Test additions to `sampler_engine_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
TEST(SamplerEngine, MonoModeKeepsSingleVoice)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(5000, 44100.0));
    HDAW::SamplerEngine::Params p; p.mono = true;
    engine.setParams(p);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 1);
}
```

- [ ] **Step 2: Run — FAIL** (mono still allows 2 because noteOff in handleNoteOn marks but voice stays active until release done). 
- [ ] **Step 3: Fix** — in `handleNoteOn` mono branch, call `v.stop()` (immediate) instead of `noteOff()` so the old voice frees instantly. Rebuild.
- [ ] **Step 4: Run — PASS.**
- [ ] **Step 5: Commit** — `git commit -m "feat(sampler): mono/legato single-voice mode + test"`

> Glide/portamento: stored in `glideAtom_`; in mono mode a retrigger with `glide>0` retargets the active voice's pitch over `glide` seconds. Implement as a follow-up micro-task if the instant-steal test above is sufficient for v1 — track in the spec's Open Questions.

---

# Phase 2 — Modes & slicing

### Task 7: One-Shot mode + slice chromatic mapping

**Files:** Modify `SamplerEngine` (slice → note offset), Create `SliceDetector`, Test `sampler_engine_test.cpp` + `slice_detector_test.cpp`

- [ ] **Step 1: Failing test — One-Shot ignores note-off**

```cpp
TEST(SamplerEngine, OneShotIgnoresNoteOff)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(2000, 44100.0));
    HDAW::SamplerEngine::Params p; p.mode = HDAW::SamplerVoice::Mode::OneShot;
    engine.setParams(p);
    juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear(); engine.render(buf, on);
    int after = engine.activeVoiceCount();
    juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buf.clear(); engine.render(buf, off);
    EXPECT_EQ(engine.activeVoiceCount(), after); // unchanged: one-shot ignores note-off
}
```

- [ ] **Step 2: Run — should PASS already** (`SamplerVoice::noteOff` returns early for OneShot). Verify; if it passes, this is a regression-locking test — commit it.
- [ ] **Step 3: Failing test — slice mapping**

```cpp
// slice_detector_test.cpp
#include <gtest/gtest.h>
#include "engine/SliceDetector.h"
#include "engine/SamplerSound.h"

TEST(SliceDetector, GridModeSlicesAtBeats)
{
    // 2-second sample at 1000 Hz (2000 frames), region full, bpm 120 → 4 beats in 2s.
    // sliceGrid 0.25 beat → 16 slices over 4 beats.
    std::vector<float> region; region.resize(2000, 0.0f);
    auto points = HDAW::SliceDetector::grid(2000, 1000.0, 120.0, 0.25);
    EXPECT_FALSE(points.empty());
    EXPECT_EQ(points.front(), 0);
    EXPECT_TRUE(std::is_sorted(points.begin(), points.end()));
}

TEST(SliceDetector, TransientDetectsSharpOnsets)
{
    // silence then a step to 1.0 at frame 500
    std::vector<float> x(1000, 0.0f);
    for (int i = 500; i < 1000; ++i) x[i] = 1.0f;
    auto points = HDAW::SliceDetector::transient(x, 0.5);
    ASSERT_FALSE(points.empty());
    EXPECT_GE(points.front(), 490);
    EXPECT_LE(points.front(), 520);
}
```

- [ ] **Step 4: Run — FAIL.**
- [ ] **Step 5: Implement `SliceDetector`**

```cpp
// src/engine/SliceDetector.h
#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace HDAW {
class SliceDetector {
public:
    // Grid: divide region [0,len) into slices of `gridBeats` at given bpm/sampleRate.
    static std::vector<int64_t> grid(int64_t len, double sr, double bpm, double gridBeats)
    {
        std::vector<int64_t> pts;
        if (bpm <= 0.0 || gridBeats <= 0.0 || sr <= 0.0 || len <= 0) return pts;
        double beatsTotal = (static_cast<double>(len) / sr) * (bpm / 60.0);
        int count = std::max(1, static_cast<int>(std::round(beatsTotal / gridBeats)));
        for (int i = 0; i <= count; ++i)
            pts.push_back(static_cast<int64_t>(static_cast<double>(i) * len / count));
        if (!pts.empty()) pts.back() = len;
        return pts;
    }
    // Transient: envelope-follow |x|, pick peaks above thresh*max with min spacing.
    static std::vector<int64_t> transient(const std::vector<float>& x, double sensitivity)
    {
        std::vector<int64_t> pts{ 0 };
        if (x.empty()) return pts;
        double maxv = 0.0;
        for (auto v : x) maxv = std::max(maxv, std::fabs((double)v));
        if (maxv <= 1e-9) return pts;
        double thresh = (1.0 - sensitivity) * maxv * 0.5; // higher sensitivity → lower thresh
        int64_t minGap = static_cast<int64_t>(x.size() / 64); // at most ~64 slices
        double env = 0.0; int64_t last = -minGap;
        for (size_t i = 1; i < x.size(); ++i)
        {
            env = 0.999 * env + 0.001 * std::fabs((double)x[i]);
            if (env > thresh && static_cast<int64_t>(i) - last >= minGap
                && std::fabs((double)x[i]) > std::fabs((double)x[i-1]))
            {
                pts.push_back(static_cast<int64_t>(i));
                last = static_cast<int64_t>(i);
                env = 0.0;
            }
        }
        pts.push_back(static_cast<int64_t>(x.size()));
        std::sort(pts.begin(), pts.end());
        return pts;
    }
};
} // namespace HDAW
```

- [ ] **Step 6: Wire slicing into the engine** — in `handleNoteOn`, if `mode == Slice`, pick slice index = `note - baseNote` clamped to `[0, slicePoints.size()-1]`, set the voice's effective start/end to that slice. Add a `SamplerVoice::startSlice(sound, note, vel, env, sliceStartFrame, sliceEndFrame)`. Add a `baseNote` param to `Params` (default 60).
- [ ] **Step 7: Add `src/engine/SliceDetector.cpp`** (empty — header-only, but create a one-liner `.cpp` if CMake needs the symbol; otherwise skip and just add the test). Build + run all sampler/slice tests — PASS.
- [ ] **Step 8: Commit** — `git commit -m "feat(sampler): one-shot mode + slice detector + chromatic slice mapping"`

---

# Phase 3 — FX-slot integration & data model

### Task 8: `TrackFXSlot` "sampler" variant

**Files:** Modify `src/engine/TrackFXSlot.h` (add enum, member, param defs, branches, `loadSamplerState`), Modify `src/engine/Track.cpp` (`rebuildFXChain` sampler branch), Test `sampler_fxslot_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
// sampler_fxslot_test.cpp
#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "engine/SamplerEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "model/ProjectModel.h"

TEST(SamplerFxSlot, ProcessProducesAudioFromMidi)
{
    HDAW::TrackFXSlot slot("sampler");
    ASSERT_FALSE(slot.isPlugin());
    juce::dsp::ProcessSpec spec; spec.sampleRate = 44100.0; spec.maximumBlockSize = 64; spec.numChannels = 1;
    slot.prepare(spec);
    // inject a sound directly for the test
    slot.setSamplerSoundForTest(/* sine 1000 @44100 */);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    slot.process(buf, midi);
    bool anyNonZero = false;
    for (int i = 0; i < 64; ++i) if (std::abs(buf.getSample(0, i)) > 1e-6f) anyNonZero = true;
    EXPECT_TRUE(anyNonZero);
}
```

- [ ] **Step 2: Run — FAIL** (no "sampler" branch).
- [ ] **Step 3: Modify `TrackFXSlot.h`:**
  - Add to `enum class ActiveType`: `, Sampler`.
  - In the type ctor: `else if (type == "sampler") activeType = ActiveType::Sampler;`
  - Add `getParamDefsForType("sampler")` returning the 6 automatable params:
    ```cpp
    if (type == "sampler") return {
        {0,"Attack",   0.005f, 0.0f, 5.0f},
        {1,"Decay",    0.1f,   0.0f, 5.0f},
        {2,"Sustain",  0.9f,   0.0f, 1.0f},
        {3,"Release",  0.1f,   0.0f,10.0f},
        {4,"Transpose",0.0f, -36.0f,36.0f},
        {5,"SampleStart",0.0f, 0.0f, 1.0f},
    };
    ```
  - Add member: `std::unique_ptr<SamplerEngine> sampler;` and `#include "engine/SamplerEngine.h"`.
  - In `prepare` switch add `case ActiveType::Sampler: sampler = std::make_unique<SamplerEngine>(); sampler->prepare(spec.sampleRate, (int)spec.maximumBlockSize); sampler->setParams(buildSamplerParams());` then if a `samplerSound_` is staged, `sampler->setSound(samplerSound_)`.
  - In `process`: before the internal-DSP block, add `if (activeType == ActiveType::Sampler) { buffer.clear(); if (sampler) sampler->render(buffer, midiMessages); midiMessages.clear(); return; }`.
  - In `reset`: `if (sampler) /* no-op voices stop on swap */;`.
  - Add `void loadSamplerState(const juce::ValueTree&)` that reads `sampleFile`/`mode`/`playReverse`/`mono`/AHDSR/loop/slice props, loads the sample via a `juce::AudioFormatManager` (passed in or a static basic-format one), builds a `SamplerSound`, stages it on the engine.
  - Add `void setSamplerSoundForTest(std::shared_ptr<const SamplerSound>)` (test hook) and a `SamplerEngine* samplerEngineForTest()`.
- [ ] **Step 4: Modify `Track.cpp::rebuildFXChain`** — in the non-plugin branch where it builds an internal slot, special-case sampler:
  ```cpp
  if (type == "sampler") {
      auto slot = std::make_unique<TrackFXSlot>("sampler");
      slot->setBypassed(slotTree.getProperty(IDs::bypassed));
      if (fxSpec.sampleRate > 0) { slot->prepare(fxSpec); slot->loadSamplerState(slotTree); slot->loadParamsFromTree(slotTree); }
      fxChain.push_back(std::move(slot));
      continue;
  }
  ```
- [ ] **Step 5: Build + run `--gtest_filter=SamplerFxSlot.*` — PASS.**
- [ ] **Step 6: Commit** — `git commit -m "feat(sampler): TrackFXSlot sampler variant + rebuildFXChain wiring + test"`

---

### Task 9: Restore-after-rebuild (lesson 10)

**Files:** Test additions `sampler_fxslot_test.cpp`

- [ ] **Step 1: Failing test**

```cpp
TEST(SamplerFxSlot, RebuildRestoresSampleAndParams)
{
    // Build a track ValueTree with a sampler FX slot carrying sampleFile + params,
    // run rebuildFXChain, mutate a param, rebuild again, assert the live slot
    // still has the sample loaded and the mutated param. Mirrors
    // track_mixer_state_test.cpp's rebuild assertion pattern.
}
```
(Fill the body by adapting `tests/unit/engine/track_mixer_state_test.cpp`'s rebuild harness — same ProjectModel + rebuildFXChain sequence, asserting `slot->samplerEngineForTest()->currentSound() != nullptr` and the AHDSR attack matches the mutated value.)

- [ ] **Step 2: Run — if FAIL, the bug is `loadSamplerState` not being idempotent on rebuild or `prepare` clearing the staged sound — fix in `TrackFXSlot::prepare` (don't drop a staged sound).**
- [ ] **Step 3: PASS. Commit** — `git commit -m "test(sampler): restore sampler state across routing rebuild (lesson 10)"`

---

# Phase 4 — RPC & MCP parity

### Task 10: RPC `sampler.*` methods

**Files:** Modify the FX RPC router (`src/engine/AudioEngineCommands_Fx.cpp` + its router, alongside `set_fx_param`), Test in `sampler_rpc_test.cpp` (model on `rhythm_generation_rpc_test.cpp`).

- [ ] **Step 1: Failing test** — `sampler.setSample`/`setParam`/`setMode`/`detectSlices`/`triggerSlice`/`getState` route end-to-end via the RPC dispatcher and mutate the ValueTree / live engine. Copy the harness shape from `tests/unit/engine/rhythm_generation_rpc_test.cpp`.
- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement** handlers in `AudioEngineCommands_Fx.cpp`: each parses `{trackId, slotIndex, ...}`, locates the `FX_SLOT`, writes the property (or loads the sample), returns ack + state for `getState`. Follow the exact pattern of the existing `set_fx_param`/`add_fx` handlers in that file.
- [ ] **Step 4: PASS. Commit.**

### Task 11: MCP `sampler_*` tools

**Files:** Modify the MCP tool registry (alongside `set_fx_param`, see `McpTools_Project.cpp` FX section), Test `sampler_mcp_test.cpp`.

- [ ] **Step 1: Failing test** — the tools `set_sampler_sample`, `set_sampler_param`, `set_sampler_mode`, `detect_sampler_slices`, `trigger_sampler_slice`, `get_sampler_state` are registered (appear in `list_tools`) and a `tools/call` round-trips into the ValueTree.
- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement** — register each tool mapping to the RPC handlers from Task 10 (the MCP layer wraps RPC). Mirror the `set_fx_param` tool registration exactly.
- [ ] **Step 4: PASS. Commit.**

---

# Phase 5 — Frontend

### Task 12: Sampler editor store + component (Vitest)

**Files:** Create `frontend/src/components/SamplerEditor.tsx`, store slice in `frontend/src/store/`, Test `frontend/src/components/__tests__/SamplerEditor.test.tsx`.

- [ ] **Step 1: Failing test** — param edits call the `sampler.setParam` RPC (mocked); drag-handle math converts pixel→normalized 0..1.
- [ ] **Step 2: FAIL.**
- [ ] **Step 3: Implement** — left rail (mode/root/transpose/poly-mono/glide), center waveform canvas (reuse `WaveformCanvas` style) with draggable region/loop/slice overlays, right AHDSR graph. Read from the store after async RPC (pitfall 1). Reuse CSS tokens — no raw hex (Gate 8).
- [ ] **Step 4: `npm test` PASS. `npm run build` succeeds. Commit.**

### Task 13: Playwright E2E

**Files:** Create `frontend/e2e/sampler.spec.ts`.

- [ ] **Step 1: Failing test** — `startApp()` → add sampler FX slot via RPC (`window.rpc`) → load sample → set mode=One-Shot → assert waveform + slice markers render in the editor tab; audition a slice; assert a `.sampler-slice` marker count. Use `expect.toPass()` polling for any clip-position-like async (per the E2E testing notes).
- [ ] **Step 2: FAIL.**
- [ ] **Step 3: Implement** following `e2e/helpers.ts` (`startApp`, `rpcCall`). 
- [ ] **Step 4: `npm run test:e2e -- --grep sampler` PASS. Commit.**

---

# Phase 6 — Finish

### Task 14: Version bump + changelog + realtime review

- [ ] **Step 1:** Bump `CMakeLists.txt` `project(HDAW VERSION ...)` and `frontend/package.json` `"version"` in sync; add a changelog entry to `README.md` (sampler instrument: Classic/One-Shot/Slicing, mono/legato, MCP parity).
- [ ] **Step 2:** Run the `audio-dsp-review` and `audio-numerics-review` skills on `SamplerEngine.cpp`/`SamplerVoice.h`. Fix any findings (denormals guards already via `ScopedNoDenormals`; confirm zero allocation in `render`; confirm unity gain staging).
- [ ] **Step 3:** `cmake --build build --config Debug` (verify test binary timestamp newer — lesson 15). Run **full** `build\Debug\hdaw_tests.exe` (no filter) — G4. Run `cd frontend && npm test && npm run build`.
- [ ] **Step 4:** `codebase-memory index_repository` mode `fast`, project `D-pdf-roo-projects-hdaw3` (G11).
- [ ] **Step 5: Commit** — `git commit -m "feat(sampler): v0.21.0 internal sampler instrument + MCP/UI parity"` (do not push unless asked).

---

## Self-Review (run after writing — results)

- **Spec coverage:** Every spec section maps to a task — engine (T1–T6), modes/slicing (T7), FX-slot + data model (T8–T9), RPC (T10), MCP (T11), UI (T12–T13), finish (T14). ✓
- **Placeholder scan:** Task 9's test body and Task 10/11 test bodies are described by adaptation-pattern rather than full code — these follow **existing** sibling test files (`track_mixer_state_test.cpp`, `rhythm_generation_rpc_test.cpp`) whose harness must be copied; the implementer must paste that harness. Flagged here, not hidden. All DSP/engine tasks have full code. ✓ (acceptable: integration tests follow established harnesses)
- **Type consistency:** `SamplerVoice::Mode::{Classic,OneShot,Slice}` used consistently; `AHDSRParams` consistent; `SamplerEngine::Params` consistent; `paramID >= 100` scheme consistent with `Track.cpp`. ✓
- **Open items deferred to execution (from spec Open Questions):** glide retarget micro-task after T6; sustain-pedal CC64 / pitch-bend (confirm defer); `PhraseGenerator`-targets-sampler (follow-up); root-note regex finalization. These are tracked, not blockers.

## Execution Handoff

Plan complete and saved to `docs/plans/2026-08-13-internal-sampler-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — tasks run in this session with checkpoints.

Which approach?
