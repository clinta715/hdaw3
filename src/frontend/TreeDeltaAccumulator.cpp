#include "TreeDeltaAccumulator.h"
#include "../engine/ReadModelImpl.h"
#include "../model/ProjectModel.h"

namespace frontend {

void TreeDeltaAccumulator::notePropertyChanged(const juce::ValueTree& tree) {
    if (fullSync_) return;
    const auto type = tree.getType();
    if (type == IDs::CLIP)        upsertClip(tree);
    else if (type == IDs::TRACK)  upsertTrack(tree);
    else                          fullSync_ = true;   // PROJECT/MARKER/FX/automation/sub-clip detail/...
}

void TreeDeltaAccumulator::noteChildAdded(const juce::ValueTree& child) {
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) upsertClip(child);
    else                              fullSync_ = true;  // TRACK add (indices shift), notes, markers, ...
}

void TreeDeltaAccumulator::noteChildRemoved(const juce::ValueTree& child) {
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) removeClip(child);
    else                              fullSync_ = true;  // TRACK remove, notes, markers, ...
}

void TreeDeltaAccumulator::noteStructuralChange() {
    fullSync_ = true;
}

void TreeDeltaAccumulator::upsertClip(const juce::ValueTree& clipTree) {
    ClipSnapshot snap = buildClipSnapshotFromTree(clipTree);
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
