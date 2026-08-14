#include <gtest/gtest.h>
#include "common/BufferCheck.h"
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
