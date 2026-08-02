// Comprehensive coverage of the engine command surface that the
// FrontendRouter (RPC layer) dispatches. Tests exercise the engine
// commands directly (same path the RPC dispatch calls into).
//
// Category coverage:
//   - Transport: record, punch, seek-to-sample, metronome
//   - Project lifecycle: newProject, save/load round-trip
//   - Clip properties: start, duration, gain, fade, offset, looping, muted
//   - Clip timestretch: sourceBpm, stretchMode, stretchRatio
//   - Clip slicing: sliceClipAtTimes, sliceClipAtPlayhead
//   - Gain envelope: add/move/remove/clear points
//   - Track properties: armed, inputMonitor, height, collapsed, hidden,
//     midiChannel, type, color, moveTrackIntoFolder/OutOfFolder
//   - Note mutations: setPitch, setVelocity, setStart, setDuration, clearNotes
//   - FX: addFxSlot with internal types, setFxSlotBypassed, setFxSlotParam
//   - Automation: addAutomationPoint, removeAutomationPoint, setAutomationEnabled,
//     setAutomationMode, removeAutomationLane
//   - Tempo points: add/remove/set
//   - CC: set record armed
//   - Transaction lifecycle: begin/end
//   - Scale: setScaleRoot, setScaleMode
//   - Markers: setMarkerTime
//   - Batch ops: addClips, removeClips, moveClips, duplicateClips
//   - Session: launchScene, stopAllSessionClips

#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/ProjectBackup.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <cmath>
#include <vector>

namespace {

ClipSnapshot requireClip(AudioEngine& engine, int clipId)
{
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        if (c.clipId == clipId)
            return c;
    ADD_FAILURE() << "clip " << clipId << " not found in snapshot";
    return {};
}

bool clipExists(AudioEngine& engine, int clipId)
{
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.clipId == clipId) return true;
    return false;
}

std::vector<ClipSnapshot> clipsOnTrack0(AudioEngine& engine)
{
    std::vector<ClipSnapshot> out;
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.trackIndex == 0)
            out.push_back(c);
    return out;
}

} // namespace

// ============================================================================
// TRANSPORT: RECORDING, PUNCH, SEEK-TO-SAMPLE
// ============================================================================

TEST(TransportSurface, StartStopRecording)
{
    AudioEngine engine;
    engine.initialize();
    auto& tc = engine.getTransportCommands();

    EXPECT_FALSE(tc.isRecording());
    tc.startRecording();
    EXPECT_TRUE(tc.isRecording());
    auto t = engine.getReadModel().getTransport();
    EXPECT_TRUE(t.isRecording);

    tc.stopRecording();
    EXPECT_FALSE(tc.isRecording());
    t = engine.getReadModel().getTransport();
    EXPECT_FALSE(t.isRecording);
}

TEST(TransportSurface, PunchEnabled)
{
    AudioEngine engine;
    engine.initialize();
    auto& tc = engine.getTransportCommands();

    EXPECT_FALSE(tc.isPunchEnabled());
    tc.setPunchEnabled(true);
    EXPECT_TRUE(tc.isPunchEnabled());
    auto t = engine.getReadModel().getTransport();
    EXPECT_TRUE(t.punchEnabled);

    tc.setPunchEnabled(false);
    EXPECT_FALSE(tc.isPunchEnabled());
}

TEST(TransportSurface, SeekToSample)
{
    AudioEngine engine;
    engine.initialize();
    engine.getTransportManager().setSampleRate(44100.0);
    auto& tc = engine.getTransportCommands();

    tc.seekToSample(44100);
    auto t = engine.getReadModel().getTransport();
    EXPECT_NEAR(t.currentTimeSeconds, 1.0, 0.01);

    tc.seekToSample(0);
    t = engine.getReadModel().getTransport();
    EXPECT_DOUBLE_EQ(t.currentTimeSeconds, 0.0);
}

TEST(TransportSurface, MetronomeEnabled)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setMetronomeEnabled(true);
    auto transportTree = engine.getProjectModel().getTransportTree();
    EXPECT_TRUE(static_cast<bool>(transportTree.getProperty(IDs::metronomeEnabled)));

    cmds.setMetronomeEnabled(false);
    EXPECT_FALSE(static_cast<bool>(transportTree.getProperty(IDs::metronomeEnabled)));
}

TEST(TransportSurface, LoopRegionBeatsSecondsConversion)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    // Set a known BPM so the beat↔second conversion is deterministic.
    pc.setTempo(120.0);

    // 4 beats at 120 BPM = 2.0 seconds; 8 beats = 4.0 seconds.
    pc.setLoopStart(4.0);
    pc.setLoopEnd(8.0);

    auto t = engine.getReadModel().getTransport();
    // Round trip: the backend stores seconds, getTransport returns beats.
    EXPECT_DOUBLE_EQ(t.loopStart, 4.0);
    EXPECT_DOUBLE_EQ(t.loopEnd, 8.0);
}

// ============================================================================
// PROJECT LIFECYCLE: NEW / SAVE / LOAD
// ============================================================================

TEST(ProjectLifecycle, NewProjectResetsClipCount)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Default project has clips on track 1 (Synth)
    int defaultClips = static_cast<int>(engine.getReadModel().snapshot().clips.size());

    cmds.addMidiClip(0, 0.0, 4.0, "Extra");
    EXPECT_GT(static_cast<int>(engine.getReadModel().snapshot().clips.size()), defaultClips);

    cmds.newProject();
    // After new project: default project has a few clips (Synth Melody/Chords)
    auto snap = engine.getReadModel().snapshot();
    EXPECT_FALSE(snap.clips.empty()); // default project has clips
}

TEST(ProjectLifecycle, SaveAndLoadRoundTrip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTempo(150.0);
    cmds.setTimeSignature(3, 4);
    cmds.setScaleRoot(7);  // G
    cmds.setScaleMode(1);  // Minor

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "SaveTest");
    ASSERT_GT(clipId, 0);
    cmds.addNote(clipId, 60, 100, 0.0, 1.0);

    QString path = QDir::tempPath() + "/hdaw_test_save.hdaw";
    bool saved = cmds.saveProject(path.toStdString());
    ASSERT_TRUE(saved);

    cmds.newProject();
    EXPECT_NE(engine.getReadModel().getTransport().bpm, 150.0);

    bool loaded = cmds.loadProject(path.toStdString());
    ASSERT_TRUE(loaded);

    auto t = engine.getReadModel().getTransport();
    EXPECT_DOUBLE_EQ(t.bpm, 150.0);
    auto snap = engine.getReadModel().snapshot();
    EXPECT_EQ(snap.scaleRoot, 7);
    EXPECT_EQ(snap.scaleMode, 1);

    QFile::remove(path);
}

// ============================================================================
// CLIP PROPERTIES
// ============================================================================

TEST(ClipProperties, SetClipStart)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "MoveMe");
    ASSERT_GT(clipId, 0);

    cmds.setClipStart(clipId, 10.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.startBeat, 10.0);
}

TEST(ClipProperties, SetClipDuration)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "ResizeMe");
    ASSERT_GT(clipId, 0);

    cmds.setClipDuration(clipId, 8.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.durationBeats, 8.0);
}

TEST(ClipProperties, SetClipGain)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "GainTest");
    ASSERT_GT(clipId, 0);

    cmds.setClipGain(clipId, 0.5f);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.gain, 0.5);
}

TEST(ClipProperties, SetClipFadeIn)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "FadeIn");
    ASSERT_GT(clipId, 0);

    cmds.setClipFadeIn(clipId, 1.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.fadeIn, 1.0);
}

TEST(ClipProperties, SetClipFadeOut)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "FadeOut");
    ASSERT_GT(clipId, 0);

    cmds.setClipFadeOut(clipId, 2.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.fadeOut, 2.0);
}

TEST(ClipProperties, SetClipOffset)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "Offset");
    ASSERT_GT(clipId, 0);

    cmds.setClipOffset(clipId, 1.5);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.offset, 1.5);
}

TEST(ClipProperties, SetClipLooping)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "LoopTest");
    ASSERT_GT(clipId, 0);

    cmds.setClipLooping(clipId, true);
    auto clip = requireClip(engine, clipId);
    EXPECT_TRUE(clip.looping);

    cmds.setClipLooping(clipId, false);
    clip = requireClip(engine, clipId);
    EXPECT_FALSE(clip.looping);
}

TEST(ClipProperties, SetClipMuted)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "MuteClip");
    ASSERT_GT(clipId, 0);

    cmds.setClipMuted(clipId, true);
    auto clip = requireClip(engine, clipId);
    EXPECT_TRUE(clip.muted);

    cmds.setClipMuted(clipId, false);
    clip = requireClip(engine, clipId);
    EXPECT_FALSE(clip.muted);
}

// ============================================================================
// CLIP TIMESTRETCH (MIDI clips — values persist even without audio source)
// ============================================================================

TEST(ClipTimestretch, SetSourceBpm)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "BpmClip");
    ASSERT_GT(clipId, 0);

    cmds.setClipSourceBpm(clipId, 128.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.sourceBpm, 128.0);
}

TEST(ClipTimestretch, SetStretchMode)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "StretchClip");
    ASSERT_GT(clipId, 0);

    cmds.setClipStretchMode(clipId, 2); // ManualRatio
    auto clip = requireClip(engine, clipId);
    EXPECT_EQ(clip.stretchMode, 2);
}

TEST(ClipTimestretch, SetStretchRatio)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "RatioClip");
    ASSERT_GT(clipId, 0);

    cmds.setClipStretchRatio(clipId, 1.5);
    auto clip = requireClip(engine, clipId);
    EXPECT_DOUBLE_EQ(clip.stretchRatio, 1.5);
}

// ============================================================================
// CLIP SLICING
// ============================================================================

TEST(ClipSlicing, SliceAtTimesSplitsClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 8.0, "SliceMe");
    ASSERT_GT(clipId, 0);
    cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    cmds.addNote(clipId, 62, 100, 3.0, 1.0);
    cmds.addNote(clipId, 64, 100, 6.0, 1.0);

    cmds.sliceClipAtTimes(clipId, {2.0, 5.0});

    EXPECT_FALSE(clipExists(engine, clipId));
    auto t0 = clipsOnTrack0(engine);
    int pieces = 0;
    for (const auto& c : t0)
        if (c.name == "SliceMe")
            ++pieces;
    EXPECT_GE(pieces, 2);
}

TEST(ClipSlicing, SliceAtPlayheadDoesNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 8.0, "PlayheadSlice");
    ASSERT_GT(clipId, 0);

    // Should not crash even on MIDI clip
    cmds.sliceClipAtPlayhead(clipId);
}

// ============================================================================
// GAIN ENVELOPE
// ============================================================================

TEST(GainEnvelope, AddAndRetrievePoints)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "EnvClip");
    ASSERT_GT(clipId, 0);

    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);
    cmds.addGainEnvelopePoint(clipId, 4.0, 1.0);

    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    ASSERT_EQ(env.size(), 3u);
    EXPECT_DOUBLE_EQ(env[0].time, 0.0);
    EXPECT_DOUBLE_EQ(env[0].gain, 1.0);
    EXPECT_DOUBLE_EQ(env[1].time, 2.0);
    EXPECT_DOUBLE_EQ(env[1].gain, 0.5);
}

TEST(GainEnvelope, MovePoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "MoveEnv");
    ASSERT_GT(clipId, 0);

    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);

    cmds.moveGainEnvelopePoint(clipId, 1, 3.0, 0.25);
    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    ASSERT_GE(env.size(), 2u);
    EXPECT_NEAR(env[1].time, 3.0, 0.001);
    EXPECT_NEAR(env[1].gain, 0.25, 0.001);
}

TEST(GainEnvelope, RemovePoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "RmEnv");
    ASSERT_GT(clipId, 0);

    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);
    cmds.addGainEnvelopePoint(clipId, 4.0, 1.0);

    cmds.removeGainEnvelopePoint(clipId, 1);
    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_EQ(env.size(), 2u);
}

TEST(GainEnvelope, ClearEnvelope)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "ClearEnv");
    ASSERT_GT(clipId, 0);

    cmds.addGainEnvelopePoint(clipId, 0.0, 1.0);
    cmds.addGainEnvelopePoint(clipId, 2.0, 0.5);

    cmds.clearGainEnvelope(clipId);
    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_TRUE(env.empty());
}

TEST(GainEnvelope, SetBulkPoints)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "BulkEnv");
    ASSERT_GT(clipId, 0);

    std::vector<std::pair<double, double>> pts = {{0.0, 1.0}, {1.0, 0.7}, {3.0, 0.0}};
    cmds.setClipGainEnvelope(clipId, pts);
    auto env = engine.getReadModel().getClipGainEnvelope(clipId);
    EXPECT_EQ(env.size(), 3u);
}

// ============================================================================
// TRACK PROPERTIES: ARMED, INPUT MONITOR, HEIGHT, COLLAPSED, HIDDEN,
// MIDI CHANNEL, TYPE, COLOR
// ============================================================================

TEST(TrackProperties, SetTrackArmed)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackArmed(0, true);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_TRUE(track.armed);

    cmds.setTrackArmed(0, false);
    track = engine.getReadModel().getTrack(0);
    EXPECT_FALSE(track.armed);
}

TEST(TrackProperties, SetTrackInputMonitor)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackInputMonitor(0, true);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_TRUE(track.inputMonitor);

    cmds.setTrackInputMonitor(0, false);
    track = engine.getReadModel().getTrack(0);
    EXPECT_FALSE(track.inputMonitor);
}

TEST(TrackProperties, SetTrackHeight)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackHeight(0, 200);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_DOUBLE_EQ(track.height, 200.0);
}

TEST(TrackProperties, SetTrackMidiChannel)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackMidiChannel(0, 10);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_EQ(track.midiChannel, 10);
}

TEST(TrackProperties, SetTrackType)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int idx = cmds.addTrack("InstTrack");
    cmds.setTrackType(idx, 1); // instrument

    auto trackTree = engine.getProjectModel().getTrackListTree().getChild(idx);
    if (trackTree.isValid())
        EXPECT_EQ(static_cast<int>(trackTree.getProperty(IDs::trackType, 0)), 1);
}

TEST(TrackProperties, SetTrackColor)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackColor(0, 0xFF59e0c4);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_EQ(track.color, 0xFF59e0c4);
}

TEST(TrackProperties, SetTrackCollapsed)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Add a folder track, then a child, and collapse the folder
    int folderIdx = cmds.addTrack("Folder");
    cmds.setTrackType(folderIdx, 2);
    int childIdx = cmds.addTrack("Child");
    cmds.moveTrackIntoFolder(childIdx, folderIdx);
    cmds.setTrackCollapsed(folderIdx, true);

    // Verify via ValueTree directly (ReadModel may not expose collapsed on all types)
    auto trackTree = engine.getProjectModel().getTrackListTree().getChild(folderIdx);
    if (trackTree.isValid())
        EXPECT_TRUE(static_cast<bool>(trackTree.getProperty(IDs::isCollapsed, false)));
}

TEST(TrackProperties, SetTrackHidden)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackHidden(0, true);
    auto track = engine.getReadModel().getTrack(0);
    EXPECT_TRUE(track.isHidden);

    cmds.setTrackHidden(0, false);
    track = engine.getReadModel().getTrack(0);
    EXPECT_FALSE(track.isHidden);
}

TEST(TrackProperties, MoveTrackIntoAndOutOfFolder)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int folderIdx = cmds.addTrack("Folder");
    cmds.setTrackType(folderIdx, 2);
    int childIdx = cmds.addTrack("Child");

    cmds.moveTrackIntoFolder(childIdx, folderIdx);

    auto trackTree = engine.getProjectModel().getTrackListTree().getChild(childIdx);
    ASSERT_TRUE(trackTree.isValid());
    int parent = static_cast<int>(trackTree.getProperty(IDs::parentId, -1));
    EXPECT_EQ(parent, folderIdx);

    cmds.moveTrackOutOfFolder(childIdx);
    parent = static_cast<int>(trackTree.getProperty(IDs::parentId, -1));
    EXPECT_EQ(parent, -1);
}

TEST(TrackProperties, MoveTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addTrack("First");
    auto snap = engine.getReadModel().snapshot();
    int origCount = static_cast<int>(snap.tracks.size());
    // Move last track to first position
    cmds.moveTrack(origCount - 1, 0);
    auto moved = engine.getReadModel().getTrack(0);
    EXPECT_EQ(moved.name, "First");
}

// ============================================================================
// NOTE MUTATIONS
// ============================================================================

TEST(NoteMutations, SetNotePitch)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "PitchNote");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    cmds.setNotePitch(noteId, 72);
    auto notes = engine.getReadModel().getNotes(clipId);
    for (const auto& n : notes)
    {
        if (n.noteId == noteId)
        {
            EXPECT_EQ(n.pitch, 72);
            break;
        }
    }
}

TEST(NoteMutations, SetNoteVelocity)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "VelNote");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    cmds.setNoteVelocity(noteId, 50);
    auto notes = engine.getReadModel().getNotes(clipId);
    for (const auto& n : notes)
    {
        if (n.noteId == noteId)
        {
            EXPECT_EQ(n.velocity, 50);
            break;
        }
    }
}

TEST(NoteMutations, SetNoteStart)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "StartNote");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    cmds.setNoteStart(noteId, 2.0);
    auto notes = engine.getReadModel().getNotes(clipId);
    for (const auto& n : notes)
    {
        if (n.noteId == noteId)
        {
            EXPECT_DOUBLE_EQ(n.startBeat, 2.0);
            break;
        }
    }
}

TEST(NoteMutations, SetNoteDuration)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "DurNote");
    int noteId = cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    cmds.setNoteDuration(noteId, 2.0);
    auto notes = engine.getReadModel().getNotes(clipId);
    for (const auto& n : notes)
    {
        if (n.noteId == noteId)
        {
            EXPECT_DOUBLE_EQ(n.durationBeats, 2.0);
            break;
        }
    }
}

TEST(NoteMutations, ClearNotes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "ClearClip");
    ASSERT_GT(clipId, 0);

    cmds.addNote(clipId, 60, 100, 0.0, 1.0);
    cmds.addNote(clipId, 62, 100, 1.0, 1.0);
    cmds.addNote(clipId, 64, 100, 2.0, 1.0);
    EXPECT_EQ(engine.getReadModel().getNotes(clipId).size(), 3u);

    cmds.clearNotes(clipId);
    EXPECT_TRUE(engine.getReadModel().getNotes(clipId).empty());
}

// ============================================================================
// FX: BYPASS, PARAM, REMOVE
// ============================================================================

TEST(FxSurface, AddAndBypassFxSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, 0); // EQ
    EXPECT_FALSE(engine.getReadModel().getFxSlots(0).empty());
    EXPECT_FALSE(engine.getReadModel().getFxSlots(0)[0].bypassed);

    cmds.setFxSlotBypassed(0, 0, true);
    EXPECT_TRUE(engine.getReadModel().getFxSlots(0)[0].bypassed);

    cmds.setFxSlotBypassed(0, 0, false);
    EXPECT_FALSE(engine.getReadModel().getFxSlots(0)[0].bypassed);
}

TEST(FxSurface, SetFxSlotParam)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, 0); // EQ
    cmds.setFxSlotParam(0, 0, 0, 0.75f);
    cmds.setFxSlotParam(0, 99, 0, 0.5f);
    cmds.setFxSlotParam(99, 0, 0, 0.5f);
}

TEST(FxSurface, RemoveFxSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, 0);
    cmds.addFxSlot(0, 1);
    EXPECT_EQ(static_cast<int>(engine.getReadModel().getFxSlots(0).size()), 2);

    cmds.removeFxSlot(0, 0);
    EXPECT_EQ(static_cast<int>(engine.getReadModel().getFxSlots(0).size()), 1);
}

TEST(FxSurface, AddMultipleInternalFxTypes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, 0); // EQ
    cmds.addFxSlot(0, 1); // Compressor
    cmds.addFxSlot(0, 2); // Reverb

    EXPECT_GE(static_cast<int>(engine.getReadModel().getFxSlots(0).size()), 3);
}

TEST(FxSurface, BypassMidiFxSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiFxSlot(0, "arpeggiator");
    EXPECT_FALSE(engine.getReadModel().getMidiFxSlots(0).empty());
    EXPECT_FALSE(engine.getReadModel().getMidiFxSlots(0)[0].bypassed);

    cmds.setMidiFxSlotBypassed(0, 0, true);
    EXPECT_TRUE(engine.getReadModel().getMidiFxSlots(0)[0].bypassed);

    cmds.setMidiFxSlotBypassed(0, 0, false);
    EXPECT_FALSE(engine.getReadModel().getMidiFxSlots(0)[0].bypassed);
}

TEST(FxSurface, RemoveMidiFxSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiFxSlot(0, "arpeggiator");
    cmds.addMidiFxSlot(0, "chord");
    EXPECT_EQ(static_cast<int>(engine.getReadModel().getMidiFxSlots(0).size()), 2);

    cmds.removeMidiFxSlot(0, 0);
    EXPECT_EQ(static_cast<int>(engine.getReadModel().getMidiFxSlots(0).size()), 1);
}

// ============================================================================
// AUTOMATION: POINTS, MODE, ENABLE/DISABLE, REMOVE LANE
// ============================================================================

TEST(AutomationSurface, AddAndRemoveAutomationPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "TestVolLane", 0); // paramID=0 (unbound) to avoid dedup
    cmds.addAutomationPoint(0, "TestVolLane", 0.0, 0.0f);
    cmds.addAutomationPoint(0, "TestVolLane", 4.0, 1.0f);

    auto points = engine.getReadModel().getAutomationPoints(0, "TestVolLane");
    EXPECT_EQ(points.size(), 2u);

    cmds.removeAutomationPoint(0, "TestVolLane", 0.0);
    points = engine.getReadModel().getAutomationPoints(0, "TestVolLane");
    EXPECT_EQ(points.size(), 1u);
}

TEST(AutomationSurface, SetAutomationEnabled)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "TestLane", 1);

    cmds.setAutomationEnabled(0, "TestLane", false);
    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "TestLane")
        {
            EXPECT_FALSE(l.enabled);
            break;
        }
    }

    cmds.setAutomationEnabled(0, "TestLane", true);
    lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "TestLane")
        {
            EXPECT_TRUE(l.enabled);
            break;
        }
    }
}

TEST(AutomationSurface, SetAutomationMode)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "ModeLane", 1);
    cmds.setAutomationMode(0, "ModeLane", "touch");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
    {
        if (l.name == "ModeLane")
        {
            EXPECT_EQ(l.mode, "touch");
            break;
        }
    }
}

TEST(AutomationSurface, RemoveAutomationLane)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "TempLane", 1);
    EXPECT_FALSE(engine.getReadModel().getAutomationLanes(0).empty());

    cmds.removeAutomationLane(0, "TempLane");
    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes)
        EXPECT_NE(l.name, "TempLane");
}

TEST(AutomationSurface, NotifyAutomationTouchDoesNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // Should not crash
    cmds.notifyAutomationTouch(0, 1, true);
    cmds.notifyAutomationTouch(0, 1, false);
}

// ============================================================================
// TEMPO POINTS
// ============================================================================

TEST(TempoPoints, AddAndRemoveTempoPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int idx = cmds.addTempoPoint(5.0, 140.0);
    EXPECT_GE(idx, 0);

    auto points = engine.getReadModel().getTempoPoints();
    bool found = false;
    for (const auto& p : points)
    {
        if (std::abs(p.bpm - 140.0) < 0.01)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    cmds.removeTempoPoint(idx);
}

TEST(TempoPoints, SetTempoPointBpm)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int idx = cmds.addTempoPoint(10.0, 120.0);
    ASSERT_GE(idx, 0);

    cmds.setTempoPointBpm(idx, 150.0);
    auto points = engine.getReadModel().getTempoPoints();
    bool found = false;
    for (const auto& p : points)
    {
        if (std::abs(p.timeSeconds - 10.0) < 0.1)
        {
            EXPECT_NEAR(p.bpm, 150.0, 0.01);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TRANSACTION LIFECYCLE
// ============================================================================

TEST(TransactionLifecycle, BeginEndTransactionGroupsMutations)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int before = engine.getReadModel().getTrackCount();

    cmds.beginTransaction("add two tracks");
    cmds.addTrack("T1");
    cmds.addTrack("T2");
    cmds.endTransaction();

    EXPECT_EQ(engine.getReadModel().getTrackCount(), before + 2);

    // Undo should revert the whole transaction
    cmds.undo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), before);

    cmds.redo();
    EXPECT_EQ(engine.getReadModel().getTrackCount(), before + 2);
}

// ============================================================================
// SCALE
// ============================================================================

TEST(Scale, SetScaleRootAndMode)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setScaleRoot(5);  // F
    cmds.setScaleMode(2);  // Dorian

    auto snap = engine.getReadModel().snapshot();
    EXPECT_EQ(snap.scaleRoot, 5);
    EXPECT_EQ(snap.scaleMode, 2);
}

// ============================================================================
// MARKERS: SET MARKER TIME
// ============================================================================

TEST(MarkerSurface, SetMarkerTime)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int idx = cmds.addMarker("MoveMe", 4.0);
    ASSERT_GE(idx, 0);

    cmds.setMarkerTime(idx, 16.0);
    auto markers = engine.getReadModel().getMarkers();
    for (const auto& m : markers)
    {
        if (m.index == idx)
        {
            EXPECT_DOUBLE_EQ(m.time, 16.0);
            break;
        }
    }
}

// ============================================================================
// BATCH OPS: addClips, removeClips, moveClips, duplicateClips
// ============================================================================

TEST(BatchOps, AddClipsReturnsMultipleIds)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    std::vector<double> starts = {0.0, 4.0, 8.0};
    std::vector<double> durations = {4.0, 4.0, 4.0};
    std::vector<std::string> names = {"A", "B", "C"};

    auto ids = cmds.addClips(0, starts, durations, names);
    EXPECT_EQ(ids.size(), 3u);
    for (int id : ids)
        EXPECT_GT(id, 0);

    auto snap = engine.getReadModel().snapshot();
    int foundCount = 0;
    for (const auto& c : snap.clips)
        for (int id : ids)
            if (c.clipId == id)
                ++foundCount;
    EXPECT_EQ(foundCount, 3);
}

TEST(BatchOps, RemoveClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(0, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(0, 4.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    cmds.removeClips({a, b});
    EXPECT_FALSE(clipExists(engine, a));
    EXPECT_FALSE(clipExists(engine, b));
}

TEST(BatchOps, MoveClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addTrack("T2");
    int a = cmds.addMidiClip(0, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(0, 4.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    cmds.moveClips({a, b}, {10.0, 14.0}, {1, 1});

    auto ca = requireClip(engine, a);
    auto cb = requireClip(engine, b);
    EXPECT_DOUBLE_EQ(ca.startBeat, 10.0);
    EXPECT_EQ(ca.trackIndex, 1);
    EXPECT_DOUBLE_EQ(cb.startBeat, 14.0);
    EXPECT_EQ(cb.trackIndex, 1);
}

TEST(BatchOps, DuplicateClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addTrack("T2");
    int a = cmds.addMidiClip(0, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(0, 4.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    auto ids = cmds.duplicateClips({a, b}, {10.0, 14.0}, {1, 1});
    EXPECT_EQ(ids.size(), 2u);
    for (int id : ids)
        EXPECT_GT(id, 0);

    auto ca = requireClip(engine, ids[0]);
    auto cb = requireClip(engine, ids[1]);
    EXPECT_DOUBLE_EQ(ca.startBeat, 10.0);
    EXPECT_EQ(ca.trackIndex, 1);
    EXPECT_DOUBLE_EQ(cb.startBeat, 14.0);
    EXPECT_EQ(cb.trackIndex, 1);
}

TEST(BatchOps, PaintClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int src = cmds.addMidiClip(0, 0.0, 4.0, "Src");
    ASSERT_GT(src, 0);

    auto ids = cmds.paintClips({src}, 8.0, 4.0, 0, 3);
    EXPECT_EQ(ids.size(), 3u);
    for (int id : ids)
        EXPECT_GT(id, 0);
}

// ============================================================================
// SESSION: launchScene, stopAllSessionClips
// ============================================================================

TEST(SessionSurface, LaunchSceneUpdatesLaunchedScene)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.launchScene(3);

    // launchScene sets the SessionManager atomic, not the ValueTree snapshot field
    EXPECT_EQ(engine.getSessionManager().getLaunchedScene(), 3);
}

TEST(SessionSurface, StopAllSessionClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.createSessionClip(0, 0, true);
    cmds.launchScene(0);

    cmds.stopAllSessionClips();
    auto snap = engine.getReadModel().snapshot();
    EXPECT_EQ(snap.launchedScene, -1);
}

// ============================================================================
// UNDO/REDO: canUndo, canRedo, getUndoDescriptions
// ============================================================================

TEST(UndoRedoSurface, CanUndoCanRedo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    EXPECT_FALSE(cmds.canRedo());

    cmds.beginTransaction("add-track");
    cmds.addTrack("UndoTest");
    cmds.endTransaction();

    EXPECT_TRUE(cmds.canUndo());
    EXPECT_FALSE(cmds.canRedo());

    cmds.undo();
    EXPECT_TRUE(cmds.canRedo());

    cmds.redo();
    EXPECT_TRUE(cmds.canUndo());
    EXPECT_FALSE(cmds.canRedo());
}

TEST(UndoRedoSurface, GetUndoDescriptions)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.beginTransaction("My Operation");
    cmds.addTrack("Test");
    cmds.endTransaction();

    auto descs = cmds.getUndoDescriptions();
    EXPECT_FALSE(descs.empty());
}

// ============================================================================
// AUDIO GRAPH: rebuildRoutingGraph does not crash
// ============================================================================

TEST(AudioGraphSurface, RebuildRoutingGraphDoesNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& ag = engine.getAudioGraphCommands();

    // Add some clips and tracks to make it non-trivial
    engine.getProjectCommands().addMidiClip(0, 0.0, 4.0, "Clip1");
    engine.getProjectCommands().addTrack("T2");
    engine.getProjectCommands().addMidiClip(1, 0.0, 4.0, "Clip2");

    ag.rebuildRoutingGraph();
    ag.rebuildTrackFX(0);
    ag.rebuildAutomationCache(0);
    ag.rebuildModulation(0);
}

// ============================================================================
// CLIP SOURCE FILE RELINKING
// ============================================================================

TEST(RelinkSurface, FindMissingClipSourceFile)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "RelinkClip");
    ASSERT_GT(clipId, 0);

    // MIDI clips have no source file — should return empty string
    std::string result = cmds.findMissingClipSourceFile(clipId, "/");
    EXPECT_TRUE(result.empty());
}

// ============================================================================
// TRACK: MOVE CLIP (basic)
// ============================================================================

TEST(ClipMoveSurface, MoveClipToNewTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addTrack("T2");
    int clipId = cmds.addMidiClip(0, 0.0, 4.0, "MoveClip");
    ASSERT_GT(clipId, 0);

    cmds.moveClip(clipId, 1, 5.0);
    auto clip = requireClip(engine, clipId);
    EXPECT_EQ(clip.trackIndex, 1);
    EXPECT_DOUBLE_EQ(clip.startBeat, 5.0);
}

// ============================================================================
// MODULATION: addLfo, removeLfo, setLfoParam
// ============================================================================

TEST(ModulationSurface, AddRemoveLfo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addLfo(0);
    auto lfos = engine.getReadModel().getModulationLfos(0);
    EXPECT_FALSE(lfos.empty());

    int lfoIndex = lfos[0].index;
    cmds.removeLfo(0, lfoIndex);
    lfos = engine.getReadModel().getModulationLfos(0);
    EXPECT_TRUE(lfos.empty());
}

TEST(ModulationSurface, SetLfoParam)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addLfo(0);
    auto lfos = engine.getReadModel().getModulationLfos(0);
    ASSERT_FALSE(lfos.empty());

    cmds.setLfoParam(0, 0, "rate", 4.0);
    lfos = engine.getReadModel().getModulationLfos(0);
    EXPECT_NEAR(lfos[0].rate, 4.0, 0.01);
}

// ============================================================================
// EDGE CASES: operations on invalid indices
// ============================================================================

TEST(EdgeCases, SetClipPropertiesOnInvalidClipDoesNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // All should be safe no-ops
    cmds.setClipStart(9999, 5.0);
    cmds.setClipDuration(9999, 5.0);
    cmds.setClipGain(9999, 0.5f);
    cmds.setClipFadeIn(9999, 1.0);
    cmds.setClipFadeOut(9999, 1.0);
    cmds.setClipOffset(9999, 1.0);
    cmds.setClipLooping(9999, true);
    cmds.setClipMuted(9999, true);
}

TEST(EdgeCases, SetTrackPropertiesOnInvalidIndexDoesNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setTrackVolume(-1, 0.5f);
    cmds.setTrackPan(-1, 0.5f);
    cmds.setTrackMuted(-1, true);
    cmds.setTrackSoloed(-1, true);
    cmds.setTrackArmed(-1, true);
    cmds.setTrackInputMonitor(-1, true);
    cmds.setTrackHeight(-1, 100);
    cmds.setTrackMidiChannel(-1, 1);
    cmds.setTrackHidden(-1, true);
    cmds.setTrackName(-1, "Invalid");
}

TEST(EdgeCases, NoteOperationsOnInvalidIdsDoNotCrash)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setNotePitch(9999, 60);
    cmds.setNoteVelocity(9999, 100);
    cmds.setNoteStart(9999, 1.0);
    cmds.setNoteDuration(9999, 1.0);
    cmds.removeNote(9999);
}

// ============================================================================
// PROJECT BACKUP
// ============================================================================

TEST(ProjectBackup, SaveCreatesTimestampedBackup)
{
    auto tmpDir = juce::File::createTempFile("hdaw_backup_test");
    tmpDir.deleteFile();
    tmpDir.createDirectory();
    auto projectFile = tmpDir.getChildFile("test.hdaw");

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 4.0, "BackupTest");

    bool saved = cmds.saveProject(projectFile.getFullPathName().toStdString());
    ASSERT_TRUE(saved);

    auto backupDir = tmpDir.getChildFile("auto-backups").getChildFile("test");
    ASSERT_TRUE(backupDir.isDirectory());

    auto files = backupDir.findChildFiles(juce::File::findFiles, false, "*.hdaw");
    ASSERT_EQ(files.size(), 1);
    EXPECT_TRUE(files[0].getFileName().contains("["));

    tmpDir.deleteRecursively();
}

TEST(ProjectBackup, PrunesOldestBeyondCap)
{
    auto tmpDir = juce::File::createTempFile("hdaw_backup_test");
    tmpDir.deleteFile();
    tmpDir.createDirectory();
    auto projectFile = tmpDir.getChildFile("test.hdaw");

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 4.0, "BackupTest");

    const int maxBackups = 3;
    const int totalSaves = 5;

    for (int i = 0; i < totalSaves; ++i)
    {
        bool saved = cmds.saveProject(projectFile.getFullPathName().toStdString());
        ASSERT_TRUE(saved);
    }

    auto backupDir = tmpDir.getChildFile("auto-backups").getChildFile("test");
    ASSERT_TRUE(backupDir.isDirectory());

    auto filesBefore = backupDir.findChildFiles(juce::File::findFiles, false, "*.hdaw");
    ASSERT_EQ(filesBefore.size(), totalSaves);

    HDAW::backupProject(projectFile, maxBackups);

    auto files = backupDir.findChildFiles(juce::File::findFiles, false, "*.hdaw");
    ASSERT_EQ(files.size(), maxBackups);

    tmpDir.deleteRecursively();
}

TEST(ProjectBackup, DoesNotCrashOnMissingSourceFile)
{
    auto tmpDir = juce::File::createTempFile("hdaw_backup_test");
    tmpDir.deleteFile();
    tmpDir.createDirectory();
    auto nonexistent = tmpDir.getChildFile("nonexistent.hdaw");

    HDAW::backupProject(nonexistent);

    auto backupDir = tmpDir.getChildFile("auto-backups");
    EXPECT_FALSE(backupDir.exists());

    tmpDir.deleteRecursively();
}
