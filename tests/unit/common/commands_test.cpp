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
    EXPECT_GT(t.currentTimeSeconds, 0.0);
    cmds.rewind();
    t = engine.getReadModel().getTransport();
    EXPECT_DOUBLE_EQ(t.currentTimeSeconds, 0.0);
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
        if (c.name == "DupClip" || c.name == "DupClip copy")
            ++count;
    EXPECT_EQ(count, 2);
}

// duplicateClipTo combines duplicate + position into one call so the frontend
// can place a ctrl-drag copy in a single round trip. Verifies direct placement
// at the requested position/track (no follow-up moveClipWithOverlap needed).
TEST(Commands, DuplicateClipToPlacesAtTarget)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Two tracks so cross-track placement is exercised.
    cmds.addTrack("T2");
    const double srcStart = 0.0;
    const double duration = 4.0;
    int clipId = cmds.addMidiClip(0, srcStart, duration, "Orig");
    EXPECT_GT(clipId, 0);

    const double targetStart = 8.5;
    const int targetTrack = 1;
    int newId = cmds.duplicateClipTo(clipId, targetStart, targetTrack);
    EXPECT_GT(newId, 0);
    EXPECT_NE(newId, clipId);

    auto dup = engine.getReadModel().getClip(newId);
    EXPECT_EQ(dup.clipId, newId);
    EXPECT_EQ(dup.trackIndex, targetTrack);
    EXPECT_DOUBLE_EQ(dup.startBeat, targetStart);
    EXPECT_DOUBLE_EQ(dup.durationBeats, duration);
    EXPECT_EQ(dup.name, "Orig copy");
    // The source clip must be untouched.
    auto orig = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(orig.trackIndex, 0);
    EXPECT_DOUBLE_EQ(orig.startBeat, srcStart);
}

// duplicateClipTo on an invalid clip id / track returns -1 (no throw).
TEST(Commands, DuplicateClipToInvalidReturnsNegative)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    EXPECT_LT(cmds.duplicateClipTo(999999, 0.0, 0), 0);
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "X");
    EXPECT_GT(clipId, 0);
    EXPECT_LT(cmds.duplicateClipTo(clipId, 0.0, 999), 0);
}

TEST(Commands, ReorderFxSlots)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Add two internal FX slots (EQ=0, Compressor=1)
    cmds.addFxSlot(0, 0);  // EQ
    cmds.addFxSlot(0, 1);  // Compressor
    // Reorder: swap them
    cmds.reorderFxSlots(0, 0, 1);
    // No crash = pass. ReadModel doesn't expose FX chain ordering.
    // Verify reorder on invalid indices doesn't crash:
    cmds.reorderFxSlots(0, -1, 0);
    cmds.reorderFxSlots(0, 0, 99);
    cmds.reorderFxSlots(-1, 0, 0);
    cmds.reorderFxSlots(0, 1, 1);  // no-op, same index
}

TEST(Commands, AddRemoveAutomationLane)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addAutomationLane(0, "CustomLane");
    cmds.removeAutomationLane(0, "CustomLane");
    // Removing non-existent lane should not crash:
    cmds.removeAutomationLane(0, "NonExistentLane");
    cmds.removeAutomationLane(-1, "Any");
}

TEST(Commands, SwitchClipTake)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getAudioGraphCommands();
    // switchClipTake on a non-existent clip should not crash
    cmds.switchClipTake(9999);
    // switchClipTake on a MIDI clip (no source file) should not crash
    int clipId = engine.getProjectCommands().addMidiClip(0, 0.0, 4.0, "TakeTest");
    EXPECT_GT(clipId, 0);
    cmds.switchClipTake(clipId);
}

TEST(Commands, AddRemoveMarker)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int idx = cmds.addMarker("TestMarker", 5.0);
    EXPECT_GE(idx, 0);
    auto markers = engine.getReadModel().getMarkers();
    EXPECT_FALSE(markers.empty());
    bool found = false;
    for (const auto& m : markers)
    {
        if (m.name == "TestMarker")
        {
            found = true;
            EXPECT_DOUBLE_EQ(m.time, 5.0);
            break;
        }
    }
    EXPECT_TRUE(found);
    cmds.removeMarker(idx);
    markers = engine.getReadModel().getMarkers();
    for (const auto& m : markers)
        EXPECT_NE(m.name, "TestMarker");
}

TEST(Commands, SetMarkerName)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int idx = cmds.addMarker("RenameMe", 2.0);
    cmds.setMarkerName(idx, "Renamed");
    auto markers = engine.getReadModel().getMarkers();
    for (const auto& m : markers)
    {
        if (m.index == idx)
        {
            EXPECT_EQ(m.name, "Renamed");
            break;
        }
    }
}

TEST(Commands, ReadModelExtensions)
{
    AudioEngine engine;
    engine.initialize();
    auto& rm = engine.getReadModel();
    auto& cmds = engine.getProjectCommands();

    // FX Slots
    cmds.addFxSlot(0, 0);  // EQ
    auto fxSlots = rm.getFxSlots(0);
    EXPECT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "eq");
    EXPECT_FALSE(fxSlots[0].bypassed);
    cmds.removeFxSlot(0, 0);

    // Automation Lanes
    cmds.addAutomationLane(0, "VolLane");
    auto lanes = rm.getAutomationLanes(0);
    EXPECT_FALSE(lanes.empty());
    bool laneFound = false;
    for (const auto& l : lanes)
    {
        if (l.name == "VolLane")
        {
            laneFound = true;
            EXPECT_TRUE(l.enabled);
            break;
        }
    }
    EXPECT_TRUE(laneFound);
    cmds.removeAutomationLane(0, "VolLane");

    // isDirty
    EXPECT_TRUE(rm.isDirty());

    // Markers
    auto markers = rm.getMarkers();
    EXPECT_TRUE(markers.empty());
    cmds.addMarker("ReadModelMarker", 3.0);
    markers = rm.getMarkers();
    EXPECT_EQ(markers.size(), 1u);
}

TEST(Commands, SetTimeSignature)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTimeSignature(3, 8);
    auto transport = engine.getProjectModel().getTransportTree();
    EXPECT_EQ(static_cast<int>(transport.getProperty(IDs::timeSigNumerator, 0)), 3);
    EXPECT_EQ(static_cast<int>(transport.getProperty(IDs::timeSigDenominator, 0)), 8);
}

TEST(Commands, DuplicateTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int before = engine.getReadModel().getTrackCount();
    int newIdx = cmds.duplicateTrack(0);
    EXPECT_EQ(engine.getReadModel().getTrackCount(), before + 1);
    EXPECT_EQ(newIdx, before);
}

TEST(Commands, SetAutomationPointValue)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addAutomationLane(0, "VolLane");
    cmds.addAutomationPoint(0, "VolLane", 4.0, 0.75f);
    cmds.setAutomationPointValue(0, "VolLane", 4.0, 0.5f);
    auto points = engine.getReadModel().getAutomationPoints(0, "VolLane");
    bool found = false;
    for (const auto& pt : points)
    {
        if (std::abs(pt.time - 4.0) < 0.001)
        {
            EXPECT_FLOAT_EQ(pt.value, 0.5f);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(Commands, SetFxSlotPlugin)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, 0);  // EQ slot
    cmds.setFxSlotPlugin(0, 0, "plugin", "test.plugin", "VST3", "/path/test.vst3");
    auto fxSlots = engine.getReadModel().getFxSlots(0);
    ASSERT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "plugin");
}

TEST(Commands, AddCcPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 8.0, "CC Test");
    cmds.addCcPoint(clipId, 1, 2.0, 64);
    // Verify through the project model directly
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(clipList.isValid());
    ASSERT_GE(clipList.getNumChildren(), 1);
    auto clip = clipList.getChild(0);
    auto ccList = clip.getChildWithName(IDs::CC_LIST);
    ASSERT_TRUE(ccList.isValid());
    EXPECT_EQ(ccList.getNumChildren(), 1);
    EXPECT_EQ(static_cast<int>(ccList.getChild(0).getProperty(IDs::controllerNumber)), 1);
    EXPECT_EQ(static_cast<int>(ccList.getChild(0).getProperty(IDs::value)), 64);
}
