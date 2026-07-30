#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/TrackFXSlot.h"

using namespace HDAW;

TEST(ModulationFx, AddChorusToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "chorus");

    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "chorus");
}

TEST(ModulationFx, AddFlangerToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "flanger");

    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "flanger");
}

TEST(ModulationFx, AddPhaserToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "phaser");

    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "phaser");
}

TEST(ModulationFx, ChorusParamDefs)
{
    auto defs = TrackFXSlot::getParamDefsForType("chorus");
    ASSERT_EQ(defs.size(), 5u);
    EXPECT_EQ(defs[0].name, juce::String("Rate"));
    EXPECT_EQ(defs[2].name, juce::String("Centre Delay"));
    EXPECT_EQ(defs[4].name, juce::String("Mix"));
}

TEST(ModulationFx, PhaserParamDefs)
{
    auto defs = TrackFXSlot::getParamDefsForType("phaser");
    ASSERT_EQ(defs.size(), 5u);
    EXPECT_EQ(defs[2].name, juce::String("Centre Frequency"));
}
