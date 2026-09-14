#include "TreeDeltaAccumulator.h"
#include "../engine/ReadModelImpl.h"
#include "../model/ProjectModel.h"

namespace frontend {

void TreeDeltaAccumulator::notePropertyChanged(const juce::ValueTree& tree, const juce::Identifier& property) {
    // B13: no early return on the fullSync latch — events keep accumulating
    // under it (see escalateToFullSync). The latch only pins the flush mode;
    // discarding events here is what swallowed the first mutation after a
    // seed burst (audit 2026-09-13).
    const auto type = tree.getType();
    if (type == IDs::CLIP) {
        upsertClip(tree);
    } else if (type == IDs::TRACK) {
        // Mute/solo changes affect effectiveMuted/effectiveSoloed which depend on
        // the parent chain — the delta path can't compute these, so we must fullSync
        // so the frontend receives the correct values from ReadModelImpl.
        // parentId/childIds change the folder hierarchy itself, which also
        // invalidates the effective mute/solo cascade for all descendants.
        if (property == IDs::isMuted || property == IDs::isSoloed
            || property == IDs::parentId || property == IDs::childIds) {
            escalateToFullSync();
            return;
        }
        upsertTrack(tree);
    } else {
        escalateToFullSync();
    }
}

void TreeDeltaAccumulator::noteChildAdded(const juce::ValueTree& child) {
    if (child.getType() == IDs::CLIP) upsertClip(child);
    else                              escalateToFullSync();  // TRACK add (indices shift), notes, markers, ...
}

void TreeDeltaAccumulator::noteChildRemoved(const juce::ValueTree& child) {
    if (child.getType() == IDs::CLIP) removeClip(child);
    else                              escalateToFullSync();  // TRACK remove, notes, markers, ...
}

void TreeDeltaAccumulator::noteStructuralChange() {
    escalateToFullSync();
}

void TreeDeltaAccumulator::escalateToFullSync() {
    // B13 (Modular Dawn audit): escalation must NOT discard the deltas already
    // accumulated in this debounce window. The old clear() swallowed them: a
    // burst that started with a delta (e.g. a clip upsert) and then escalated
    // (track add) lost the delta — the client's fullSync re-fetch covered the
    // final state, but the seed's own notification was never flushed before
    // the next mutation landed under the escalated latch, whose upsert was
    // then dropped. Retain the pending maps instead: the flush broadcasts
    // fullSync=true (the client re-fetches the whole snapshot, so retained
    // deltas cost nothing), and after the flush's reset() the next events
    // delta normally again.
    fullSync_ = true;
}

void TreeDeltaAccumulator::upsertClip(const juce::ValueTree& clipTree) {
    ClipSnapshot snap = buildClipSnapshotFromTree(clipTree, bpm_);
    clipsUpserted_[snap.clipId] = snap;
    clipsRemoved_.erase(snap.clipId);   // re-added cancels a pending removal
}

void TreeDeltaAccumulator::removeClip(const juce::ValueTree& clipTree) {
    const int clipId = static_cast<int>(clipTree.getProperty(IDs::clipID, 0));
    clipsRemoved_.insert(clipId);
    clipsUpserted_.erase(clipId);       // removed drops a pending upsert
}

void TreeDeltaAccumulator::upsertTrack(const juce::ValueTree& trackTree) {
    TrackSnapshot snap = buildTrackSnapshotFromTree(trackTree);
    tracksUpserted_[snap.index] = snap;
}

void TreeDeltaAccumulator::reset() {
    clipsUpserted_.clear();
    clipsRemoved_.clear();
    tracksUpserted_.clear();
    fullSync_ = false;
}

} // namespace frontend
