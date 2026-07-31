// Tests for the automation-lane authoring contract — specifically the binding
// of a lane to a target paramID (built-in 1/2/3 for volume/pan/mute, or the
// compound 100 + slotIndex*100 + paramIndex for plugin FX params).
//
// The audio-thread *application* of these paramIDs (Track::processBlock →
// TrackFXSlot::setAutomationParam) is exercised separately; these tests cover
// the write path: that addAutomationLane persists the paramID, exposes it via
// the read model, and rejects duplicate targets.
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

namespace {
// Find a lane snapshot by name; returns nullptr if absent.
const AutomationLaneSnapshot* findLane(const std::vector<AutomationLaneSnapshot>& lanes,
                                       const std::string& name)
{
    for (const auto& l : lanes)
        if (l.name == name)
            return &l;
    return nullptr;
}
}

// Adding a lane with a paramID persists that paramID on the lane and surfaces
// it through the read model. This is the core of FX-parameter automation: a
// lane with paramID 0 is dead at runtime (Track::processBlock ignores it).
TEST(Automation, AddLaneWithParamIDPersistsBinding)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // 100 + slotIndex(0)*100 + paramIndex(5) == 105 — a plugin FX param.
    cmds.addAutomationLane(0, "S0 Cutoff", 105);

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    const auto* lane = findLane(lanes, "S0 Cutoff");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramID, 105);
    EXPECT_TRUE(lane->enabled);
}

// Two lanes must not drive the same target paramID — the second add with a
// duplicate paramID is a no-op (the first lane is untouched, no second lane).
TEST(Automation, DuplicateParamIDIsRejected)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "S0 Gain", 100);
    cmds.addAutomationLane(0, "S0 Other", 100); // same paramID, different name

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    ASSERT_NE(findLane(lanes, "S0 Gain"), nullptr);   // first lane survived
    EXPECT_EQ(findLane(lanes, "S0 Other"), nullptr);  // duplicate was rejected
}

// The legacy duplicate-name guard still applies independent of paramID.
TEST(Automation, DuplicateNameIsRejected)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "CustomLane", 200);
    cmds.addAutomationLane(0, "CustomLane", 201); // same name, different paramID

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    int count = 0;
    for (const auto& l : lanes)
        if (l.name == "CustomLane")
            ++count;
    EXPECT_EQ(count, 1);
    // The surviving lane keeps the first paramID.
    const auto* lane = findLane(lanes, "CustomLane");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramID, 200);
}

// Backward compatibility: calling the legacy 2-arg form (or omitting paramID)
// creates an unbound lane (paramID 0). Such lanes are harmless — the runtime
// ignores paramID 0 — but they remain supported so existing callers/tests
// that don't pass a paramID keep working.
TEST(Automation, OmittedParamIDCreatesUnboundLane)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "LegacyLane");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    const auto* lane = findLane(lanes, "LegacyLane");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramID, 0);
}

// A paramID of 0 means "unbound", so multiple unbound lanes are allowed (the
// duplicate-paramID guard must not fire for 0). Two unbound lanes with distinct
// names both survive.
TEST(Automation, MultipleUnboundLanesAllowed)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "UnboundA");
    cmds.addAutomationLane(0, "UnboundB");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    EXPECT_NE(findLane(lanes, "UnboundA"), nullptr);
    EXPECT_NE(findLane(lanes, "UnboundB"), nullptr);
}
