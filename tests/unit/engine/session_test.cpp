#include <gtest/gtest.h>
#include <string>
#include "engine/AudioEngine.h"
#include "engine/SessionManager.h"
#include "model/ProjectModel.h"

namespace {
// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Seed exactly the track(s) the test
// addresses and drain the coalesced routing rebuild (lessons 9/10/12; no
// sleeps). Intentional invalid-track/invalid-scene tests stay unseeded.
int seedTrack(AudioEngine& engine, int count = 1)
{
    int idx = -1;
    for (int i = 0; i < count; ++i)
        idx = engine.getProjectCommands().addTrack("Track " + std::to_string(i));
    engine.drainPendingRoutingRebuild();
    return idx;
}
} // namespace

TEST(SessionModel, ClipHasDefaultSceneIndex)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

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
    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

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

    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

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

    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

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

TEST(SessionManager, LaunchSceneStartsClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(seedTrack(engine, 2), 0);  // tracks 0+1 (zero-track default, v0.33+)

    int clip1 = cmds.createSessionClip(0, 0, true);
    int clip2 = cmds.createSessionClip(1, 0, true);
    ASSERT_GT(clip1, 0);
    ASSERT_GT(clip2, 0);

    auto& sm = engine.getSessionManager();
    sm.launchScene(0);

    EXPECT_EQ(sm.getLaunchedScene(), 0);
}

TEST(SessionManager, StopAllClearsLaunchedScene)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

    cmds.createSessionClip(0, 0, true);
    auto& sm = engine.getSessionManager();
    sm.launchScene(0);
    EXPECT_EQ(sm.getLaunchedScene(), 0);

    sm.stopAll();
    EXPECT_EQ(sm.getLaunchedScene(), -1);
}

TEST(SessionManager, SceneSwitchChangesLaunchedScene)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

    cmds.createSessionClip(0, 0, true);
    cmds.createSessionClip(0, 1, true);

    auto& sm = engine.getSessionManager();
    sm.launchScene(0);
    EXPECT_EQ(sm.getLaunchedScene(), 0);

    sm.launchScene(1);
    EXPECT_EQ(sm.getLaunchedScene(), 1);
}

TEST(SessionManager, GetClipStatesReturnsSessionClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(seedTrack(engine), 0);  // track 0 (zero-track default, v0.33+)

    int clip1 = cmds.createSessionClip(0, 0, true);
    int clip2 = cmds.createSessionClip(0, 1, true);
    ASSERT_GT(clip1, 0);
    ASSERT_GT(clip2, 0);
    cmds.addMidiClip(0, 0.0, 4.0, "arrangement"); // not a session clip

    auto& sm = engine.getSessionManager();
    auto states = sm.getClipStates();
    EXPECT_EQ(states.size(), 2u);  // only session clips

    sm.launchScene(0);
    states = sm.getClipStates();
    for (const auto& s : states) {
        if (s.sceneIndex == 0) EXPECT_TRUE(s.isPlaying);
        if (s.sceneIndex == 1) EXPECT_FALSE(s.isPlaying);
    }
}
