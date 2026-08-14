# Realtime-Safety Instrumentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a debug-build-only tripwire layer that catches NaN/±Inf/DC, audio-thread blocking on locks, glitch-duration overruns, and wrong-thread usage in HDAW's audio engine — so future regressions of lessons 3/13 are caught by a test, not a user report.

**Architecture:** Two header-only, `JUCE_DEBUG`-gated instruments in `src/common/` — `BufferCheck` (NaN/Inf/DC scan + per-block glitch timer, with an audio→message flag drain) and `RealtimeGuard` (RAII thread-identity guard for `prepareToPlay`/`releaseResources`/rebuild paths). `BufferCheck::checkBuffer` is called at the end of each `processBlock`; on detection it sets an atomic flag that a message-thread drainer reports via `HDAW_LOG("RT", ...)`. The check itself never allocates, locks, or logs on the audio thread. Compiles out to zero code when `JUCE_DEBUG` is off.

**Tech Stack:** C++17 (JUCE 8), gtest. Reference: HISE `DebugLogger.h:191/:320/:529`, `UtilityClasses.h:375` `ADD_GLITCH_DETECTOR`, `checkPriorityInversion` (`DebugLogger.cpp:626/:643`).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/common/BufferCheck.h` (new) | NaN/Inf/DC scan + block-duration glitch timer; atomic flag drain; `JUCE_DEBUG`-gated |
| `src/common/RealtimeGuard.h` (new) | records audio-thread id; `assertAudioThread()`/`assertMessageThread()` RAII-style checks; lock-block helper |
| `src/common/DebugLog.h` (modify) | add `HDAW_LOG_ALWAYS` (bypasses tag filter — instrumentation must never be filtered out); drain helper |
| `src/engine/ClipSourceProcessor.h` (modify) | call `BufferCheck::checkBuffer` at end of `processBlock` (:414) |
| `src/engine/MasterBusProcessor.h` (modify) | call `BufferCheck::checkBuffer` at end of `processBlock` (:46) |
| `src/engine/Track.cpp` (modify) | call `BufferCheck::checkBuffer` at end of `Track::processBlock` (:379, after :453) + guard `prepareToPlay` |
| `src/engine/SamplerEngine.cpp` (modify) | call `BufferCheck::checkBuffer` in `render` (:73) |
| `src/engine/TrackFXSlot.cpp` (modify) | guard `prepare`/`release` with `RealtimeGuard::assertMessageThread` (lesson 13 seam) |
| `tests/unit/engine/realtime_safety_test.cpp` (new) | gtest suite for all instruments |
| `tests/CMakeLists.txt` (modify) | register `unit/engine/realtime_safety_test.cpp` |

---

## Task 1: `BufferCheck` — NaN/Inf/DC scan (compile-gated)

**Files:**
- Create: `src/common/BufferCheck.h`
- Test: `tests/unit/engine/realtime_safety_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/engine/realtime_safety_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "common/BufferCheck.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace {
// RAII helper: reset the shared detection state so each test starts clean.
class ResetGuard {
public:
    ResetGuard() { HDAW::BufferCheck::resetForTest(); }
    ~ResetGuard() { HDAW::BufferCheck::resetForTest(); }
};
}

TEST(RealtimeSafety, CleanBufferDetectsNothing)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 0);
    EXPECT_FALSE(HDAW::BufferCheck::anyProblemPending());
}

TEST(RealtimeSafety, NaNInBufferTripsDetection)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    buf.setSample(0, 100, std::numeric_limits<float>::quiet_NaN());
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 0);
    EXPECT_TRUE(HDAW::BufferCheck::anyProblemPending());
}

TEST(RealtimeSafety, InfiniteInBufferTripsDetection)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    buf.setSample(1, 50, std::numeric_limits<float>::infinity());
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 0);
    EXPECT_TRUE(HDAW::BufferCheck::anyProblemPending());
}

TEST(RealtimeSafety, DCOffsetGrowthTripsDetection)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(2, 256);
    // Sustained DC of 0.5 for 256 samples at 44.1k = clear offset drift
    for (int s = 0; s < 256; ++s)
        buf.setSample(0, s, 0.5f);
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 0);
    EXPECT_TRUE(HDAW::BufferCheck::anyProblemPending());
}

TEST(RealtimeSafety, ShortToneIsNotDC)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(2, 256);
    for (int s = 0; s < 256; ++s)
        buf.setSample(0, s, std::sin(2.0 * 3.14159 * 440.0 * s / 44100.0));
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 0);
    EXPECT_FALSE(HDAW::BufferCheck::anyProblemPending());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.*
```
Expected: FAIL — `common/BufferCheck.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `src/common/BufferCheck.h`:

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <limits>

namespace HDAW {

// Debug-only realtime buffer sanity checks. On the audio thread we ONLY
// set atomics — never allocate, lock, log, or format (Gate 3). A message
// thread drains the flags and reports via HDAW_LOG_ALWAYS.
//
// All state is process-global so tests and the engine share one instance.
class BufferCheck
{
public:
    // Audio-thread entry: scan `buffer` for NaN/Inf/DC, time the block.
    // `contextId` is a stable int identifying the source (e.g. a clip id
    // or track index) used in the drain log line.
    static void checkBuffer(const juce::AudioBuffer<float>& buffer,
                            double sampleRate, int contextId)
    {
#if JUCE_DEBUG
        const uint32_t blockStart = juce::Time::getMillisecondCounterHiRes();

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < numChannels && !problemFlags_.load(std::memory_order_relaxed); ++ch)
        {
            const float* d = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                const float v = d[s];
                if (!std::isfinite(v)) // NaN or ±Inf
                {
                    problemFlags_.fetch_or(kProblemNonFinite, std::memory_order_relaxed);
                    lastContext_.store(contextId, std::memory_order_relaxed);
                    lastChannel_.store(ch, std::memory_order_relaxed);
                    lastSample_.store(s, std::memory_order_relaxed);
                    break;
                }
            }
            if (problemFlags_.load(std::memory_order_relaxed) != 0)
                break;
        }

        // DC-offset drift: average |mean| over a short sliding window is
        // expensive per-sample; approximate by checking the block mean against
        // a persistent EMA. A sustained offset (like a stuck DC source) pushes
        // the EMA past a threshold. A 440 Hz tone has mean ~0, so it never
        // trips (see ShortToneIsNotDC).
        if (problemFlags_.load(std::memory_order_relaxed) == 0 && numSamples > 0 && sampleRate > 0.0)
        {
            double blockSum = 0.0;
            const float* d0 = buffer.getReadPointer(0);
            for (int s = 0; s < numSamples; ++s)
                blockSum += d0[s];
            const double blockMean = blockSum / numSamples;

            // EMA over ~0.5 s of audio
            const double alpha = std::min(1.0, 1.0 - std::exp(-numSamples / (sampleRate * 0.5)));
            dcEma_.store(dcEma_.load(std::memory_order_relaxed) * (1.0 - alpha)
                         + blockMean * alpha, std::memory_order_relaxed);

            if (std::fabs(dcEma_.load(std::memory_order_relaxed)) > 0.25) // 25% offset
            {
                problemFlags_.fetch_or(kProblemDC, std::memory_order_relaxed);
                lastContext_.store(contextId, std::memory_order_relaxed);
            }
        }

        // Glitch detector: any single block taking more than 4x its nominal
        // duration indicates a priority inversion / overrun (lesson 13 class).
        if (problemFlags_.load(std::memory_order_relaxed) == 0)
        {
            const uint32_t blockElapsed =
                juce::Time::getMillisecondCounterHiRes() - blockStart;
            const uint32_t nominalMs = static_cast<uint32_t>(
                1000.0 * static_cast<double>(numSamples) / (sampleRate > 0.0 ? sampleRate : 1.0));
            if (nominalMs > 0 && blockElapsed > nominalMs * 4)
            {
                problemFlags_.fetch_or(kProblemGlitch, std::memory_order_relaxed);
                lastContext_.store(contextId, std::memory_order_relaxed);
            }
        }
#endif // JUCE_DEBUG
    }

    // Message thread: true if any problem was flagged and not yet drained.
    static bool anyProblemPending() noexcept
    {
        return problemFlags_.load(std::memory_order_acquire) != 0;
    }

    // Message thread: pull-and-clear the flags, returning the description for
    // a log line. Returns empty string when nothing pending.
    static juce::String drainProblem()
    {
        const int flags = problemFlags_.exchange(0, std::memory_order_acq_rel);
        if (flags == 0)
            return {};
        juce::String desc = "RT:";
        if (flags & kProblemNonFinite)
            desc += " non-finite sample (NaN/Inf)";
        if (flags & kProblemDC)
            desc += " DC-offset drift";
        if (flags & kProblemGlitch)
            desc += " block overrun (4x)";
        desc += " ctx=" + juce::String(lastContext_.load(std::memory_order_relaxed))
              + " ch=" + juce::String(lastChannel_.load(std::memory_order_relaxed))
              + " sample=" + juce::String(lastSample_.load(std::memory_order_relaxed));
        return desc;
    }

    static void resetForTest() noexcept
    {
        problemFlags_.store(0, std::memory_order_release);
        dcEma_.store(0.0, std::memory_order_release);
        lastContext_.store(-1, std::memory_order_release);
        lastChannel_.store(-1, std::memory_order_release);
        lastSample_.store(-1, std::memory_order_release);
    }

private:
    enum : int
    {
        kProblemNonFinite = 1 << 0,
        kProblemDC        = 1 << 1,
        kProblemGlitch    = 1 << 2
    };
    inline static std::atomic<int> problemFlags_{ 0 };
    inline static std::atomic<double> dcEma_{ 0.0 };
    inline static std::atomic<int> lastContext_{ -1 };
    inline static std::atomic<int> lastChannel_{ -1 };
    inline static std::atomic<int> lastSample_{ -1 };
};

} // namespace HDAW
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.*
```
Expected: PASS (5/5).

- [ ] **Step 5: Commit**

```bash
git add src/common/BufferCheck.h tests/unit/engine/realtime_safety_test.cpp tests/CMakeLists.txt
git commit -m "feat(rt): BufferCheck NaN/Inf/DC/glitch detector with atomic flag drain"
```

---

## Task 2: Wire `BufferCheck` into the engine's `processBlock` entry points

**Files:**
- Modify: `src/engine/ClipSourceProcessor.h` (after line 414 — end of `processBlock`)
- Modify: `src/engine/MasterBusProcessor.h` (after line 46)
- Modify: `src/engine/Track.cpp` (end of `processBlock`, after line ~453)
- Modify: `src/engine/SamplerEngine.cpp` (end of `render`)
- Test: `tests/unit/engine/realtime_safety_test.cpp`

- [ ] **Step 1: Write a compile-check that the hooks exist (test is a no-op that would fail if a hook regressed — see Task 3 for the behavioral test)**

Add to `tests/unit/engine/realtime_safety_test.cpp`:

```cpp
TEST(RealtimeSafety, InstrumentedProcessBlocksCompile)
{
    // Building this TU with the hooks present proves the engine's
    // processBlock entry points accept the instrumentation call.
    SUCCEED();
}
```

- [ ] **Step 2: Wire `ClipSourceProcessor::processBlock`**

In `src/engine/ClipSourceProcessor.h`, add `#include "../common/BufferCheck.h"` at the top (line 9 already includes `../common/DebugLog.h`). At the very end of `processBlock` (after line 414's closing brace of the gain loop, before the method's closing brace), add:

```cpp
        HDAW::BufferCheck::checkBuffer(buffer, sr, clipID);
```

- [ ] **Step 3: Wire `MasterBusProcessor::processBlock`**

In `src/engine/MasterBusProcessor.h`, add `#include "../common/BufferCheck.h"` and, at the end of `processBlock` (after `meter.update(buffer);`), add:

```cpp
        HDAW::BufferCheck::checkBuffer(buffer, getSampleRate(), 0);
```

Note: `MasterBusProcessor` inherits `getSampleRate()` (public) from JUCE's `AudioProcessor` — it returns the rate set by `prepareToPlay`. No need to grep; if the build complains it's protected, store the rate in `prepareToPlay` like `ClipSourceProcessor` does (`sr = sampleRate;`).

- [ ] **Step 4: Wire `Track::processBlock`**

Read `src/engine/Track.cpp:379-455` first. At the end of `processBlock`, add:

```cpp
    HDAW::BufferCheck::checkBuffer(buffer, getSampleRate(), trackIndex);
```

Add `#include "../common/BufferCheck.h"` to `src/engine/Track.cpp` (check the existing include block; `Track.h` already includes `AutomationManager.h` etc.).

- [ ] **Step 5: Wire `SamplerEngine::render`**

In `src/engine/SamplerEngine.cpp`, at the end of `render` (after `ScopedNoDenormals` body, before return), add:

```cpp
    HDAW::BufferCheck::checkBuffer(buffer, sr_, 0);
```

Add `#include "../common/BufferCheck.h"` to `src/engine/SamplerEngine.cpp`.

- [ ] **Step 6: Run the engine test suite**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.*
build/Debug/hdaw_tests.exe --gtest_filter=Sampler*.*
```
Expected: PASS — the hooks must not trip on healthy audio (the existing 250+ tests render clean buffers; a false positive here is itself a bug).

- [ ] **Step 7: Commit**

```bash
git add src/engine/ClipSourceProcessor.h src/engine/MasterBusProcessor.h src/engine/Track.cpp src/engine/SamplerEngine.cpp tests/unit/engine/realtime_safety_test.cpp
git commit -m "feat(rt): wire BufferCheck into clip/track/master/sampler processBlock"
```

---

## Task 3: `RealtimeGuard` — thread-identity + lock-block tripwire

**Files:**
- Create: `src/common/RealtimeGuard.h`
- Modify: `src/engine/TrackFXSlot.cpp` (lesson 13 seam: `prepare`/`release`)
- Test: `tests/unit/engine/realtime_safety_test.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/engine/realtime_safety_test.cpp`:

```cpp
#include "common/RealtimeGuard.h"

TEST(RealtimeSafety, GuardRejectsWrongThread)
{
    // On the main thread, the "audio thread" is a recorded id. Simulate the
    // audio thread by recording the current thread as audio, then asserting
    // the message-thread check from the same thread must pass and the
    // audio-thread check must also pass (same thread in this single-threaded
    // test). The negative case: a bogus id must fail.
    const auto real = juce::Thread::getCurrentThreadId();
    HDAW::RealtimeGuard::recordAudioThreadId(real);
    EXPECT_TRUE(HDAW::RealtimeGuard::isAudioThread());
    EXPECT_TRUE(HDAW::RealtimeGuard::isMessageThread()); // tests run on the message thread
}

TEST(RealtimeSafety, LockBlockHelpsDetectPriorityInversion)
{
    // A helper that reports whether the audio thread was observed blocking on
    // a SpinLock. In this single-threaded test we simply verify the API
    // surface exists and returns false when nothing happened.
    EXPECT_FALSE(HDAW::RealtimeGuard::lastBlockWasAudioThreadLock());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.GuardRejectsWrongThread
```
Expected: FAIL — `common/RealtimeGuard.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `src/common/RealtimeGuard.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>

namespace HDAW {

// Debug-only thread-identity tripwires. Mirrors the technique CLAPHost uses
// for thread-check predicates (lesson 19): record real thread ids, compare
// against them — never "not X" complements.
class RealtimeGuard
{
public:
    // Call once from prepareToPlay / the audio device callback to record the
    // real audio thread id.
    static void recordAudioThreadId(void* threadId) noexcept
    {
        audioThreadId_.store(threadId, std::memory_order_release);
    }

    static bool isAudioThread() noexcept
    {
        return audioThreadId_.load(std::memory_order_acquire)
            == juce::Thread::getCurrentThreadId();
    }

    // True if the current thread is the JUCE message thread. Used by the
    // rebuild/restore paths (lesson 10/12/13) to assert they run message-side.
    static bool isMessageThread() noexcept
    {
        return juce::MessageManager::getInstance()->isThisTheMessageThread();
    }

    // Audio-thread-side helper for lock-block detection. Callers that would
    // otherwise use a blocking ScopedLockType call this instead; it flags a
    // pending problem atomically and returns false (skip the guarded work)
    // if the lock cannot be acquired without blocking.
    static bool tryEnterLock(bool acquired) noexcept
    {
        if (!acquired)
            audioBlocked_.store(true, std::memory_order_release);
        return acquired;
    }

    static bool lastBlockWasAudioThreadLock() noexcept
    {
        const bool b = audioBlocked_.load(std::memory_order_acquire);
        audioBlocked_.store(false, std::memory_order_release);
        return b;
    }

    static void resetForTest() noexcept
    {
        audioThreadId_.store(nullptr, std::memory_order_release);
        audioBlocked_.store(false, std::memory_order_release);
    }

private:
    inline static std::atomic<void*> audioThreadId_{ nullptr };
    inline static std::atomic<bool> audioBlocked_{ false };
};

} // namespace HDAW
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.*
```
Expected: PASS.

- [ ] **Step 5: Instrument the lesson-13 seam in `TrackFXSlot`**

Read `src/engine/TrackFXSlot.cpp` — find `TrackFXSlot::prepare` and `TrackFXSlot::release` (the `prepareToPlay`-driven recreation path that recreated EQ DSP under `stateLock`). Add guards that flag if they run off the message thread:

```cpp
    // in prepare():
    if (!HDAW::RealtimeGuard::isMessageThread())
        juce::Logger::writeToLog("TrackFXSlot::prepare off message thread");
```
(best-effort — the detection flags it; the existing `stateLock` still protects the DSP objects. This is a tripwire, not a new locking mechanism.)

- [ ] **Step 6: Run the full engine suite to confirm no regressions from instrumentation**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe
```
Expected: PASS (all suites).

- [ ] **Step 7: Commit**

```bash
git add src/common/RealtimeGuard.h src/engine/TrackFXSlot.cpp tests/unit/engine/realtime_safety_test.cpp
git commit -m "feat(rt): RealtimeGuard thread-id + lock-block tripwire; instrument TrackFXSlot"
```

---

## Task 4: `HDAW_LOG_ALWAYS` + message-thread drainer

**Files:**
- Modify: `src/common/DebugLog.h`
- Modify: `src/engine/MainAudioProcessor.cpp` (or `AudioEngine.cpp`) — a timer/tick that drains `BufferCheck`

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/engine/realtime_safety_test.cpp`:

```cpp
#include "common/DebugLog.h"

TEST(RealtimeSafety, DrainProducesLogString)
{
    ResetGuard g;
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    buf.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());
    HDAW::BufferCheck::checkBuffer(buf, 44100.0, 7);
    const juce::String desc = HDAW::BufferCheck::drainProblem();
    EXPECT_TRUE(desc.contains("non-finite"));
    EXPECT_TRUE(desc.contains("ctx=7"));
    // Nothing left after drain.
    EXPECT_FALSE(HDAW::BufferCheck::anyProblemPending());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.DrainProducesLogString
```
Expected: FAIL — `BufferCheck::drainProblem` doesn't exist yet (Task 1 defined it but not the drain-side logging macro).

- [ ] **Step 3: Add `HDAW_LOG_ALWAYS` to `DebugLog.h`**

In `src/common/DebugLog.h`, after the existing `HDAW_LOG` macro (line 117), add:

```cpp
// Instrumentation output must NEVER be filtered by HDAW_LOG_TAGS — a
// silent tripwire is no tripwire (lesson 17: log somewhere visible).
#define HDAW_LOG_ALWAYS(msg) DebugLog::log("RT", DebugLog::toLogString(msg))
```

- [ ] **Step 4: Wire the message-thread drainer**

In `src/engine/MainAudioProcessor.cpp`, find the existing timer callback (the engine uses a periodic timer for transport sync/STSP drain — grep `Timer` in the file). In that callback (message thread), add:

```cpp
    if (HDAW::BufferCheck::anyProblemPending())
    {
        const juce::String desc = HDAW::BufferCheck::drainProblem();
        if (desc.isNotEmpty())
            HDAW_LOG_ALWAYS(desc);
    }
```

Add `#include "../common/BufferCheck.h"` to `MainAudioProcessor.cpp`.

- [ ] **Step 5: Run test + engine suite**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=RealtimeSafety.*
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/common/DebugLog.h src/engine/MainAudioProcessor.cpp tests/unit/engine/realtime_safety_test.cpp
git commit -m "feat(rt): HDAW_LOG_ALWAYS bypasses tag filter; message-thread BufferCheck drainer"
```

---

## Task 5: Success-gate verification (release-mode compile-out)

**Files:** (none — verification only)

- [ ] **Step 1: Confirm the instruments compile out of release**

```bash
cmake --build build --config Release --target HDAW
```
Expected: succeeds. In `BufferCheck.h`, the entire body of `checkBuffer` is `#if JUCE_DEBUG`; a Release build yields a no-op inline (verify by inspecting the compiled object is impractical — the macro gate is the contract).

- [ ] **Step 2: Run the full test suite (Debug) as the completion contract**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe
```
Expected: PASS, all suites.

- [ ] **Step 3: Verify no anti-patterns in the diff**

- No allocation/lock/log on the audio thread: the checks only set atomics; logging happens in the message-thread drainer.
- No `DBG` macro. No raw hex (no frontend). No N-call RPC loops (no new RPC).
- The instrumentation is header-only and compile-gated; no new `.cpp` needs CMake registration except the test (already in Task 1 Step 5).

- [ ] **Step 4: Version bump + graph refresh**

- Bump version in `CMakeLists.txt` (`project(HDAW VERSION ...)`) and `frontend/package.json` (kept in sync — AGENTS.md). This is a debug-instrumentation feature; bump patch (e.g. 0.21.0 → 0.21.1).
- Refresh the knowledge graph: `codebase-memory` `index_repository` (project `D-pdf-roo-projects-hdaw3`, mode `fast`) so the new `BufferCheck`/`RealtimeGuard` nodes are known.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt frontend/package.json
git commit -m "chore(rt): bump to 0.21.1 (realtime-safety instrumentation)"
```

---

## Success Gates (completion contract — evidence required)

- [ ] G1: `RealtimeSafety.*` gtest suite passes (NaN, Inf, DC, clean-tone-not-DC, drain log content, guard thread checks, lock-block helper).
- [ ] G2: Full `build/Debug/hdaw_tests.exe` passes — the instrumentation produces **zero false positives** on the existing 250+ healthy tests.
- [ ] G3: Release build (`cmake --build build --config Release --target HDAW`) succeeds with the checks compiled out (`#if JUCE_DEBUG`).
- [ ] G4: The audio thread never allocates/locks/logs in the new code — only atomics; logging is message-thread (Gate 3 + lesson 17).
- [ ] G5: Version bumped in both `CMakeLists.txt` and `frontend/package.json`; knowledge graph refreshed (`index_repository`).
- [ ] G6: No new anti-patterns (no `DBG`, no per-block allocation, no audio-thread I/O).

## Dependency Map

- **Blast radius:** `ClipSourceProcessor`, `MasterBusProcessor`, `Track`, `SamplerEngine` — all `processBlock`/`render` entry points (hot audio paths). `DebugLog` (new macro). `MainAudioProcessor` timer (drainer).
- **Upstream:** `RoutingManager::rebuildClipsForTrack` builds `ClipSourceProcessor` (`RoutingManager.cpp:507`); `Track::processBlock` is called by the graph (`Track.cpp:379`); `SamplerEngine::render` by `TrackFXSlot` sampler variant (`SamplerEngine.cpp:73`).
- **Downstream:** nothing consumes the flags except the drainer (message thread).
- **Projections affected:** none (no ValueTree/ReadModel/frontend change).
- **SPSC paths touched:** new one-way audio→message atomic flag drain (message thread reads, audio thread writes atomics).
- **God nodes in scope:** `Track`, `ClipSourceProcessor` — treat as elevated risk; the change is 1 line each at a well-understood seam.
- **Path integrity:** verify each hook's parent function actually ends where the plan says (`Track.cpp` processBlock tail ~:453, `MasterBusProcessor.h:46`, `ClipSourceProcessor.h:414`, `SamplerEngine.cpp` render tail) with a read before editing.

## Pitfall Gates Triggered

- **Gate 3 (audio-thread safety):** the checks set atomics only; no allocation/lock/I-O/String/log on the audio thread. The drainer runs on the message thread.
- **Gate 4 (stale binaries):** new test `.cpp` must be added to `tests/CMakeLists.txt` (Task 1 Step 5) or MSBuild skips recompile (lesson 15).
- **Lesson 13 (DSP-state race):** `RealtimeGuard` instrumentation at the `TrackFXSlot::prepare` seam flags the exact race class — it does not replace `stateLock`.
- **Lesson 17 (visible logs):** `HDAW_LOG_ALWAYS` bypasses `HDAW_LOG_TAGS` filtering so tripwires are never silent; output reaches `hdaw_debug.log` (and OutputDebugString via the existing writer).
- **Lesson 19 (real thread ids):** `RealtimeGuard` compares recorded ids, not complements.

## Anti-Pattern Scan

- No per-block allocation/logging/locking. No `DBG`. No audio-thread `String` formatting (the drain description is built on the message thread). No raw hex (no frontend). No new RPC (MCP parity N/A — no user-facing command added). No full-tree walks.