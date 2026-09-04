#include "ChainLibrary.h"
#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../common/DebugLog.h"
#include "../model/ProjectModel.h"
#include "../engine/PluginManager.h"
#include "../proxy/PluginProxySlot.h"
#include "TrackFXSlot.h"
#include "engine/FmSynthEngine.h"
#include "engine/VirusSysexImport.h"
#include "engine/SliceDetector.h"
#include "engine/PsyFmState.h"
#include "engine/PsyFmModMatrix.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>

// Qt defines `slots` as a keyword macro (qobjectdefs.h); TUs in this project
// that pull in Qt headers (directly or transitively, e.g. via AudioEngine.h)
// would otherwise textually blank every `.slots` token of HDAW::ChainPreset
// below. ChainLibrary.h is included FIRST so the member declaration parses
// before the macro exists; the macro is undefined here, after all includes.
// This TU uses no Qt signals/slots keywords.
#ifdef slots
#undef slots
#endif

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
        case 7: typeStr = "filter"; break;
        case 8: typeStr = "sub_synth"; break;
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

    // Write-side clamp: internal FX param values land in the ValueTree and
    // are re-read verbatim on every rebuild/export (loadParamsFromTree), so
    // an out-of-range write would persist into saves and can drive recursive
    // DSP (reverb comb feedback) to inf/NaN — the "export silent after 0.6s"
    // bug. Clamp to the slot type's documented defs BEFORE the property
    // write. Plugin/none slots have no defs and pass through unchanged
    // (their params use the 0..1 plugin cache, not these defs).
    const juce::String fxType = slot.getProperty(IDs::fxType, "").toString();
    if (fxType.isNotEmpty() && fxType != "plugin" && fxType != "none")
    {
        auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxType);
        if (paramIndex >= 0 && paramIndex < static_cast<int>(defs.size()))
        {
            const auto& d = defs[static_cast<size_t>(paramIndex)];
            value = juce::jlimit(d.minValue, d.maxValue, value);
        }
    }

    juce::String propName = "param_" + juce::String(paramIndex);
    slot.setProperty(juce::Identifier(propName), static_cast<double>(value), &um);
}

void AudioEngineCommands::setFmPatch(int trackIndex, int slotIndex,
                                     const std::string& patchBase64)
{
    // Tree-first (deviceless-safe): the fmPatchData property is what
    // tree-copy renders (export/gain-stage/audition) and save/load restore.
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;
    if (slot.getProperty(IDs::fxType, "").toString() != "fm_synth") return;

    // Gate 9: validate at the command boundary — exactly 156 bytes.
    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(juce::String(patchBase64)))
        return;
    if (block.getSize() != FmSynthEngine::kPatchSize)
        return;

    // nullptr undo — matches the pluginState volatile-cache convention in
    // applyPluginProgram (AudioEngineCommands_Composition.cpp).
    slot.setProperty(IDs::fmPatchData, juce::String(patchBase64), nullptr);

    // Live load (best-effort): no crash when the live processor/track/slot is
    // absent — the tree write above is what headless/no-device exports need.
    if (auto* proc = engine_.getMainProcessor())
    {
        auto* track = proc->getTrack(trackIndex);
        if (track)
        {
            auto& chain = track->getFXChain();
            if (slotIndex >= 0 && slotIndex < static_cast<int>(chain.size()) && chain[slotIndex])
            {
                if (auto* fm = chain[slotIndex]->fmSynthEngine())
                    fm->loadPatch(static_cast<const uint8_t*>(block.getData()));
            }
        }
    }
}

AudioEngineCommands::VirusLoadResult AudioEngineCommands::loadVirusPatch(
    int trackIndex, int slotIndex, const std::string& filePath, int voiceIndex)
{
    VirusLoadResult result;
    auto fail = [&](const juce::String& msg) -> VirusLoadResult {
        result.error = msg.toStdString();
        return result;
    };

    // Gate 9: validate the slot is a sub_synth BEFORE touching anything.
    auto fxSlots = engine_.getReadModel().getFxSlots(trackIndex);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxSlots.size()))
        return fail("slot not found");
    if (fxSlots[static_cast<size_t>(slotIndex)].fxType != "sub_synth")
        return fail("slot is not a sub_synth");

    juce::File f(filePath);
    if (!f.existsAsFile())
        return fail("file not found: " + juce::String(filePath));

    juce::MemoryBlock raw;
    if (!f.loadFileAsData(raw))
        return fail("failed to read file");

    const auto* bytes = static_cast<const uint8_t*>(raw.getData());
    const size_t fileSize = raw.getSize();

    // Detect container: 267-byte B/C single, or a 524-byte-block TI bank
    // (a full bank is 67072 bytes). Both must carry the Access manufacturer
    // header F0 00 20 33 01.
    std::optional<HDAW::VirusPatch> patch;
    const bool hasVirusHeader = fileSize >= 5
        && bytes[0] == 0xF0 && bytes[1] == 0x00 && bytes[2] == 0x20
        && bytes[3] == 0x33 && bytes[4] == 0x01;

    if (fileSize == 267 && hasVirusHeader)
    {
        patch = HDAW::parseBcSingle(bytes, fileSize);
    }
    else if (fileSize >= 524 && fileSize % 524 == 0 && hasVirusHeader)
    {
        auto patches = HDAW::parseTiBank(bytes, fileSize);
        const int vi = voiceIndex < 0 ? 0 : voiceIndex;
        if (vi < 0 || vi >= static_cast<int>(patches.size()))
            return fail("voiceIndex out of range (" + juce::String(static_cast<int>(patches.size())) + " patches)");
        patch = std::move(patches[static_cast<size_t>(vi)]);
    }
    else
    {
        return fail("not a recognized Access Virus SysEx file "
                    "(expected 267-byte B/C single or 524-byte-block TI bank)");
    }

    if (!patch.has_value())
        return fail("failed to parse SysEx data (bad header, size, or checksum)");

    // One undo unit for the whole patch load (applyFxChain precedent): every
    // setFxSlotParam writes under &um between the two beginNewTransaction
    // calls coalesce into a single undo step. All validation happened above,
    // so this loop cannot fail — the slot only changes here.
    beginTransaction("Load Virus patch");
    int mappedCount = 0;
    for (int i = 0; i < 24; ++i)
    {
        if (patch->mapped[static_cast<size_t>(i)].has_value())
        {
            setFxSlotParam(trackIndex, slotIndex, i, *patch->mapped[static_cast<size_t>(i)]);
            ++mappedCount;
        }
    }
    endTransaction();

    result.ok = true;
    result.name = patch->name;
    result.bank = patch->bank;
    result.program = patch->program;
    result.mappedCount = mappedCount;
    result.unmapped = patch->unmapped;
    return result;
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

void AudioEngineCommands::setSamplerKeyRange(int trackIndex, int slotIndex,
                                             int keyLow, int keyHigh)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;
    if (slot.getProperty(IDs::fxType, "").toString() != "sampler") return;

    // Validate: both must be -1 (full range) or 0..127 with low <= high
    if (keyLow != -1 && keyHigh != -1)
    {
        keyLow = juce::jlimit(0, 127, keyLow);
        keyHigh = juce::jlimit(0, 127, keyHigh);
        if (keyLow > keyHigh) std::swap(keyLow, keyHigh);
    }
    else
    {
        keyLow = -1;
        keyHigh = -1;
    }

    slot.setProperty(IDs::keyRangeLow, keyLow, &um);
    slot.setProperty(IDs::keyRangeHigh, keyHigh, &um);

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

// ─── PsyFm preset/matrix commands (tree-first, deviceless-safe) ──────

bool AudioEngineCommands::setFxSlotPsyFmPreset(int trackIndex, int slotIndex,
                                               const std::string& presetName)
{
    auto preset = HDAW::PsyFmState::findPreset(presetName);
    if (!preset) return false;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return false;
    if (slot.getProperty(IDs::fxType).toString() != "psy_fm") return false;

    // Write all 33 params: ratios 0–5, feedback 6, envelopes 7–30, level 31, algorithm 32
    for (int i = 0; i < 6; ++i)
        slot.setProperty(juce::Identifier("param_" + juce::String(i)),
                         static_cast<double>(preset->ratios[i]), &um);
    slot.setProperty(juce::Identifier("param_6"),
                     static_cast<double>(preset->feedback), &um);
    for (int op = 0; op < 6; ++op)
    {
        int base = 7 + op * 4;
        for (int k = 0; k < 4; ++k)
            slot.setProperty(juce::Identifier("param_" + juce::String(base + k)),
                             static_cast<double>(preset->env[op][k]), &um);
    }
    slot.setProperty(juce::Identifier("param_31"),
                     static_cast<double>(preset->outputLevel), &um);
    slot.setProperty(juce::Identifier("param_32"),
                     static_cast<double>(preset->algorithm), &um);

    // Matrix + sweep rate
    slot.setProperty(juce::Identifier("psyFmMatrix"),
                     juce::String(preset->matrix), &um);
    slot.setProperty(juce::Identifier("psyFmSweepRate"),
                     static_cast<double>(preset->sweepRateHz), &um);
    return true;
}

void AudioEngineCommands::setFxSlotPsyFmModRoute(int trackIndex, int slotIndex,
                                                 const std::string& srcName,
                                                 const std::string& destName,
                                                 float depth)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;
    if (slot.getProperty(IDs::fxType).toString() != "psy_fm") return;

    // Read existing routes, upsert by source+dest key
    juce::String current = slot.getProperty("psyFmMatrix", "").toString();
    auto routes = HDAW::PsyFmState::decodeRoutes(current.toStdString());

    auto src = HDAW::PsyFmState::sourceFromName(srcName);
    auto dst = HDAW::PsyFmState::destFromName(destName);
    if (!src || !dst) return; // unknown name — silently ignore

    bool updated = false;
    for (auto& r : routes)
    {
        if (r.source == *src && r.dest == *dst)
        {
            r.depth = depth;
            updated = true;
            break;
        }
    }
    if (!updated)
        routes.push_back({ *src, *dst, depth });

    slot.setProperty(juce::Identifier("psyFmMatrix"),
                     juce::String(HDAW::PsyFmState::encodeRoutes(routes)), &um);
}

void AudioEngineCommands::clearFxSlotPsyFmModRoutes(int trackIndex, int slotIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;
    slot.setProperty(juce::Identifier("psyFmMatrix"), juce::String(), &um);
}

// ─── FX chain presets (plan 2026-09-02-fx-chain-presets, Task 2) ───

int AudioEngineCommands::getTrackCount() const
{
    return engine_.getProjectModel().getTrackListTree().getNumChildren();
}

HDAW::ChainPreset AudioEngineCommands::exportFxChain(int trackIndex)
{
    HDAW::ChainPreset preset;
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return preset;

    // 1. Pre-pass: capture live plugin state into the tree, so parameters
    // tweaked through a plugin's own UI are exported. Same pattern as
    // ProjectSerializer::save (ProjectSerializer.cpp:64-84). Export is a
    // read-only op (no undo), hence the nullptr manager, matching save().
    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* track = proc->getTrack(trackIndex))
        {
            auto& fxChain = track->getFXChain();
            auto fxChainTree = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
            if (fxChainTree.isValid())
            {
                for (size_t si = 0; si < fxChain.size(); ++si)
                {
                    auto& slot = fxChain[si];
                    if (!slot || !slot->isPlugin() || !slot->getPluginInstance())
                        continue;

                    juce::MemoryBlock state;
                    slot->getPluginInstance()->getStateInformation(state);

                    // Match by pluginID (same pattern as Track::rebuildFXChain).
                    if (static_cast<int>(si) < fxChainTree.getNumChildren())
                    {
                        auto slotTree = fxChainTree.getChild(static_cast<int>(si));
                        if (slotTree.getProperty(IDs::pluginID).toString() == slot->getPluginID())
                        {
                            if (state.getSize() > 0)
                                slotTree.setProperty(IDs::pluginState, state.toBase64Encoding(), nullptr);
                        }
                    }
                }
            }
        }
    }

    // 2. Walk the FX_CHAIN children (Track.cpp:740-746 resolveFXChainTree).
    auto fxChainTree = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChainTree.isValid())
        return preset;

    for (int i = 0; i < fxChainTree.getNumChildren(); ++i)
    {
        auto slotTree = fxChainTree.getChild(i);
        HDAW::ChainPreset::Slot s;
        s.fxType = slotTree.getProperty(IDs::fxType, "").toString();
        s.bypassed = static_cast<bool>(slotTree.getProperty(IDs::bypassed, false));
        s.name = slotTree.getProperty(IDs::name, "").toString();

        // Internal params: one "param_N" entry per def (ReadModelImpl
        // getInternalFxParams pattern). Missing props read as the def default.
        if (s.fxType.isNotEmpty() && s.fxType != "plugin" && s.fxType != "none")
        {
            auto defs = HDAW::TrackFXSlot::getParamDefsForType(s.fxType);
            for (const auto& d : defs)
            {
                juce::String propName = "param_" + juce::String(d.index);
                const double v = static_cast<double>(
                    slotTree.getProperty(juce::Identifier(propName),
                                         static_cast<double>(d.defaultValue)));
                s.params[propName] = v;
            }
        }

        if (s.fxType == "plugin")
        {
            s.plugin.id = slotTree.getProperty(IDs::pluginID, "").toString();
            s.plugin.format = slotTree.getProperty(IDs::pluginFormat, "").toString();
            s.plugin.path = slotTree.getProperty(IDs::pluginPath, "").toString();
            s.plugin.stateBase64 = slotTree.getProperty(IDs::pluginState, "").toString();
        }

        if (s.fxType == "sampler")
        {
            static const char* const kKeys[] = {
                "sampleFile", "mode", "rootNote", "mono", "playReverse",
                "transpose", "baseNote", "sampleStart", "sampleEnd",
                "loopStart", "loopEnd", "loopEnabled", "sliceMode",
                "sliceGrid", "sliceSensitivity", "keyRangeLow", "keyRangeHigh",
            };
            for (const auto* k : kKeys)
            {
                juce::Identifier id(k);
                if (slotTree.hasProperty(id))
                    s.sampler[k] = slotTree.getProperty(id).toString();
            }
            s.slicePoints = slotTree.getProperty("slicePoints", "").toString();
        }

        if (s.fxType == "psy_fm")
        {
            s.psyFmMatrix = slotTree.getProperty("psyFmMatrix", "").toString();
            s.psyFmSweepRate =
                static_cast<double>(slotTree.getProperty("psyFmSweepRate", 0.0));
        }

        preset.slots.push_back(std::move(s));
    }

    return preset;
}

namespace {
// Parse a "param_N" key to N, or -1 when the shape is wrong. Gate 9: never
// write stray props — keys must be exactly "param_" + short digits.
int parsePresetParamIndex(const juce::String& key, int defCount)
{
    if (!key.startsWith("param_"))
        return -1;
    juce::String digits = key.substring(6);
    if (digits.isEmpty() || digits.length() > 6)
        return -1;
    for (auto c : digits)
        if (!juce::CharacterFunctions::isDigit(c))
            return -1;
    const int idx = digits.getIntValue();
    if (idx < 0 || idx >= defCount)
        return -1;
    return idx;
}

// Sampler tree props grouped by value type so apply writes back the same
// types export read (export stores everything as strings in the schema map).
bool isSamplerIntKey(const juce::String& k)
{
    return k == "rootNote" || k == "transpose" || k == "baseNote"
        || k == "keyRangeLow" || k == "keyRangeHigh";
}

bool isSamplerBoolKey(const juce::String& k)
{
    return k == "mono" || k == "playReverse" || k == "loopEnabled";
}

bool isSamplerDoubleKey(const juce::String& k)
{
    return k == "sampleStart" || k == "sampleEnd" || k == "loopStart"
        || k == "loopEnd" || k == "sliceGrid" || k == "sliceSensitivity";
}
} // namespace

bool AudioEngineCommands::applyFxChain(int trackIndex, const HDAW::ChainPreset& preset,
                                       juce::String* error)
{
    auto fail = [&](const juce::String& msg) -> bool {
        if (error != nullptr)
            *error = msg;
        return false;
    };

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return fail("applyFxChain: invalid track index " + juce::String(trackIndex));

    // 1. Gate 9 — validate EVERYTHING before any write.
    for (size_t si = 0; si < preset.slots.size(); ++si)
    {
        const auto& s = preset.slots[si];
        const juce::String where = "applyFxChain: slot " + juce::String(static_cast<int>(si));
        if (s.fxType.isEmpty())
            return fail(where + ": empty fxType");

        const bool isPlugin = (s.fxType == "plugin");
        const bool isNone = (s.fxType == "none");
        auto defs = HDAW::TrackFXSlot::getParamDefsForType(s.fxType);

        if (defs.empty() && !isPlugin && !isNone)
            return fail(where + ": unknown fxType '" + s.fxType + "'");

        if (isPlugin || isNone)
        {
            // No defs exist for these types, so any param would be a stray prop.
            if (!s.params.empty())
                return fail(where + ": stray params on '" + s.fxType + "' slot");
            if (isPlugin && s.plugin.id.isEmpty())
                return fail(where + ": plugin slot is missing its plugin id");
            continue;
        }

        for (const auto& kv : s.params)
        {
            if (parsePresetParamIndex(kv.first, static_cast<int>(defs.size())) < 0)
                return fail(where + ": param '" + kv.first + "' is out of range for '"
                            + s.fxType + "'");
        }
    }

    // 2. One undo unit for the whole apply.
    auto& um = engine_.getProjectModel().getUndoManager();
    beginTransaction("Apply FX chain preset");

    // 3a. Remove existing slots WITHOUT the per-op rebuild that removeFxSlot
    // performs — direct tree ops under &um, single rebuild at the end.
    auto trackTree = trackList.getChild(trackIndex);
    auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        trackTree.addChild(fxChain, -1, &um);
    }
    while (fxChain.getNumChildren() > 0)
        fxChain.removeChild(0, &um);

    // 3b. Add each slot via the no-rebuild worker, then restore its state with
    // direct tree writes under &um (same properties the per-op setters write,
    // but without their per-call rebuildTrackFX). Params go through
    // setFxSlotParam for the write-side clamp (lesson 23); it performs no
    // rebuild itself.
    int slotIndex = 0;
    for (const auto& s : preset.slots)
    {
        const std::string typeStr = s.fxType.toStdString();
        const std::string pluginId =
            (s.fxType == "plugin") ? s.plugin.id.toStdString() : std::string();
        addFxSlotInternal(trackIndex, typeStr, -1, pluginId);

        auto slotTree = findFxSlot(trackIndex, slotIndex);
        if (!slotTree.isValid())
        {
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildTrackFX(trackIndex);
            endTransaction();
            return fail("applyFxChain: failed to create slot "
                        + juce::String(slotIndex));
        }

        slotTree.setProperty(IDs::bypassed, s.bypassed, &um);
        if (s.name.isNotEmpty())
            slotTree.setProperty(IDs::name, s.name, &um);

        for (const auto& kv : s.params)
        {
            auto defs = HDAW::TrackFXSlot::getParamDefsForType(s.fxType);
            const int idx =
                parsePresetParamIndex(kv.first, static_cast<int>(defs.size()));
            // Re-checked: indices were validated pre-write; a miss here can
            // only mean the tree changed under us — fail loudly, never write
            // a stray prop.
            if (idx < 0)
            {
                if (auto* proc = engine_.getMainProcessor())
                    proc->rebuildTrackFX(trackIndex);
                endTransaction();
                return fail("applyFxChain: param '" + kv.first + "' rejected during apply");
            }
            setFxSlotParam(trackIndex, slotIndex, idx, static_cast<float>(kv.second));
        }

        if (s.fxType == "plugin")
        {
            // setFxSlotPlugin path (:339-351), minus its per-call rebuild.
            if (s.plugin.format.isNotEmpty())
                slotTree.setProperty(IDs::pluginFormat, s.plugin.format, &um);
            if (s.plugin.path.isNotEmpty())
                slotTree.setProperty(IDs::pluginPath, s.plugin.path, &um);
            if (s.plugin.stateBase64.isNotEmpty())
                slotTree.setProperty(IDs::pluginState, s.plugin.stateBase64, &um);
        }

        if (s.fxType == "sampler")
        {
            // Sampler file fallback: stored absolute path → engine-side
            // library filename search → slot WITHOUT sample + HDAW_LOG
            // warning (Gate 2: warn, never silently pass).
            auto it = s.sampler.find("sampleFile");
            if (it != s.sampler.end() && it->second.isNotEmpty())
            {
                juce::String resolved;
                juce::File stored(it->second);
                if (stored.existsAsFile())
                {
                    resolved = stored.getFullPathName();
                }
                else
                {
                    const juce::String base = stored.getFileName();
                    auto hits = engine_.getFileLibraryManager().search(
                        base, "audio", {}, -1.0, -1.0, -1.0, -1.0, {}, 0, 10);
                    for (const auto& h : hits)
                    {
                        if (juce::File(h.path).existsAsFile())
                        {
                            resolved = juce::File(h.path).getFullPathName();
                            break;
                        }
                    }
                    if (resolved.isEmpty())
                        HDAW_LOG("FxChainPreset",
                                 ("applyFxChain: sample '" + it->second
                                  + "' not found; applying sampler slot without sample")
                                     .toStdString());
                }
                if (resolved.isNotEmpty())
                    slotTree.setProperty(juce::Identifier("sampleFile"), resolved, &um);
            }

            for (const auto& kv : s.sampler)
            {
                if (kv.first == "sampleFile")
                    continue; // handled above
                juce::Identifier id(kv.first);
                if (isSamplerIntKey(kv.first))
                    slotTree.setProperty(id, kv.second.getIntValue(), &um);
                else if (isSamplerBoolKey(kv.first))
                    slotTree.setProperty(id,
                                         kv.second.getIntValue() != 0
                                             || kv.second.trim().equalsIgnoreCase("true"),
                                         &um);
                else if (isSamplerDoubleKey(kv.first))
                    slotTree.setProperty(id, kv.second.getDoubleValue(), &um);
                else
                    slotTree.setProperty(id, kv.second, &um);
            }
            if (s.slicePoints.isNotEmpty())
                slotTree.setProperty(juce::Identifier("slicePoints"), s.slicePoints, &um);
        }

        if (s.fxType == "psy_fm")
        {
            // setFxSlotPsyFmPreset/setFxSlotPsyFmModRoute path, batched:
            // matrix + sweep rate are plain tree props restored by
            // loadPsyFmStateFromTree on the single rebuild below.
            if (s.psyFmMatrix.isNotEmpty())
                slotTree.setProperty(juce::Identifier("psyFmMatrix"), s.psyFmMatrix, &um);
            slotTree.setProperty(juce::Identifier("psyFmSweepRate"),
                                 static_cast<double>(s.psyFmSweepRate), &um);
        }

        ++slotIndex;
    }

    // 4. ONE rebuild for the whole apply.
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
    endTransaction();
    return true;
}
