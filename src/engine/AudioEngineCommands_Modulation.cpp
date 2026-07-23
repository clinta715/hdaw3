#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"

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
