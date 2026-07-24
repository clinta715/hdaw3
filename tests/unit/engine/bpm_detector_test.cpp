#include <gtest/gtest.h>
#include "engine/BpmDetector.h"
#include <cmath>
#include <vector>

namespace
{

std::vector<float> makeClickTrain(double bpm, double seconds, double sampleRate)
{
    int n = static_cast<int>(seconds * sampleRate);
    std::vector<float> buf(n, 0.0f);
    double interval = 60.0 / bpm;
    double t = 0.0;
    while (t < seconds)
    {
        int idx = static_cast<int>(t * sampleRate);
        if (idx < n)
            buf[idx] = 1.0f;
        t += interval;
    }
    return buf;
}

TEST(BpmDetectorTest, DetectsClickTrain120Bpm)
{
    auto buf = makeClickTrain(120.0, 5.0, 44100.0);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()), 44100.0, 5.0);
    EXPECT_GE(result.bpm, 118.0);
    EXPECT_LE(result.bpm, 122.0);
    EXPECT_GT(result.confidence, 0.01);
}

TEST(BpmDetectorTest, DetectsClickTrain90Bpm)
{
    auto buf = makeClickTrain(90.0, 5.0, 44100.0);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()), 44100.0, 5.0);
    EXPECT_GE(result.bpm, 88.0);
    EXPECT_LE(result.bpm, 92.0);
}

TEST(BpmDetectorTest, ReturnsZeroForSilence)
{
    std::vector<float> buf(44100 * 3, 0.0f);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()), 44100.0, 3.0);
    EXPECT_DOUBLE_EQ(result.bpm, 0.0);
}

TEST(BpmDetectorTest, ReturnsZeroForNullInput)
{
    auto result = HDAW::BpmDetector::detect(nullptr, 0, 44100.0);
    EXPECT_DOUBLE_EQ(result.bpm, 0.0);
}

TEST(BpmDetectorTest, RespectsMaxSecondsLimit)
{
    auto buf = makeClickTrain(120.0, 10.0, 44100.0);
    auto result = HDAW::BpmDetector::detect(buf.data(), static_cast<int>(buf.size()), 44100.0, 2.0);
    EXPECT_GE(result.bpm, 115.0);
    EXPECT_LE(result.bpm, 125.0);
}

}
