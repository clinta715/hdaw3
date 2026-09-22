// ToneVerity engine tests (Phase 2 gates H3 + H4): real offline solo render of
// an fm_synth instrument track, envelope measurability, and END-TO-END
// modulation detection — a track volume LFO (unsynced 3 Hz sine) must show up
// in the analyzer's AM rate. Deviceless like verify_part (lesson 11 pump +
// lesson 9 drain).
// Contract: docs/plans/2026-09-22-param-verity-pipeline.md.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

// fm_synth instrument + a held 4-beat line — enough sustained tone to measure.
int makeFmTrack(AudioEngine& engine, std::string& err)
{
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("ToneVerity");
    if (track < 0) { err = "addTrack failed"; return -1; }
    cmds.addFxSlot(track, "fm_synth", -1, "");
    const int clipId = cmds.addMidiClip(track, 0.0, 8.0, "ToneVerity");
    if (clipId <= 0) { err = "addMidiClip failed"; return -1; }
    for (int i = 0; i < 16; ++i)
        if (cmds.addNote(clipId, 60, 100, i * 0.5, 0.5) <= 0)
        { err = "addNote failed"; return -1; }
    engine.drainPendingRoutingRebuild();
    return track;
}

} // namespace

TEST(ToneVerityEngine, FmSynthEnvelopeIsMeasurable)
{
    AudioEngine engine;
    engine.initialize();
    std::string err;
    const int track = makeFmTrack(engine, err);
    ASSERT_GE(track, 0) << err;

    ProjectCommands::ToneVerityParams p;
    p.trackIndex = track;
    p.windowSeconds = 4.0;

    const auto r = engine.getProjectCommands().verifyTone(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.baselineAudible) << "the fm_synth render must be non-silent";
    EXPECT_FALSE(r.envelopeRms.empty());
    EXPECT_FALSE(std::isnan(r.attackMs)) << "sustained fm notes must reach 90% of peak";
    EXPECT_GT(r.peakRms, 0.05);
    ASSERT_FALSE(std::isnan(r.sustainRatio));
    EXPECT_GT(r.sustainRatio, 0.1);
}

TEST(ToneVerityEngine, VolumeLfoRateIsDetectedEndToEnd)
{
    AudioEngine engine;
    engine.initialize();
    std::string err;
    const int track = makeFmTrack(engine, err);
    ASSERT_GE(track, 0) << err;

    // Unsynced 3 Hz sine LFO on the track volume (paramID 1) — the AM the
    // analyzer must find in the rendered envelope.
    auto& cmds = engine.getProjectCommands();
    cmds.addLfo(track);
    cmds.setLfoParam(track, 0, "rateSync", 0.0);   // rate in Hz
    cmds.setLfoParam(track, 0, "rate", 3.0);
    cmds.setLfoParam(track, 0, "depth", 0.6);
    cmds.setLfoParam(track, 0, "waveform", 0.0);   // sine
    cmds.setLfoParam(track, 0, "targetParamID", 1.0);
    cmds.setLfoParam(track, 0, "enabled", 1.0);
    engine.drainPendingRoutingRebuild();

    ProjectCommands::ToneVerityParams p;
    p.trackIndex = track;
    p.windowSeconds = 4.0;                         // 12 LFO cycles
    p.modRateHz = 3.0;
    p.modRateTolPct = 15.0;

    const auto r = engine.getProjectCommands().verifyTone(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.baselineAudible);
    ASSERT_FALSE(std::isnan(r.modRateHz))
        << "a 3 Hz volume LFO over 4 s must produce detectable AM";
    EXPECT_NEAR(r.modRateHz, 3.0, 0.45);
    EXPECT_GT(r.modProminence, 3.0);
    EXPECT_TRUE(r.pass) << "the modRateHz expectation must pass";
    EXPECT_EQ(r.expectationsChecked, 1);
}

TEST(ToneVerityEngine, SilentTrackIsRejectedNotMisjudged)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Empty");
    ASSERT_GE(track, 0);
    // Clip with notes but NO instrument -> silence (the Phase-1 lesson).
    const int clipId = cmds.addMidiClip(track, 0.0, 4.0, "Silent");
    ASSERT_GT(clipId, 0);
    cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    engine.drainPendingRoutingRebuild();

    ProjectCommands::ToneVerityParams p;
    p.trackIndex = track;
    p.windowSeconds = 2.0;

    const auto r = cmds.verifyTone(p);
    // Either an explicit error or the lesson-25 void-verdict error — never a
    // silent-but-passing measurement.
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("silent"), std::string::npos);
}
