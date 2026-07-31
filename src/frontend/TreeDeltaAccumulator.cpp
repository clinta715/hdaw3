#include "TreeDeltaAccumulator.h"
#include "../engine/ReadModelImpl.h"
#include "../model/ProjectModel.h"

namespace frontend {

void TreeDeltaAccumulator::notePropertyChanged(const juce::ValueTree& tree, const juce::Identifier& property) {
    if (fullSync_) return;
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
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) upsertClip(child);
    else                              escalateToFullSync();  // TRACK add (indices shift), notes, markers, ...
}

void TreeDeltaAccumulator::noteChildRemoved(const juce::ValueTree& child) {
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) removeClip(child);
    else                              escalateToFullSync();  // TRACK remove, notes, markers, ...
}

void TreeDeltaAccumulator::noteStructuralChange() {
    escalateToFullSync();
}

void TreeDeltaAccumulator::escalateToFullSync() {
    fullSync_ = true;
    clipsUpserted_.clear();
    clipsRemoved_.clear();
    tracksUpserted_.clear();
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
