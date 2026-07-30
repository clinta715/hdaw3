#include <gtest/gtest.h>
#include "engine/LevelMeter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

TEST(LevelMeter, PeakReportsMagnitude)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0, 512);

    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    for (int s = 0; s < 512; ++s)
        buf.setSample(0, s, 0.5f);
    for (int s = 0; s < 512; ++s)
        buf.setSample(1, s, 0.25f);

    meter.update(buf);

    EXPECT_FLOAT_EQ(meter.getLeftLevel(), 0.5f);
    EXPECT_FLOAT_EQ(meter.getRightLevel(), 0.25f);
}

TEST(LevelMeter, RmsConvergesToSignalLevel)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0, 512);

    juce::AudioBuffer<float> buf(2, 512);
    for (int s = 0; s < 512; ++s) {
        buf.setSample(0, s, 0.5f);
        buf.setSample(1, s, 0.5f);
    }

    for (int i = 0; i < 200; ++i)
        meter.update(buf);

    EXPECT_NEAR(meter.getRmsLeft(), 0.5f, 0.01f);
    EXPECT_NEAR(meter.getRmsRight(), 0.5f, 0.01f);
}

TEST(LevelMeter, RmsIsLowerThanPeakForNonConstantSignal)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0, 512);

    juce::AudioBuffer<float> buf(1, 512);
    for (int s = 0; s < 512; ++s)
        buf.setSample(0, s, std::sin(static_cast<float>(s) * 0.1f));

    for (int i = 0; i < 200; ++i)
        meter.update(buf);

    EXPECT_NEAR(meter.getLeftLevel(), 1.0f, 0.01f);
    EXPECT_NEAR(meter.getRmsLeft(), 0.707f, 0.05f);
}

TEST(LevelMeter, LufsReturnsValueAfterProcessing)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0, 512);

    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    meter.update(buf);
    EXPECT_FLOAT_EQ(meter.getLufsMomentary(), -70.0f);

    // Use a sine wave — K-weighting high-pass removes DC, so constant signal
    // would be filtered to near-zero. A 1 kHz sine at 0.8 amplitude should
    // give a meaningful LUFS reading.
    for (int s = 0; s < 512; ++s) {
        float val = 0.8f * std::sin(static_cast<float>(s) * 2.0f * 3.14159265f * 1000.0f / 44100.0f);
        buf.setSample(0, s, val);
        buf.setSample(1, s, val);
    }
    for (int i = 0; i < 500; ++i)
        meter.update(buf);

    float lufs = meter.getLufsMomentary();
    EXPECT_GT(lufs, -70.0f);
    // K-weighted 1kHz sine at 0.8 amplitude should be roughly -3 to -8 LUFS
    EXPECT_GT(lufs, -20.0f);
}

TEST(LevelMeter, PrepareInitializesCorrectly)
{
    HDAW::LevelMeter meter;
    EXPECT_FLOAT_EQ(meter.getLufsMomentary(), -70.0f);
    EXPECT_FLOAT_EQ(meter.getRmsLeft(), 0.0f);

    meter.prepare(48000.0, 512);
    EXPECT_FLOAT_EQ(meter.getLufsMomentary(), -70.0f);
    EXPECT_FLOAT_EQ(meter.getRmsLeft(), 0.0f);
}

TEST(LevelMeter, MonoChannelRmsMatchesLeft)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0, 512);

    juce::AudioBuffer<float> buf(1, 512);
    for (int s = 0; s < 512; ++s)
        buf.setSample(0, s, 0.6f);

    for (int i = 0; i < 200; ++i)
        meter.update(buf);

    EXPECT_NEAR(meter.getRmsLeft(), 0.6f, 0.01f);
    EXPECT_NEAR(meter.getRmsRight(), 0.6f, 0.01f);
    EXPECT_FLOAT_EQ(meter.getLeftLevel(), 0.6f);
    EXPECT_FLOAT_EQ(meter.getRightLevel(), 0.6f);
}
