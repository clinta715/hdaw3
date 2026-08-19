#include <gtest/gtest.h>
#include <cstdlib>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
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

// TyrellN6 is the deterministic real-plugin test subject (installed at
// C:\Program Files\Common Files\VST3\TyrellN6(x64).vst3, present in the plugin
// cache with 128 programs). The real-plugin suites only run when
// HDAW_REAL_PLUGIN_TESTS is set AND the VST3 file exists — otherwise they skip
// (GTEST_SKIP pattern from export_volume_bypass_test.cpp).
bool tyrellN6Available()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    if (s.trim().isEmpty() || s.trim() == "0")
        return false;
    return juce::File("C:\\Program Files\\Common Files\\VST3\\TyrellN6(x64).vst3").existsAsFile();
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

// ─── G2 — programIndex ─────────────────────────────────────────────

TEST(InstrumentPart, ProgramIndexRequiresPluginId)
{
    AudioEngine engine;
    engine.initialize();
    const int baseline = engine.getReadModel().getTrackCount();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.programIndex = 0;   // no pluginId → rejected at the boundary

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    EXPECT_FALSE(res.error.empty());
    EXPECT_NE(res.error.find("pluginId"), std::string::npos);
    EXPECT_EQ(res.trackIndex, -1);
    EXPECT_EQ(engine.getReadModel().getTrackCount(), baseline);
}

TEST(InstrumentPart, ProgramIndexOutOfRangeErrors)
{
    AudioEngine engine;
    engine.initialize();
    const int baseline = engine.getReadModel().getTrackCount();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.pluginId = "test.plugin.id";   // fake — never instantiates
    params.programIndex = 0;
    params.seed = 7;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    // The composite commits the track, then the fake plugin's slot becomes
    // "none" at rebuild — applyPluginProgram sees a non-plugin slot and fails.
    // The command rolls the WHOLE composite back (undo + close transaction),
    // so the project is left untouched — no dead track with a broken slot.
    EXPECT_FALSE(res.error.empty()) << "applyPluginProgram must reject a non-plugin slot";
    EXPECT_EQ(engine.getReadModel().getTrackCount(), baseline);

    // The engine is still healthy: a follow-up composite succeeds.
    ProjectCommands::InstrumentPartParams ok2;
    ok2.trackName = "Bass";
    ok2.style = "BassLine";
    ok2.seed = 8;
    auto res2 = engine.getProjectCommands().addInstrumentPart(ok2);
    EXPECT_TRUE(res2.error.empty()) << res2.error;
}

TEST(InstrumentPart, ProgramIndexSetsLiveProgram)
{
    if (!tyrellN6Available())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6 missing";

    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.pluginId = "C:\\Program Files\\Common Files\\VST3\\TyrellN6(x64).vst3";
    params.programIndex = 1;
    params.seed = 9;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    // Gate 1/10: assert the LIVE processor (not just the read model).
    auto* track = engine.getMainProcessor()->getTrack(res.trackIndex);
    ASSERT_NE(track, nullptr);
    ASSERT_FALSE(track->getFXChain().empty());
    auto& slot = track->getFXChain()[0];
    ASSERT_NE(slot, nullptr);
    ASSERT_TRUE(slot->isPlugin());
    ASSERT_NE(slot->getPluginInstance(), nullptr);
    EXPECT_EQ(slot->getCurrentProgram(), 1);

    // The tree snapshot must carry the pick so tree-copy renders (gain stage,
    // audition, export) and save/load capture it (Gate 1/10).
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto fxChain = trackList.getChild(res.trackIndex).getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChain.isValid());
    ASSERT_GT(fxChain.getNumChildren(), 0);
    EXPECT_FALSE(fxChain.getChild(0).getProperty(IDs::pluginState).toString().isEmpty());

    // Existing behavior: one undo removes the whole part.
    engine.getProjectCommands().undo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), 3);
}

// ─── Task B — autoGainToTarget global-scale fallback ────────────────

namespace {

ProjectCommands::InstrumentPartParams loudLeadPart(uint64_t seed)
{
    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 1;
    params.seed = seed;
    return params;
}

} // namespace

// Deterministic loud part: Lead seed 42 clips the full mix at unity (true
// peak ≈ 1.13, measured 0.5657 at master 0.5 in MasterGain.RenderAttenuation)
// and its raw solo RMS is well below 0.5, so targetRms 0.5 wants a fader > 1
// → clamps → the global-scale path scales the master bus down and raises the
// fader into the created headroom, in ONE undo unit.
TEST(GlobalScale, ClippedMixScaledDown)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    auto res = pc.addInstrumentPart(loudLeadPart(42));
    ASSERT_TRUE(res.error.empty()) << res.error;

    auto gain = pc.autoGainToTarget(res.trackIndex, 0.5f, 4.0, false, true);
    std::cout << "[GlobalScale] fader=" << gain.fader << " clamped=" << gain.clamped
              << " globalScale=" << gain.globalScale << " masterGain=" << gain.masterGain
              << " mixPeak=" << gain.mixPeak << " rawRms=" << gain.measuredRms << std::endl;
    ASSERT_TRUE(gain.error.empty()) << gain.error;
    ASSERT_TRUE(gain.ok);
    EXPECT_TRUE(gain.clamped);
    EXPECT_GT(gain.globalScale, 0.0f);
    EXPECT_LT(gain.globalScale, 1.0f);
    EXPECT_GT(gain.masterGain, 0.0f);
    EXPECT_LT(gain.masterGain, 1.0f);
    EXPECT_NEAR(gain.masterGain, gain.globalScale, 1e-6f);   // baseline master was 1.0
    EXPECT_GT(gain.fader, 1.0f);                             // raised into the headroom
    // Single-track mix: the fader raise exactly compensates the master scale
    // (the target track IS the mix), so the post-scale mix still touches full
    // scale and the 24-bit WAV measures exactly 1.0 (JUCE reads int24 full
    // scale as 1/0x7fffff). mixPeak < 1.0 is unreachable after a global
    // scale: the moment that set the pre-scale peak renders at >= full scale
    // (target silent there: exactly 1.0; target active: the fader raise adds).
    EXPECT_GT(gain.mixPeak, 0.0f);
    EXPECT_FLOAT_EQ(gain.mixPeak, 1.0f);

    // One undo unit: fader + master gain revert together.
    pc.undo();
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);
    const auto snap = engine.getReadModel().snapshot();
    ASSERT_GT(static_cast<int>(snap.tracks.size()), res.trackIndex);
    EXPECT_NEAR(snap.tracks[res.trackIndex].volume, 1.0, 1e-6);
}

// Default path (allowGlobalScale=false): clamps at unity exactly as before,
// master bus untouched.
TEST(GlobalScale, DefaultLeavesMasterUntouched)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    auto res = pc.addInstrumentPart(loudLeadPart(42));
    ASSERT_TRUE(res.error.empty()) << res.error;

    auto gain = pc.autoGainToTarget(res.trackIndex, 0.5f, 4.0, false, false);
    ASSERT_TRUE(gain.error.empty()) << gain.error;
    ASSERT_TRUE(gain.ok);
    EXPECT_TRUE(gain.clamped);
    EXPECT_FLOAT_EQ(gain.fader, 1.0f);
    EXPECT_FLOAT_EQ(gain.globalScale, 1.0f);
    EXPECT_NEAR(gain.masterGain, 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(gain.mixPeak, 0.0f);
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);
}

// A track whose fader clamps (raw RMS below target) but whose full mix stays
// under 1.0: the global-scale path must NOT fire. The 0.5-amp sine renders at
// RMS 0.25 / peak ≈ 0.35 (center-pan law), so targetRms 0.4 wants fader 1.6
// (clamps) while the mix probe measures a true peak ≈ 0.35 < 1.0. (The fm_synth
// default patch is velocity-insensitive, so an instrument part can't be quieted
// this way — the sine is the deterministic quiet source.)
TEST(GlobalScale, NonClippingMixUntouched)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const juce::File sine = writeSineWav(2.0);
    const int trackIndex = pc.addTrack("QuietSine", -1, -1, 0);
    const int clipId = pc.addAudioClip(trackIndex, 0.0, 4.0,
                                       sine.getFullPathName().toStdString(), "sine");
    ASSERT_GE(clipId, 0);

    auto gain = pc.autoGainToTarget(trackIndex, 0.4f, 1.0, false, true);
    ASSERT_TRUE(gain.error.empty()) << gain.error;
    ASSERT_TRUE(gain.ok);
    EXPECT_TRUE(gain.clamped);
    EXPECT_FLOAT_EQ(gain.fader, 1.0f);
    EXPECT_FLOAT_EQ(gain.globalScale, 1.0f);
    EXPECT_NEAR(gain.masterGain, 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(gain.mixPeak, 0.0f);
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);

    sine.deleteFile();
}
