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

class ClipClipboard
{
public:
    static ClipboardEntry deepCopy(const juce::ValueTree& sourceClip);
    static void copyClips(const std::vector<ClipboardEntry>& entries);
    static const std::vector<ClipboardEntry>& getClips();
    static bool hasContent();
    static void clear();
};
}
