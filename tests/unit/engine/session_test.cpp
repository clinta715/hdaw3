#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

TEST(SessionModel, ClipHasDefaultSceneIndex)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "test");
    ASSERT_GT(clipId, 0);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, -1);
}

TEST(SessionModel, DefaultProjectHasSessionState)
{
    AudioEngine engine;
    engine.initialize();

    auto snap = engine.getReadModel().snapshot();
    EXPECT_EQ(snap.launchedScene, -1);
    EXPECT_EQ(snap.sceneCount, 8);
}

TEST(SessionModel, SetClipSceneUpdatesSnapshot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "test");
    ASSERT_GT(clipId, 0);

    cmds.setClipScene(clipId, 3);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, 3);
}

TEST(SessionModel, CreateSessionClipReturnsValidId)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.createSessionClip(0, 2, true);
    ASSERT_GT(clipId, 0);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, 2);
    EXPECT_TRUE(clip.isMidi);
    EXPECT_TRUE(clip.looping);
}

TEST(SessionModel, SetClipSceneToArrangement)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.createSessionClip(0, 3, true);
    ASSERT_GT(clipId, 0);

    auto clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, 3);

    // Move back to arrangement
    cmds.setClipScene(clipId, -1);
    clip = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(clip.sceneIndex, -1);
}

TEST(SessionModel, SetClipSceneNonexistentClipIsNoop)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Should not crash
    cmds.setClipScene(99999, 5);
}

TEST(SessionModel, CreateSessionClipInvalidTrackReturnsNegOne)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.createSessionClip(-1, 0, true);
    EXPECT_EQ(clipId, -1);

    clipId = cmds.createSessionClip(999, 0, true);
    EXPECT_EQ(clipId, -1);
}

TEST(SessionModel, CreateSessionClipInvalidSceneReturnsNegOne)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.createSessionClip(0, -1, true);
    EXPECT_EQ(clipId, -1);
}
