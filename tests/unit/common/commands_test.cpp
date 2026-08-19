#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/RoutingManager.h"
#include "engine/MidiClipProcessor.h"

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

TEST(Commands, PlayAfterAutoStopRestartsPlayback)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();
    auto& tm = engine.getTransportManager();

    cmds.play();
    ASSERT_TRUE(tm.isPlayingNow());

    // Simulate the audio thread reaching the project end: auto-stop fires on
    // the audio thread (isPlaying=false + flag) while the ValueTree still
    // says playing — the engine's 50 ms timer hasn't synced it yet.
    tm.setProjectEndSample(1000);
    tm.setCurrentSample(999);
    tm.advance(512); // crosses project end → auto-stop
    ASSERT_FALSE(tm.isPlayingNow());
    ASSERT_TRUE(engine.getReadModel().getTransport().isPlaying); // tree stale

    // User presses Play inside that window — must start playback, not be
    // swallowed by the no-op setProperty / stale auto-stop.
    cmds.play();

    EXPECT_TRUE(tm.isPlayingNow());
    EXPECT_EQ(tm.getCurrentSample(), 0);
    EXPECT_FALSE(tm.consumeAutoStopRequested());
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

// Regression: moving (or duplicating) a clip to a position that FULLY COVERS
// another clip removes the covered clip. The overwrite rule is that a fully
// shadowed clip is discarded so parts never overlap — the replacement clip wins.
// Partial overlaps (trim/split) are handled by the neighbouring cases and are
// untouched here.
TEST(Commands, MoveFullyCoveringReplacesCoveredClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int origId = cmds.addMidiClip(0, 0.0, 4.0, "Orig");   // [0, 4]
    EXPECT_GT(origId, 0);
    // Give Orig a real note so we can distinguish its data from nothing.
    cmds.addNote(origId, 60, 100, 0.0, 1.0);
    int otherId = cmds.addMidiClip(0, 20.0, 8.0, "Other"); // elsewhere
    EXPECT_GT(otherId, 0);

    // Move the 8-beat clip to start 0 → it fully covers Orig ([0,8] ⊇ [0,4]).
    cmds.moveClipWithOverlap(otherId, 0, 0.0);

    // Orig must be gone — no clip with origId remains in the snapshot.
    bool origGone = true;
    for (const auto& clip : engine.getReadModel().snapshot().clips)
    {
        if (clip.clipId == origId) { origGone = false; break; }
    }
    EXPECT_TRUE(origGone) << "fully-covered clip was NOT removed (overwrite rule)";

    // Other wins: placed at [0, 8], still present.
    auto other = engine.getReadModel().getClip(otherId);
    EXPECT_DOUBLE_EQ(other.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(other.durationBeats, 8.0);
}

// Regression: the user's workaround (move a replacement overlay away, delete the
// covered original, move it back) must keep the surviving MIDI clip wired into
// the audio graph with its notes intact — this is the "no silence" contract.
TEST(Commands, OverlayMoveBackKeepsReplacementAudible)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Add a second track to move the replacement away and back. Capture the
    // returned index; the default project already ships track 0 and 1, so the
    // new track appends rather than landing at a fixed index.
    int awayTrack = cmds.addTrack("T2");
    ASSERT_GE(awayTrack, 0);

    // Track 0: place A with a note.
    int aId = cmds.addMidiClip(0, 0.0, 4.0, "A");          // [0, 4]
    ASSERT_GT(aId, 0);
    cmds.addNote(aId, 60, 100, 0.0, 1.0);

    // Simulate placing a replacement clip B at the SAME span, then moving it
    // into place → Case 1 fires and removes A.
    int bId = cmds.addMidiClip(0, 0.0, 4.0, "B");          // [0, 4] same span
    ASSERT_GT(bId, 0);
    cmds.addNote(bId, 62, 100, 0.0, 1.0);
    cmds.moveClipWithOverlap(bId, 0, 0.0);

    bool aGone = true;
    for (const auto& clip : engine.getReadModel().snapshot().clips)
    {
        if (clip.clipId == aId) { aGone = false; break; }
    }
    EXPECT_TRUE(aGone) << "A (covered) should have been removed by Case 1";
    ASSERT_GT(engine.getReadModel().getClip(bId).clipId, 0) << "B must survive";

    // Mirror the user's workaround: move B to the other track, delete the
    // (now-already-gone) original A as a no-op, move B back to track 0.
    cmds.moveClipWithOverlap(bId, awayTrack, 0.0);
    cmds.removeClip(aId);
    cmds.moveClipWithOverlap(bId, 0, 0.0);

    // No message-loop in the gtest, so the coalesced async rebuild never runs
    // on its own — run it explicitly to mirror the production message loop.
    engine.getMainProcessor()->rebuildRoutingGraph();

    // B must be wired into the live routing graph with its note intact.
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    bool bWired = false;
    HDAW::MidiClipProcessor* bProc = nullptr;
    for (const auto& kv : rm->getMidiClipSources())
    {
        if (static_cast<int>(kv.second->getClipTree().getProperty(IDs::clipID, -1)) == bId)
        {
            bWired = true;
            bProc = kv.second;
            break;
        }
    }
    ASSERT_TRUE(bWired) << "replacement clip B missing from the routing graph";
    auto noteList = bProc->getClipTree().getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_TRUE(noteList.isValid());
    EXPECT_GT(noteList.getNumChildren(), 0) << "B's note did not survive the move";

    // Only B remains on track 0 at [0,4] — no overlap left behind.
    int track0Clips = 0;
    int track0bId = -1;
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipList0 = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    for (int c = 0; c < clipList0.getNumChildren(); ++c)
    {
        ++track0Clips;
        int cid = static_cast<int>(clipList0.getChild(c).getProperty(IDs::clipID, -1));
        if (cid == bId) track0bId = cid;
    }
    EXPECT_EQ(track0Clips, 1) << "track 0 should hold exactly the replacement clip";
    EXPECT_EQ(track0bId, bId);
    auto bFinal = engine.getReadModel().getClip(bId);
    EXPECT_DOUBLE_EQ(bFinal.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(bFinal.durationBeats, 4.0);
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
    EXPECT_EQ(fxSlots[0].pluginFormat, "VST3");
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

TEST(Commands, SetAndRemoveCcPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 8.0, "CC Edit");
    cmds.addCcPoint(clipId, 74, 1.0, 64);

    auto ccList = [&]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        return trackList.getChild(0).getChildWithName(IDs::CLIP_LIST)
            .getChild(0).getChildWithName(IDs::CC_LIST);
    };
    ASSERT_TRUE(ccList().isValid());
    ASSERT_EQ(ccList().getNumChildren(), 1);
    int ccId = static_cast<int>(ccList().getChild(0).getProperty(IDs::ccID, 0));
    EXPECT_GT(ccId, 0);

    cmds.setCcPoint(ccId, 3.0, 100);
    EXPECT_DOUBLE_EQ(static_cast<double>(ccList().getChild(0).getProperty(IDs::beat)), 3.0);
    EXPECT_EQ(static_cast<int>(ccList().getChild(0).getProperty(IDs::value)), 100);

    cmds.removeCcPoint(ccId);
    EXPECT_EQ(ccList().getNumChildren(), 0);
}

TEST(Commands, CcRecordingWritesToClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 8.0, "RecTarget");
    engine.setTrackArmed(0, true);

    engine.getTransportManager().setSampleRate(44100.0);
    auto& tc = engine.getTransportCommands();
    tc.play();
    tc.seekToSeconds(2.0);

    cmds.setCcRecordArmed(true);
    EXPECT_TRUE(engine.isMidiCcRecordArmed());
    engine.recordMidiCc(1, 74, 99);

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clip = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST).getChild(0);
    auto ccList = clip.getChildWithName(IDs::CC_LIST);
    ASSERT_TRUE(ccList.isValid());
    ASSERT_EQ(ccList.getNumChildren(), 1);
    EXPECT_EQ(static_cast<int>(ccList.getChild(0).getProperty(IDs::controllerNumber)), 74);
    EXPECT_EQ(static_cast<int>(ccList.getChild(0).getProperty(IDs::value)), 99);
    EXPECT_DOUBLE_EQ(static_cast<double>(ccList.getChild(0).getProperty(IDs::beat)), 4.0);
}

TEST(Commands, AddMidiFxSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addMidiFxSlot(0, "arpeggiator");
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto chain = trackList.getChild(0).getChildWithName(IDs::MIDI_FX_CHAIN);
    ASSERT_TRUE(chain.isValid());
    ASSERT_EQ(chain.getNumChildren(), 1);
    EXPECT_EQ(chain.getChild(0).getProperty(IDs::fxType).toString(), juce::String("arpeggiator"));
}

TEST(Commands, SetMidiFxSlotParam)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addMidiFxSlot(0, "transpose");
    cmds.setMidiFxSlotParam(0, 0, "semitones", 7.0);
    auto slot = engine.getProjectModel().getTrackListTree()
        .getChild(0).getChildWithName(IDs::MIDI_FX_CHAIN).getChild(0);
    EXPECT_EQ(static_cast<int>(slot.getProperty(IDs::semitones)), 7);
}

TEST(Commands, MidiNoteRecording)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.setTrackArmed(0, true);
    engine.getTransportManager().setSampleRate(44100.0);

    cmds.setMidiNoteRecordArmed(true);
    engine.recordMidiNoteEvent(1, 60, 100, true, 0);
    engine.recordMidiNoteEvent(1, 60, 0, false, 44100);

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    bool found = false;
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto nl = clipList.getChild(c).getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!nl.isValid()) continue;
        for (int n = 0; n < nl.getNumChildren(); ++n)
        {
            auto note = nl.getChild(n);
            if (static_cast<int>(note.getProperty(IDs::noteNumber)) == 60)
            {
                EXPECT_NEAR(static_cast<double>(note.getProperty(IDs::startBeat)), 0.0, 1e-6);
                EXPECT_NEAR(static_cast<double>(note.getProperty(IDs::durationBeats)), 2.0, 0.01);
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST(Commands, MidiNoteRecordingFlushOnDisarm)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.setTrackArmed(0, true);
    engine.getTransportManager().setSampleRate(44100.0);

    cmds.setMidiNoteRecordArmed(true);
    engine.recordMidiNoteEvent(1, 62, 90, true, 0);
    engine.getTransportManager().setCurrentSample(44100);
    cmds.setMidiNoteRecordArmed(false);

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    bool found = false;
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto nl = clipList.getChild(c).getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!nl.isValid()) continue;
        for (int n = 0; n < nl.getNumChildren(); ++n)
        {
            if (static_cast<int>(nl.getChild(n).getProperty(IDs::noteNumber)) == 62)
                found = true;
        }
    }
    EXPECT_TRUE(found);
}
