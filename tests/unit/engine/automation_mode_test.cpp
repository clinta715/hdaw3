#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Seed exactly the track the test
// addresses and drain the coalesced routing rebuild (lessons 9/10/12).
static int seedTrack(AudioEngine& engine)
{
    const int idx = engine.getProjectCommands().addTrack("Track 0");
    engine.drainPendingRoutingRebuild();
    return idx;
}

TEST(AutomationMode, DefaultModeIsRead)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addAutomationLane(0, "Volume", 1);

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    bool found = false;
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
        {
            EXPECT_EQ(l.mode, "read");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AutomationMode, SetAutomationModeWrite)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addAutomationLane(0, "Volume", 1);
    cmds.setAutomationMode(0, "Volume", "write");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
            EXPECT_EQ(l.mode, "write");
    }
}

TEST(AutomationMode, SetAutomationModeTouch)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addAutomationLane(0, "Volume", 1);
    cmds.setAutomationMode(0, "Volume", "touch");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
            EXPECT_EQ(l.mode, "touch");
    }
}

TEST(AutomationMode, SetAutomationModeLatch)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addAutomationLane(0, "Volume", 1);
    cmds.setAutomationMode(0, "Volume", "latch");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
            EXPECT_EQ(l.mode, "latch");
    }
}
