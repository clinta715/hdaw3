#pragma once
#include <juce_core/juce_core.h>

namespace HDAW {

struct RegionClipboardEntry {
    juce::String sourceFile;
    double offset = 0.0;
    double duration = 0.0;
};

class RegionClipboard {
public:
    static void store(const RegionClipboardEntry& entry);
    static const RegionClipboardEntry& get();
    static bool hasContent();
    static void clear();
};

} // namespace HDAW
