// Layer handoff ledger (psy-song-session hybrid workflow; project-native).
// Covers set/clear commands, validation (Gate 9), one-undo-unit semantics,
// and save/load persistence. MODEL-level (ValueTree track properties) — no
// DSP state, so projections under test are the tree + command layer.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <string>

#include "engine/AudioEngine.h"
#include "common/ProjectCommands.h"
#include "model/ProjectModel.h"

namespace {

ProjectCommands::LayerHandoff makeHandoff()
{
    ProjectCommands::LayerHandoff h;
    h.role = "lead";
    h.soundIntent = "acid psy_fm with phaser bite";
    h.patternIntent = "call-response hook by bar 24";
    h.modulation = "{\"target\":\"filter cutoff\",\"recipe\":\"phaseSweep\",\"depth\":\"medium\"}";
    h.verify = "{\"beforeRms\":0.120,\"afterRms\":0.144,\"verifyPart\":\"audible=1;nonClipping=1\",\"warnings\":[]}";
    return h;
}

std::string trackProp(AudioEngine& e, int idx, const juce::Identifier& id)
{
    return e.getProjectModel().getTrackListTree().getChild(idx).getProperty(id, "").toString().toStdString();
}

} // namespace

TEST(LayerHandoff, SetWritesAllPropsAndReadsBack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Lead");
    ASSERT_GE(track, 0);

    auto h = makeHandoff();
    std::string err;
    ASSERT_TRUE(cmds.setLayerHandoff(track, h, &err)) << err;

    EXPECT_EQ(trackProp(engine, track, IDs::layerRole), "lead");
    EXPECT_EQ(trackProp(engine, track, IDs::layerSoundIntent), "acid psy_fm with phaser bite");
    EXPECT_EQ(trackProp(engine, track, IDs::layerPatternIntent), "call-response hook by bar 24");
    EXPECT_EQ(trackProp(engine, track, IDs::layerModulation), h.modulation);
    EXPECT_EQ(trackProp(engine, track, IDs::layerVerify), h.verify);
}

TEST(LayerHandoff, RejectsOutOfRangeAndEmpty)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track");

    std::string err;
    EXPECT_FALSE(cmds.setLayerHandoff(99, makeHandoff(), &err));
    EXPECT_FALSE(err.empty());

    err.clear();
    EXPECT_FALSE(cmds.clearLayerHandoff(99, &err));
    EXPECT_FALSE(err.empty());

    err.clear();
    EXPECT_FALSE(cmds.setLayerHandoff(0, ProjectCommands::LayerHandoff{}, &err));
    EXPECT_FALSE(err.empty());
}

TEST(LayerHandoff, ClearRemovesAllProps)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Lead");
    ASSERT_GE(track, 0);

    std::string err;
    ASSERT_TRUE(cmds.setLayerHandoff(track, makeHandoff(), &err));
    ASSERT_TRUE(cmds.clearLayerHandoff(track, &err));

    const auto tr = engine.getProjectModel().getTrackListTree().getChild(track);
    EXPECT_FALSE(tr.hasProperty(IDs::layerRole));
    EXPECT_FALSE(tr.hasProperty(IDs::layerSoundIntent));
    EXPECT_FALSE(tr.hasProperty(IDs::layerPatternIntent));
    EXPECT_FALSE(tr.hasProperty(IDs::layerModulation));
    EXPECT_FALSE(tr.hasProperty(IDs::layerVerify));
}

TEST(LayerHandoff, UndoCoalescesToOneStep)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Lead");
    ASSERT_GE(track, 0);

    std::string err;
    ASSERT_TRUE(cmds.setLayerHandoff(track, makeHandoff(), &err));

    auto& um = engine.getProjectModel().getUndoManager();
    um.undo(); // ONE undo step must remove the whole handoff (5 properties)
    EXPECT_FALSE(engine.getProjectModel().getTrackListTree().getChild(track)
                     .hasProperty(IDs::layerRole));
    EXPECT_FALSE(engine.getProjectModel().getTrackListTree().getChild(track)
                     .hasProperty(IDs::layerVerify));

    um.redo();
    EXPECT_EQ(trackProp(engine, track, IDs::layerRole), "lead");
    EXPECT_EQ(trackProp(engine, track, IDs::layerModulation), makeHandoff().modulation);
}

TEST(LayerHandoff, SurvivesSaveLoad)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Lead");
    ASSERT_GE(track, 0);
    auto h = makeHandoff();
    std::string err;
    ASSERT_TRUE(cmds.setLayerHandoff(track, h, &err));

    const auto file = juce::File::getSpecialLocation(juce::File::SpecialLocationType::tempDirectory)
                          .getNonexistentChildFile("layer_handoff_test", ".hdaw", false);
    ASSERT_TRUE(cmds.saveProject(file.getFullPathName().toStdString()));

    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(engine2.getProjectCommands().loadProject(file.getFullPathName().toStdString()));
    file.deleteFile();

    EXPECT_EQ(trackProp(engine2, 0, IDs::layerRole), "lead");
    EXPECT_EQ(trackProp(engine2, 0, IDs::layerSoundIntent), "acid psy_fm with phaser bite");
    EXPECT_EQ(trackProp(engine2, 0, IDs::layerPatternIntent), "call-response hook by bar 24");
    EXPECT_EQ(trackProp(engine2, 0, IDs::layerModulation), h.modulation);
    EXPECT_EQ(trackProp(engine2, 0, IDs::layerVerify), h.verify);
    EXPECT_GE(engine2.getProjectModel().getTrackListTree().getNumChildren(), 1);
}