#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"
// The LFO param vocabulary + its shared unknown-name refusal, used by the
// batched setLfoParams (and, from slice 3 on, the single set_lfo_param tool).
#include "../common/FxParamBatchJson.h"

// ─── ProjectCommands — MIDI CC ──────────────────────────────────────────

int AudioEngineCommands::addModulation(int trackIndex, const juce::ValueTree& modulationTree)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid())
    {
        modList = juce::ValueTree(IDs::MODULATION_LIST);
        track.addChild(modList, -1, &um);
    }

    int lfoIndex = modList.getNumChildren();
    auto newMod = modulationTree.createCopy();
    modList.addChild(newMod, -1, &um);
    return lfoIndex;
}

void AudioEngineCommands::removeModulation(int trackIndex, int lfoIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    modList.removeChild(modList.getChild(lfoIndex), &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

void AudioEngineCommands::setModulationProperty(int trackIndex, int lfoIndex,
    const std::string& propertyID, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto modList = track.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    auto modTree = modList.getChild(lfoIndex);
    auto propName = juce::Identifier(juce::String(propertyID));
    modTree.setProperty(propName, static_cast<double>(value), &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

// ─── ProjectCommands — Modulation (LFO) ──────────────────────────

void AudioEngineCommands::addLfo(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid())
    {
        modList = juce::ValueTree(IDs::MODULATION_LIST);
        trackTree.addChild(modList, -1, &um);
    }

    auto newMod = juce::ValueTree(IDs::MODULATION);
    newMod.setProperty("id", juce::String("lfo_") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty("type", "lfo", nullptr);
    newMod.setProperty(IDs::name, juce::String("LFO ") + juce::String(modList.getNumChildren() + 1), nullptr);
    newMod.setProperty(IDs::waveform, 0, nullptr);
    newMod.setProperty(IDs::rate, 1.0, nullptr);
    newMod.setProperty(IDs::rateSync, true, nullptr);
    newMod.setProperty(IDs::depth, 0.3, nullptr);
    newMod.setProperty(IDs::bipolar, false, nullptr);
    newMod.setProperty(IDs::phaseOffset, 0.0, nullptr);
    newMod.setProperty(IDs::targetParamID, 1, nullptr);
    newMod.setProperty(IDs::enabled, true, nullptr);
    modList.addChild(newMod, -1, &um);
}

void AudioEngineCommands::removeLfo(int trackIndex, int lfoIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    modList.removeChild(modList.getChild(lfoIndex), &um);
}

void AudioEngineCommands::setLfoParam(int trackIndex, int lfoIndex,
                                      const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return;

    auto modTree = modList.getChild(lfoIndex);

    if (paramName == "waveform")
        modTree.setProperty(IDs::waveform, static_cast<int>(value), &um);
    else if (paramName == "rate")
        modTree.setProperty(IDs::rate, value, &um);
    else if (paramName == "rateSync")
        modTree.setProperty(IDs::rateSync, value != 0.0, &um);
    else if (paramName == "depth")
        modTree.setProperty(IDs::depth, value, &um);
    else if (paramName == "bipolar")
        modTree.setProperty(IDs::bipolar, value != 0.0, &um);
    else if (paramName == "phaseOffset")
        modTree.setProperty(IDs::phaseOffset, value, &um);
    else if (paramName == "targetParamID")
        modTree.setProperty(IDs::targetParamID, static_cast<int>(value), &um);
    else if (paramName == "enabled")
        modTree.setProperty(IDs::enabled, value != 0.0, &um);
}

// N LFO param writes in ONE undo transaction — PARTIAL-APPLY with per-write
// errors (the setFxParams / set_cells precedent). Each write loops the single
// setLfoParam; the vocabulary is validated HERE (setLfoParam's if/else chain has
// no else branch, so a typo used to be a silent no-op — lesson 38) with the
// shared unknown-name text, and the track/LFO bounds are checked before the
// write (setLfoParam itself silently no-ops on bad indices). paramBatchActive_
// is set for symmetry with the bus batch and to make the ONE-unit guarantee
// robust if setLfoParam ever gains a boundary of its own.
ProjectCommands::BatchResult
AudioEngineCommands::setLfoParams(const std::vector<LfoParamWrite>& writes,
                                  std::vector<std::string>* errors)
{
    BatchResult result;
    if (errors) errors->assign(writes.size(), std::string());
    if (writes.empty())
    {
        result.error = "writes must not be empty";
        return result;
    }

    auto trackList = engine_.getProjectModel().getTrackListTree();

    // Order matters: begin the unit BEFORE suppressing per-write boundaries,
    // and clear the flag BEFORE the seal so endTransaction itself runs.
    beginTransaction("Set LFO params");
    paramBatchActive_ = true;
    int written = 0;
    std::string firstError;
    for (std::size_t i = 0; i < writes.size(); ++i)
    {
        const LfoParamWrite& w = writes[i];
        std::string err;
        if (w.trackIndex < 0 || w.trackIndex >= trackList.getNumChildren())
            err = "track not found";
        else
        {
            auto modList = trackList.getChild(w.trackIndex).getChildWithName(IDs::MODULATION_LIST);
            if (!modList.isValid() || w.lfoIndex < 0 || w.lfoIndex >= modList.getNumChildren())
                err = "trackId or lfoIndex out of range";
            else if (!HDAW::isLfoParamName(w.paramName))
                err = HDAW::unknownLfoParamError(w.paramName);
        }
        if (!err.empty())
        {
            if (firstError.empty()) firstError = err;
            if (errors) (*errors)[i] = err;
            continue;
        }
        setLfoParam(w.trackIndex, w.lfoIndex, w.paramName, w.value);
        ++written;
    }
    paramBatchActive_ = false;   // clear BEFORE the seal so endTransaction runs
    endTransaction();

    result.applied = written;
    result.ok = written > 0;
    if (written == 0) result.error = firstError;
    return result;
}
