#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "../common/ReadModel.h"
#include <unordered_map>
#include <set>

namespace frontend {

// Accumulates granular juce::ValueTree change callbacks over a debounce window
// and coalesces them into a minimal delta (changed/removed clips, changed
// tracks). Any change it cannot cleanly represent as a delta (track add/remove,
// markers, tempo, FX, automation, sub-clip detail, reorder) sets a fullSync flag
// so the client falls back to a whole-snapshot re-fetch.
//
// Used by FrontendTreeWatcher. Unit-testable in isolation (no server needed).
class TreeDeltaAccumulator {
public:
    void notePropertyChanged(const juce::ValueTree& tree);
    void noteChildAdded(const juce::ValueTree& child);
    void noteChildRemoved(const juce::ValueTree& child);
    void noteStructuralChange();

    bool fullSync() const { return fullSync_; }
    const std::unordered_map<int, ClipSnapshot>& clipsUpserted() const { return clipsUpserted_; }
    const std::set<int>& clipsRemoved() const { return clipsRemoved_; }
    const std::unordered_map<int, TrackSnapshot>& tracksUpserted() const { return tracksUpserted_; }
    bool empty() const {
        return !fullSync_ && clipsUpserted_.empty() && clipsRemoved_.empty() && tracksUpserted_.empty();
    }
    void reset();

private:
    void upsertClip(const juce::ValueTree& clipTree);
    void removeClip(const juce::ValueTree& clipTree);
    void upsertTrack(const juce::ValueTree& trackTree);

    std::unordered_map<int, ClipSnapshot> clipsUpserted_;   // clipId -> latest snapshot
    std::set<int> clipsRemoved_;
    std::unordered_map<int, TrackSnapshot> tracksUpserted_; // trackIndex -> latest snapshot
    bool fullSync_ = false;
};

} // namespace frontend
