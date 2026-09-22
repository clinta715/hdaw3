// ParamVerity engine test — per-(slot, param) audibility sweep through the real
// offline render path (tree copy + MixReport analysis), deviceless like the
// verify_part tests (lesson 11 message pump + lesson 9 drain).
// Setup per track: fm_synth INSTRUMENT slot 0 (makes the MIDI clip sound) +
// saturator FX slot 1 (the swept param carrier).
// Contract: docs/plans/2026-09-22-param-verity-pipeline.md.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

constexpr int kSatDrive = 0;   // "Drive dB" 0..40 (TrackFXSlot saturator defs)

struct Setup
{
    int track = -1;
    bool ok = false;
    std::string error;
};

Setup makeVerityTrack(AudioEngine& engine)
{
    Setup s;
    auto& cmds = engine.getProjectCommands();
    s.track = cmds.addTrack("Verity");
    if (s.track < 0) { s.error = "addTrack failed"; return s; }
    // Slot 0: the instrument (fm_synth) so the clip actually sounds.
    cmds.addFxSlot(s.track, "fm_synth", -1, "");
    // Slot 1: the FX under test (saturator).
    cmds.addFxSlot(s.track, "saturator", -1, "");
    const int clipId = cmds.addMidiClip(s.track, 0.0, 4.0, "Verity");
    if (clipId <= 0) { s.error = "addMidiClip failed"; return s; }
    // A stable 8th-note line: loud enough that every render is non-silent.
    for (int i = 0; i < 8; ++i)
        if (cmds.addNote(clipId, 60 + (i % 3), 100, i * 0.5, 0.45) <= 0)
        { s.error = "addNote failed"; return s; }
    engine.drainPendingRoutingRebuild();
    s.ok = true;
    return s;
}

} // namespace

TEST(ParamVerity, SaturatorDriveSweepIsAudibleAndRestored)
{
    AudioEngine engine;
    engine.initialize();
    const auto s = makeVerityTrack(engine);
    ASSERT_TRUE(s.ok) << s.error;

    ProjectCommands::ParamVerityParams p;
    p.trackIndex = s.track;
    p.slotIndex = 1;                 // saturator
    p.paramIndex = kSatDrive;        // "Drive dB" 0..40
    p.steps = { 0.0f, 1.0f };        // 0 dB vs 40 dB of drive
    p.baselineRuns = 2;
    p.windowSeconds = 2.0;

    const auto r = engine.getProjectCommands().verifyParamSweep(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.fxType, "saturator");
    EXPECT_EQ(r.paramName, "Drive dB");
    EXPECT_TRUE(r.baselineAudible) << "baseline render must be non-silent (lesson 25)";
    EXPECT_FALSE(r.inconclusive);
    EXPECT_EQ(r.baselineRuns, 2);
    EXPECT_EQ(r.steps.size(), 2u);
    EXPECT_TRUE(r.anyAudible) << "0 dB vs 40 dB drive must move the render";
    EXPECT_TRUE(r.restored) << "probe must restore the original param value";

    // Deterministic internal-FX renders: the same-input spread must be tiny
    // relative to the observed deltas.
    EXPECT_LT(r.spread, 1e-3);
    for (const auto& st : r.steps)
        EXPECT_TRUE(st.audible) << "step value " << st.value;

    // G4: the ValueTree param is back at the saturator default (12 dB drive).
    const auto slotTree = engine.getProjectModel().getTrackListTree()
                              .getChild(s.track).getChildWithName(IDs::FX_CHAIN).getChild(1);
    EXPECT_NEAR(static_cast<double>(slotTree.getProperty("param_0", 12.0)), 12.0, 1e-6);
}

TEST(ParamVerity, BypassedSlotIsVerdictNotAudible)
{
    AudioEngine engine;
    engine.initialize();
    const auto s = makeVerityTrack(engine);
    ASSERT_TRUE(s.ok) << s.error;
    // Bypass ONLY the saturator: the fm_synth instrument keeps the mix alive,
    // so the baseline stays audible and the swept param provably does nothing.
    engine.getProjectCommands().setFxSlotBypassed(s.track, 1, true);
    engine.drainPendingRoutingRebuild();

    ProjectCommands::ParamVerityParams p;
    p.trackIndex = s.track;
    p.slotIndex = 1;
    p.paramIndex = kSatDrive;
    p.steps = { 0.0f, 1.0f };
    p.windowSeconds = 2.0;

    const auto r = engine.getProjectCommands().verifyParamSweep(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.baselineAudible) << "the dry clip still sounds with the FX bypassed";
    EXPECT_FALSE(r.anyAudible) << "a bypassed slot must not show param deltas";
}

TEST(ParamVerity, RejectsBadArguments)
{
    AudioEngine engine;
    engine.initialize();
    const auto s = makeVerityTrack(engine);
    ASSERT_TRUE(s.ok) << s.error;
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::ParamVerityParams badTrack;
    badTrack.trackIndex = 99;
    badTrack.paramIndex = 0;
    EXPECT_FALSE(cmds.verifyParamSweep(badTrack).ok);

    ProjectCommands::ParamVerityParams badSlot;
    badSlot.trackIndex = s.track;
    badSlot.slotIndex = 7;
    badSlot.paramIndex = 0;
    EXPECT_FALSE(cmds.verifyParamSweep(badSlot).ok);

    ProjectCommands::ParamVerityParams badParam;
    badParam.trackIndex = s.track;
    badParam.slotIndex = 1;
    badParam.paramIndex = 99;
    EXPECT_FALSE(cmds.verifyParamSweep(badParam).ok);

    ProjectCommands::ParamVerityParams noParam;
    noParam.trackIndex = s.track;
    noParam.paramIndex = -1;
    EXPECT_FALSE(cmds.verifyParamSweep(noParam).ok);
}
