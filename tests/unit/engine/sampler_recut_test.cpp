// The two RE-CUT invariants that the sampler slice surfaces promise and that
// the first cut of the feature broke:
//
//   1. A re-cut of ONE window CARRIES the persisted per-piece metadata
//      (sliceMeta's frame:bandMask:strength triples) of every boundary it does
//      NOT touch — rebuilding the meta wholesale erased the band mask and
//      strength of every boundary outside the window.
//   2. A re-cut ANALYSES its window, not the file: the band-aware onset engine's
//      adaptive statistics (histogram percentile, mean + k*stddev floor, the
//      participation gate) must describe the piece being re-cut, so a quiet
//      window still yields its onsets when loud material lives elsewhere.
//
// Both are synthetic and deterministic: a 2 s click track with one broadband
// burst every 0.25 s, generated from a fixed seed so two files can share
// BIT-IDENTICAL in-window material while their outside material differs.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 44100.0;
constexpr double kBpm = 120.0;
constexpr double kSeconds = 2.0;
constexpr double kClickStep = 0.25;      // one burst every 0.25 s
constexpr double kFrom = 0.2;            // the re-cut window, normalized
constexpr double kTo = 0.8;

int64_t wavFrames()
{
    return static_cast<int64_t>(std::llround(kSeconds * kSr));
}

// One broadband burst every 0.25 s (the slice_detector_test family: a 25 ms
// exponential decay of uniform noise). `insideAmp` scales the samples inside
// [fromNorm, toNorm), `outsideAmp` the rest, so two files written with the same
// arguments differ ONLY outside the window.
juce::File writeRecutWav(double insideAmp, double outsideAmp,
                         double fromNorm, double toNorm)
{
    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("hdaw_recut", ".wav", false);
    const int64_t len = wavFrames();
    const int64_t fromF = static_cast<int64_t>(std::llround(fromNorm * static_cast<double>(len)));
    const int64_t toF   = static_cast<int64_t>(std::llround(toNorm * static_cast<double>(len)));

    juce::AudioBuffer<float> buf(1, static_cast<int>(len));
    buf.clear();

    std::mt19937 rng(20261001u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const double decayRate = 1.0 / (25.0 * 0.001 * kSr);   // ~25 ms
    for (int i = 1; i * kClickStep < kSeconds; ++i)
    {
        const int64_t at = static_cast<int64_t>(std::llround(i * kClickStep * kSr));
        for (int64_t k = 0; at + k < len; ++k)
        {
            const double env = std::exp(-static_cast<double>(k) * decayRate);
            if (env < 1e-4) break;
            buf.addSample(0, static_cast<int>(at + k),
                          static_cast<float>(0.9 * env) * dist(rng));
        }
    }
    for (int64_t i = 0; i < len; ++i)
    {
        const float a = static_cast<float>((i >= fromF && i < toF) ? insideAmp : outsideAmp);
        buf.setSample(0, static_cast<int>(i), buf.getSample(0, static_cast<int>(i)) * a);
    }

    juce::WavAudioFormat wav;
    auto* out = new juce::FileOutputStream(f);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out, kSr, 1, 16, {}, 0));
    if (writer == nullptr) { delete out; return {}; }
    writer->writeFromAudioSampleBuffer(buf, 0, static_cast<int>(len));
    writer->flush();
    return f;
}

// An engine with ONE sampler track holding `wav`, at the pinned 120 BPM the
// detection math reads.
struct RecutFixture
{
    AudioEngine engine;
    int track = -1;

    bool setUp(const juce::File& wav)
    {
        engine.initialize();
        auto& cmds = engine.getAudioEngineCommands();
        track = cmds.addTrack("Recut", -1, -1, 0);
        if (track < 0) return false;
        cmds.addFxSlot(track, std::string("sampler"), -1, std::string());
        cmds.setSamplerSample(track, 0, wav.getFullPathName().toStdString(), 60);
        engine.drainPendingRoutingRebuild();
        engine.getTransportManager().setBPM(kBpm);
        return slot().isValid();
    }

    // Non-const: getProjectModel() has no const overload.
    juce::ValueTree slot()
    {
        return engine.getProjectModel().getTrackListTree().getChild(track)
            .getChildWithName(IDs::FX_CHAIN).getChild(0);
    }

    std::string prop(const char* key)
    {
        return slot().getProperty(key, "").toString().toStdString();
    }
};

// sliceMeta -> { frame text -> whole triple text }. Keyed by the FRAME TEXT so
// "byte-identical" is a string comparison, not a float comparison.
std::map<std::string, std::string> parseMeta(const std::string& meta)
{
    std::map<std::string, std::string> out;
    for (auto& tok : juce::StringArray::fromTokens(juce::String(meta), ",", ""))
    {
        const juce::String t = tok.trim();
        const int colon = t.indexOfChar(':');
        if (colon <= 0) continue;
        out[t.substring(0, colon).toStdString()] = t.toStdString();
    }
    return out;
}

// Rewrite every triple's mask:strength tail, keeping the frame text.
std::string poisonMeta(const std::string& meta, const std::string& maskAndStrength)
{
    juce::StringArray parts;
    for (auto& tok : juce::StringArray::fromTokens(juce::String(meta), ",", ""))
    {
        const juce::String t = tok.trim();
        const int colon = t.indexOfChar(':');
        parts.add(colon > 0 ? t.substring(0, colon) + ":" + juce::String(maskAndStrength)
                            : t);
    }
    return parts.joinIntoString(",").toStdString();
}

bool isEndpoint(double norm) { return norm <= 0.0 || norm >= 1.0; }
bool inWindow(double norm)   { return norm >= kFrom && norm < kTo; }

} // namespace

// ── Defect 1: a partial re-cut destroys the persisted per-piece metadata ─────

TEST(SamplerRecut, RecutPreservesSliceMetaOutsideTheWindow)
{
    const juce::File wav = writeRecutWav(0.9, 0.9, kFrom, kTo);
    ASSERT_TRUE(wav.existsAsFile());

    RecutFixture fx;
    ASSERT_TRUE(fx.setUp(wav));
    auto& cmds = fx.engine.getAudioEngineCommands();

    // Seed REAL per-piece metadata with a whole-file transient detect.
    const auto seeded = cmds.detectSamplerSlices(fx.track, 0, "transient", 1.0, 0.5, 0.0, 1.0);
    ASSERT_TRUE(seeded.ok) << seeded.error;
    const std::string beforeMeta = fx.prop("sliceMeta");
    ASSERT_FALSE(beforeMeta.empty()) << "the seed detect must write sliceMeta";

    // Poison every stored triple with a mask/strength the detector cannot
    // produce (a strength is clamped to 0..1), so a CARRIED value and a
    // RECOMPUTED one are distinguishable by inspection.
    const std::string poisoned = poisonMeta(beforeMeta, "7:9.999");
    fx.slot().setProperty(juce::Identifier("sliceMeta"), juce::String(poisoned), nullptr);
    const auto stored = parseMeta(poisoned);

    // Re-cut ONE window, keeping the boundaries outside it.
    const auto recut = cmds.recutSamplerSlices(fx.track, 0, "transient", 1.0, 0.5,
                                               kFrom, kTo, /*keepOverrides=*/true);
    ASSERT_TRUE(recut.ok) << recut.error;

    const auto after = parseMeta(fx.prop("sliceMeta"));
    ASSERT_FALSE(after.empty()) << "a re-cut must write the merged sliceMeta";

    int outsideChecked = 0, insideFresh = 0;
    for (const auto& kv : after)
    {
        const double norm = juce::String(kv.first).getDoubleValue();
        if (isEndpoint(norm)) continue;   // frame 0 / len are structural, always 0/0

        if (!inWindow(norm))
        {
            // PRESERVED boundary: the STORED triple comes back byte-identical.
            const auto it = stored.find(kv.first);
            ASSERT_NE(it, stored.end())
                << "unexpected boundary outside the window: " << kv.first;
            EXPECT_EQ(kv.second, it->second)
                << "boundary " << kv.first << " outside the window lost its metadata";
            ++outsideChecked;
        }
        else
        {
            // IN-WINDOW boundary: FRESH values from this pass. The poisoned
            // strength (9.999) is impossible, so its absence proves the value
            // was recomputed rather than carried.
            EXPECT_EQ(kv.second.find("9.999"), std::string::npos)
                << "in-window boundary " << kv.first << " carried the stored meta: "
                << kv.second;
            ++insideFresh;
        }
    }

    // Non-vacuous on both sides: boundaries really were carried AND really were
    // re-detected inside the window (the window holds 5 of the 7 clicks).
    EXPECT_GE(outsideChecked, 2);
    EXPECT_GE(insideFresh, 3);

    wav.deleteFile();
}

// ── Defect 2: a re-cut analyses the WHOLE file, not the window ──────────────

TEST(SamplerRecut, RecutAnalysisIsWindowLocal)
{
    // The SAME quiet in-window material in both files (0.05 amplitude); only the
    // material OUTSIDE the window differs — 0.05 vs 0.9 is ~25 dB louder, which
    // under whole-file statistics masks every quiet in-window onset.
    const juce::File quiet   = writeRecutWav(0.05, 0.05, kFrom, kTo);
    const juce::File loudOut = writeRecutWav(0.05, 0.9,  kFrom, kTo);
    ASSERT_TRUE(quiet.existsAsFile());
    ASSERT_TRUE(loudOut.existsAsFile());

    // The in-window boundaries a re-cut of that window produces.
    const auto inWindowPoints = [](const juce::File& wav) {
        std::vector<double> pts;
        RecutFixture fx;
        if (!fx.setUp(wav)) return pts;
        auto& cmds = fx.engine.getAudioEngineCommands();
        // A deterministic boundary set to merge with (grid mode is pure
        // arithmetic, no detector tuning involved).
        const auto seed = cmds.detectSamplerSlices(fx.track, 0, "grid", 1.0, 0.5, 0.0, 1.0);
        if (!seed.ok) return pts;
        const auto r = cmds.recutSamplerSlices(fx.track, 0, "transient", 1.0, 0.5,
                                               kFrom, kTo, /*keepOverrides=*/true);
        if (!r.ok) return pts;
        for (float p : r.slicePoints)
            if (inWindow(static_cast<double>(p)))
                pts.push_back(static_cast<double>(p));
        return pts;
    };

    const auto quietPts = inWindowPoints(quiet);
    const auto loudPts  = inWindowPoints(loudOut);

    // The window's own onsets are found at all (the quiet clicks are the only
    // signal inside the window, in BOTH files).
    EXPECT_GE(quietPts.size(), 3u)
        << "the window-local analysis must find the quiet window's onsets";
    // And the loud material elsewhere changes nothing: the in-window frames are
    // identical because the analysed sub-buffer is identical.
    EXPECT_EQ(quietPts, loudPts)
        << "material outside the window must not change the window's onsets";

    quiet.deleteFile();
    loudOut.deleteFile();
}
