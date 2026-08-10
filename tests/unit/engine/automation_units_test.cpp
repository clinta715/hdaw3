// Unit-A0 regression tests for the beats<->seconds convention on the
// automation-lane and clip-gain-envelope point paths.
//
// Contract (docs/architecture.md "Time-unit convention"): RPC/command/MCP
// boundaries speak beats; ValueTree storage and the audio processors speak
// seconds. The default project is 120 BPM, so beats * 0.5 = seconds and the
// round trip (convert on write, convert back on read) is exact.
//
// The default "Volume" lane ships with two seed points (0.0 and 16.0 seconds,
// ProjectModel::createTrackAutomationList), so count assertions that must see
// an empty lane create a dedicated lane instead.
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

juce::ValueTree findClipTree(AudioEngine& engine, int clipId)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == clipId)
                return clip;
        }
    }
    return {};
}

juce::ValueTree findAutomationPointAtSeconds(AudioEngine& engine, int trackIndex,
                                             const std::string& laneName, double timeSec)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto autoList = trackList.getChild(trackIndex).getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid()) return {};
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        auto lane = autoList.getChild(i);
        if (lane.getProperty(IDs::name, "").toString().toStdString() != laneName)
            continue;
        auto pointList = lane.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid()) return {};
        for (int p = 0; p < pointList.getNumChildren(); ++p)
        {
            auto pt = pointList.getChild(p);
            if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == timeSec)
                return pt;
        }
        return {};
    }
    return {};
}

} // namespace

// Gate 2: full path. A point written in beats must land in the ValueTree as
// seconds (120 BPM: 4.0 beats -> 2.0 seconds) and read back through the
// ReadModel as beats (2.0 seconds -> 4.0 beats).
TEST(AutomationUnits, BeatsInSecondsStoredBeatsOut)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationPoint(0, "Volume", 4.0, 0.75f);

    auto stored = findAutomationPointAtSeconds(engine, 0, "Volume", 2.0);
    ASSERT_TRUE(stored.isValid());
    EXPECT_DOUBLE_EQ(static_cast<double>(stored.getProperty(IDs::startTime, 0.0)), 2.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(stored.getProperty(IDs::gain, 0.0)), 0.75);

    auto points = engine.getReadModel().getAutomationPoints(0, "Volume");
    bool found = false;
    for (const auto& pt : points)
    {
        if (pt.time == 4.0)
        {
            EXPECT_FLOAT_EQ(pt.value, 0.75f);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Remove matches after identical beats->seconds conversion: the value written
// and the value searched must both be converted, so the exact-match removes the
// point it added.
TEST(AutomationUnits, RemoveMatchesAfterConversion)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "UnitsLane"); // unbound, empty lane
    cmds.addAutomationPoint(0, "UnitsLane", 4.0, 0.75f);
    EXPECT_EQ(engine.getReadModel().getAutomationPoints(0, "UnitsLane").size(), 1u);

    cmds.removeAutomationPoint(0, "UnitsLane", 4.0);
    EXPECT_TRUE(engine.getReadModel().getAutomationPoints(0, "UnitsLane").empty());
}

// setAutomationPointValue converts once and uses the same converted value for
// the match and the write.
TEST(AutomationUnits, SetValueMatchesAfterConversion)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "UnitsLane2");
    cmds.addAutomationPoint(0, "UnitsLane2", 4.0, 0.75f);
    cmds.setAutomationPointValue(0, "UnitsLane2", 4.0, 0.5f);

    auto points = engine.getReadModel().getAutomationPoints(0, "UnitsLane2");
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].time, 4.0);
    EXPECT_FLOAT_EQ(points[0].value, 0.5f);
}

// setClipGainEnvelope takes beats; the clip ValueTree stores clip-relative
// seconds; the ReadModel converts back to beats.
TEST(GainEnvelopeUnits, RoundTripPreservesBeats)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "EnvClip");
    ASSERT_GT(clipId, 0);

    cmds.setClipGainEnvelope(clipId, {{4.0, 0.5}});

    auto clip = findClipTree(engine, clipId);
    ASSERT_TRUE(clip.isValid());
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    ASSERT_TRUE(envelope.isValid());
    ASSERT_EQ(envelope.getNumChildren(), 1);
    EXPECT_DOUBLE_EQ(static_cast<double>(envelope.getChild(0).getProperty(IDs::pointTime, 0.0)), 2.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(envelope.getChild(0).getProperty(IDs::pointGain, 1.0)), 0.5);

    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    ASSERT_EQ(env.size(), 1u);
    EXPECT_DOUBLE_EQ(env[0].time, 4.0);
    EXPECT_DOUBLE_EQ(env[0].gain, 0.5);
}

// addGainEnvelopePoint and moveGainEnvelopePoint take beats and convert on
// write; the ReadModel reports beats.
TEST(GainEnvelopeUnits, AddMoveConvert)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "MoveEnv");
    ASSERT_GT(clipId, 0);

    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);
    cmds.moveGainEnvelopePoint(clipId, 0, 4.0, 0.25);

    auto clip = findClipTree(engine, clipId);
    ASSERT_TRUE(clip.isValid());
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    ASSERT_TRUE(envelope.isValid());
    ASSERT_EQ(envelope.getNumChildren(), 1);
    EXPECT_DOUBLE_EQ(static_cast<double>(envelope.getChild(0).getProperty(IDs::pointTime, 0.0)), 2.0);

    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    ASSERT_EQ(env.size(), 1u);
    EXPECT_DOUBLE_EQ(env[0].time, 4.0);
    EXPECT_DOUBLE_EQ(env[0].gain, 0.25);
}
