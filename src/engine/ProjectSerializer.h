#pragma once
#include <juce_core/juce_core.h>
#include "../model/ProjectModel.h"

class MainAudioProcessor;

namespace HDAW {

class ProjectSerializer
{
public:
    static bool save(ProjectModel& model, const juce::File& file, MainAudioProcessor* processor = nullptr);
    static bool load(ProjectModel& model, const juce::File& file);
    static void createNew(ProjectModel& model);
};

// Guard against plugin-state size regressions on save: a load→save round trip
// once shrank 177KB plugin states to ~262B stubs (the live child's
// getStateInformation returned a default stub and the serializer overwrote
// the real blob). Returns true when the freshly read state should REPLACE the
// existing tree blob:
// - empty/absent existing, or tiny existing (<4KB decoded): trust the read
// - substantial existing: replace only when the new read is non-trivial
//   (>=1KB and >= existing/8) — a suspiciously tiny read keeps the previous
//   blob and the caller logs the regression.
inline bool shouldReplacePluginState(long long existingDecodedBytes, size_t newBytes)
{
    if (newBytes == 0)
        return false;
    if (existingDecodedBytes < 4096)
        return true;
    return newBytes >= 1024 && static_cast<long long>(newBytes) >= existingDecodedBytes / 8;
}

} // namespace HDAW
