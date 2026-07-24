#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include "model/ProjectModel.h"
#include "engine/ReadModelImpl.h"
#include "frontend/TreeDeltaAccumulator.h"

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

using frontend::TreeDeltaAccumulator;

TEST(TreeDelta, ClipPropertyChangeUpsertsClip) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);

    EXPECT_FALSE(acc.fullSync());
    ASSERT_EQ(acc.clipsUpserted().size(), 1u);
    EXPECT_EQ(acc.clipsUpserted().at(5).clipId, 5);
    EXPECT_TRUE(acc.clipsRemoved().empty());
}

TEST(TreeDelta, RepeatedClipChangesCoalesceToLatest) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    clip.setProperty(IDs::startTime, 9.0, nullptr);   // simulate a drag
    acc.notePropertyChanged(clip);

    EXPECT_EQ(acc.clipsUpserted().size(), 1u);        // coalesced
    EXPECT_DOUBLE_EQ(acc.clipsUpserted().at(5).startBeat, 9.0);  // latest wins
}

TEST(TreeDelta, ClipRemovedThenReaddedCancels) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.noteChildRemoved(clip);
    EXPECT_EQ(acc.clipsRemoved().size(), 1u);
    acc.noteChildAdded(clip);                          // re-added
    EXPECT_TRUE(acc.clipsRemoved().empty());           // removal cancelled
    EXPECT_EQ(acc.clipsUpserted().size(), 1u);
}

TEST(TreeDelta, ClipAddAfterRemoveOfSameIdIsUpsert) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.noteChildAdded(clip);                          // upsert
    acc.noteChildRemoved(clip);                        // then removed
    EXPECT_EQ(acc.clipsRemoved().size(), 1u);
    EXPECT_TRUE(acc.clipsUpserted().empty());          // upsert dropped
}

TEST(TreeDelta, TrackPropertyChangeUpsertsTrack) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(track);
    EXPECT_FALSE(acc.fullSync());
    ASSERT_EQ(acc.tracksUpserted().size(), 1u);
    EXPECT_EQ(acc.tracksUpserted().at(0).name, "Synth");
}

TEST(TreeDelta, SubClipDetailChangeIsFullSync) {
    ValueTree note(IDs::MIDI_NOTE);
    note.setProperty(IDs::noteNumber, 60, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(note);
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, TrackAddIsFullSync) {
    ValueTree track = makeTrackTree("New", 1.0);
    TreeDeltaAccumulator acc;
    acc.noteChildAdded(track);
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, StructuralChangeIsFullSync) {
    TreeDeltaAccumulator acc;
    acc.noteStructuralChange();
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, FullSyncEscalationDiscardsPendingDelta) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    EXPECT_EQ(acc.clipsUpserted().size(), 1u);
    acc.noteStructuralChange();                        // escalates to fullSync
    EXPECT_TRUE(acc.fullSync());
    EXPECT_TRUE(acc.clipsUpserted().empty());          // pending delta discarded
    EXPECT_TRUE(acc.clipsRemoved().empty());
    EXPECT_TRUE(acc.tracksUpserted().empty());
}

TEST(TreeDelta, FullSyncLatchIgnoresFurtherChanges) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.noteStructuralChange();                        // latch fullSync
    acc.notePropertyChanged(clip);                     // ignored
    acc.notePropertyChanged(track);                    // ignored
    acc.noteChildAdded(clip);                          // ignored
    EXPECT_TRUE(acc.fullSync());
    EXPECT_TRUE(acc.clipsUpserted().empty());
    EXPECT_TRUE(acc.tracksUpserted().empty());
    EXPECT_TRUE(acc.clipsRemoved().empty());
}

TEST(TreeDelta, ResetClearsState) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    acc.reset();
    EXPECT_TRUE(acc.empty());
    EXPECT_FALSE(acc.fullSync());
}
