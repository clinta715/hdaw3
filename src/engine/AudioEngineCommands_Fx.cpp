#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../engine/PluginManager.h"
#include "../proxy/PluginProxySlot.h"
#include "engine/SliceDetector.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>

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
    addFxSlotInternal(trackIndex, type, position, pluginId);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::addFxSlotInternal(int trackIndex, const std::string& type,
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
    else if (type == "transpose")
    {
        slot.setProperty(IDs::semitones, 0, &um);
    }
    else if (type == "keyfilter")
    {
        slot.setProperty(IDs::keyFilterRoot, 0, &um);
        slot.setProperty(IDs::keyFilterScale, 0, &um);
    }
    else if (type == "multinote")
    {
        slot.setProperty(IDs::multiNoteIntervals, juce::String("0,7,12"), &um);
    }
    else if (type == "velocitycurve")
    {
        slot.setProperty(IDs::curveType, 0, &um);
        slot.setProperty(IDs::curveAmount, 0.5, &um);
    }
    else if (type == "notechance")
    {
        slot.setProperty(IDs::noteChance, 1.0, &um);
    }
    else if (type == "mididelay")
    {
        slot.setProperty(IDs::delayBeats, 0.25, &um);
        slot.setProperty(IDs::delayFeedback, 0.0, &um);
        slot.setProperty(IDs::delayMix, 0.5, &um);
    }
    else if (type == "humanize")
    {
        slot.setProperty(IDs::humanizeTiming, 0.0, &um);
        slot.setProperty(IDs::humanizeVelocity, 0.0, &um);
        slot.setProperty(IDs::humanizePitch, 0.0, &um);
    }
    else if (type == "strum")
    {
        slot.setProperty(IDs::strumTime, 0.02, &um);
        slot.setProperty(IDs::strumDirection, 0, &um);
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

void AudioEngineCommands::setMidiFxSlotParam(int trackIndex, int slotIndex,
                                              const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slotTree = findMidiFxSlot(trackIndex, slotIndex);
    if (!slotTree.isValid()) return;
    slotTree.setProperty(juce::Identifier(paramName), value, &um);

    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* track = proc->getTrack(trackIndex))
        {
            auto& chain = track->getMidiFxChain();
            if (slotIndex >= 0 && slotIndex < static_cast<int>(chain.size()) && chain[slotIndex])
            {
                auto defs = HDAW::getMidiFxParamDefs(chain[slotIndex]->getType());
                for (const auto& def : defs)
                {
                    if (paramName == def.name)
                    {
                        float normalized = (def.maxValue != def.minValue)
                            ? static_cast<float>((value - def.minValue) / (def.maxValue - def.minValue))
                            : 0.0f;
                        normalized = juce::jlimit(0.0f, 1.0f, normalized);
                        chain[slotIndex]->setAutomationParam(def.index, normalized);
                        break;
                    }
                }
            }
        }
    }
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

    const bool pluginChanged = slot.getProperty(IDs::pluginID).toString() != juce::String(pluginID);

    slot.setProperty(IDs::fxType, juce::String(fxType), &um);
    slot.setProperty(IDs::pluginID, juce::String(pluginID), &um);
    slot.setProperty(IDs::pluginFormat, juce::String(pluginFormat), &um);
    slot.setProperty(IDs::pluginPath, juce::String(pluginPath), &um);

    if (pluginChanged)
    {
        slot.removeProperty(IDs::pluginState, &um);
        slot.removeProperty(juce::Identifier("pluginStateB"), &um);
    }

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setSamplerSample(int trackIndex, int slotIndex,
                                           const std::string& filePath, int rootNote)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    slot.setProperty(juce::Identifier("sampleFile"), juce::String(filePath), &um);
    slot.setProperty(juce::Identifier("rootNote"), rootNote, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setSamplerMode(int trackIndex, int slotIndex,
                                         const std::string& mode)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    juce::String m = juce::String(mode).trim().toLowerCase();
    if (m != "classic" && m != "one-shot" && m != "slice")
        return;

    slot.setProperty(juce::Identifier("mode"), m, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setSamplerProperty(int trackIndex, int slotIndex,
                                             const std::string& property, bool value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    if (property == "mono" || property == "playReverse")
        slot.setProperty(juce::Identifier(property), value, &um);
    else if (property == "transpose" || property == "baseNote")
        slot.setProperty(juce::Identifier(property), static_cast<int>(value), &um);
    else
        return;

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::setSamplerSliceMode(int trackIndex, int slotIndex,
                                              const std::string& sliceMode,
                                              double sliceGrid, double sliceSensitivity)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    juce::String sm = juce::String(sliceMode).trim().toLowerCase();
    if (sm != "transient" && sm != "grid")
        return;

    slot.setProperty(juce::Identifier("sliceMode"), sm, &um);
    slot.setProperty(juce::Identifier("sliceGrid"), sliceGrid, &um);
    slot.setProperty(juce::Identifier("sliceSensitivity"), sliceSensitivity, &um);
}

AudioEngineCommands::SamplerDetectionResult AudioEngineCommands::detectSamplerSlices(
    int trackIndex, int slotIndex, const std::string& sliceMode, double sliceGrid, double sliceSensitivity)
{
    SamplerDetectionResult result;
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return result;

    juce::String sampleFile = slot.getProperty("sampleFile", "").toString();
    if (sampleFile.isEmpty()) return result;

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(juce::File(sampleFile)));
    if (!reader) return result;
    const int64_t len = reader->lengthInSamples;
    if (len <= 0) return result;

    std::vector<int64_t> points;
    const bool grid = (juce::String(sliceMode).trim().toLowerCase() == "grid");
    if (grid)
    {
        const double bpm = engine_.getTransportManager().getBPM();
        points = HDAW::SliceDetector::grid(len, reader->sampleRate, bpm, sliceGrid);
    }
    else
    {
        // Cap the decode so a pathological huge file cannot exhaust memory;
        // detection on the capped region is fine for the >2^28-sample case.
        const int readLen = static_cast<int>((std::min)(len, static_cast<int64_t>(1) << 28));
        std::vector<float> mono(static_cast<size_t>(readLen), 0.0f);
        juce::AudioBuffer<float> buf(1, readLen);
        if (!reader->read(&buf, 0, readLen, 0, true, true))
            return result;
        const float* ch0 = buf.getReadPointer(0);
        std::copy(ch0, ch0 + readLen, mono.begin());
        points = HDAW::SliceDetector::transient(mono, sliceSensitivity);
    }
    if (points.size() < 2) return result;

    // Store normalized (0..1), comma-separated, for loadSamplerState to restore.
    juce::String parts;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i) parts += ",";
        parts += juce::String(points[i] / static_cast<double>(len), 6);
    }
    slot.setProperty(juce::Identifier("sliceMode"), juce::String(sliceMode), &um);
    slot.setProperty(juce::Identifier("sliceGrid"), sliceGrid, &um);
    slot.setProperty(juce::Identifier("sliceSensitivity"), sliceSensitivity, &um);
    slot.setProperty(juce::Identifier("slicePoints"), parts, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);

    result.ok = true;
    result.totalSlices = static_cast<int>(points.size()) - 1;
    for (int64_t p : points)
        result.slicePoints.push_back(static_cast<float>(p / static_cast<double>(len)));
    return result;
}

AudioEngineCommands::SamplerTriggerResult AudioEngineCommands::triggerSamplerSlice(
    int trackIndex, int slotIndex, int sliceIndex, float velocity)
{
    SamplerTriggerResult result;
    auto* proc = engine_.getMainProcessor();
    if (!proc) return result;
    auto* track = proc->getTrack(trackIndex);
    if (!track) return result;
    auto& chain = track->getFXChain();
    if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size()) || !chain[slotIndex])
        return result;
    auto* slot = chain[slotIndex].get();
    if (slot->getType() != "sampler") return result;
    auto* eng = slot->samplerEngineForTest();
    if (!eng) return result;
    const auto* sound = eng->currentSound();
    if (!sound || sound->slicePoints.size() < 2) return result;
    if (sliceIndex < 0 || sliceIndex >= static_cast<int>(sound->slicePoints.size()) - 1)
        return result;
    eng->triggerSlice(sliceIndex, velocity);
    result.ok = true;
    result.totalSlices = static_cast<int>(sound->slicePoints.size()) - 1;
    return result;
}

void AudioEngineCommands::respawnFxSlot(int trackIndex, int slotIndex)
{
    auto* proc = engine_.getMainProcessor();
    if (!proc) return;
    auto* track = proc->getTrack(trackIndex);
    if (!track) return;
    auto& chain = track->getFXChain();
    if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size())) return;
    auto* slot = chain[static_cast<size_t>(slotIndex)].get();
    if (!slot) return;
    auto* proxy = dynamic_cast<proxy::PluginProxySlot*>(slot->getPluginInstance());
    if (!proxy) return;
    engine_.getPluginManager().recovery().requestRespawn(proxy->getSlotId(), true);
}
