// End-to-end coverage for the sampler slice-mode vocabulary
// (common/SamplerSliceModes.h) at the AudioEngineCommands level: the `aligned`
// mode (SliceDetector::driftAlignedGrid) must be REACHABLE from
// detectSamplerSlices and must actually follow a source that plays a few
// percent slow — the capability the detector has had (and unit-tested in
// slice_detector_test.cpp) but which no production path could reach before.
//
// The synthetic sample is deterministic: one broadband click per beat, every
// click placed at i*step*drift, so the nominal grid is systematically wrong and
// the onset-fitted grid is systematically right. The assertion is the RATIO of
// mean absolute errors against the generated onsets, not an absolute tolerance.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 44100.0;
constexpr double kBpm = 120.0;
constexpr double kGridBeats = 1.0;   // one slice per beat -> 0.5 s steps
constexpr double kSeconds = 4.0;
constexpr double kDrift = 1.04;      // the source plays 4 % slow
constexpr int    kSteps = 8;         // 4 s of 0.5 s steps

int64_t stepSamples()
{
    return static_cast<int64_t>(std::llround((60.0 / kBpm) * kGridBeats * kSr));
}

// The TRUE onsets: step i lands at i*step*drift (i = 1..kSteps-1). By the last
// one the nominal grid is 4 % of the loop length (0.14 s) away.
std::vector<int64_t> trueOnsets()
{
    std::vector<int64_t> out;
    const double s = static_cast<double>(stepSamples());
    for (int i = 1; i < kSteps; ++i)
        out.push_back(static_cast<int64_t>(std::llround(i * s * kDrift)));
    return out;
}

juce::File writeDriftingClickWav()
{
    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("hdaw_slice_aligned", ".wav", false);
    const int len = static_cast<int>(std::llround(kSeconds * kSr));
    juce::AudioBuffer<float> buf(1, len);
    buf.clear();

    // Broadband burst with an exponential decay (a click/hat hit) — the same
    // synthetic family slice_detector_test.cpp uses, so the band-aware engine
    // has real onsets to find.
    std::mt19937 rng(20261001u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const double decayRate = 1.0 / (25.0 * 0.001 * kSr);   // ~25 ms
    for (int64_t at : trueOnsets())
    {
        for (int64_t k = 0; at + k < len; ++k)
        {
            const double env = std::exp(-static_cast<double>(k) * decayRate);
            if (env < 1e-4) break;
            buf.addSample(0, static_cast<int>(at + k),
                          static_cast<float>(0.9 * env) * dist(rng));
        }
    }

    juce::WavAudioFormat wav;
    auto* out = new juce::FileOutputStream(f);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out, kSr, 1, 16, {}, 0));
    if (writer == nullptr) { delete out; return {}; }
    writer->writeFromAudioSampleBuffer(buf, 0, len);
    writer->flush();
    return f;
}

// Interior boundaries (the implicit 0 and len endpoints dropped) as frames.
std::vector<int64_t> interiorFrames(const std::vector<float>& normalized, int64_t len)
{
    std::vector<int64_t> out;
    for (float p : normalized)
    {
        if (p <= 0.0f || p >= 1.0f) continue;
        out.push_back(static_cast<int64_t>(
            std::llround(static_cast<double>(p) * static_cast<double>(len))));
    }
    return out;
}

// Mean, over the TRUE onsets, of the distance to the nearest returned boundary.
double meanAbsErrorMs(const std::vector<int64_t>& got, const std::vector<int64_t>& truth)
{
    double acc = 0.0;
    for (int64_t t : truth)
    {
        int64_t best = INT64_MAX;
        for (int64_t g : got)
        {
            const int64_t d = g > t ? g - t : t - g;
            best = std::min(best, d);
        }
        acc += static_cast<double>(best);
    }
    return acc / static_cast<double>(truth.size()) / kSr * 1000.0;
}

} // namespace

TEST(SamplerSliceMode, AlignedFollowsADriftingSourceBetterThanGrid)
{
    const juce::File wav = writeDriftingClickWav();
    ASSERT_TRUE(wav.existsAsFile());

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getAudioEngineCommands();

    const int track = cmds.addTrack("SliceAligned", -1, -1, 0);
    ASSERT_GE(track, 0);
    cmds.addFxSlot(track, std::string("sampler"), -1, std::string());
    cmds.setSamplerSample(track, 0, wav.getFullPathName().toStdString(), 60);
    engine.drainPendingRoutingRebuild();

    // The grid math below assumes 120 BPM; the transport manager is what
    // runSamplerDetection reads, so pin it AFTER the setup rebuilds.
    engine.getTransportManager().setBPM(kBpm);

    const int64_t len = static_cast<int64_t>(std::llround(kSeconds * kSr));
    const auto truth = trueOnsets();

    // The nominal grid: correct only at the very start of the loop.
    const auto grid = cmds.detectSamplerSlices(track, 0, "grid", kGridBeats, 0.5, 0.0, 1.0);
    ASSERT_TRUE(grid.ok) << grid.error;

    // `aligned` is the onset-fitted grid; the token is case-insensitive.
    const auto aligned = cmds.detectSamplerSlices(track, 0, "ALIGNED", kGridBeats, 0.5, 0.0, 1.0);
    ASSERT_TRUE(aligned.ok) << aligned.error;

    const auto gridPts = interiorFrames(grid.slicePoints, len);
    const auto alignedPts = interiorFrames(aligned.slicePoints, len);
    ASSERT_GE(gridPts.size(), truth.size());
    ASSERT_GE(alignedPts.size(), truth.size());

    const double gridErr = meanAbsErrorMs(gridPts, truth);
    const double alignedErr = meanAbsErrorMs(alignedPts, truth);

    // The whole point of the mode: the fitted grid tracks the PERFORMANCE, the
    // nominal grid cannot (it is off by a growing fraction of a step).
    EXPECT_LT(alignedErr * 3.0, gridErr)
        << "aligned MAE " << alignedErr << " ms vs grid MAE " << gridErr << " ms";

    // A fitted grid carries no per-onset band information (like grid mode).
    EXPECT_TRUE(std::all_of(aligned.bandMasks.begin(), aligned.bandMasks.end(),
                            [](uint32_t m) { return m == 0u; }));
    EXPECT_TRUE(std::all_of(aligned.strengths.begin(), aligned.strengths.end(),
                            [](float s) { return s == 0.0f; }));

    // The CANONICAL token landed in the tree (the vocabulary is closed).
    const auto slot = engine.getProjectModel().getTrackListTree().getChild(track)
                          .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    EXPECT_EQ(slot.getProperty("sliceMode", "").toString().toStdString(), std::string("aligned"));

    wav.deleteFile();
}

TEST(SamplerSliceMode, UnknownModeIsRefusedAndWritesNothing)
{
    const juce::File wav = writeDriftingClickWav();
    ASSERT_TRUE(wav.existsAsFile());

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getAudioEngineCommands();

    const int track = cmds.addTrack("SliceRefusal", -1, -1, 0);
    ASSERT_GE(track, 0);
    cmds.addFxSlot(track, std::string("sampler"), -1, std::string());
    cmds.setSamplerSample(track, 0, wav.getFullPathName().toStdString(), 60);
    engine.drainPendingRoutingRebuild();
    engine.getTransportManager().setBPM(kBpm);

    // Seed a real boundary set + mode so "nothing was written" is observable.
    const auto seeded = cmds.detectSamplerSlices(track, 0, "grid", kGridBeats, 0.5, 0.0, 1.0);
    ASSERT_TRUE(seeded.ok) << seeded.error;
    const auto slot = engine.getProjectModel().getTrackListTree().getChild(track)
                          .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    const juce::String before = slot.getProperty("slicePoints", "").toString();

    // detect / recut / setSliceMode all reach the ONE validator.
    for (const auto& r : { cmds.detectSamplerSlices(track, 0, "transientt", kGridBeats, 0.5, 0.0, 1.0),
                           cmds.recutSamplerSlices(track, 0, "transientt", kGridBeats, 0.5, 0.0, 1.0, true) })
    {
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.error.find("transient, grid, aligned"), std::string::npos) << r.error;
    }

    const std::string modeError =
        cmds.setSamplerSliceMode(track, 0, "transientt", kGridBeats, 0.5);
    EXPECT_NE(modeError.find("transient, grid, aligned"), std::string::npos) << modeError;

    EXPECT_EQ(slot.getProperty("slicePoints", "").toString(), before);
    EXPECT_EQ(slot.getProperty("sliceMode", "").toString().toStdString(), std::string("grid"));

    wav.deleteFile();
}
