#pragma once
#include "ModulationSource.h"
#include "../model/ProjectModel.h"
#include <vector>
#include <memory>
#include <atomic>

namespace HDAW {

class ModulationManager {
public:
    ModulationManager() = default;

    void prepare(double sampleRate);

    // Rebuild the source list from the track's MODULATION_LIST ValueTree.
    // Called on the UI thread under stateLock.
    void rebuild(const juce::ValueTree& modulationListTree, double sampleRate);

    // Called per-sample from the audio thread.
    // Returns the sum of all enabled modulation source outputs targeting paramID.
    float getModulation(int paramID, double bpm, double sampleRate);

    int getNumSources() const { return static_cast<int>(sources.size()); }
    LFOModulationSource* getSource(int index);

private:
    std::vector<std::unique_ptr<LFOModulationSource>> sources;
    double sampleRate = 44100.0;
};

// ── inline implementations ──

inline void ModulationManager::prepare(double sr)
{
    sampleRate = sr;
    for (auto& s : sources)
        if (s) s->prepare(sr);
}

inline void ModulationManager::rebuild(const juce::ValueTree& modListTree, double sr)
{
    sampleRate = sr;
    sources.clear();
    if (!modListTree.isValid()) return;

    for (int i = 0; i < modListTree.getNumChildren(); ++i)
    {
        auto modTree = modListTree.getChild(i);
        juce::String type = modTree.getProperty("type", "lfo").toString();
        if (type != "lfo") continue;

        auto src = std::make_unique<LFOModulationSource>();
        src->fromValueTree(modTree);
        src->prepare(sr);
        sources.push_back(std::move(src));
    }
}

inline float ModulationManager::getModulation(int paramID, double bpm, double sr)
{
    float sum = 0.0f;
    for (auto& s : sources)
    {
        if (!s || !s->isEnabled()) continue;
        if (s->getTargetParamID() != paramID) continue;
        sum += s->getNextValue(bpm, sr);
    }
    return sum;
}

inline LFOModulationSource* ModulationManager::getSource(int index)
{
    if (index < 0 || index >= static_cast<int>(sources.size()))
        return nullptr;
    return sources[index].get();
}

} // namespace HDAW
