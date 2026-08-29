#include <gtest/gtest.h>
#include "engine/MixReport.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <functional>
#include <vector>

namespace {

// Write a deterministic mono WAV to a temp file. `f(x)` receives absolute
// sample index and returns the sample value. Returns the temp file.
juce::File writeSynthWav(int lengthSamples,
                         double sampleRate,
                         const std::function<float(int64_t)>& sampleAt)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_mix_report_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
    f.deleteFile();
    {
        std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sampleRate, 1, 24, {}, 0));
        if (writer == nullptr) return {};
        out.release();
        juce::AudioBuffer<float> buf(1, lengthSamples);
        for (int64_t i = 0; i < lengthSamples; ++i)
            buf.setSample(0, static_cast<int>(i), sampleAt(i));
        writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    }
    return f;
}

// t in seconds; phase-continuous sine.
double sine(double t, double freq, double amp)
{
    return amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t);
}

// 8 s @ 48 kHz mono (4 s per section so each section holds 8 beats at the
// 120 bpm used by the pump tests — the pump-depth contract requires
// sections with >= 8 beats):
//   section A [0,4): 60 Hz @ 0.5 + 440 Hz @ 0.3
//   section B [4,8): 440 Hz @ 0.2 + 8000 Hz @ 0.3
// All frequencies have an integer number of cycles per section, so the
// per-section RMS is exact: A = sqrt(0.17) ~ 0.4123, B = sqrt(0.065) ~ 0.2549.
juce::File writeTwoSectionWav()
{
    constexpr double sr = 48000.0;
    constexpr int len = static_cast<int>(sr * 8.0);
    return writeSynthWav(len, sr, [](int64_t i) {
        const double t = static_cast<double>(i) / 48000.0;
        float v = 0.0f;
        if (t < 4.0)
            v = static_cast<float>(sine(t, 60.0, 0.5) + sine(t, 440.0, 0.3));
        else
            v = static_cast<float>(sine(t, 440.0, 0.2) + sine(t, 8000.0, 0.3));
        return v;
    });
}

// 4 s @ 48 kHz mono, one section [0,4): 8 beats of 0.5 s (120 bpm) of 440 Hz,
// amplitude 0.9 on even beats, 0.1 on odd beats -> strong pump.
juce::File writePumpedWav()
{
    constexpr double sr = 48000.0;
    constexpr int len = static_cast<int>(sr * 4.0);
    return writeSynthWav(len, sr, [](int64_t i) {
        const double t = static_cast<double>(i) / 48000.0;
        const int beat = static_cast<int>(t / 0.5);
        const double amp = (beat % 2 == 0) ? 0.9 : 0.1;
        return static_cast<float>(sine(t, 440.0, amp));
    });
}

} // namespace

// Gate 2: analyze() is a pure file reader + FFT — no engine, no audio thread.
TEST(MixReportTest, SynthesizedWavBandsRmsPumpKick)
{
    const juce::File f = writeTwoSectionWav();
    ASSERT_TRUE(f.existsAsFile());
    HDAW::MixReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"A", 0.0, 4.0},
        HDAW::SectionWindow{"B", 4.0, 8.0}}, 120.0, rep, err)) << err.toStdString();

    // Duration / rate
    EXPECT_NEAR(rep.duration, 8.0, 1e-6);
    EXPECT_NEAR(rep.sampleRate, 48000.0, 1.0);

    // Whole-file RMS/peak
    EXPECT_NEAR(rep.rms, std::sqrt(0.1175), 0.01);          // 0.3428
    EXPECT_GT(rep.peak, 0.79);                              // sines in phase at t=0
    EXPECT_LE(rep.peak, 0.81);

    // Two sections with the expected RMS arc.
    ASSERT_EQ(rep.sections.size(), 2u);
    EXPECT_EQ(rep.sections[0].name, "A");
    EXPECT_EQ(rep.sections[1].name, "B");
    EXPECT_NEAR(rep.sections[0].rms, std::sqrt(0.17), 0.01);    // 0.4123
    EXPECT_NEAR(rep.sections[1].rms, std::sqrt(0.065), 0.01);   // 0.2549
    EXPECT_GT(rep.sections[0].peak, rep.sections[1].peak);

    // Band energies: the 60 Hz sine dominates the sub band in A; B has no sub
    // content, so its sub energy is far lower; the 8 kHz sine dominates high.
    EXPECT_GT(rep.sections[0].bandEnergy[HDAW::kMixBandSub], rep.sections[0].bandEnergy[HDAW::kMixBandBass]);
    EXPECT_GT(rep.sections[0].bandEnergy[HDAW::kMixBandSub], rep.sections[0].bandEnergy[HDAW::kMixBandBody]);
    EXPECT_GT(rep.sections[0].bandEnergy[HDAW::kMixBandSub], rep.sections[1].bandEnergy[HDAW::kMixBandSub] * 10.0);
    EXPECT_GT(rep.sections[1].bandEnergy[HDAW::kMixBandHigh], rep.sections[1].bandEnergy[HDAW::kMixBandBody]);
    EXPECT_GT(rep.bands[HDAW::kMixBandSub], rep.bands[HDAW::kMixBandBass]);

    // Kick prominence: strong 60 Hz (35-110) against a silent 120-320 region.
    EXPECT_GT(rep.kickProminence, 0.9);
    EXPECT_LE(rep.kickProminence, 1.0);

    // Pump: constant material -> per-beat RMS arc ~ flat.
    EXPECT_TRUE(rep.hasPumpDepth);
    EXPECT_LT(rep.pumpDepth, 0.05);

    f.deleteFile();
}

TEST(MixReportTest, PumpedSignalYieldsHighPumpDepth)
{
    const juce::File f = writePumpedWav();
    ASSERT_TRUE(f.existsAsFile());
    HDAW::MixReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"pump", 0.0, 4.0}}, 120.0, rep, err)) << err.toStdString();

    ASSERT_TRUE(rep.hasPumpDepth);
    // Per-beat RMS: 0.9/sqrt(2) vs 0.1/sqrt(2) -> (0.9-0.1)/0.5 = 1.6.
    EXPECT_NEAR(rep.pumpDepth, 1.6, 0.3);
    EXPECT_GT(rep.pumpDepth, 1.0);

    // The square-wave beat envelope produces broadband click energy at each
    // 0.9->0.1 step, so the ratio stays in [0, 1] but is not near 0; the
    // dominance assertion for a sub-rich signal lives in the fixture above.
    EXPECT_GE(rep.kickProminence, 0.0);
    EXPECT_LE(rep.kickProminence, 1.0);

    // A single accelerating half (bpm=240 -> 0.25 s beats) still qualifies
    // (16 beats) with the same depth.
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"pump", 0.0, 4.0}}, 240.0, rep, err)) << err.toStdString();
    EXPECT_TRUE(rep.hasPumpDepth);
    EXPECT_NEAR(rep.pumpDepth, 1.6, 0.3);

    f.deleteFile();
}

TEST(MixReportTest, NoBpmOmitsPumpDepth)
{
    const juce::File f = writeTwoSectionWav();
    HDAW::MixReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {}, 0.0, rep, err)) << err.toStdString();
    EXPECT_FALSE(rep.hasPumpDepth);
    // Empty windows -> single "whole" section covering the file.
    ASSERT_EQ(rep.sections.size(), 1u);
    EXPECT_EQ(rep.sections[0].name, "whole");
    EXPECT_NEAR(rep.sections[0].start, 0.0, 1e-6);
    EXPECT_NEAR(rep.sections[0].end, rep.duration, 1e-6);
    f.deleteFile();
}

TEST(MixReportTest, MissingFileErrors)
{
    HDAW::MixReport rep;
    juce::String err;
    EXPECT_FALSE(HDAW::MixReportAnalyzer::analyze(
        juce::File("C:/definitely/missing/render.wav"), {}, 120.0, rep, err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(MixReportTest, DegenerateAndOutOfFileSectionsError)
{
    const juce::File f = writeTwoSectionWav();
    HDAW::MixReport rep;
    juce::String err;

    // end <= start
    EXPECT_FALSE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"bad", 2.0, 1.0}}, 120.0, rep, err));
    EXPECT_TRUE(err.contains("end <= start"));

    // window beyond file duration
    EXPECT_FALSE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"late", 0.0, 99.0}}, 120.0, rep, err));
    EXPECT_TRUE(err.contains("outside file duration"));

    f.deleteFile();
}
