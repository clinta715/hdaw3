#include <gtest/gtest.h>
#include "engine/ReadModelImpl.h"
#include "model/ProjectModel.h"

// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Seed a minimal TRACK node with the same
// shape AudioEngineCommands::createTrackValueTree() builds, and return it.
static juce::ValueTree addSeedTrack(ProjectModel& model, const char* name)
{
    juce::ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, juce::String(name), nullptr);
    track.setProperty(IDs::volume, 1.0, nullptr);
    track.setProperty(IDs::pan, 0.0, nullptr);
    track.setProperty(IDs::isMuted, false, nullptr);
    track.setProperty(IDs::isSoloed, false, nullptr);
    track.setProperty(IDs::isArm, false, nullptr);
    track.setProperty(IDs::inputMonitor, false, nullptr);
    track.setProperty(IDs::midiChannel, 1, nullptr);
    track.setProperty(IDs::trackHeight, 80.0, nullptr);
    track.setProperty(IDs::trackType, 0, nullptr);
    track.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
    track.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, nullptr);
    track.addChild(ProjectModel::createTrackAutomationList(), -1, nullptr);
    model.getTrackListTree().addChild(track, -1, nullptr);
    return track;
}

TEST(ReadModel, EmptyProjectSnapshot)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    EXPECT_FALSE(snap.name.empty());
    EXPECT_EQ(readModel.getTrackCount(), static_cast<int>(snap.tracks.size()));
}

TEST(ReadModel, TrackQuery)
{
    ProjectModel model;
    model.createDefaultProject();
    // Zero-track default: seed the track this test queries (lesson 9).
    addSeedTrack(model, "Track 1");
    ReadModelImpl readModel(model);
    ASSERT_GT(readModel.getTrackCount(), 0);
    auto track = readModel.getTrack(0);
    EXPECT_EQ(track.index, 0);
}

TEST(ReadModel, DefaultProjectTrackCount)
{
    ProjectModel model;
    model.createDefaultProject();
    // New contract: a fresh default project owns NO tracks — tests and
    // sessions seed exactly the tracks they need.
    ReadModelImpl readModel(model);
    EXPECT_EQ(readModel.getTrackCount(), 0);
}

TEST(ReadModel, TrackProperties)
{
    ProjectModel model;
    model.createDefaultProject();
    // Zero-track default: seed the track this test reads (lesson 9).
    addSeedTrack(model, "Track 1");
    ReadModelImpl readModel(model);
    ASSERT_GT(readModel.getTrackCount(), 0);
    auto track = readModel.getTrack(0);
    EXPECT_EQ(track.name, "Track 1");
    EXPECT_DOUBLE_EQ(track.volume, 1.0);
    EXPECT_DOUBLE_EQ(track.pan, 0.0);
    EXPECT_FALSE(track.muted);
    EXPECT_FALSE(track.soloed);
}

TEST(ReadModel, Track2Properties)
{
    ProjectModel model;
    model.createDefaultProject();
    // Zero-track default: seed two tracks and put the asserted properties on
    // track 1 ourselves (lesson 9).
    addSeedTrack(model, "Track 1");
    auto synth = addSeedTrack(model, "Synth");
    synth.setProperty(IDs::volume, 0.85, nullptr);
    ReadModelImpl readModel(model);
    ASSERT_EQ(readModel.getTrackCount(), 2);
    auto track = readModel.getTrack(1);
    ASSERT_EQ(track.index, 1);
    EXPECT_EQ(track.name, "Synth");
    EXPECT_DOUBLE_EQ(track.volume, 0.85);
    EXPECT_EQ(track.midiChannel, 1);
}

TEST(ReadModel, ClipSnapshot)
{
    ProjectModel model;
    model.createDefaultProject();
    // The default project now ships empty; seed track 0 and a MIDI clip to
    // verify the snapshot reflects clip fields.
    addSeedTrack(model, "Track 1");
    auto clip = model.createMidiClipEmpty("TestClip", 0.0, 4.0);
    model.getTrackListTree().getChild(0)
        .getChildWithName(IDs::CLIP_LIST)
        .addChild(clip, -1, nullptr);

    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    EXPECT_FALSE(snap.clips.empty());

    bool foundMidi = false;
    for (const auto& c : snap.clips) {
        if (c.isMidi) {
            foundMidi = true;
            EXPECT_FALSE(c.name.empty());
            EXPECT_GT(c.durationBeats, 0.0);
        }
    }
    EXPECT_TRUE(foundMidi);
}

TEST(ReadModel, GetClipById)
{
    ProjectModel model;
    model.createDefaultProject();
    // The default project now ships empty; seed track 0 and a clip, then look
    // it up by id.
    addSeedTrack(model, "Track 1");
    auto clip = model.createMidiClipEmpty("TestClip", 0.0, 4.0);
    model.getTrackListTree().getChild(0)
        .getChildWithName(IDs::CLIP_LIST)
        .addChild(clip, -1, nullptr);
    int id = clip.getProperty(IDs::clipID);

    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    ASSERT_FALSE(snap.clips.empty());
    auto found = readModel.getClip(id);
    EXPECT_EQ(found.clipId, id);
}

TEST(ReadModel, GetNotesForMidiClip)
{
    ProjectModel model;
    model.createDefaultProject();
    // The default project now ships empty; seed track 0, a MIDI clip and a
    // note ourselves.
    addSeedTrack(model, "Track 1");
    auto clip = model.createMidiClipEmpty("TestClip", 0.0, 4.0);
    clip.getChildWithName(IDs::MIDI_NOTE_LIST)
        .addChild(model.createMidiNote(60, 0.8f, 0.0, 1.0), -1, nullptr);
    model.getTrackListTree().getChild(0)
        .getChildWithName(IDs::CLIP_LIST)
        .addChild(clip, -1, nullptr);
    int clipId = clip.getProperty(IDs::clipID);

    ReadModelImpl readModel(model);
    auto notes = readModel.getNotes(clipId);
    EXPECT_FALSE(notes.empty());
    for (const auto& note : notes) {
        EXPECT_GE(note.pitch, 0);
        EXPECT_LE(note.pitch, 127);
        EXPECT_GT(note.velocity, 0);
        EXPECT_GE(note.durationBeats, 0.0);
    }
}

TEST(ReadModel, TransportSnapshot)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto transport = readModel.getTransport();
    EXPECT_DOUBLE_EQ(transport.bpm, 120.0);
    EXPECT_FALSE(transport.isPlaying);
    EXPECT_FALSE(transport.isLooping);
    EXPECT_DOUBLE_EQ(transport.loopStart, 0.0);
    // loop region is stored seconds (default 8.0) and getTransport() converts to
    // beats for the frontend: 8.0s * bpm/60 = 8.0 * 120/60 = 16.0 beats.
    EXPECT_DOUBLE_EQ(transport.loopEnd, 16.0);
}

TEST(ReadModel, ScaleInfo)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    EXPECT_EQ(readModel.getScaleRoot(), 0);
    EXPECT_EQ(readModel.getScaleMode(), 0);
}

TEST(ReadModel, OutOfRangeTrackReturnsDefault)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto track = readModel.getTrack(999);
    EXPECT_TRUE(track.name.empty());
    EXPECT_EQ(track.index, -1);
}

TEST(ReadModel, ClipCountPerTrack)
{
    ProjectModel model;
    model.createDefaultProject();
    // Zero-track default: seed the two tracks this test reads (lesson 9).
    addSeedTrack(model, "Track 1");
    addSeedTrack(model, "Track 2");
    ReadModelImpl readModel(model);
    ASSERT_EQ(readModel.getTrackCount(), 2);
    auto t0 = readModel.getTrack(0);
    EXPECT_EQ(t0.clipCount, 0);

    auto t1 = readModel.getTrack(1);
    EXPECT_EQ(t1.clipCount, 0);
}

TEST(ReadModel, TempoPointsEmptyByDefault)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    // The default project's createTempoPointList() adds one point, so the
    // accessor must return exactly that.
    auto pts = readModel.getTempoPoints();
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_DOUBLE_EQ(pts[0].bpm, 120.0);
}

TEST(ReadModel, TempoPointsRoundTrip)
{
    ProjectModel model;
    model.createDefaultProject();
    auto projectTree = model.getTree();
    // Replace the default tempo list with two known points.
    auto existing = projectTree.getChildWithName(IDs::TEMPO_POINT_LIST);
    if (existing.isValid())
        projectTree.removeChild(existing, nullptr);

    auto tempoList = juce::ValueTree(IDs::TEMPO_POINT_LIST);
    {
        juce::ValueTree pt(IDs::TEMPO_POINT);
        pt.setProperty(IDs::startTime, 0.0, nullptr);
        pt.setProperty(IDs::tempo, 100.0, nullptr);
        tempoList.addChild(pt, -1, nullptr);
    }
    {
        juce::ValueTree pt(IDs::TEMPO_POINT);
        pt.setProperty(IDs::startTime, 8.0, nullptr);
        pt.setProperty(IDs::tempo, 140.0, nullptr);
        tempoList.addChild(pt, -1, nullptr);
    }
    projectTree.addChild(tempoList, -1, nullptr);

    ReadModelImpl readModel(model);
    auto pts = readModel.getTempoPoints();
    ASSERT_EQ(pts.size(), 2u);
    EXPECT_DOUBLE_EQ(pts[0].timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(pts[0].bpm, 100.0);
    EXPECT_DOUBLE_EQ(pts[1].timeSeconds, 8.0);
    EXPECT_DOUBLE_EQ(pts[1].bpm, 140.0);
}

TEST(ReadModel, AutomatableParamsEmptyWithoutEngine)
{
    // getAutomatableParams walks the live FX chain via the AudioEngine; with
    // no engine attached it must return empty rather than crash.
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto params = readModel.getAutomatableParams(0);
    EXPECT_TRUE(params.empty());
}
