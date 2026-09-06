#include <gtest/gtest.h>
#include "engine/LoopAnalyzer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace {

constexpr double kSampleRate = 44100.0;

// 4-on-floor percussion loop. Kicks are short (40 ms) exponentially-decaying
// ~55 Hz bursts at every beat, `bars` bars long at `bpm`. `leadingSilence`
// prepends silence before the first beat; `tailSeconds` appends a decaying
// reverb-style tail (same amplitude as a kick, abrupt attack) after the loop.
void makePercussionLoop(juce::AudioBuffer<float>& mono, double sampleRate,
                        int bars, double bpm, double leadingSilence = 0.0,
                        double tailSeconds = 0.0)
{
    const double beat = 60.0 / bpm;
    const double loopLen = bars * 4.0 * beat;
    const double total = leadingSilence + loopLen + tailSeconds;
    const int n = static_cast<int>(total * sampleRate);
    mono.setSize(1, n, false, false, true);
    mono.clear();
    const double amp = 0.5;
    const double freq = 55.0;
    const double burstLen = 0.04;
    const double decay = 0.012;
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t0 = leadingSilence + b * beat;
        const int start = static_cast<int>(t0 * sampleRate);
        const int burstSamples = static_cast<int>(burstLen * sampleRate);
        for (int i = 0; i < burstSamples && start + i < n; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            mono.setSample(0, start + i, static_cast<float>(
                amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)
                    * std::exp(-t / decay)));
        }
    }
    if (tailSeconds > 0.0)
    {
        const int tailStart = static_cast<int>((leadingSilence + loopLen) * sampleRate);
        const int tailSamples = static_cast<int>(tailSeconds * sampleRate);
        for (int i = 0; i < tailSamples && tailStart + i < n; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            mono.setSample(0, tailStart + i, static_cast<float>(
                amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)
                    * std::exp(-t / (tailSeconds * 0.25))));
        }
    }
}

// Sustained sine-note lead: one note per beat (0.75 * beat long) across
// `bars` bars at `bpm`. Pitches step up a scale so consecutive notes differ.
void makeSynthLead(juce::AudioBuffer<float>& mono, double sampleRate, int bars, double bpm)
{
    const double beat = 60.0 / bpm;
    const double loopLen = bars * 4.0 * beat;
    const int n = static_cast<int>(loopLen * sampleRate);
    mono.setSize(1, n, false, false, true);
    mono.clear();
    const double pitches[] = { 440.0, 494.0, 523.25, 587.33, 659.25, 698.46, 783.99, 880.0 };
    const double noteDur = beat * 0.75;
    const double amp = 0.45;
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t0 = b * beat;
        const int start = static_cast<int>(t0 * sampleRate);
        const int lenSamples = static_cast<int>(noteDur * sampleRate);
        const double freq = pitches[b % 8];
        for (int i = 0; i < lenSamples && start + i < n; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            mono.setSample(0, start + i, static_cast<float>(
                amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)));
        }
    }
}

// A single 0.3 s decaying burst followed by ~2 s of silence.
void makeOneShot(juce::AudioBuffer<float>& mono, double sampleRate)
{
    const int n = static_cast<int>(2.3 * sampleRate);
    mono.setSize(1, n, false, false, true);
    mono.clear();
    const double amp = 0.5;
    const double freq = 220.0;
    const double burstLen = 0.3;
    const double decay = 0.05;
    const int burstSamples = static_cast<int>(burstLen * sampleRate);
    for (int i = 0; i < burstSamples; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        mono.setSample(0, i, static_cast<float>(
            amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)
                * std::exp(-t / decay)));
    }
}

} // namespace

TEST(LoopAnalyzer, PercussionLoop120Bpm4Bars)
{
    juce::AudioBuffer<float> mono;
    makePercussionLoop(mono, kSampleRate, /*bars=*/4, /*bpm=*/120.0);
    auto r = HDAW::LoopAnalyzer::analyzeBuffer(mono, kSampleRate);
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.bpm, 120.0, 3.6);       // ±3%
    EXPECT_EQ(r.bars, 4);
    EXPECT_NEAR(r.downbeatOffset, 0.0, 0.1);
    EXPECT_GT(r.confidence, 0.5);
}

TEST(LoopAnalyzer, SynthLead100Bpm2Bars)
{
    juce::AudioBuffer<float> mono;
    makeSynthLead(mono, kSampleRate, /*bars=*/2, /*bpm=*/100.0);
    auto r = HDAW::LoopAnalyzer::analyzeBuffer(mono, kSampleRate);
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.bpm, 100.0, 3.0);       // ±3%
    EXPECT_EQ(r.bars, 2);
}

TEST(LoopAnalyzer, LeadingSilenceShiftsDownbeat)
{
    juce::AudioBuffer<float> mono;
    makePercussionLoop(mono, kSampleRate, /*bars=*/4, /*bpm=*/120.0, /*leadingSilence=*/0.25);
    auto r = HDAW::LoopAnalyzer::analyzeBuffer(mono, kSampleRate);
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.downbeatOffset, 0.25, 0.1);
}

TEST(LoopAnalyzer, TrailingReverbTailTrimmed)
{
    juce::AudioBuffer<float> mono;
    makePercussionLoop(mono, kSampleRate, /*bars=*/4, /*bpm=*/120.0,
                       /*leadingSilence=*/0.0, /*tailSeconds=*/0.5);
    auto r = HDAW::LoopAnalyzer::analyzeBuffer(mono, kSampleRate);
    ASSERT_TRUE(r.ok);
    EXPECT_LT(r.trailingSlack, 0.3);
    EXPECT_NEAR(r.loopSpanSourceSeconds, 8.0, 0.5);
    EXPECT_EQ(r.bars, 4);
}

TEST(LoopAnalyzer, OneShotRejected)
{
    juce::AudioBuffer<float> mono;
    makeOneShot(mono, kSampleRate);
    auto r = HDAW::LoopAnalyzer::analyzeBuffer(mono, kSampleRate);
    EXPECT_FALSE(r.ok);
}