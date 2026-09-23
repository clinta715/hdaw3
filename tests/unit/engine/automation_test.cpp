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
#include "engine/AutomationPreset.h"
#include <string>
#include <utility>
#include <vector>

namespace {
// Zero-track default contract (v0.33+): createDefaultProject() ships an
// empty TRACK_LIST — tests own their setup. Seed exactly the track(s) the
// test addresses and drain the coalesced routing rebuild so lane/track reads
// are deterministic (lessons 9/10/12; no sleeps).
int seedTrack(AudioEngine& engine, int count = 1)
{
    int idx = -1;
    for (int i = 0; i < count; ++i)
        idx = engine.getProjectCommands().addTrack("Track " + std::to_string(i));
    engine.drainPendingRoutingRebuild();
    return idx;
}

// Find a lane snapshot by name; returns nullptr if absent.
const AutomationLaneSnapshot* findLane(const std::vector<AutomationLaneSnapshot>& lanes,
                                       const std::string& name)
{
    for (const auto& l : lanes)
        if (l.name == name)
            return &l;
    return nullptr;
}

// A lane's (startTime, gain) points read straight from the ValueTree — the
// durable source a rebuild restores, not a read-model projection.
std::vector<std::pair<double, double>> lanePoints(AudioEngine& engine, int trackIndex,
                                                  const std::string& laneName)
{
    std::vector<std::pair<double, double>> pts;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return pts;
    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid()) return pts;
    auto lane = autoList.getChildWithProperty(IDs::name, juce::String(laneName));
    if (!lane.isValid()) return pts;
    auto pointList = lane.getChildWithName(IDs::POINT_LIST);
    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        pts.emplace_back(static_cast<double>(pt.getProperty(IDs::startTime, 0.0)),
                         static_cast<double>(pt.getProperty(IDs::gain, 0.0)));
    }
    return pts;
}

// How many lanes the track has bound to `paramID` (tree truth, not a snapshot).
int countLanesBoundTo(AudioEngine& engine, int trackIndex, int paramID)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return 0;
    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid()) return 0;
    int n = 0;
    for (int i = 0; i < autoList.getNumChildren(); ++i)
        if (static_cast<int>(autoList.getChild(i).getProperty(IDs::paramID, 0)) == paramID)
            ++n;
    return n;
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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

    // Every seeded track ships a "Volume" (paramID 1) lane starting DISABLED
    // (createTrackAutomationList), and addAutomationLane is a no-op on a
    // duplicate name, so enable it explicitly; add a distinct non-volume lane
    // too.
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
    // Two tracks so the project-wide (-1) path is proven across multiple
    // tracks, as it was when the default project shipped three.
    ASSERT_GE(seedTrack(engine, 2), 0);

    const int numTracks = engine.getProjectModel().getTrackListTree().getNumChildren();
    ASSERT_GE(numTracks, 1);

    // Enable every track's Volume lane first so the disable is observable
    // (lanes start disabled).
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
    // Seed the track whose lanes the before/after comparison reads, so the
    // no-op proof is over a real lane set, not two empty vectors.
    ASSERT_GE(seedTrack(engine), 0);

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

// ── Lane upsert by paramID (the post-arrangement pass's re-run path) ────────
// G1: replace=true takes ownership of the lane bound to paramID. The lane is
// RENAMED IN PLACE, so points written outside the pass's windows survive — a
// delete+recreate would silently drop them.
TEST(Automation, G1_ReplaceRenamesLaneBoundToParamIdAndKeepsPoints)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_TRUE(cmds.addAutomationLane(0, "Old", 139));
    cmds.addAutomationPoint(0, "Old", 4.0, 0.25f);
    cmds.addAutomationPoint(0, "Old", 8.0, 0.75f);
    const auto before = lanePoints(engine, 0, "Old");
    ASSERT_EQ(before.size(), 2u);

    EXPECT_TRUE(cmds.addAutomationLane(0, "DubThrow", 139, /*replace*/ true));

    // Tree truth: exactly one lane drives 139, it carries the new name, and
    // it is the SAME lane as before (same points, same values).
    EXPECT_EQ(countLanesBoundTo(engine, 0, 139), 1);
    const auto after = lanePoints(engine, 0, "DubThrow");
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(after[i].first, before[i].first);
        EXPECT_DOUBLE_EQ(after[i].second, before[i].second);
    }
    EXPECT_TRUE(lanePoints(engine, 0, "Old").empty());   // the old name is gone

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    EXPECT_EQ(findLane(lanes, "Old"), nullptr);
    const auto* lane = findLane(lanes, "DubThrow");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramID, 139);

    // Re-running the same upsert is the idempotent no-op it must be.
    EXPECT_TRUE(cmds.addAutomationLane(0, "DubThrow", 139, true));
    EXPECT_EQ(countLanesBoundTo(engine, 0, 139), 1);
    EXPECT_EQ(lanePoints(engine, 0, "DubThrow").size(), 2u);
}

// G2: the default (no replace / replace=false) is unchanged — the conflict
// guard still fires, nothing moves — while the same-name+same-paramID create
// stays the idempotent true.
TEST(Automation, G2_ReplaceOffKeepsConflictGuard)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_TRUE(cmds.addAutomationLane(0, "Old", 139));
    cmds.addAutomationPoint(0, "Old", 4.0, 0.25f);

    EXPECT_FALSE(cmds.addAutomationLane(0, "DubThrow", 139));         // legacy form
    EXPECT_FALSE(cmds.addAutomationLane(0, "DubThrow", 139, false));  // explicit false

    // Changed nothing (tree truth): the lane is still "Old"/139 with its point.
    EXPECT_EQ(countLanesBoundTo(engine, 0, 139), 1);
    EXPECT_EQ(lanePoints(engine, 0, "Old").size(), 1u);
    EXPECT_TRUE(lanePoints(engine, 0, "DubThrow").empty());

    // Idempotent create is still true (same name, same paramID, twice).
    EXPECT_TRUE(cmds.addAutomationLane(0, "X", 7));
    EXPECT_TRUE(cmds.addAutomationLane(0, "X", 7));
    EXPECT_EQ(countLanesBoundTo(engine, 0, 7), 1);
    EXPECT_FALSE(cmds.addAutomationLane(0, "X", 8, true));  // name already owns paramID 7
    EXPECT_EQ(countLanesBoundTo(engine, 0, 7), 1);
    EXPECT_EQ(countLanesBoundTo(engine, 0, 8), 0);
}

// G2 (frozen contract clause): replace=true with paramID 0 means "unbound" —
// there is no binding to take over, so it falls back to the create path
// exactly as before (two unbound lanes with distinct names are allowed and
// no existing lane is renamed).
TEST(Automation, G2b_ReplaceWithZeroParamIdFallsBackToCreate)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    EXPECT_TRUE(cmds.addAutomationLane(0, "U1", 0, true));
    EXPECT_TRUE(cmds.addAutomationLane(0, "U2", 0, true));   // a second unbound lane
    EXPECT_TRUE(cmds.addAutomationLane(0, "U2", 0, true));   // idempotent on the same name

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    const auto* u1 = findLane(lanes, "U1");
    const auto* u2 = findLane(lanes, "U2");
    ASSERT_NE(u1, nullptr);
    ASSERT_NE(u2, nullptr);
    EXPECT_EQ(u1->paramID, 0);
    EXPECT_EQ(u2->paramID, 0);
}

// G4: replace=true must never steal a name that a DIFFERENT paramID owns, and
// must leave the tree untouched when it refuses.
TEST(Automation, G4_ReplaceDoesNotStealNameBoundToOtherParamId)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_TRUE(cmds.addAutomationLane(0, "Ride", 200));
    ASSERT_TRUE(cmds.addAutomationLane(0, "Old", 139));

    EXPECT_FALSE(cmds.addAutomationLane(0, "Ride", 139, true));

    // Nothing changed: "Ride" still drives 200, "Old" still drives 139.
    EXPECT_EQ(countLanesBoundTo(engine, 0, 200), 1);
    EXPECT_EQ(countLanesBoundTo(engine, 0, 139), 1);
    auto lanes = engine.getReadModel().getAutomationLanes(0);
    const auto* ride = findLane(lanes, "Ride");
    const auto* old = findLane(lanes, "Old");
    ASSERT_NE(ride, nullptr);
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(ride->paramID, 200);
    EXPECT_EQ(old->paramID, 139);
}

// G3: the two-step post-arrangement pass — add_automation_lane(replace=true)
// then automation_preset{clear:true} with a fixed seed — is re-runnable: the
// second identical run produces the same point count AND values.
TEST(Automation, G3_PostPassRerunIsIdempotent)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    HDAW::AutomationPreset::PresetWindow w;
    w.start = 0.0;
    w.end = 16.0;
    w.preset = HDAW::AutomationPreset::Preset::Pump;
    const std::vector<HDAW::AutomationPreset::PresetWindow> windows{ w };

    const auto runPass = [&]() -> std::vector<std::pair<double, double>>
    {
        EXPECT_TRUE(cmds.addAutomationLane(0, "DubThrow", 139, /*replace*/ true));
        int added = -1;
        const std::string err =
            cmds.applyAutomationPreset(0, "DubThrow", windows, /*clear*/ true, 12345, &added);
        EXPECT_TRUE(err.empty()) << err;
        EXPECT_GT(added, 0);
        return lanePoints(engine, 0, "DubThrow");
    };

    const auto first = runPass();
    ASSERT_GT(first.size(), 0u);
    const auto second = runPass();

    // Second run: same lane, same binding, same point count, same values.
    EXPECT_EQ(countLanesBoundTo(engine, 0, 139), 1);
    ASSERT_EQ(second.size(), first.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(second[i].first, first[i].first) << "point " << i << " time";
        EXPECT_DOUBLE_EQ(second[i].second, first[i].second) << "point " << i << " value";
    }
}
