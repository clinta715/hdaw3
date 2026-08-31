#pragma once
#include "ModulationSource.h"
#include "../model/ProjectModel.h"
#include <vector>
#include <memory>
#include <atomic>

namespace HDAW {

// ── FM modulation paramIDs (track-level LFO → PsyFm engine) ──
// Range 300-309 reserved for FM modulation targets.
// The ModulationManager / Track::processBlock applies these to the
// PsyFmEngine's mod source pool or directly to operator params.
namespace FmModParamIDs {
    static constexpr int Op1Ratio     = 300;  // LFO → operator 1 ratio offset
    static constexpr int Op2Ratio     = 301;  // LFO → operator 2 ratio offset
    static constexpr int Op3Ratio     = 302;  // LFO → operator 3 ratio offset
    static constexpr int Op4Ratio     = 303;  // LFO → operator 4 ratio offset
    static constexpr int Op5Ratio     = 304;  // LFO → operator 5 ratio offset
    static constexpr int Op6Ratio     = 305;  // LFO → operator 6 ratio offset
    static constexpr int Op6Feedback  = 306;  // LFO → operator 6 feedback
    static constexpr int OutputLevel  = 307;  // LFO → master output level
    static constexpr int RatioSweepRate = 308; // LFO → ratio-sweep LFO rate (nested)
    static constexpr int FirstFmParam = 300;
    static constexpr int LastFmParam  = 308;
}


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
    int getSourceParamID(int index) const;

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

inline int ModulationManager::getSourceParamID(int index) const
{
    if (index < 0 || index >= static_cast<int>(sources.size()))
        return -1;
    return sources[index]->getTargetParamID();
}

} // namespace HDAW
