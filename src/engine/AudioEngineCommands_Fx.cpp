#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../engine/PluginManager.h"

// ─── ProjectCommands — FX operations ──────────────────────────────

void AudioEngineCommands::addFxSlot(int trackIndex, int type, int position,
                                    const std::string& pluginId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, &um);
    }

    // Map integer type to string
    std::string typeStr;
    switch (type)
    {
        case 0: typeStr = "eq"; break;
        case 1: typeStr = "compressor"; break;
        case 2: typeStr = "reverb"; break;
        case 3: typeStr = "delay"; break;
        case 4: typeStr = "chorus"; break;
        case 5: typeStr = "flanger"; break;
        case 6: typeStr = "phaser"; break;
        default: typeStr = "plugin"; break;
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(typeStr), &um);
    if (typeStr == "plugin" && !pluginId.empty())
    {
        slot.setProperty(IDs::pluginID, juce::String(pluginId), &um);
        slot.setProperty(IDs::pluginFormat,
            juce::String(engine_.getProjectModel().resolvePluginFormat(pluginId)), &um);
        slot.setProperty(IDs::name,
            juce::String(resolvePluginName(pluginId)), &um);
    }
    slot.setProperty(IDs::bypassed, false, &um);

    int n = fxChain.getNumChildren();
    int insertIdx = (position < 0 || position > n) ? n : position;
    fxChain.addChild(slot, insertIdx, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::addFxSlot(int trackIndex, const std::string& type,
                                    int position, const std::string& pluginId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, &um);
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(type), &um);
    if (type == "plugin" && !pluginId.empty())
    {
        slot.setProperty(IDs::pluginID, juce::String(pluginId), &um);
        slot.setProperty(IDs::pluginFormat,
            juce::String(engine_.getProjectModel().resolvePluginFormat(pluginId)), &um);
        slot.setProperty(IDs::name,
            juce::String(resolvePluginName(pluginId)), &um);
    }
    slot.setProperty(IDs::bypassed, false, &um);

    int n = fxChain.getNumChildren();
    int insertIdx = (position < 0 || position > n) ? n : position;
    fxChain.addChild(slot, insertIdx, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::addMidiFxSlot(int trackIndex, const std::string& type, int position)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;

    auto track = trackList.getChild(trackIndex);
    auto chain = track.getChildWithName(IDs::MIDI_FX_CHAIN);
    if (!chain.isValid())
    {
        chain = juce::ValueTree(IDs::MIDI_FX_CHAIN);
        track.addChild(chain, -1, &um);
    }

    juce::ValueTree slot(IDs::MIDI_FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(type), &um);
    slot.setProperty(IDs::bypassed, false, &um);
    if (type == "arpeggiator")
    {
        slot.setProperty(IDs::arpRate, 0.25, &um);
        slot.setProperty(IDs::arpPattern, 0, &um);
        slot.setProperty(IDs::arpOctaves, 1, &um);
        slot.setProperty(IDs::arpGate, 0.5, &um);
    }
    else if (type == "velocity")
    {
        slot.setProperty(IDs::velFactor, 1.0, &um);
    }
    else if (type == "chord")
    {
        slot.setProperty(IDs::chordType, 0, &um);
    }
    else if (type == "scale")
    {
        slot.setProperty(IDs::scaleRoot, 0, &um);
        slot.setProperty(IDs::scaleType, 0, &um);
    }
    else if (type == "notelength")
    {
        slot.setProperty(IDs::lengthFactor, 1.0, &um);
    }

    int n = chain.getNumChildren();
    int insertIdx = (position < 0 || position > n) ? n : position;
    chain.addChild(slot, insertIdx, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildMidiTrackFX(trackIndex);
}

void AudioEngineCommands::removeMidiFxSlot(int trackIndex, int slotIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findMidiFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.getParent().removeChild(slot, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildMidiTrackFX(trackIndex);
}

void AudioEngineCommands::setMidiFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findMidiFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.setProperty(IDs::bypassed, bypassed, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildMidiTrackFX(trackIndex);
}

juce::ValueTree AudioEngineCommands::findMidiFxSlot(int trackIndex, int slotIndex) const
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return {};
    auto chain = trackList.getChild(trackIndex).getChildWithName(IDs::MIDI_FX_CHAIN);
    if (!chain.isValid() || slotIndex < 0 || slotIndex >= chain.getNumChildren())
        return {};
    return chain.getChild(slotIndex);
}

void AudioEngineCommands::removeFxSlot(int trackIndex, int slotIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.getParent().removeChild(slot, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (slot.isValid())
        slot.setProperty(IDs::bypassed, bypassed, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                                         float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    juce::String propName = "param_" + juce::String(paramIndex);
    slot.setProperty(juce::Identifier(propName), static_cast<double>(value), &um);
}

void AudioEngineCommands::reorderFxSlots(int trackIndex, int fromSlot, int toSlot)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid()) return;
    int n = fxChain.getNumChildren();
    if (fromSlot < 0 || fromSlot >= n || toSlot < 0 || toSlot >= n) return;
    if (fromSlot == toSlot) return;
    auto slot = fxChain.getChild(fromSlot);
    fxChain.removeChild(fromSlot, &um);
    if (toSlot > fromSlot) --toSlot;
    fxChain.addChild(slot, toSlot, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setFxSlotPlugin(int trackIndex, int slotIndex,
    const std::string& fxType, const std::string& pluginID,
    const std::string& pluginFormat, const std::string& pluginPath)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    slot.setProperty(IDs::fxType, juce::String(fxType), &um);
    slot.setProperty(IDs::pluginID, juce::String(pluginID), &um);
    slot.setProperty(IDs::pluginFormat, juce::String(pluginFormat), &um);
    slot.setProperty(IDs::pluginPath, juce::String(pluginPath), &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}
