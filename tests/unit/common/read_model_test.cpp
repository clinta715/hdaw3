#include <gtest/gtest.h>
#include "engine/ReadModelImpl.h"
#include "model/ProjectModel.h"

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
    ReadModelImpl readModel(model);
    EXPECT_GT(readModel.getTrackCount(), 0);
    auto track = readModel.getTrack(0);
    EXPECT_EQ(track.index, 0);
}

TEST(ReadModel, DefaultProjectTrackCount)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    EXPECT_EQ(readModel.getTrackCount(), 3);
}

TEST(ReadModel, TrackProperties)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
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
    ReadModelImpl readModel(model);
    auto track = readModel.getTrack(1);
    EXPECT_EQ(track.name, "Synth");
    EXPECT_DOUBLE_EQ(track.volume, 0.85);
    EXPECT_EQ(track.midiChannel, 1);
}

TEST(ReadModel, ClipSnapshot)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    EXPECT_FALSE(snap.clips.empty());

    bool foundMidi = false;
    for (const auto& clip : snap.clips) {
        if (clip.isMidi) {
            foundMidi = true;
            EXPECT_FALSE(clip.name.empty());
            EXPECT_GT(clip.durationBeats, 0.0);
        }
    }
    EXPECT_TRUE(foundMidi);
}

TEST(ReadModel, GetClipById)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    ASSERT_FALSE(snap.clips.empty());
    int id = snap.clips[0].clipId;
    auto clip = readModel.getClip(id);
    EXPECT_EQ(clip.clipId, id);
}

TEST(ReadModel, GetNotesForMidiClip)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto snap = readModel.snapshot();
    ASSERT_FALSE(snap.clips.empty());

    for (const auto& clip : snap.clips) {
        if (clip.isMidi) {
            auto notes = readModel.getNotes(clip.clipId);
            EXPECT_FALSE(notes.empty());
            for (const auto& note : notes) {
                EXPECT_GE(note.pitch, 0);
                EXPECT_LE(note.pitch, 127);
                EXPECT_GT(note.velocity, 0);
                EXPECT_GE(note.durationBeats, 0.0);
            }
            return;
        }
    }
    FAIL() << "No MIDI clip found in default project";
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
    EXPECT_DOUBLE_EQ(transport.loopEnd, 8.0);
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
    EXPECT_EQ(track.index, 0);
}

TEST(ReadModel, ClipCountPerTrack)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);
    auto t0 = readModel.getTrack(0);
    EXPECT_EQ(t0.clipCount, 0);

    auto t1 = readModel.getTrack(1);
    EXPECT_EQ(t1.clipCount, 2);
}
