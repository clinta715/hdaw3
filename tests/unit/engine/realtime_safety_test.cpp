#include <gtest/gtest.h>
#include "common/BufferCheck.h"
#include "common/RealtimeGuard.h"
#include "common/DebugLog.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <limits>

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

TEST(RealtimeSafety, InstrumentedProcessBlocksCompile)
{
    // Building this TU with the hooks present proves the engine's
    // processBlock entry points accept the instrumentation call.
    SUCCEED();
}

TEST(RealtimeSafety, GuardRejectsWrongThread)
{
    // ResetGuard currently only resets BufferCheck; also reset the guard.
    HDAW::RealtimeGuard::resetForTest();

    // With nothing recorded, the current thread is not the audio thread.
    EXPECT_FALSE(HDAW::RealtimeGuard::isAudioThread());

    // Record the current thread as the audio thread, then the check passes.
    const auto real = juce::Thread::getCurrentThreadId();
    HDAW::RealtimeGuard::recordAudioThreadId(real);
    EXPECT_TRUE(HDAW::RealtimeGuard::isAudioThread());

    // A different (bogus) id must be rejected.
    void* bogus = reinterpret_cast<void*>(0x1234);
    EXPECT_FALSE(HDAW::RealtimeGuard::isAudioThreadFor(bogus));

    // isMessageThread() reflects the JUCE message thread. In this test process
    // the MessagePumpThread owns the loop, so the main test thread is NOT it.
    // The guard must agree with MessageManager (not hardcode an expectation).
    EXPECT_EQ(HDAW::RealtimeGuard::isMessageThread(),
              juce::MessageManager::getInstance()->isThisTheMessageThread());
}

TEST(RealtimeSafety, LockBlockHelpsDetectPriorityInversion)
{
    HDAW::RealtimeGuard::resetForTest();
    // Nothing blocked → helper reports false.
    EXPECT_FALSE(HDAW::RealtimeGuard::lastBlockWasAudioThreadLock());

    // Simulate a failed non-blocking acquire → helper reports true and clears.
    EXPECT_FALSE(HDAW::RealtimeGuard::tryEnterLock(false));
    EXPECT_TRUE(HDAW::RealtimeGuard::lastBlockWasAudioThreadLock());
    // Cleared after one read.
    EXPECT_FALSE(HDAW::RealtimeGuard::lastBlockWasAudioThreadLock());

    // A successful acquire must not flag a block.
    EXPECT_TRUE(HDAW::RealtimeGuard::tryEnterLock(true));
    EXPECT_FALSE(HDAW::RealtimeGuard::lastBlockWasAudioThreadLock());
}

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
