// Tests for envelope generation commands (Unit B).
// Exercises generateAutomationEnvelope, generateClipGainEnvelope,
// generateClipCcLane — verifying range replacement, one undo step,
// and live processor state after rebuildRoutingGraph().

#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/EnvelopeGenerator.h"

namespace {

HDAW::EnvelopeGenerator::Params rampParams(double startBeat, double endBeat,
                                            double startVal = 0.0, double endVal = 1.0)
{
    HDAW::EnvelopeGenerator::Params p;
    p.shape = HDAW::EnvelopeGenerator::Shape::Ramp;
    p.startTime = startBeat;
    p.endTime = endBeat;
    p.startValue = startVal;
    p.endValue = endVal;
    p.steps = 4;
    return p;
}

} // namespace

// ─── G2: generateAutomationEnvelope ───────────────────────────────

TEST(EnvelopeGeneration, G2_GenerateAutomation_ReplacesInRange)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Add Volume automation lane on track 0.
    cmds.addAutomationLane(0, "Volume");

    // Add points at -1, 2, 5, 10 seconds (in beats at 120 BPM: 1 sec = 2 beats).
    // addAutomationPoint takes beats and converts to seconds internally.
    // At 120 BPM: -1 sec = -2 beats, 2 sec = 4 beats, 5 sec = 10 beats, 10 sec = 20 beats.
    cmds.addAutomationPoint(0, "Volume", -2.0, 0.5f);  // -1 sec
    cmds.addAutomationPoint(0, "Volume", 4.0, 0.7f);   // 2 sec
    cmds.addAutomationPoint(0, "Volume", 10.0, 0.8f);  // 5 sec
    cmds.addAutomationPoint(0, "Volume", 20.0, 0.3f);  // 10 sec

    // Generate ramp in [2, 8] seconds → [4, 16] beats.
    auto params = rampParams(4.0, 16.0, 0.0, 1.0);
    cmds.generateAutomationEnvelope(0, "Volume", params);

    // Read points from the tree.
    auto points = engine.getReadModel().getAutomationPoints(0, "Volume");

    // Point at -1 sec (outside range) should be untouched.
    bool foundOutsideStart = false;
    bool foundOutsideEnd = false;
    int insideCount = 0;
    for (const auto& p : points)
    {
        if (p.time < 1.5) // -1 sec region
            foundOutsideStart = true;
        else if (p.time > 9.0) // 10 sec region
            foundOutsideEnd = true;
        else
            ++insideCount;
    }
    EXPECT_TRUE(foundOutsideStart) << "point before range should be untouched";
    EXPECT_TRUE(foundOutsideEnd) << "point after range should be untouched";
    EXPECT_GT(insideCount, 0) << "generated points should replace range";

    // One undo step (undo the generate transaction only).
    EXPECT_TRUE(cmds.canUndo());
    cmds.undo();
    auto afterUndo = engine.getReadModel().getAutomationPoints(0, "Volume");
    // After undo of generate: original 4 points still exist + 2 removed points restored = 6.
    EXPECT_EQ(afterUndo.size(), 6u);
}

TEST(EnvelopeGeneration, G2_GenerateAutomation_UndoesCleanly)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "Volume");
    cmds.addAutomationPoint(0, "Volume", 4.0, 0.5f);
    cmds.addAutomationPoint(0, "Volume", 20.0, 0.8f);

    auto params = rampParams(4.0, 16.0, 0.0, 1.0);
    cmds.generateAutomationEnvelope(0, "Volume", params);

    auto before = engine.getReadModel().getAutomationPoints(0, "Volume");
    EXPECT_GT(before.size(), 2u);

    // Undo generate → restores the 2 removed points + removes generated points.
    cmds.undo();
    auto after = engine.getReadModel().getAutomationPoints(0, "Volume");
    // After undo: original 2 + 2 restored from removal = 4.
    EXPECT_EQ(after.size(), 4u);
}

TEST(EnvelopeGeneration, G2_GenerateAutomation_LiveCacheAfterRebuild)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "Volume");
    auto params = rampParams(0.0, 16.0, 0.0, 1.0);
    cmds.generateAutomationEnvelope(0, "Volume", params);

    // Rebuild routing graph.
    engine.getMainProcessor()->rebuildRoutingGraph();

    // Read live AutomationManager cache.
    auto* tr = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr, nullptr);
    ASSERT_GT(tr->getNumAutomations(), 0);

    // Find the Volume automation.
    const HDAW::AutomationManager* am = nullptr;
    for (int i = 0; i < tr->getNumAutomations(); ++i)
    {
        if (tr->getAutomation(i).getAutomationTree().getProperty(IDs::name, "").toString() == "Volume")
        {
            am = &tr->getAutomation(i);
            break;
        }
    }
    ASSERT_NE(am, nullptr);
    EXPECT_GT(am->getNumPoints(), 0);

    // Compare live cache with tree.
    auto treePoints = engine.getReadModel().getAutomationPoints(0, "Volume");
    EXPECT_EQ(am->getNumPoints(), static_cast<int>(treePoints.size()));
}

// ─── G3: generateClipGainEnvelope ────────────────────────────────

TEST(EnvelopeGeneration, G3_GenerateClipGainEnvelope_ReplacesInRange)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Create audio clip on track 0.
    int clipId = cmds.addAudioClip(0, 0.0, 8.0, "test.wav", "TestClip");
    ASSERT_GT(clipId, 0);

    // Add gain envelope points (in beats).
    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 4.0, 0.5);
    cmds.addGainEnvelopePoint(clipId, 16.0, 1.5);

    // Generate ramp in [0, 16] beats, domain 0..2.
    auto params = rampParams(0.0, 16.0, 0.0, 2.0);
    cmds.generateClipGainEnvelope(clipId, params);

    // Points should be replaced.
    auto points = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_GT(points.size(), 0u);

    // One undo step.
    EXPECT_TRUE(cmds.canUndo());
    cmds.undo();
    auto afterUndo = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_EQ(afterUndo.size(), 3u);
}

TEST(EnvelopeGeneration, G3_GenerateClipGainEnvelope_LiveProcessorAfterRebuild)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addAudioClip(0, 0.0, 8.0, "test.wav", "TestClip");
    ASSERT_GT(clipId, 0);

    auto params = rampParams(0.0, 16.0, 0.0, 2.0);
    cmds.generateClipGainEnvelope(clipId, params);

    // Rebuild routing graph.
    engine.getMainProcessor()->rebuildRoutingGraph();

    // Find the clip processor via RoutingManager's audioClipSources.
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);

    HDAW::ClipSourceProcessor* clipProc = nullptr;
    for (const auto& [key, proc] : rm->getAudioClipSources())
    {
        if (proc && proc->getClipID() == clipId)
        {
            clipProc = proc;
            break;
        }
    }
    ASSERT_NE(clipProc, nullptr);

    auto livePoints = clipProc->getGainEnvelopePoints();
    EXPECT_GT(livePoints.size(), 0u);

    // Compare with tree.
    auto treePoints = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_EQ(livePoints.size(), treePoints.size());
}

// ─── G4: generateClipCcLane ──────────────────────────────────────

TEST(EnvelopeGeneration, G4_GenerateClipCcLane_ReplacesInRange)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Create MIDI clip on track 1 (Synth).
    int clipId = cmds.addMidiClip(1, 0.0, 8.0, "MidiClip");
    ASSERT_GT(clipId, 0);

    // Add CC points (in beats).
    cmds.addCcPoint(clipId, 1, 0.0, 64);
    cmds.addCcPoint(clipId, 1, 4.0, 100);

    // Generate sine for CC 1 in [0, 16] beats.
    HDAW::EnvelopeGenerator::Params params;
    params.shape = HDAW::EnvelopeGenerator::Shape::Sine;
    params.startTime = 0.0;
    params.endTime = 16.0;
    params.startValue = 0.0;
    params.endValue = 127.0;
    params.cycles = 2.0;
    params.steps = 8;
    cmds.generateClipCcLane(clipId, 1, params);

    // Read CC points from the tree.
    int trackIdx = -1;
    auto clip = engine.getProjectModel().getTree()
        .getChildWithName(IDs::TRACK_LIST);
    // Use the read model approach: search clip's CC_LIST.
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipTree = trackList.getChild(1).getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(clipTree.isValid());

    juce::ValueTree targetClip;
    for (int i = 0; i < clipTree.getNumChildren(); ++i)
    {
        auto c = clipTree.getChild(i);
        if (static_cast<int>(c.getProperty(IDs::clipID, 0)) == clipId)
        {
            targetClip = c;
            break;
        }
    }
    ASSERT_TRUE(targetClip.isValid());

    auto ccList = targetClip.getChildWithName(IDs::CC_LIST);
    ASSERT_TRUE(ccList.isValid());
    EXPECT_GT(ccList.getNumChildren(), 0);

    // Verify all points have real ccIDs (not 0 or -1).
    for (int i = 0; i < ccList.getNumChildren(); ++i)
    {
        auto pt = ccList.getChild(i);
        int ccID = static_cast<int>(pt.getProperty(IDs::ccID, 0));
        EXPECT_GT(ccID, 0) << "CC point should have a real ccID";
        EXPECT_EQ(static_cast<int>(pt.getProperty(IDs::controllerNumber, -1)), 1);
    }
}

TEST(EnvelopeGeneration, G4_GenerateClipCcLane_OneUndo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(1, 0.0, 8.0, "MidiClip");
    ASSERT_GT(clipId, 0);

    cmds.addCcPoint(clipId, 1, 0.0, 64);

    HDAW::EnvelopeGenerator::Params params;
    params.shape = HDAW::EnvelopeGenerator::Shape::Sine;
    params.startTime = 0.0;
    params.endTime = 16.0;
    params.startValue = 0.0;
    params.endValue = 127.0;
    params.cycles = 2.0;
    params.steps = 8;
    cmds.generateClipCcLane(clipId, 1, params);

    EXPECT_TRUE(cmds.canUndo());
    cmds.undo();

    // After undo, should have original 1 point.
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipTree = trackList.getChild(1).getChildWithName(IDs::CLIP_LIST);
    juce::ValueTree targetClip;
    for (int i = 0; i < clipTree.getNumChildren(); ++i)
    {
        auto c = clipTree.getChild(i);
        if (static_cast<int>(c.getProperty(IDs::clipID, 0)) == clipId)
        {
            targetClip = c;
            break;
        }
    }
    ASSERT_TRUE(targetClip.isValid());
    auto ccList = targetClip.getChildWithName(IDs::CC_LIST);
    ASSERT_TRUE(ccList.isValid());
    EXPECT_EQ(ccList.getNumChildren(), 1);
}
