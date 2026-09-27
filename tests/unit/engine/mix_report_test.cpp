#include <gtest/gtest.h>
#include "engine/MixReport.h"
#include "common/MixReportJson.h"
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

TEST(MixReportTest, QuietIntroDetectsNothing)
{
    const juce::File f = writeSynthWav(static_cast<int>(48000.0 * 2.0), 48000.0,
        [](int64_t i) { return static_cast<float>(sine(i / 48000.0, 440.0, 0.1)); });
    ASSERT_TRUE(f.existsAsFile());
    HDAW::BlastReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyzeBlast(f, 2.0, 0.125, rep, err))
        << err.toStdString();
    EXPECT_FALSE(rep.detected);
    EXPECT_FALSE(rep.clipping);
    EXPECT_FALSE(rep.loudTransient);
    EXPECT_FALSE(rep.silenceAfter);
    EXPECT_FALSE(rep.dcOffset);
    f.deleteFile();
}

TEST(MixReportTest, ClippingBlastDetectedAtStart)
{
    // 3 s: 0.3 s of amplitude-1.0 sine (clips the WAV), then a quiet 0.05 bed.
    const juce::File f = writeSynthWav(static_cast<int>(48000.0 * 3.0), 48000.0,
        [](int64_t i) {
            const double t = static_cast<double>(i) / 48000.0;
            if (t < 0.3) return static_cast<float>(sine(t, 200.0, 1.0));
            return static_cast<float>(sine(t, 440.0, 0.05));
        });
    ASSERT_TRUE(f.existsAsFile());
    HDAW::BlastReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyzeBlast(f, 3.0, 0.125, rep, err))
        << err.toStdString();
    EXPECT_TRUE(rep.detected);
    EXPECT_TRUE(rep.clipping);
    EXPECT_TRUE(rep.loudTransient);
    EXPECT_LE(rep.blastStart, 0.01);
    EXPECT_GT(rep.blastPeak, 0.99);
    EXPECT_GT(rep.blastRms, 0.5);
    EXPECT_LT(rep.preBlastRms, 1e-6);
    EXPECT_LT(rep.postBlastRms, rep.blastRms);
    EXPECT_GE(rep.blastEnd, 0.2);
    EXPECT_FALSE(rep.silenceAfter);   // bed is quiet but NOT digital silence
    f.deleteFile();
}

TEST(MixReportTest, SaturationThenSilenceFlagged)
{
    // 1.5 s: 0.4 s near-full-scale blast, then hard digital silence (0.0).
    const juce::File f = writeSynthWav(static_cast<int>(48000.0 * 1.5), 48000.0,
        [](int64_t i) {
            const double t = static_cast<double>(i) / 48000.0;
            if (t < 0.4) return static_cast<float>(sine(t, 100.0, 0.99));
            return 0.0f;
        });
    ASSERT_TRUE(f.existsAsFile());
    HDAW::BlastReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyzeBlast(f, 1.5, 0.125, rep, err))
        << err.toStdString();
    EXPECT_TRUE(rep.detected);
    EXPECT_TRUE(rep.loudTransient);
    EXPECT_EQ(rep.postBlastRms, 0.0);
    EXPECT_TRUE(rep.silenceAfter);
    f.deleteFile();
}

TEST(MixReportTest, DcOffsetFlagged)
{
    // 0.2 DC offset + 440 Hz @ 0.1 — mean ~0.2, no loud run.
    const juce::File f = writeSynthWav(static_cast<int>(48000.0 * 1.0), 48000.0,
        [](int64_t i) {
            const double t = static_cast<double>(i) / 48000.0;
            return static_cast<float>(0.2 + sine(t, 440.0, 0.1));
        });
    ASSERT_TRUE(f.existsAsFile());
    HDAW::BlastReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyzeBlast(f, 1.0, 0.125, rep, err))
        << err.toStdString();
    EXPECT_TRUE(rep.dcOffset);
    EXPECT_FALSE(rep.loudTransient);
    EXPECT_FALSE(rep.detected);
    f.deleteFile();
}

// boundaryPeak = max |sample| over the FIRST 0.1 s of a section: the
// "drop entry" transient gate (a section can slam in far louder than its
// sustained RMS). Loud 0.5 s head, quiet tail; the second section starts quiet.
TEST(MixReportTest, BoundaryPeakProbesSectionStart)
{
    const juce::File f = writeSynthWav(static_cast<int>(48000.0 * 1.5), 48000.0,
        [](int64_t i) {
            const double t = static_cast<double>(i) / 48000.0;
            return static_cast<float>(t < 0.5 ? sine(t, 200.0, 0.9)
                                              : sine(t, 200.0, 0.05));
        });
    ASSERT_TRUE(f.existsAsFile());
    HDAW::MixReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {
        HDAW::SectionWindow{"loud", 0.0, 0.5},
        HDAW::SectionWindow{"quiet", 0.5, 1.5}}, 0.0, rep, err)) << err.toStdString();
    ASSERT_EQ(rep.sections.size(), 2u);
    EXPECT_GT(rep.sections[0].boundaryPeak, 0.6);
    EXPECT_LT(rep.sections[1].boundaryPeak, 0.15);
    EXPECT_LE(rep.sections[0].boundaryPeak, rep.sections[0].peak + 1e-9);
    EXPECT_LE(rep.sections[1].boundaryPeak, rep.sections[1].peak + 1e-9);
    f.deleteFile();
}

TEST(MixReportTest, DropVsBuildGateIgnoresFinaleOutro)
{
    QJsonObject root;
    root["sections"] = QJsonArray{
        QJsonObject{ { "name", "build" }, { "rms", 0.8 } },
        QJsonObject{ { "name", "outro" }, { "rms", 0.2 } }
    };
    HDAW::applyDropVsBuildGate(root, QJsonObject{
        { "build", "build" },
        { "outro", "finale" }
    }, 0.85);
    EXPECT_FALSE(root.contains("loudnessGates"))
        << "finale/outro should not be judged as a drop payoff";

    root["sections"] = QJsonArray{
        QJsonObject{ { "name", "build" }, { "rms", 0.8 } },
        QJsonObject{ { "name", "drop" }, { "rms", 0.2 } }
    };
    HDAW::applyDropVsBuildGate(root, QJsonObject{
        { "build", "build" },
        { "drop", "mainB" }
    }, 0.85);
    ASSERT_TRUE(root.contains("loudnessGates"));
    const auto gates = root.value("loudnessGates").toObject();
    EXPECT_FALSE(gates.value("ok").toBool(true));
    EXPECT_EQ(gates.value("dropVsBuild").toArray().size(), 1);
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

// B6: ceilingHitPct counts PER-CHANNEL full-scale frames that the mono
// (L+R)/2 downmix cannot see. For the 20 clamped frames L=+1.0 and R=-1.0
// average to EXACT zero, so rms/peak measure nothing of them - precisely the
// one-sided-clamp blindness that motivated the metric (the vector-bloom
// 19-frame R-channel clamp was invisible to the mono peak).
TEST(MixReportTest, CeilingHitPctCountsPerChannelClampsMonoBlind)
{
    constexpr double sr = 48000.0;
    constexpr int len = static_cast<int>(sr);  // 1 s
    constexpr int kClampFrames = 20;
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_mix_report_ceiling_"
                                     + juce::String(juce::Random::getSystemRandom().nextInt())
                                     + ".wav");
    f.deleteFile();
    {
        std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sr, 2, 24, {}, 0));
        ASSERT_NE(writer, nullptr);
        out.release();
        juce::AudioBuffer<float> buf(2, len);
        buf.clear();
        for (int i = 0; i < len; ++i)
        {
            const float v = static_cast<float>(
                0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr));
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        // Clamped frames: L pinned at +1.0, R at -1.0 -> mono contribution 0.
        for (int i = 0; i < kClampFrames; ++i)
        {
            buf.setSample(0, 100 + i, 1.0f);
            buf.setSample(1, 100 + i, -1.0f);
        }
        writer->writeFromAudioSampleBuffer(buf, 0, len);
    }

    HDAW::MixReport rep;
    juce::String err;
    ASSERT_TRUE(HDAW::MixReportAnalyzer::analyze(f, {}, 0.0, rep, err)) << err.toStdString();
    EXPECT_EQ(rep.ceilingHitFrames, kClampFrames);
    EXPECT_NEAR(rep.ceilingHitPct, 100.0 * kClampFrames / sr, 1e-9);
    // Mono blindness proof: the clamps contribute NOTHING to the mono peak
    // (0.25 sine + exact-cancelling clamps), yet the metric sees every frame.
    EXPECT_LT(rep.peak, 0.26);
    f.deleteFile();
}

// B6: the shared targets shaper - rows, conventions, and the +/-5% masterRms
// band (the vector-bloom precedent: -4.75% mono passed by hand).
TEST(MixReportTest, TargetGatesShapeRowsAndPass)
{
    QJsonObject root{
        { "rms", 0.1524 },        // -4.75% against 0.16 -> inside the +/-5% band
        { "ceilingHitPct", 0.000065 },
        { "kickProminence", 0.62 },
        { "duration", 304.714 } };
    HDAW::applyTargetGates(root, QJsonObject{
        { "masterRms", 0.16 },
        { "ceilingHitPctMax", 5.0 },
        { "kickProminenceMin", 0.6 },
        { "targetDurationSeconds", 304.7 } });
    ASSERT_TRUE(root.contains("targetChecks"));
    EXPECT_TRUE(root.value("targetsOk").toBool());
    const auto rows = root.value("targetChecks").toArray();
    ASSERT_EQ(rows.size(), 4);
    EXPECT_EQ(rows[0].toObject().value("target").toString().toStdString(), "masterRms");
    EXPECT_TRUE(rows[0].toObject().value("pass").toBool());
    EXPECT_TRUE(rows[1].toObject().value("pass").toBool());
    EXPECT_TRUE(rows[2].toObject().value("pass").toBool());
    EXPECT_TRUE(rows[3].toObject().value("pass").toBool());

    // Out-of-band masterRms (-6%) fails, and a quiet mix fails the floor gates.
    QJsonObject quiet{
        { "rms", 0.1504 }, { "ceilingHitPct", 6.0 },
        { "kickProminence", 0.4 }, { "duration", 300.0 } };
    HDAW::applyTargetGates(quiet, QJsonObject{
        { "masterRms", 0.16 },
        { "ceilingHitPctMax", 5.0 },
        { "kickProminenceMin", 0.6 },
        { "targetDurationSeconds", 304.7 } });
    EXPECT_FALSE(quiet.value("targetsOk").toBool());
    const auto fails = quiet.value("targetChecks").toArray();
    EXPECT_FALSE(fails[0].toObject().value("pass").toBool()) << "-6% is outside the +/-5% band";
    EXPECT_FALSE(fails[1].toObject().value("pass").toBool());
    EXPECT_FALSE(fails[2].toObject().value("pass").toBool());
    EXPECT_FALSE(fails[3].toObject().value("pass").toBool()) << "|300 - 304.7| = 4.7 s exceeds the +/-2 s window";
}

// B6: empty targets is a no-op (no rows, no targetsOk key).
TEST(MixReportTest, TargetGatesNoopOnEmptyTargets)
{
    QJsonObject root{ { "rms", 0.1 } };
    HDAW::applyTargetGates(root, QJsonObject{});
    EXPECT_FALSE(root.contains("targetChecks"));
    EXPECT_FALSE(root.contains("targetsOk"));
}
