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

// setFaderAuthoritative disables ALL Volume automation on a track so the fader
// is authoritative again in playback/export. Non-destructive: only the
// enabled flag toggles; the lane and its points are kept. Re-enabling
// (authoritative=false) restores Volume automation.
TEST(Automation, SetFaderAuthoritativeDisablesVolumeLanes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // The default project ships a "Volume" (paramID 1) lane on every track
    // starting DISABLED, and addAutomationLane is a no-op on a duplicate name,
    // so enable it explicitly; add a distinct non-volume lane too.
    cmds.setAutomationEnabled(0, "Volume", true);
    cmds.addAutomationLane(0, "S0 Cutoff", 105);
    cmds.addAutomationPoint(0, "Volume", 0.0, 0.5);

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    const auto* vol = findLane(lanes, "Volume");
    const auto* cutoff = findLane(lanes, "S0 Cutoff");
    ASSERT_NE(vol, nullptr);
    ASSERT_NE(cutoff, nullptr);
    EXPECT_EQ(vol->paramID, 1);
    EXPECT_TRUE(vol->enabled);
    EXPECT_TRUE(cutoff->enabled);

    // Fader authoritative: Volume automation off, non-volume lane untouched.
    cmds.setFaderAuthoritative(0, true);
    lanes = engine.getReadModel().getAutomationLanes(0);
    vol = findLane(lanes, "Volume");
    cutoff = findLane(lanes, "S0 Cutoff");
    ASSERT_NE(vol, nullptr);
    ASSERT_NE(cutoff, nullptr);
    EXPECT_FALSE(vol->enabled);
    EXPECT_TRUE(cutoff->enabled);

    // Non-destructive: the Volume lane's point list still exists (points kept).
    {
        auto track = engine.getProjectModel().getTrackListTree().getChild(0);
        auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
        auto volLaneTree = autoList.getChildWithProperty(IDs::name, juce::String("Volume"));
        ASSERT_TRUE(volLaneTree.isValid());
        EXPECT_TRUE(volLaneTree.getChildWithName(IDs::POINT_LIST).isValid());
    }

    // Re-enable Volume automation.
    cmds.setFaderAuthoritative(0, false);
    lanes = engine.getReadModel().getAutomationLanes(0);
    vol = findLane(lanes, "Volume");
    ASSERT_NE(vol, nullptr);
    EXPECT_TRUE(vol->enabled);
}

// trackIndex -1 = project-wide: Volume lanes on ALL tracks get disabled,
// non-volume lanes untouched.
TEST(Automation, SetFaderAuthoritativeProjectWide)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    const int numTracks = engine.getProjectModel().getTrackListTree().getNumChildren();
    ASSERT_GE(numTracks, 1);

    // Enable every track's Volume lane first so the disable is observable
    // (default project lanes start disabled).
    for (int t = 0; t < numTracks; ++t)
        cmds.setAutomationEnabled(t, "Volume", true);

    cmds.setFaderAuthoritative(-1, true);

    for (int t = 0; t < numTracks; ++t)
    {
        auto lanes = engine.getReadModel().getAutomationLanes(t);
        const auto* vol = findLane(lanes, "Volume");
        ASSERT_NE(vol, nullptr);
        EXPECT_FALSE(vol->enabled) << "track " << t << " Volume still enabled";
        // Non-volume lanes untouched (default Pan/Mute start disabled).
        for (const auto& l : lanes)
            if (l.name != "Volume")
                EXPECT_FALSE(l.enabled) << "track " << t << " lane " << l.name
                                        << " unexpectedly enabled";
    }
}

// Out-of-range trackIndex is a no-op: no crash, no lane changes.
TEST(Automation, SetFaderAuthoritativeOutOfRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    auto before = engine.getReadModel().getAutomationLanes(0);

    cmds.setFaderAuthoritative(999, true);
    cmds.setFaderAuthoritative(-2, true);

    auto after = engine.getReadModel().getAutomationLanes(0);
    ASSERT_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(before[i].name, after[i].name);
        EXPECT_EQ(before[i].enabled, after[i].enabled);
    }
}

// addAutomationLane returns true on success and false on collision (duplicate
// name or duplicate paramID). This is the MCP/RPC error-signaling contract.
TEST(Automation, AddLaneParamIdCollisionReturnsError)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Use a fresh track — the default project's track 0 already has Volume/Pan/Mute lanes.
    int trackIdx = cmds.addTrack("Test", -1, -1, 0);
    ASSERT_GE(trackIdx, 0);

    // Every track ships Volume(paramID=1), Pan(2), Mute(3) by default.
    // Use paramIDs outside that range to avoid colliding with built-in lanes.
    bool first = cmds.addAutomationLane(trackIdx, "My Cutoff", 105);
    EXPECT_TRUE(first);

    // Try to add another lane with the same paramID — should fail.
    bool second = cmds.addAutomationLane(trackIdx, "Other Cutoff", 105);
    EXPECT_FALSE(second);

    // Try to add a lane with the same name — should fail.
    bool third = cmds.addAutomationLane(trackIdx, "My Cutoff", 106);
    EXPECT_FALSE(third);

    // A lane with a different name AND different paramID should succeed.
    bool fourth = cmds.addAutomationLane(trackIdx, "My Resonance", 106);
    EXPECT_TRUE(fourth);
}
