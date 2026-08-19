#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/ExportManager.h"
#include "model/ProjectModel.h"

// G1 + G2 suites for the instrument-part composer (Task A):
//   addInstrumentPart — track + instrument FX slot + phrase + paint in ONE
//     undo unit; the live processor shows the slot after a rebuild.
//   autoGainToTarget — deterministic sine-WAV gain staging: fader ≈
//     target/knownRms, verified re-render ≈ target, too-loud clamps at 1.0,
//     silent track errors instead of divide-by-zero/NaN.

namespace {

// Writes a temporary STEREO WAV filled with a 220 Hz sine at amplitude 0.5 per
// channel (RMS = 0.5/sqrt(2) ≈ 0.3536) and returns its path. Caller deletes
// the file. A stereo source is required: a MONO source is widened to stereo by
// the engine's equal-power mono→stereo path (each channel × 0.707), which
// would render at RMS 0.25 and defeat the known-RMS expectation.
juce::File writeSineWav(double seconds, int sampleRate = 44100)
{
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::SpecialLocationType::tempDirectory);
    auto f = tempDir.getNonexistentChildFile("hdaw_instrument_part", ".wav", false);

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    jassert(fos != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w(
        fmt.createWriterFor(fos.get(), sampleRate, 2, 16, {}, 0));
    fos.release(); // writer owns it now

    const int total = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buf(2, total);
    const double amp = 0.5;
    for (int i = 0; i < total; ++i)
    {
        const float v = static_cast<float>(amp * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return f;
}

double clipListMaxEndSec(const juce::ValueTree& trackList, int trackIndex)
{
    double maxEnd = 0.0;
    auto clipList = trackList.getChild(trackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return 0.0;
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto clip = clipList.getChild(c);
        const double start = static_cast<double>(clip.getProperty(IDs::startTime, 0.0));
        const double dur = static_cast<double>(clip.getProperty(IDs::duration, 0.0));
        maxEnd = std::max(maxEnd, start + dur);
    }
    return maxEnd;
}

} // namespace

TEST(InstrumentPart, CompositeCreatesTrackFxAndPhraseInOneUndoUnit)
{
    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 2;
    params.seed = 1;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    // Default project ships 3 empty tracks (lesson 9) — the new track is 3.
    ASSERT_EQ(res.trackIndex, 3);
    ASSERT_EQ(res.clipIds.size(), 2u);   // original + 1 ghost copy (count=2)
    EXPECT_GT(res.noteCount, 0);

    // Read-model: track 3 has an fm_synth fx slot.
    auto fx = engine.getReadModel().getFxSlots(3);
    ASSERT_FALSE(fx.empty());
    EXPECT_EQ(fx[0].fxType, "fm_synth");

    // LIVE processor after a fresh rebuild (Gate 1/10 — not just ReadModel).
    engine.getMainProcessor()->rebuildRoutingGraph();
    auto* track = engine.getMainProcessor()->getTrack(3);
    ASSERT_NE(track, nullptr);
    ASSERT_FALSE(track->getFXChain().empty());
    EXPECT_EQ(track->getFXChain()[0]->getType(), "fm_synth");

    // One undo unit: undo() removes the whole part (track 3 gone).
    engine.getProjectCommands().undo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), 3);
    EXPECT_TRUE(engine.getReadModel().getFxSlots(3).empty());
}

TEST(InstrumentPart, WholeSongPlacementCoversProject)
{
    AudioEngine engine;
    engine.initialize();

    auto& pc = engine.getProjectCommands();

    // Define a long project duration with a clip on an existing track:
    // 32 beats at 120 BPM = 16 s of content → project duration ≈ 19 s.
    const int longClip = pc.addMidiClip(0, 0.0, 32.0, "long");
    ASSERT_GE(longClip, 0);
    const double projectDurSec =
        HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Bass";
    params.style = "BassLine";
    params.lengthBeats = 4.0;
    params.placement = "wholeSong";
    params.seed = 2;

    auto res = pc.addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    const double lastEndSec =
        clipListMaxEndSec(engine.getProjectModel().getTrackListTree(), res.trackIndex);
    EXPECT_GE(lastEndSec + 1e-6, projectDurSec)
        << "wholeSong placement must cover the project duration";
}

TEST(InstrumentPart, PluginIdWritesPluginSlot)
{
    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.pluginId = "test.plugin.id";
    params.lengthBeats = 4.0;
    params.seed = 3;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    // Assert the tree/read-model only — a real scanned plugin is not required.
    auto fx = engine.getReadModel().getFxSlots(res.trackIndex);
    ASSERT_FALSE(fx.empty());
    EXPECT_EQ(fx[0].fxType, "plugin");
    EXPECT_EQ(fx[0].pluginId, "test.plugin.id");
}

TEST(AutoGain, DeterministicSineHitsTarget)
{
    AudioEngine engine;
    engine.initialize();

    const juce::File sine = writeSineWav(2.0); // 220 Hz @ amp 0.5 → RMS ≈ 0.3536

    auto& pc = engine.getProjectCommands();
    const int trackIndex = pc.addTrack("SineTrack", -1, -1, 0);
    const int clipId = pc.addAudioClip(trackIndex, 0.0, 4.0,
                                       sine.getFullPathName().toStdString(), "sine");
    ASSERT_GE(clipId, 0);

    auto gain = pc.autoGainToTarget(trackIndex, 0.1f, 1.0, /*verify=*/true);
    ASSERT_TRUE(gain.error.empty()) << gain.error;
    ASSERT_TRUE(gain.ok);

    // Expected fader = targetRms / renderedSourceRms. The source is a 0.5-amp
    // sine (raw RMS 0.5/sqrt(2) ≈ 0.3536), but the track's equal-power pan law
    // (Track.cpp:638-640) applies cos(pi/4)=sin(pi/4)=0.7071 to a center-panned
    // track, so the rendered source RMS is 0.3536 × 0.7071 = 0.25. The gain
    // stage MEASURES this actual render, so fader = 0.1/0.25 = 0.4 (±20% for
    // window alignment / resampling edge effects).
    const float expectedFader = 0.1f / 0.25f;
    EXPECT_NEAR(gain.fader, expectedFader, expectedFader * 0.2f);
    EXPECT_GT(gain.fader, 0.0f);
    EXPECT_LE(gain.fader, 1.0f);
    EXPECT_FALSE(gain.clamped);
    // Verified re-render measures ≈ target (±20%) — the closed-loop contract.
    EXPECT_NEAR(gain.measuredRms, 0.1f, 0.1f * 0.2f);

    sine.deleteFile();
}

TEST(AutoGain, TooLoudTargetClampsAtUnity)
{
    AudioEngine engine;
    engine.initialize();

    const juce::File sine = writeSineWav(2.0);

    auto& pc = engine.getProjectCommands();
    const int trackIndex = pc.addTrack("SineTrack2", -1, -1, 0);
    const int clipId = pc.addAudioClip(trackIndex, 0.0, 4.0,
                                       sine.getFullPathName().toStdString(), "sine");
    ASSERT_GE(clipId, 0);

    auto gain = pc.autoGainToTarget(trackIndex, 5.0f, 1.0);
    ASSERT_TRUE(gain.error.empty()) << gain.error;
    ASSERT_TRUE(gain.ok);
    EXPECT_EQ(gain.fader, 1.0f);
    EXPECT_TRUE(gain.clamped);

    sine.deleteFile();
}

TEST(AutoGain, SilentTrackErrors)
{
    AudioEngine engine;
    engine.initialize();

    auto& pc = engine.getProjectCommands();
    const int trackIndex = pc.addTrack("Silent", -1, -1, 0);
    // Empty MIDI clip on a track with no instrument → renders silence.
    const int clipId = pc.addMidiClip(trackIndex, 0.0, 4.0, "empty");
    ASSERT_GE(clipId, 0);

    auto gain = pc.autoGainToTarget(trackIndex, 0.1f, 1.0);
    EXPECT_FALSE(gain.ok);
    EXPECT_FALSE(gain.error.empty());
    EXPECT_TRUE(std::isfinite(gain.fader));
    EXPECT_TRUE(std::isfinite(gain.measuredRms));
}
