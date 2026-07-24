#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include "model/ProjectModel.h"
#include "engine/ReadModelImpl.h"

using namespace juce;

namespace {

// Build a minimal PROJECT > TRACK_LIST > TRACK > CLIP_LIST > CLIP hierarchy so
// buildClipSnapshotFromTree can walk up to compute trackIndex.
ValueTree makeClipTree(int clipId, double startBeat, const String& name) {
    ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipID, clipId, nullptr);
    clip.setProperty(IDs::name, name, nullptr);
    clip.setProperty(IDs::startTime, startBeat, nullptr);
    clip.setProperty(IDs::duration, 4.0, nullptr);
    clip.setProperty(IDs::gain, 0.5, nullptr);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    clip.setProperty(IDs::looping, true, nullptr);
    return clip;
}

ValueTree makeTrackTree(const String& name, double volume) {
    ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, name, nullptr);
    track.setProperty(IDs::volume, volume, nullptr);
    track.setProperty(IDs::color, 7, nullptr);
    track.addChild(ValueTree(IDs::CLIP_LIST), -1, nullptr);
    return track;
}

} // namespace

TEST(TreeDeltaBuilders, ClipSnapshotFromTree) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 0.8);
    ValueTree clipList = track.getChildWithName(IDs::CLIP_LIST);
    ValueTree clip = makeClipTree(42, 4.0, "Riff");
    clipList.addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    ClipSnapshot cs = buildClipSnapshotFromTree(clip);
    EXPECT_EQ(cs.clipId, 42);
    EXPECT_EQ(cs.trackIndex, 0);          // first (only) track
    EXPECT_EQ(cs.name, "Riff");
    EXPECT_DOUBLE_EQ(cs.startBeat, 4.0);
    EXPECT_DOUBLE_EQ(cs.durationBeats, 4.0);
    EXPECT_DOUBLE_EQ(cs.gain, 0.5);
    EXPECT_TRUE(cs.isMidi);
    EXPECT_TRUE(cs.looping);
}

TEST(TreeDeltaBuilders, ClipSnapshotTrackIndexReflectsPosition) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree t0 = makeTrackTree("A", 1.0);
    ValueTree t1 = makeTrackTree("B", 1.0);
    ValueTree clip = makeClipTree(7, 0.0, "OnTrack1");
    t1.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(t0, -1, nullptr);
    trackList.addChild(t1, -1, nullptr);

    ClipSnapshot cs = buildClipSnapshotFromTree(clip);
    EXPECT_EQ(cs.trackIndex, 1);          // second track
}

TEST(TreeDeltaBuilders, TrackSnapshotFromTree) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Drums", 0.6);
    trackList.addChild(track, -1, nullptr);

    TrackSnapshot ts = buildTrackSnapshotFromTree(track);
    EXPECT_EQ(ts.index, 0);
    EXPECT_EQ(ts.name, "Drums");
    EXPECT_DOUBLE_EQ(ts.volume, 0.6);
    EXPECT_EQ(ts.color, 7);
    EXPECT_EQ(ts.clipCount, 0);
}
