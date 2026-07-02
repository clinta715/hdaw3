#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace HDAW
{
struct ClipboardEntry
{
    juce::ValueTree clipTree;      // Deep copy of the source clip
    int sourceTrackIndex = -1;     // Track the clip originally lived on
    double sourceStartTime = 0.0;  // Start time at copy time (used to offset paste)
};

// Metadata for the clipboard as a whole — the minimum startTime across
// all entries. Used by pasteClips to offset the group so the earliest
// clip lands at the playhead.
struct ClipboardMeta
{
    double minStartTime = 0.0;
};

class ClipClipboard
{
public:
    static ClipboardEntry deepCopy(const juce::ValueTree& sourceClip);
    static void copyClips(const std::vector<ClipboardEntry>& entries);
    static const std::vector<ClipboardEntry>& getClips();
    static const ClipboardMeta& getMeta();
    static bool hasContent();
    static void clear();
};
}
