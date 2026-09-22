// ToneVerity analyzer tests — synthesized WAVs, no engine (Phase 2 gates H2).
// Contract: docs/plans/2026-09-22-param-verity-pipeline.md + ToneVerity.h.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "common/ToneVerity.h"

#include <cmath>
#include <functional>

namespace {

constexpr double kSr = 48000.0;

juce::File writeWav(int lengthSamples,
                    const std::function<float(int64_t)>& sampleAt)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_tone_verity_"
                                     + juce::String(juce::Random::getSystemRandom().nextInt())
                                     + ".wav");
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out.get(), kSr, 1, 24, {}, 0));
    if (writer == nullptr) return {};
    out.release();
    juce::AudioBuffer<float> buf(1, lengthSamples);
    for (int64_t i = 0; i < lengthSamples; ++i)
        buf.setSample(0, (int) i, sampleAt(i));
    writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    return f;
}

double sine(double t, double freq, double amp)
{
    return amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t);
}

} // namespace

TEST(ToneVerityAnalyzer, Sine220PitchEnvelopeAndSustain)
{
    // 2 s of steady 220 Hz — the classic tuning reference (A3).
    const int len = (int) (kSr * 2.0);
    juce::File f = writeWav(len, [](int64_t i) {
        return (float) sine((double) i / kSr, 220.0, 0.5);
    });
    ASSERT_TRUE(f.existsAsFile());

    const auto a = HDAW::analyzeToneWav(f, 0.01);
    ASSERT_TRUE(a.ok) << a.error;

    // Pitch: HPS must land within 1 Hz of 220 and resolve to MIDI 57 (A3).
    ASSERT_FALSE(std::isnan(a.f0Hz));
    EXPECT_NEAR(a.f0Hz, 220.0, 1.0);
    EXPECT_EQ(a.f0Midi, 57);
    EXPECT_GT(a.f0Confidence, 1.0);

    // Envelope: the steady tone reaches full level within one bin or two and
    // holds to the end.
    EXPECT_GT(a.samplePeak, 1e-4);
    EXPECT_GE(a.attackMs, 0.0);
    EXPECT_LT(a.attackMs, 60.0);
    ASSERT_FALSE(std::isnan(a.sustainRatio));
    EXPECT_GT(a.sustainRatio, 0.8);
    EXPECT_LT(a.trailingSilenceSeconds, 0.05);

    // No amplitude modulation in a steady tone: central-80% depth ~ 0.
    EXPECT_LT(a.amDepth, 0.1);

    f.deleteFile();
}

TEST(ToneVerityAnalyzer, AmplitudeModulationRateIsDetected)
{
    // 3 Hz tremolo on a 440 Hz carrier over 4 s (12 AM cycles).
    const int len = (int) (kSr * 4.0);
    juce::File f = writeWav(len, [](int64_t i) {
        const double t = (double) i / kSr;
        const double amp = 0.5 + 0.4 * std::sin(2.0 * juce::MathConstants<double>::pi * 3.0 * t);
        return (float) sine(t, 440.0, amp);
    });
    ASSERT_TRUE(f.existsAsFile());

    const auto a = HDAW::analyzeToneWav(f, 0.01);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_FALSE(std::isnan(a.modRateHz));
    EXPECT_NEAR(a.modRateHz, 3.0, 0.15);          // 5% tolerance
    EXPECT_GT(a.amDepth, 0.8);                    // (0.9-0.1)/0.5 = 1.6 ideal
    EXPECT_GT(a.modProminence, 3.0);
    EXPECT_GT(a.modCycles, 10.0);

    f.deleteFile();
}

TEST(ToneVerityAnalyzer, CentroidTrajectoryTracksFilterSweep)
{
    // First half 220 Hz, second half 1760 Hz: the end/start centroid ratio
    // must exceed 4 (an octave-spanning open-up sweep).
    const int len = (int) (kSr * 2.0);
    juce::File f = writeWav(len, [](int64_t i) {
        const double t = (double) i / kSr;
        return (float) (t < 1.0 ? sine(t, 220.0, 0.5) : sine(t, 1760.0, 0.5));
    });
    ASSERT_TRUE(f.existsAsFile());

    const auto a = HDAW::analyzeToneWav(f, 0.01);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_FALSE(std::isnan(a.centroidStart));
    ASSERT_FALSE(std::isnan(a.centroidEnd));
    EXPECT_GT(a.centroidEnd / a.centroidStart, 4.0);

    f.deleteFile();
}

TEST(ToneVerityAnalyzer, AttackAndTrailingSilenceMeasured)
{
    // 0.25 s silence, 1.5 s tone with a 0.2 s linear attack ramp, 0.25 s silence.
    // (An instant-onset sine reaches full RMS within its first bin — attack 0.)
    const int len = (int) (kSr * 2.0);
    juce::File f = writeWav(len, [](int64_t i) {
        const double t = (double) i / kSr;
        if (t < 0.25 || t >= 1.75) return 0.0f;
        const double ramp = std::min(1.0, (t - 0.25) / 0.2);
        return (float) sine(t, 220.0, 0.5 * ramp);
    });
    ASSERT_TRUE(f.existsAsFile());

    const auto a = HDAW::analyzeToneWav(f, 0.01);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_FALSE(std::isnan(a.attackMs));
    EXPECT_GE(a.attackMs, 100.0);   // the 10%->90% climb spans the ramp bins
    EXPECT_LE(a.attackMs, 300.0);
    ASSERT_FALSE(std::isnan(a.trailingSilenceSeconds));
    EXPECT_NEAR(a.trailingSilenceSeconds, 0.25, 0.06);

    f.deleteFile();
}

TEST(ToneVerityAnalyzer, ExpectationEvaluationPassFail)
{
    // Pure evaluation math via a fabricated result — no render needed.
    ProjectCommands::ToneVerityParams p;
    p.modRateHz = 3.0;
    p.modRateTolPct = 10.0;
    p.f0Hz = 220.0;
    p.f0CentsMax = 50.0;

    ProjectCommands::ToneVerityResult r;
    r.modRateHz = 3.1;
    r.f0Hz = 221.5;
    HDAW::evaluateToneExpectations(p, r);
    EXPECT_EQ(r.expectationsChecked, 2);
    EXPECT_TRUE(r.pass);

    // Xenia-style near-miss: f0 6% sharp fails a 50-cent gate.
    ProjectCommands::ToneVerityResult r2;
    r2.modRateHz = 3.1;
    r2.f0Hz = 220.0 * std::pow(2.0, 60.0 / 1200.0);   // 60 cents sharp
    HDAW::evaluateToneExpectations(p, r2);
    EXPECT_FALSE(r2.pass);
    EXPECT_EQ(r2.expectations.size(), 2u);

    // Vacuous pass when no expectations are supplied.
    ProjectCommands::ToneVerityResult r3;
    HDAW::evaluateToneExpectations(ProjectCommands::ToneVerityParams{}, r3);
    EXPECT_TRUE(r3.pass);
    EXPECT_EQ(r3.expectationsChecked, 0);
}
