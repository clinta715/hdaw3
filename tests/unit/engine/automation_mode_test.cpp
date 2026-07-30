#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(AutomationMode, DefaultModeIsRead)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

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

    cmds.addAutomationLane(0, "Volume", 1);
    cmds.setAutomationMode(0, "Volume", "latch");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
            EXPECT_EQ(l.mode, "latch");
    }
}
