#include <gtest/gtest.h>
#include "common/ProjectCommands.h"
#include "common/StableRefResolve.h"   // design B2: the stable-id argument rule
#include "common/TrackIdRefs.h"        // design B3: stable-id folder/cell refs
#include "engine/AudioEngine.h"
#include "engine/RoutingManager.h"
#include "engine/MidiClipProcessor.h"
#include "model/ProjectModel.h"

#include <algorithm>
#include <string>
#include <vector>

// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Seed exactly the tracks the test uses
// (indices 0/1/...) and drain the coalesced routing rebuild so live-processor
// /RoutingManager reads are deterministic (lessons 9/10/12; no sleeps).
static int seedTrack(AudioEngine& engine, const char* name)
{
    const int idx = engine.getProjectCommands().addTrack(name);
    engine.drainPendingRoutingRebuild();
    EXPECT_GE(idx, 0);
    return idx;
}

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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    cmds.setTrackVolume(0, 0.5f);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_DOUBLE_EQ(track.volume, 0.5);
}

TEST(Commands, SetTrackPan)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    cmds.setTrackPan(0, 0.25f);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_DOUBLE_EQ(track.pan, 0.25);
}

TEST(Commands, SetTrackMuted)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    cmds.setTrackName(0, "MyTrack");
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_EQ(track.name, "MyTrack");
}

TEST(Commands, AddMidiClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "NoteClip");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "NoteClip");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
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
    // Zero-track default: rebuildTrackFX/AutomationCache/Modulation target
    // track 0 — seed it so the indices are real (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Two tracks so cross-track placement is exercised. Zero-track default:
    // seed both explicitly; the second track must land at index 1.
    ASSERT_GE(seedTrack(engine, "T1"), 0);
    ASSERT_EQ(seedTrack(engine, "T2"), 1);
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
    // Zero-track default: track 0 must exist for the valid-clip leg below.
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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

    // Zero-track default: seed track 0 (clip host) and a second track to move
    // the replacement away and back — the indices are owned by this test.
    ASSERT_GE(seedTrack(engine, "T1"), 0);
    int awayTrack = seedTrack(engine, "T2");
    ASSERT_GE(awayTrack, 1);

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

    // No message pump in the gtest, so the coalesced async routing rebuild
    // never runs on its own — drain it explicitly (deterministic; see
    // AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();

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
    // Zero-track default: FX slots are added on track 0 — seed it (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // switchClipTake on a MIDI clip (no source file) should not crash.
    // Zero-track default: seed track 0 for the clip (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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

TEST(Commands, SetClipName)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "OriginalName");
    ASSERT_GT(clipId, 0);
    ASSERT_EQ(engine.getReadModel().getClip(clipId).name, "OriginalName");

    cmds.setClipName(clipId, "RenamedClip");

    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    juce::ValueTree clipTree;
    for (int i = 0; i < clipList.getNumChildren(); ++i)
        if (static_cast<int>(clipList.getChild(i).getProperty(IDs::clipID, -1)) == clipId)
            clipTree = clipList.getChild(i);
    ASSERT_TRUE(clipTree.isValid());
    EXPECT_EQ(clipTree.getProperty(IDs::name).toString().toStdString(), "RenamedClip");
    EXPECT_EQ(engine.getReadModel().getClip(clipId).name, "RenamedClip");

    cmds.undo();
    // Name reverts to the value before setClipName was called. Since addMidiClip
    // sets the name via createMidiClipEmpty (not through the undo manager), the
    // undo manager records "" as the prior value.
    EXPECT_EQ(engine.getReadModel().getClip(clipId).name, "");

    cmds.setClipName(9999, "NoCrash");
}

TEST(Commands, ReadModelExtensions)
{
    AudioEngine engine;
    engine.initialize();
    auto& rm = engine.getReadModel();
    auto& cmds = engine.getProjectCommands();
    // Zero-track default: seed track 0 — FX slots/lanes are added on it (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);

    // FX Slots
    cmds.addFxSlot(0, 0);  // EQ
    auto fxSlots = rm.getFxSlots(0);
    // SEH guard (0xc0000005): a failed empty-check must stop the test before
    // fxSlots[0] is indexed.
    ASSERT_FALSE(fxSlots.empty());
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
    // Zero-track default: duplicateTrack(0) needs a real source track.
    ASSERT_GE(seedTrack(engine, "Source"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);

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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: seed the track this test drives (lesson 9).
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: notes are recorded onto the armed track 0 — seed it.
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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
    // Zero-track default: notes are recorded onto the armed track 0 — seed it.
    ASSERT_GE(seedTrack(engine, "Track"), 0);
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

// ─── Handoff 7 / design B3: durable refs are STABLE IDS, not positions ─────
// A folder's childTrackIDs holds child trackIDs, a child's parentTrackID holds
// the folder's trackID, and a SONG_PLAN cell's cellTrackID holds its target's
// trackID. A splice (removeTrack/moveTrack) renumbers INDICES but changes no
// durable reference, so the tests below assert WHAT EACH REF POINTS AT — resolve
// the id and compare the entity (name) — never a raw number. Every test follows
// Gate 10 discipline: mutate, drain the routing rebuild, assert the LIVE tree.

namespace {
ProjectCommands::SongPlanData shiftRefsPlan()
{
    ProjectCommands::SongPlanData plan;
    plan.bpm = 138.0;
    plan.keyRoot = 5;
    plan.scaleMode = 7;
    plan.style = "test";
    plan.seed = 42;
    plan.totalBars = 8;
    plan.sections = { { "intro", "intro", 8, 0.0, 32.0 } };
    return plan;
}

ProjectCommands::CellRecipe shiftRefsCell(const char* role, int trackId)
{
    ProjectCommands::CellRecipe r;
    r.section = "intro";
    r.role = role;
    r.trackId = trackId;
    r.sourceKind = "phrase";
    r.paramsJson = "{}";
    r.seed = 1;
    return r;
}

// The track a durable REF actually POINTS AT: resolve the stored id to the
// index it currently occupies and read that track's name. "<none>" when the id
// resolves to nothing (a removed parent, a foreign id) — the entity-level
// comparison a raw-number assertion cannot make (design B3: an index is a
// position, an id is an identity).
std::string nameNamedByID(const juce::ValueTree& trackList, int id)
{
    const int idx = HDAW::trackIndexForID(trackList, id);
    return idx < 0 ? std::string("<none>")
                   : trackList.getChild(idx).getProperty(IDs::name).toString().toStdString();
}

// The names a folder claims, resolved from its childTrackIDs CSV in order.
std::vector<std::string> childNamesOf(const juce::ValueTree& trackList, const juce::ValueTree& folder)
{
    std::vector<std::string> names;
    for (int id : HDAW::parseIDList(folder, IDs::childTrackIDs))
        names.push_back(nameNamedByID(trackList, id));
    return names;
}

// The name a track's parentTrackID names, resolved to the current index.
std::string parentNameOf(const juce::ValueTree& trackList, const juce::ValueTree& track)
{
    return nameNamedByID(trackList, static_cast<int>(track.getProperty(IDs::parentTrackID, -1)));
}

// The name a SONG_PLAN cell's cellTrackID names, resolved to the current index.
std::string cellTargetName(const juce::ValueTree& trackList, const juce::ValueTree& cell)
{
    return nameNamedByID(trackList, static_cast<int>(cell.getProperty(IDs::cellTrackID, -1)));
}
} // namespace

TEST(Commands, RemoveTrackKeepsFolderRefsPointingAtTheSameTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [X, A(folder), B, C] with B and C children of A.
    ASSERT_EQ(cmds.addTrack("X"), 0);
    ASSERT_EQ(cmds.addTrack("A", -1, -1, 2), 1);   // trackType 2 = folder
    const int b = cmds.addTrack("B");
    const int c = cmds.addTrack("C");
    engine.drainPendingRoutingRebuild();
    cmds.moveTrackIntoFolder(b, 1);
    cmds.moveTrackIntoFolder(c, 1);

    auto trackList = engine.getProjectModel().getTrackListTree();
    const int idA = static_cast<int>(trackList.getChild(1).getProperty(IDs::trackID, 0));
    const int idB = static_cast<int>(trackList.getChild(2).getProperty(IDs::trackID, 0));
    const int idC = static_cast<int>(trackList.getChild(3).getProperty(IDs::trackID, 0));
    ASSERT_GT(idA, 0);
    ASSERT_GT(idB, 0);
    ASSERT_GT(idC, 0);

    // A's childTrackIDs names B and C by IDENTITY (their trackIDs).
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(1)),
              (std::vector<std::string>{ "B", "C" }));

    // Phase 1: remove X (index 0) — every index above decrements. The two
    // folder refs are ids, so nothing needs remapping: each still resolves to
    // the SAME entity at its new index.
    auto r = cmds.removeTrack(0);
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.removed, 0);
    ASSERT_EQ(r.shifted.size(), 3u);   // advisory: indices moved, ids did not
    EXPECT_EQ(r.shifted[0], std::make_pair(1, 0));
    EXPECT_EQ(r.shifted[1], std::make_pair(2, 1));
    EXPECT_EQ(r.shifted[2], std::make_pair(3, 2));

    EXPECT_EQ(trackList.getNumChildren(), 3);
    EXPECT_EQ(trackList.getChild(0).getProperty(IDs::name).toString().toStdString(), "A");
    // A's childTrackIDs still names B and C; B/C still point at A — all three
    // refs resolve to the SAME tracks, now at indices 1/2 under A at 0.
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(HDAW::trackIndexForID(trackList, idA))),
              (std::vector<std::string>{ "B", "C" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(HDAW::trackIndexForID(trackList, idB))), "A");
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(HDAW::trackIndexForID(trackList, idC))), "A");

    // Phase 2: remove the folder's MIDDLE child (B) — B's id is PRUNED from
    // A's childTrackIDs (a dangling ref could re-point at a later mint), so A
    // claims exactly C; C's parentTrackID is an identity and still names A.
    r = cmds.removeTrack(HDAW::trackIndexForID(trackList, idB));
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.shifted.size(), 1u);
    EXPECT_EQ(r.shifted[0], std::make_pair(2, 1));
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(HDAW::trackIndexForID(trackList, idA))),
              (std::vector<std::string>{ "C" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(HDAW::trackIndexForID(trackList, idC))), "A");

    // Phase 3: remove the FOLDER itself — C's parentTrackID is PRUNED to the -1
    // sentinel, since the removed folder's id would otherwise be reused by the
    // next minted track.
    r = cmds.removeTrack(HDAW::trackIndexForID(trackList, idA));
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(trackList.getNumChildren(), 1);
    EXPECT_EQ(trackList.getChild(0).getProperty(IDs::name).toString().toStdString(), "C");
    EXPECT_FALSE(trackList.getChild(0).hasProperty(IDs::childTrackIDs));
    EXPECT_EQ(static_cast<int>(trackList.getChild(0).getProperty(IDs::parentTrackID, -999)), -1)
        << "a surviving child of the removed folder must be pruned to the -1 sentinel";
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(0)), "<none>");
    EXPECT_EQ(engine.getReadModel().snapshot().tracks[0].parentId, -1)
        << "the wire parentId reads the folder-less sentinel";
}

TEST(Commands, RemoveTrackKeepsSongPlanCellsOnTheSameTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(cmds.addTrack("T0"), 0);
    ASSERT_GE(cmds.addTrack("T1"), 0);
    ASSERT_GE(cmds.addTrack("T2"), 0);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    const int idT0 = static_cast<int>(trackList.getChild(0).getProperty(IDs::trackID, 0));
    const int idT1 = static_cast<int>(trackList.getChild(1).getProperty(IDs::trackID, 0));
    const int idT2 = static_cast<int>(trackList.getChild(2).getProperty(IDs::trackID, 0));
    ASSERT_GT(idT0, 0);
    ASSERT_GT(idT1, 0);
    ASSERT_GT(idT2, 0);

    ASSERT_TRUE(cmds.setSongPlan(shiftRefsPlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(shiftRefsCell("onT0", 0), &err)) << err;
    ASSERT_TRUE(cmds.setCellRecipe(shiftRefsCell("onT1", 1), &err)) << err;
    ASSERT_TRUE(cmds.setCellRecipe(shiftRefsCell("onT2", 2), &err)) << err;

    // Remove the MIDDLE track: its cell's cellTrackID is PRUNED to -1 (a
    // dangling id would be reusable by the next minted track); the cells of the
    // surviving tracks still name T0 / T2.
    auto r = cmds.removeTrack(1);
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.removed, 1);
    ASSERT_EQ(r.shifted.size(), 1u);

    auto cells = engine.getProjectModel().getTree()
                     .getChildWithName(IDs::SONG_PLAN).getChildWithName(IDs::CELLS);
    ASSERT_EQ(cells.getNumChildren(), 3);
    EXPECT_EQ(static_cast<int>(cells.getChild(0).getProperty(IDs::cellTrackID, -1)), idT0);
    EXPECT_EQ(static_cast<int>(cells.getChild(1).getProperty(IDs::cellTrackID, -1)), -1)
        << "the cell on the removed track must be pruned to the -1 sentinel";
    EXPECT_EQ(static_cast<int>(cells.getChild(2).getProperty(IDs::cellTrackID, -1)), idT2);
    EXPECT_EQ(cellTargetName(trackList, cells.getChild(0)), "T0")
        << "the cell on T0 must still name T0";
    EXPECT_EQ(cellTargetName(trackList, cells.getChild(1)), "<none>")
        << "the cell on the removed track must target nothing";
    EXPECT_EQ(cellTargetName(trackList, cells.getChild(2)), "T2")
        << "the cell on T2 must still name T2";

    // The command-level view agrees (the wire keeps the positional contract).
    const auto view = cmds.getCells();
    ASSERT_EQ(view.size(), 3u);
    EXPECT_EQ(view[1].trackId, -1);
    EXPECT_EQ(view[2].trackId, 1);

    // Gate 2: a fill over the sentinel cell FAILS with a reason and lands no
    // clip on the wrong track — the two live cells fill exactly one clip each.
    auto batch = cmds.fillCells("all");
    EXPECT_FALSE(batch.ok);
    EXPECT_EQ(batch.filled, 2);
    EXPECT_EQ(batch.failed, 1);
    ASSERT_EQ(batch.cells.size(), 3u);
    EXPECT_FALSE(batch.cells[1].ok);
    EXPECT_TRUE(batch.cells[1].error.find("missing track") != std::string::npos)
        << batch.cells[1].error;
    trackList = engine.getProjectModel().getTrackListTree();
    ASSERT_EQ(trackList.getNumChildren(), 2);
    EXPECT_EQ(trackList.getChild(0).getChildWithName(IDs::CLIP_LIST).getNumChildren(), 1);
    EXPECT_EQ(trackList.getChild(1).getChildWithName(IDs::CLIP_LIST).getNumChildren(), 1);
}

// The hazard the prune exists to kill: allocateTrackID() is max(existing)+1, so
// removing the HIGHEST-id track makes the very next mint REUSE that id. A
// dangling durable ref would silently re-point at the brand-new track; the prune
// must leave nothing to re-point. Assert both premises (highest id, id reused)
// so the test can never pass vacuously.
TEST(Commands, RemoveTrackPrunesRefsSoAReusedIdCannotRePoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [T0, Folder(type 2), T1] — T1 is created LAST, so it holds the highest id.
    ASSERT_EQ(cmds.addTrack("T0"), 0);
    ASSERT_EQ(cmds.addTrack("Folder", -1, -1, 2), 1);
    const int t1 = cmds.addTrack("T1");
    ASSERT_EQ(t1, 2);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    int maxID = 0;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
        maxID = std::max(maxID, static_cast<int>(trackList.getChild(t).getProperty(IDs::trackID, 0)));
    const int deadID = static_cast<int>(trackList.getChild(t1).getProperty(IDs::trackID, 0));
    ASSERT_GT(deadID, 0);
    ASSERT_EQ(deadID, maxID) << "premise: T1 must hold the HIGHEST trackID";

    // T1 is claimed by the folder AND targeted by a SONG_PLAN cell.
    cmds.moveTrackIntoFolder(t1, 1);
    ASSERT_TRUE(cmds.setSongPlan(shiftRefsPlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(shiftRefsCell("onT1", t1), &err)) << err;
    engine.drainPendingRoutingRebuild();
    ASSERT_EQ(childNamesOf(trackList, trackList.getChild(1)), (std::vector<std::string>{ "T1" }));
    auto cells = engine.getProjectModel().getTree()
                     .getChildWithName(IDs::SONG_PLAN).getChildWithName(IDs::CELLS);
    ASSERT_EQ(cells.getNumChildren(), 1);
    ASSERT_EQ(cellTargetName(trackList, cells.getChild(0)), "T1");

    // Remove the highest-id track: both refs must be pruned, not left dangling.
    const auto removed = cmds.removeTrack(t1);
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(removed.ok);
    EXPECT_EQ(HDAW::trackIndexForID(trackList, deadID), -1) << "the id is gone with the track";
    ASSERT_EQ(trackList.getNumChildren(), 2);
    const int folderIdx = 1;   // [T0, Folder] after the splice
    ASSERT_EQ(trackList.getChild(folderIdx).getProperty(IDs::name).toString().toStdString(), "Folder");
    EXPECT_TRUE(HDAW::parseIDList(trackList.getChild(folderIdx), IDs::childTrackIDs).empty())
        << "the removed child's id must be pruned from the folder's childTrackIDs";
    EXPECT_EQ(static_cast<int>(cells.getChild(0).getProperty(IDs::cellTrackID, -1)), -1)
        << "the cell that targeted the removed track must be pruned to -1";

    // Mint a new track — it REUSES the dead id (max+1). This is the hazard made
    // real; the assertions below are what a dangling ref would fail.
    const int t2 = cmds.addTrack("T2");
    engine.drainPendingRoutingRebuild();
    ASSERT_EQ(t2, 2);
    const int newID = static_cast<int>(trackList.getChild(t2).getProperty(IDs::trackID, 0));
    ASSERT_EQ(newID, deadID) << "premise: the next mint must REUSE the removed id";

    EXPECT_EQ(static_cast<int>(cells.getChild(0).getProperty(IDs::cellTrackID, -1)), -1)
        << "the cell must NOT re-point at the reused id";
    ASSERT_EQ(cmds.getCells().size(), 1u);
    EXPECT_EQ(cmds.getCells()[0].trackId, -1)
        << "the cell must still read as targeting nothing";
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        const auto tr = trackList.getChild(t);
        for (int id : HDAW::parseIDList(tr, IDs::childTrackIDs))
            EXPECT_NE(id, newID) << "no folder's childTrackIDs may list the reused id";
        EXPECT_NE(static_cast<int>(tr.getProperty(IDs::parentTrackID, -1)), newID)
            << "no surviving track's parentTrackID may name the reused id";
    }
}


TEST(Commands, MoveTrackKeepsFolderRefsPointingAtTheSameTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [A(folder), B(child of A), X].
    ASSERT_EQ(cmds.addTrack("A", -1, -1, 2), 0);
    const int b = cmds.addTrack("B");
    ASSERT_EQ(cmds.addTrack("X"), 2);
    engine.drainPendingRoutingRebuild();
    cmds.moveTrackIntoFolder(b, 0);

    auto trackList = engine.getProjectModel().getTrackListTree();
    const int idB = static_cast<int>(trackList.getChild(b).getProperty(IDs::trackID, 0));
    ASSERT_GT(idB, 0);

    // Move X to the front -> [X, A, B]. A's childTrackIDs (B's id) and B's
    // parentTrackID (A's id) are IDENTITIES, so the permutation renumbers their
    // indices without touching either ref — with a positional ref B.parentId
    // would have stayed 0 and handed mute/solo to X.
    cmds.moveTrack(2, 0);
    engine.drainPendingRoutingRebuild();

    ASSERT_EQ(trackList.getNumChildren(), 3);
    EXPECT_EQ(trackList.getChild(0).getProperty(IDs::name).toString().toStdString(), "X");
    EXPECT_EQ(trackList.getChild(1).getProperty(IDs::name).toString().toStdString(), "A");
    EXPECT_EQ(trackList.getChild(2).getProperty(IDs::name).toString().toStdString(), "B");
    // After the splice each ref still resolves to the SAME entity: A at index 1
    // names B, and B at index 2 names A.
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(1)), (std::vector<std::string>{ "B" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(2)), "A");
    EXPECT_EQ(HDAW::trackIndexForID(trackList, idB), 2)
        << "B's id followed B to its new index";
}

// ─── Duplicate track: durable stable-id ref fixup ──────────────────────────
// duplicateTrack APPENDS its copy, so no existing index shifts and no remap is
// involved — but the copy must not inherit the source's durable folder refs
// (childTrackIDs CSV / parentTrackID): a cloned childTrackIDs makes two folders
// claim the same children, and a cloned parentTrackID with no matching CSV entry
// leaves the two refs disagreeing (ReadModelImpl resolves the mute/solo cascade
// through parentTrackID, folder semantics read childTrackIDs). Gate 10
// discipline: mutate, drain the routing rebuild, assert the LIVE tree plus the
// ReadModel projection of the cascade.

TEST(Commands, DuplicateFolderCopyDoesNotClaimChildren)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [A(folder), B, C] with B and C children of A.
    ASSERT_EQ(cmds.addTrack("A", -1, -1, 2), 0);   // trackType 2 = folder
    const int b = cmds.addTrack("B");
    const int c = cmds.addTrack("C");
    ASSERT_EQ(b, 1);
    ASSERT_EQ(c, 2);
    engine.drainPendingRoutingRebuild();
    cmds.moveTrackIntoFolder(b, 0);
    cmds.moveTrackIntoFolder(c, 0);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)),
              (std::vector<std::string>{ "B", "C" }));

    const int copyIdx = cmds.duplicateTrack(0);
    engine.drainPendingRoutingRebuild();

    ASSERT_EQ(copyIdx, 3);
    ASSERT_EQ(trackList.getNumChildren(), 4);
    auto copy = trackList.getChild(copyIdx);
    EXPECT_EQ(static_cast<int>(copy.getProperty(IDs::trackType, 0)), 2);  // still a folder
    EXPECT_EQ(copy.getProperty(IDs::name).toString().toStdString(), "A copy");

    // The copy does NOT claim the original's children. createTrackValueTree
    // never sets childTrackIDs (a track only acquires it in moveTrackIntoFolder),
    // so the exact fresh-track value is the property ABSENT, not "".
    EXPECT_FALSE(copy.hasProperty(IDs::childTrackIDs));
    // No track's parentTrackID resolves to the COPY's index.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
        EXPECT_NE(HDAW::trackIndexForID(trackList,
                  static_cast<int>(trackList.getChild(t).getProperty(IDs::parentTrackID, -1))), copyIdx);

    // The original folder is untouched and every original child still resolves
    // to the ORIGINAL folder — not to the copy.
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)),
              (std::vector<std::string>{ "B", "C" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(b)), "A");
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(c)), "A");

    // Cascade agreement: muting the original folder reaches exactly its own
    // children — the copy would have double-counted them via a cloned childIds.
    cmds.setTrackMuted(0, true);
    engine.drainPendingRoutingRebuild();
    const auto snap = engine.getReadModel().snapshot();
    ASSERT_EQ(snap.tracks.size(), 4u);
    EXPECT_TRUE(snap.tracks[b].effectiveMuted);
    EXPECT_TRUE(snap.tracks[c].effectiveMuted);
    EXPECT_FALSE(snap.tracks[copyIdx].effectiveMuted);
    EXPECT_EQ(snap.tracks[copyIdx].parentId, -1);
}

TEST(Commands, DuplicateChildCopyLinksIntoFolder)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [A(folder), B(child of A), C].
    ASSERT_EQ(cmds.addTrack("A", -1, -1, 2), 0);
    const int b = cmds.addTrack("B");
    ASSERT_EQ(b, 1);
    ASSERT_EQ(cmds.addTrack("C"), 2);
    engine.drainPendingRoutingRebuild();
    cmds.moveTrackIntoFolder(b, 0);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)), (std::vector<std::string>{ "B" }));

    const int copyIdx = cmds.duplicateTrack(b);
    engine.drainPendingRoutingRebuild();

    ASSERT_EQ(copyIdx, 3);
    ASSERT_EQ(trackList.getNumChildren(), 4);
    // The copy keeps the source's parentTrackID (the folder did not move)...
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(copyIdx)), "A");
    // ...and the folder's childTrackIDs learns about it exactly once, by the
    // COPY's id (resolved here to the copy's name).
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)),
              (std::vector<std::string>{ "B", "B copy" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(2)), "<none>")   // C stays free
        << "C must not be dragged into the folder";

    // Refs are symmetric: walk the CSV, resolve each id, and check that the
    // track it names points back at the folder.
    const auto children = HDAW::parseIDList(trackList.getChild(0), IDs::childTrackIDs);
    ASSERT_EQ(children.size(), 2u);
    for (int childID : children)
    {
        const int childIdx = HDAW::trackIndexForID(trackList, childID);
        ASSERT_GE(childIdx, 0);
        ASSERT_LT(childIdx, trackList.getNumChildren());
        EXPECT_EQ(parentNameOf(trackList, trackList.getChild(childIdx)), "A");
    }

    // Cascade agreement: the folder's mute reaches B and the copy, not C.
    cmds.setTrackMuted(0, true);
    engine.drainPendingRoutingRebuild();
    const auto snap = engine.getReadModel().snapshot();
    ASSERT_EQ(snap.tracks.size(), 4u);
    EXPECT_EQ(snap.tracks[copyIdx].parentId, 0);
    EXPECT_TRUE(snap.tracks[b].effectiveMuted);
    EXPECT_TRUE(snap.tracks[copyIdx].effectiveMuted);
    EXPECT_FALSE(snap.tracks[2].effectiveMuted);
}

TEST(Commands, DuplicateTrackRefWriteIsOneUndoUnit)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // [A(folder), B(child of A)].
    ASSERT_EQ(cmds.addTrack("A", -1, -1, 2), 0);
    const int b = cmds.addTrack("B");
    ASSERT_EQ(b, 1);
    engine.drainPendingRoutingRebuild();
    cmds.moveTrackIntoFolder(b, 0);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)), (std::vector<std::string>{ "B" }));

    // Isolate the duplicate as ONE undo unit, then undo it once.
    cmds.beginTransaction("Duplicate track");
    const int copyIdx = cmds.duplicateTrack(b);
    cmds.endTransaction();
    engine.drainPendingRoutingRebuild();

    ASSERT_EQ(copyIdx, 2);
    ASSERT_EQ(trackList.getNumChildren(), 3);
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)),
              (std::vector<std::string>{ "B", "B copy" }));

    cmds.undo();
    engine.drainPendingRoutingRebuild();

    // The copy AND the folder's childTrackIDs write are reverted together: a
    // separate undo unit would leave the CSV naming a track that no longer
    // exists. After the undo the lone child ref resolves to B under A again.
    EXPECT_EQ(trackList.getNumChildren(), 2);
    EXPECT_EQ(childNamesOf(trackList, trackList.getChild(0)), (std::vector<std::string>{ "B" }));
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(b)), "A");
    EXPECT_EQ(trackList.getChild(b).getProperty(IDs::name).toString().toStdString(), "B");
}

// A foreign (unresolvable) parentTrackID is LEFT ALONE: duplicating must not
// index it and must not invent a childTrackIDs entry anywhere.
TEST(Commands, DuplicateTrackLeavesForeignParentRefAlone)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ASSERT_GE(seedTrack(engine, "A"), 0);
    const int b = cmds.addTrack("B");
    ASSERT_EQ(b, 1);
    engine.drainPendingRoutingRebuild();

    auto trackList = engine.getProjectModel().getTrackListTree();
    trackList.getChild(b).setProperty(IDs::parentTrackID, 99, nullptr);   // no track 99
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(b)), "<none>")
        << "the foreign parent id resolves to no track";

    const int copyIdx = cmds.duplicateTrack(b);
    engine.drainPendingRoutingRebuild();

    ASSERT_EQ(copyIdx, 2);
    ASSERT_EQ(trackList.getNumChildren(), 3);
    // The copy carries the same foreign ref as its source, untouched, and it
    // still resolves to nothing.
    EXPECT_EQ(static_cast<int>(trackList.getChild(copyIdx).getProperty(IDs::parentTrackID, -1)), 99);
    EXPECT_EQ(parentNameOf(trackList, trackList.getChild(copyIdx)), "<none>");
    // No track grew a childTrackIDs property.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
        EXPECT_FALSE(trackList.getChild(t).hasProperty(IDs::childTrackIDs));
}

// ─── Stable track/send ids (design B1) ─────────────────────────────────────
// An INDEX is not an identity: TRACK_LIST / SEND_LIST positions shift under
// removeTrack / moveTrack / removeSend, which is why every held or durable
// reference (and the wire's positional trackId / sendIndex) can go stale. A
// trackID / sendID is minted at creation from the tree (max existing id + 1,
// floor 1), echoed on the wire, and NEVER renumbered by a splice.

namespace {
int trackIdAt(AudioEngine& engine, int index)
{
    const auto tl = engine.getProjectModel().getTrackListTree();
    if (index < 0 || index >= tl.getNumChildren()) return -1;
    return static_cast<int>(tl.getChild(index).getProperty(IDs::trackID, 0));
}
// The index that currently carries `id`, or -1 — how a reference that only knows
// the identity finds its entity again after a splice (the whole point of B1).
int indexOfTrackId(AudioEngine& engine, int id)
{
    const auto tl = engine.getProjectModel().getTrackListTree();
    for (int i = 0; i < tl.getNumChildren(); ++i)
        if (static_cast<int>(tl.getChild(i).getProperty(IDs::trackID, 0)) == id) return i;
    return -1;
}
} // namespace

// G1/G3: the id is stamped at creation, is non-zero, is what the command
// interface reports, and SURVIVES a reorder and a removal of another track.
TEST(Commands, StableTrackIDsSurviveMoveAndRemoval)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int a = seedTrack(engine, "A");
    const int b = seedTrack(engine, "B");
    const int c = seedTrack(engine, "C");

    const int idA = trackIdAt(engine, a);
    const int idB = trackIdAt(engine, b);
    const int idC = trackIdAt(engine, c);
    EXPECT_GT(idA, 0);
    EXPECT_GT(idB, 0);
    EXPECT_GT(idC, 0);
    EXPECT_NE(idA, idB);
    EXPECT_NE(idB, idC);
    EXPECT_EQ(cmds.getTrackID(a), idA) << "the command interface reads the same id";
    EXPECT_EQ(cmds.getTrackID(99), 0) << "an index that names no track reports 0";

    // Reorder: every id follows its track (the index does not).
    cmds.moveTrack(c, 0);
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(trackIdAt(engine, 0), idC);
    EXPECT_EQ(trackIdAt(engine, 1), idA);
    EXPECT_EQ(trackIdAt(engine, 2), idB);

    // Remove one: the survivors keep their ids while their indices shift down.
    const auto removed = cmds.removeTrack(1);   // A
    engine.drainPendingRoutingRebuild();
    EXPECT_TRUE(removed.ok);
    EXPECT_EQ(indexOfTrackId(engine, idA), -1) << "A's id is gone with A";
    EXPECT_EQ(indexOfTrackId(engine, idC), 0);
    EXPECT_EQ(indexOfTrackId(engine, idB), 1) << "the id still names B, not B's old slot";
}

// G2: ids are unique, and a duplicate is a NEW entity — copy() must not inherit
// the source's id (two live entities sharing one identity breaks every id-based
// reference and the allocator's own invariant).
TEST(Commands, DuplicateGetsAFreshIDAndAllIDsStayUnique)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int src = seedTrack(engine, "Source");
    const int srcID = trackIdAt(engine, src);

    ASSERT_EQ(cmds.createSend(src, 1, 0.5f, false).sendIndex, 0);
    engine.drainPendingRoutingRebuild();
    const int srcSendID = engine.getReadModel().getTrackSends(src).at(0).sendID;
    ASSERT_GT(srcSendID, 0);

    const int copyIdx = cmds.duplicateTrack(src);
    engine.drainPendingRoutingRebuild();
    ASSERT_GE(copyIdx, 0);
    const int copyID = trackIdAt(engine, copyIdx);
    EXPECT_GT(copyID, 0);
    EXPECT_NE(copyID, srcID) << "the copy must not inherit the source's stable id";

    // The copied SEND is a new entity too.
    const auto copySends = engine.getReadModel().getTrackSends(copyIdx);
    ASSERT_EQ(copySends.size(), 1u);
    EXPECT_GT(copySends[0].sendID, 0);
    EXPECT_NE(copySends[0].sendID, srcSendID) << "the copied send needs its own id";

    // No two tracks (or sends, project-wide) share an id.
    const auto tl = engine.getProjectModel().getTrackListTree();
    std::vector<int> ids;
    for (int t = 0; t < tl.getNumChildren(); ++t)
        ids.push_back(static_cast<int>(tl.getChild(t).getProperty(IDs::trackID, 0)));
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end()) << "duplicate trackID";
}

// G5: this is the property sendIndex lacks — removing a send renumbers the ones
// above it while every surviving send keeps its identity.
TEST(Commands, SendIDsSurviveRemoveSendWhileSendIndexRenumbers)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine, "S"), 0);

    ASSERT_EQ(cmds.createSend(0, 1, 0.25f, false).sendIndex, 0);   // A
    ASSERT_EQ(cmds.createSend(0, 1, 0.50f, false).sendIndex, 1);   // B
    ASSERT_EQ(cmds.createSend(0, 1, 0.75f, false).sendIndex, 2);   // C
    engine.drainPendingRoutingRebuild();

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 3u);
    const int idA = sends[0].sendID, idB = sends[1].sendID, idC = sends[2].sendID;
    ASSERT_GT(idA, 0);
    EXPECT_NE(idA, idB);
    EXPECT_NE(idB, idC);

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    engine.drainPendingRoutingRebuild();

    sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 2u);
    EXPECT_EQ(sends[0].sendIndex, 0) << "B renumbers into slot 0 (positional)";
    EXPECT_EQ(sends[1].sendIndex, 1) << "C renumbers into slot 1";
    EXPECT_EQ(sends[0].sendID, idB) << "B's identity is untouched by the splice";
    EXPECT_EQ(sends[1].sendID, idC);
    EXPECT_EQ(sends[0].level, 0.50f) << "and it is still the same send";
}

// G4: the load-path backfill. A tree saved before B1 (or copied in from another
// model) has no ids; scanAndSyncTrackIDs gives every TRACK and SEND one, keeps
// the order, is idempotent, and never mints a duplicate of an id already present.
TEST(Commands, ScanAndSyncTrackIDsBackfillsLegacyTrees)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine, "Keep1"), 0);
    ASSERT_GE(seedTrack(engine, "Keep2"), 0);
    ASSERT_EQ(cmds.createSend(0, 1, 0.5f, false).sendIndex, 0);
    engine.drainPendingRoutingRebuild();

    auto& model = engine.getProjectModel();
    auto tl = model.getTrackListTree();
    const int keep2ID = static_cast<int>(tl.getChild(1).getProperty(IDs::trackID, 0));
    ASSERT_GT(keep2ID, 0);

    // Simulate a pre-B1 file: every id gone, one track keeps a foreign high id
    // (a project that carries ids from a merge must not have them collided with).
    for (int t = 0; t < tl.getNumChildren(); ++t)
        tl.getChild(t).removeProperty(IDs::trackID, nullptr);
    const auto sendList = tl.getChild(0).getChildWithName(IDs::SEND_LIST);
    ASSERT_TRUE(sendList.isValid());
    ASSERT_EQ(sendList.getNumChildren(), 1);
    sendList.getChild(0).removeProperty(IDs::sendID, nullptr);
    tl.getChild(1).setProperty(IDs::trackID, keep2ID + 40, nullptr);   // foreign high id

    model.scanAndSyncTrackIDs();

    const int id0 = trackIdAt(engine, 0);
    const int id1 = trackIdAt(engine, 1);
    EXPECT_GT(id0, 0) << "a legacy track gets an id";
    EXPECT_EQ(id1, keep2ID + 40) << "an existing id is left exactly as it was";
    EXPECT_NE(id0, id1) << "the backfill must not mint a colliding id";
    EXPECT_GT(static_cast<int>(sendList.getChild(0).getProperty(IDs::sendID, 0)), 0)
        << "the legacy send gets one";
    EXPECT_EQ(tl.getChild(0).getProperty(IDs::name).toString(), juce::String("Keep1"))
        << "order and every other property are untouched";

    // Idempotent: a second run changes nothing (no no-op churn, lesson 2).
    const int id0Again = trackIdAt(engine, 0);
    const int sendAgain = static_cast<int>(sendList.getChild(0).getProperty(IDs::sendID, 0));
    model.scanAndSyncTrackIDs();
    EXPECT_EQ(trackIdAt(engine, 0), id0Again);
    EXPECT_EQ(static_cast<int>(sendList.getChild(0).getProperty(IDs::sendID, 0)), sendAgain);
}

// ─── Stable ids as ARGUMENTS (design B2) ───────────────────────────────────
// The rule itself, in isolation: `common/StableRefResolve.h` is header-only and
// Qt-free, so it is tested here as what it is — a pure function over the tree.
// The surfaces only pin that they CALL it (the twin tests in
// tests/unit/frontend/ compare their payloads and failure texts); this is where
// the four cases the rule is made of are pinned: id wins, disagreement is an
// error, unknown id is an error (never a silent fall back), and the positional
// argument alone keeps today's behaviour byte for byte.
//
// `kNoRef` is what a surface passes when the positional KEY WAS ABSENT — the
// surfaces decide that with QJsonObject::contains(), never with the value, so an
// explicit `trackId: 0` reaches here as index 0 (see the low-index case below).

// G1 + G3 + G4 + G6: the track half of the rule.
TEST(Commands, StableTrackRefResolverPrefersTheIdAndRefusesTheRest)
{
    AudioEngine engine;
    engine.initialize();
    const int a = seedTrack(engine, "A");
    const int b = seedTrack(engine, "B");
    const int c = seedTrack(engine, "C");
    ASSERT_EQ(a, 0); ASSERT_EQ(b, 1); ASSERT_EQ(c, 2);
    const auto tl = engine.getProjectModel().getTrackListTree();
    const int idA = trackIdAt(engine, 0), idB = trackIdAt(engine, 1), idC = trackIdAt(engine, 2);
    ASSERT_GT(idA, 0);

    // G2 (resolver half): the id alone names the entity's CURRENT position.
    const auto byId = HDAW::resolveTrackRef(tl, HDAW::kNoRef, idB);
    ASSERT_TRUE(byId.ok) << byId.error;
    EXPECT_EQ(byId.index, 1);

    // The id also wins over a positional argument that agrees with it — the
    // shape a caller rediscovers its own index in.
    const auto agrees = HDAW::resolveTrackRef(tl, 1, idB);
    ASSERT_TRUE(agrees.ok) << agrees.error;
    EXPECT_EQ(agrees.index, 1);

    // G1: the positional argument ALONE is passed through untouched, valid or
    // not. B2 is additive: an out-of-range index keeps the outcome the command
    // layer already produced for it (its own refusal / the guard's preview),
    // which is why nothing here range-checks it.
    const auto posOnly = HDAW::resolveTrackRef(tl, 2, 0);
    ASSERT_TRUE(posOnly.ok) << posOnly.error;
    EXPECT_EQ(posOnly.index, 2);
    const auto posOutOfRange = HDAW::resolveTrackRef(tl, 99, 0);
    ASSERT_TRUE(posOutOfRange.ok) << "an out-of-range positional index stays the command's business";
    EXPECT_EQ(posOutOfRange.index, 99);

    // `trackId: 0` — the low-index case the surfaces must NOT mistake for
    // "absent" (they read presence with contains(), which is why this arrives as
    // index 0 rather than kNoRef).
    const auto firstTrack = HDAW::resolveTrackRef(tl, 0, 0);
    ASSERT_TRUE(firstTrack.ok) << firstTrack.error;
    EXPECT_EQ(firstTrack.index, 0);

    // Neither argument: today's message, and the sentinel on the way out.
    const auto none = HDAW::resolveTrackRef(tl, HDAW::kNoRef, 0);
    EXPECT_FALSE(none.ok);
    EXPECT_EQ(none.error, "trackId required");
    EXPECT_EQ(none.index, HDAW::kNoRef);

    // G3: an unknown id is an error NAMING it — never a fall back to the
    // positional index (that is how the wrong track gets mutated: the caller's
    // id was stale and it must hear so, not act on a neighbour).
    const auto unknown = HDAW::resolveTrackRef(tl, HDAW::kNoRef, 4242);
    EXPECT_FALSE(unknown.ok);
    EXPECT_EQ(unknown.error, "unknown trackID 4242");
    EXPECT_EQ(unknown.index, HDAW::kNoRef)
        << "a failed resolution must not hand out an index at all";

    // G6: the trap of an ambiguous single number — 3 tracks whose ids are 1,2,3,
    // so trackID 3 is ALSO a valid index. The rule keys on WHICH KEY was sent,
    // never on the value, and here the two agree: id 3 IS the track at index 2,
    // so this is a legal, agreeing call, not an error. (The disagreement case
    // below, after a move, is what actually catches a stale index.)
    ASSERT_EQ(idA, 1) << "the construction below needs the id space to start at 1";
    ASSERT_EQ(idB, 2);
    ASSERT_EQ(idC, 3);
    ASSERT_EQ(trackIdAt(engine, 2), 3) << "trackID 3 doubles as track 2's index: the trap";
    const auto agreeing = HDAW::resolveTrackRef(tl, 2, idC);
    ASSERT_TRUE(agreeing.ok) << agreeing.error;
    EXPECT_EQ(agreeing.index, 2) << "id 3 names the track that index 2 already named";

    // G4: the same disagreement on two DIFFERENT entities whose numbers happen
    // to be unequal — the message reports the values that were actually sent.
    const auto clash2 = HDAW::resolveTrackRef(tl, 0, idC);
    ASSERT_FALSE(clash2.ok);
    EXPECT_EQ(clash2.error, "trackId 0 and trackID 3 disagree");

    // The id still names its track after a splice moved it — and the id that
    // moved does NOT resolve to the index it used to sit at.
    engine.getProjectCommands().moveTrack(2, 0);
    engine.drainPendingRoutingRebuild();
    const auto afterMove = HDAW::resolveTrackRef(tl, HDAW::kNoRef, idC);
    ASSERT_TRUE(afterMove.ok) << afterMove.error;
    EXPECT_EQ(afterMove.index, 0);
    EXPECT_EQ(tl.getChild(0).getProperty(IDs::name).toString().toStdString(), "C");
    // …and the STALE index now names a different track: the disagreement is
    // exactly what catches that, instead of mutating A.
    const auto stale = HDAW::resolveTrackRef(tl, 2, idC);
    ASSERT_FALSE(stale.ok);
    EXPECT_EQ(stale.error, "trackId 2 and trackID 3 disagree");

    // A track list that is not there (an unloaded/foreign tree) is "unknown",
    // not a crash and not a fall back to the positional argument.
    const auto noList = HDAW::resolveTrackRef(juce::ValueTree(), HDAW::kNoRef, idA);
    EXPECT_FALSE(noList.ok);
    EXPECT_EQ(noList.error, "unknown trackID " + std::to_string(idA));
}

// G1 + G5: the send half — same rule, plus the one property B1 gave sendID:
// after `removeSend(0)` the SURVIVOR is still addressed by its sendID while its
// sendIndex has renumbered.
TEST(Commands, StableSendRefResolverAddressesTheSurvivorByIdentity)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, "S"), 0);
    auto& cmds = engine.getProjectCommands();
    ASSERT_EQ(cmds.createSend(0, 1, 0.25f, false).sendIndex, 0);   // A
    ASSERT_EQ(cmds.createSend(0, 1, 0.50f, false).sendIndex, 1);   // B
    ASSERT_EQ(cmds.createSend(0, 1, 0.75f, false).sendIndex, 2);   // C
    engine.drainPendingRoutingRebuild();

    const auto tl = engine.getProjectModel().getTrackListTree();
    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 3u);
    const int idA = sends[0].sendID, idB = sends[1].sendID, idC = sends[2].sendID;
    ASSERT_GT(idA, 0);

    // By index (unchanged) and by identity — the same send, same position.
    const auto byIndex = HDAW::resolveSendRef(tl, 0, 1, 0);
    ASSERT_TRUE(byIndex.ok) << byIndex.error;
    EXPECT_EQ(byIndex.index, 1);
    const auto byId = HDAW::resolveSendRef(tl, 0, HDAW::kNoRef, idC);
    ASSERT_TRUE(byId.ok) << byId.error;
    EXPECT_EQ(byId.index, 2);

    // G5: remove send 0 → C renumbers to 1 and is STILL addressed by its id.
    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    engine.drainPendingRoutingRebuild();
    sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 2u);
    EXPECT_EQ(sends[1].sendID, idC) << "C keeps its identity";
    EXPECT_EQ(sends[1].sendIndex, 1) << "…while its address moved";

    const auto afterSplice = HDAW::resolveSendRef(tl, 0, HDAW::kNoRef, idC);
    ASSERT_TRUE(afterSplice.ok) << afterSplice.error;
    EXPECT_EQ(afterSplice.index, 1) << "the id names C where C now sits";
    EXPECT_DOUBLE_EQ(static_cast<double>(tl.getChild(0).getChildWithName(IDs::SEND_LIST)
                          .getChild(1).getProperty(IDs::sendLevel, 0.0)), 0.75)
        << "…and it is C (level 0.75), not the send that renumbered into its old slot";
    // The disagreement case, and the removed send's id is now unknown.
    const auto clash = HDAW::resolveSendRef(tl, 0, 0, idC);
    EXPECT_FALSE(clash.ok);
    EXPECT_EQ(clash.error, "sendIndex 0 and sendID " + std::to_string(idC) + " disagree");
    const auto gone = HDAW::resolveSendRef(tl, 0, HDAW::kNoRef, idA);
    EXPECT_FALSE(gone.ok);
    EXPECT_EQ(gone.error, "unknown sendID " + std::to_string(idA));

    // Neither argument → today's message. `sendIndex: 0` is a real argument (the
    // surfaces read presence with contains()), so it resolves rather than erroring.
    const auto neither = HDAW::resolveSendRef(tl, 0, HDAW::kNoRef, 0);
    EXPECT_FALSE(neither.ok);
    EXPECT_EQ(neither.error, "sendIndex required");
    const auto zeroIndex = HDAW::resolveSendRef(tl, 0, 0, 0);
    ASSERT_TRUE(zeroIndex.ok) << zeroIndex.error;
    EXPECT_EQ(zeroIndex.index, 0);

    // The sendID space is project-wide but the ADDRESS is (track, send): the same
    // id asked for on a track that does not hold it — or on a track index that
    // does not exist — is unknown, never "found elsewhere".
    ASSERT_GE(seedTrack(engine, "T2"), 0);
    ASSERT_EQ(cmds.createSend(1, 1, 0.5f, false).sendIndex, 0);
    engine.drainPendingRoutingRebuild();
    const auto wrongTrack = HDAW::resolveSendRef(tl, 1, HDAW::kNoRef, idB);
    EXPECT_FALSE(wrongTrack.ok);
    EXPECT_EQ(wrongTrack.error, "unknown sendID " + std::to_string(idB));
    const auto noTrack = HDAW::resolveSendRef(tl, 99, HDAW::kNoRef, idB);
    EXPECT_FALSE(noTrack.ok);
    EXPECT_EQ(noTrack.error, "unknown sendID " + std::to_string(idB));
    const auto onRightTrack = HDAW::resolveSendRef(tl, 0, HDAW::kNoRef, idB);
    ASSERT_TRUE(onRightTrack.ok) << onRightTrack.error;
    EXPECT_EQ(onRightTrack.index, 0) << "B is track 0's only remaining send";
}

// The folder target resolves through the SAME lookup with its own key names
// (`folderId` / `folderID`) — a folder IS a track in TRACK_LIST, and the message
// must name the FOLDER argument, not the track one (the surfaces hand a missing
// folder argument straight through, so the text is the whole UX).
TEST(Commands, StableFolderRefUsesTheFolderKeyNames)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, "Folder"), 0);
    ASSERT_GE(seedTrack(engine, "Child"), 0);
    const auto tl = engine.getProjectModel().getTrackListTree();
    const int folderID = trackIdAt(engine, 0);

    const auto byId = HDAW::resolveTrackRef(tl, HDAW::kNoRef, folderID, HDAW::kFolderRefKeys);
    ASSERT_TRUE(byId.ok) << byId.error;
    EXPECT_EQ(byId.index, 0);

    const auto unknown = HDAW::resolveTrackRef(tl, HDAW::kNoRef, 4242, HDAW::kFolderRefKeys);
    EXPECT_FALSE(unknown.ok);
    EXPECT_EQ(unknown.error, "unknown folderID 4242");

    const auto clash = HDAW::resolveTrackRef(tl, 1, folderID, HDAW::kFolderRefKeys);
    EXPECT_FALSE(clash.ok);
    EXPECT_EQ(clash.error, "folderId 1 and folderID " + std::to_string(folderID) + " disagree");

    const auto neither = HDAW::resolveTrackRef(tl, HDAW::kNoRef, 0, HDAW::kFolderRefKeys);
    EXPECT_FALSE(neither.ok);
    EXPECT_EQ(neither.error, "folderId required");
}
