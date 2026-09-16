// Movement plan (FX & Automation choreography): batch automation-preset
// application across tracks in ONE undo unit with lane auto-create/reuse by
// paramID (never two lanes on one parameter). MODEL-level (ValueTree tracks)
// + command layer — no DSP state under test.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "common/ProjectCommands.h"
#include "model/ProjectModel.h"

namespace {

using MovementEvent = ProjectCommands::MovementEvent;

MovementEvent makeEv(int track, const char* preset, double start, double end, int paramID = -1)
{
    MovementEvent e;
    e.trackIndex = track;
    e.preset = preset;
    e.startBeats = start;
    e.endBeats = end;
    e.paramID = paramID;
    return e;
}

juce::ValueTree autoListOf(AudioEngine& e, int track)
{
    return e.getProjectModel().getTrackListTree().getChild(track)
        .getChildWithName(IDs::AUTOMATION_LIST);
}

int laneCountForParam(AudioEngine& e, int track, int paramID)
{
    const auto list = autoListOf(e, track);
    if (!list.isValid()) return 0;
    int n = 0;
    for (int i = 0; i < list.getNumChildren(); ++i)
        if (static_cast<int>(list.getChild(i).getProperty(IDs::paramID, 0)) == paramID) ++n;
    return n;
}

bool laneExists(AudioEngine& e, int track, const std::string& name)
{
    const auto list = autoListOf(e, track);
    if (!list.isValid()) return false;
    for (int i = 0; i < list.getNumChildren(); ++i)
        if (list.getChild(i).getProperty(IDs::name, "").toString().toStdString() == name) return true;
    return false;
}

int lanePointCount(AudioEngine& e, int track, const std::string& name)
{
    const auto list = autoListOf(e, track);
    if (!list.isValid()) return 0;
    for (int i = 0; i < list.getNumChildren(); ++i)
    {
        auto lane = list.getChild(i);
        if (lane.getProperty(IDs::name, "").toString().toStdString() != name) continue;
        const auto pts = lane.getChildWithName(IDs::POINT_LIST);
        return pts.isValid() ? pts.getNumChildren() : 0;
    }
    return 0;
}

} // namespace

TEST(MovementPlan, BatchesLanesAndPointsInOneUndoUnit)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Bass");
    ASSERT_GE(track, 0);

    auto res = cmds.applyMovementPlan({ makeEv(0, "pump", 0.0, 8.0, 1),
                                        makeEv(0, "riser", 8.0, 16.0, 2) });
    EXPECT_EQ(res.okCount, 2);
    EXPECT_EQ(res.failCount, 0);
    for (const auto& r : res.events)
    {
        EXPECT_TRUE(r.ok) << r.error;
        EXPECT_GT(r.pointsWritten, 0);
    }

    // pump reuses the built-in Volume lane (paramID 1); riser reuses the
    // built-in Pan lane (paramID 2) — never two lanes on one parameter.
    EXPECT_TRUE(laneExists(engine, 0, "Volume"));
    EXPECT_TRUE(laneExists(engine, 0, "Pan"));
    EXPECT_EQ(laneCountForParam(engine, 0, 1), 1);
    EXPECT_EQ(laneCountForParam(engine, 0, 2), 1);

    // ONE undo restores the built-in lanes (both predate the transaction):
    // pump points revert to the 2 static defaults, Pan to its 2 defaults.
    engine.getProjectModel().getUndoManager().undo();
    EXPECT_TRUE(laneExists(engine, 0, "Volume"));
    EXPECT_TRUE(laneExists(engine, 0, "Pan"));
    EXPECT_EQ(laneCountForParam(engine, 0, 1), 1);
    EXPECT_EQ(laneCountForParam(engine, 0, 2), 1);
    EXPECT_EQ(lanePointCount(engine, 0, "Volume"), 2); // built-in defaults restored
}

TEST(MovementPlan, ReusesLaneByParamIDInsteadOfStacking)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Bass");

    // Two events on paramID 2 in one call -> one lane, both write.
    auto r1 = cmds.applyMovementPlan({ makeEv(0, "riser", 0.0, 8.0, 2),
                                       makeEv(0, "macro", 8.0, 16.0, 2) });
    EXPECT_EQ(r1.okCount, 2);
    EXPECT_EQ(laneCountForParam(engine, 0, 2), 1);
    EXPECT_TRUE(laneExists(engine, 0, "Pan"));

    // A later plan on the same param with a new preset reuses the lane (no
    // duplicate, points replaced inside the window).
    auto r2 = cmds.applyMovementPlan({ makeEv(0, "phaseSweep", 0.0, 8.0, 2) });
    EXPECT_EQ(r2.okCount, 1);
    EXPECT_EQ(laneCountForParam(engine, 0, 2), 1);
    EXPECT_TRUE(laneExists(engine, 0, "Pan"));
}

TEST(MovementPlan, PartialFailureKeepsGoodEvents)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Bass");

    auto res = cmds.applyMovementPlan({ makeEv(99, "pump", 0.0, 8.0),
                                        makeEv(0, "pump", 8.0, 16.0) });
    EXPECT_EQ(res.failCount, 1);
    EXPECT_EQ(res.okCount, 1);
    EXPECT_EQ(res.events[0].error, "track not found");
    EXPECT_TRUE(laneExists(engine, 0, "Volume"));
    EXPECT_GT(lanePointCount(engine, 0, "Volume"), 1);
}

TEST(MovementPlan, UnknownPresetAndBadWindowRejected)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Bass");

    auto r1 = cmds.applyMovementPlan({ makeEv(0, "wobble", 0.0, 8.0) });
    EXPECT_EQ(r1.failCount, 1);
    EXPECT_EQ(r1.okCount, 0);
    EXPECT_EQ(r1.events[0].error, "unknown preset: wobble");

    auto r2 = cmds.applyMovementPlan({ makeEv(0, "pump", 8.0, 8.0) });
    EXPECT_EQ(r2.failCount, 1);
    EXPECT_FALSE(r2.events[0].error.empty());

    // No stray lanes beyond the built-in one after failures.
    EXPECT_EQ(laneCountForParam(engine, 0, 1), 1);
}