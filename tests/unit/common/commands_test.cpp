#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(Commands, AddRemoveTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int initial = engine.getReadModel().getTrackCount();
    int idx = cmds.addTrack("Test");
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initial + 1);
    cmds.removeTrack(idx);
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initial);
}

TEST(Commands, TransportPlayStop)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();
    cmds.play();
    EXPECT_TRUE(engine.getReadModel().getTransport().isPlaying);
    cmds.stop();
    EXPECT_FALSE(engine.getReadModel().getTransport().isPlaying);
}

TEST(Commands, TransportPause)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();
    cmds.play();
    EXPECT_TRUE(engine.getReadModel().getTransport().isPlaying);
    cmds.pause();
    EXPECT_FALSE(engine.getReadModel().getTransport().isPlaying);
}

TEST(Commands, TransportRewind)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();
    cmds.seekToSeconds(5.0);
    auto t = engine.getReadModel().getTransport();
    EXPECT_GT(t.currentSample, 0.0);
    cmds.rewind();
    t = engine.getReadModel().getTransport();
    EXPECT_DOUBLE_EQ(t.currentSample, 0.0);
}

TEST(Commands, ToggleLoop)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();
    EXPECT_FALSE(engine.getReadModel().getTransport().isLooping);
    cmds.toggleLoop();
    EXPECT_TRUE(engine.getReadModel().getTransport().isLooping);
    cmds.toggleLoop();
    EXPECT_FALSE(engine.getReadModel().getTransport().isLooping);
}

TEST(Commands, SetTrackVolume)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTrackVolume(0, 0.5f);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_DOUBLE_EQ(track.volume, 0.5);
}

TEST(Commands, SetTrackPan)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTrackPan(0, 0.25f);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_DOUBLE_EQ(track.pan, 0.25);
}

TEST(Commands, SetTrackMuted)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTrackMuted(0, true);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_TRUE(track.muted);
    cmds.setTrackMuted(0, false);
    track = engine.getReadModel().getTrack(0);
    EXPECT_FALSE(track.muted);
}

TEST(Commands, SetTrackName)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTrackName(0, "MyTrack");
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_EQ(track.name, "MyTrack");
}

TEST(Commands, AddMidiClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "TestClip");
    EXPECT_GT(clipId, 0);
    auto snap = engine.getReadModel().snapshot();
    bool found = false;
    for (const auto& clip : snap.clips)
    {
        if (clip.clipId == clipId)
        {
            found = true;
            EXPECT_EQ(clip.trackIndex, 0);
            EXPECT_DOUBLE_EQ(clip.durationBeats, 4.0);
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(Commands, RemoveClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "ToRemove");
    EXPECT_GT(clipId, 0);
    cmds.removeClip(clipId);
    auto snap = engine.getReadModel().snapshot();
    for (const auto& clip : snap.clips)
        EXPECT_NE(clip.clipId, clipId);
}

TEST(Commands, AddNote)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "NoteClip");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    EXPECT_GT(noteId, 0);
    auto notes = engine.getReadModel().getNotes(clipId);
    EXPECT_FALSE(notes.empty());
    bool found = false;
    for (const auto& n : notes)
    {
        if (n.noteId == noteId)
        {
            found = true;
            EXPECT_EQ(n.pitch, 60);
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(Commands, RemoveNote)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "NoteClip");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    EXPECT_GT(noteId, 0);
    cmds.removeNote(noteId);
    auto notes = engine.getReadModel().getNotes(clipId);
    for (const auto& n : notes)
        EXPECT_NE(n.noteId, noteId);
}

TEST(Commands, UndoRedo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int initial = engine.getReadModel().getTrackCount();
    cmds.addTrack("UndoTest");
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initial + 1);
    cmds.undo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initial);
    cmds.redo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initial + 1);
}

TEST(Commands, SetTempo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    EXPECT_DOUBLE_EQ(engine.getReadModel().getTransport().bpm, 140.0);
}

TEST(Commands, SetLoopBounds)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setLoopStart(2.0);
    cmds.setLoopEnd(8.0);
    auto t = engine.getReadModel().getTransport();
    EXPECT_DOUBLE_EQ(t.loopStart, 2.0);
    EXPECT_DOUBLE_EQ(t.loopEnd, 8.0);
}

TEST(Commands, AudioGraphCommands)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getAudioGraphCommands();
    // rebuildRoutingGraph should not crash
    cmds.rebuildRoutingGraph();
    cmds.rebuildTrackFX(0);
    cmds.rebuildAutomationCache(0);
    cmds.rebuildModulation(0);
}

TEST(Commands, DuplicateClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "DupClip");
    EXPECT_GT(clipId, 0);
    int newId = cmds.duplicateClip(clipId);
    EXPECT_GT(newId, 0);
    EXPECT_NE(clipId, newId);
    auto snap = engine.getReadModel().snapshot();
    int count = 0;
    for (const auto& c : snap.clips)
        if (c.name == "DupClip")
            ++count;
    EXPECT_EQ(count, 2);
}
