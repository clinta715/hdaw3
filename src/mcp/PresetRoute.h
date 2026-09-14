#pragma once
// Shared preset-loading dispatch for the MCP FX domain (docs/plans/2026-09-18-apply-preset-mcp-dispatch.md).
//
// resolvePresetRoute() is the pure dispatch table behind the apply_preset MCP
// tool: it maps (slot fxType, slot pluginId, file header bytes) onto the same
// loaders the five individual preset tools use. The run* executors below are
// the SINGLE implementations of those loaders — the five individual tools
// (load_nord_bank, load_virus_preset, load_dexed_cartridge, fm_synth_import_
// sysex, sub_synth_import_sysex, load_plugin_preset_file) delegate to them so
// apply_preset never duplicates their logic.
//
// MCP-layer only: no audio-thread code, no graph mutation, no DSP writes
// beyond the pre-existing command-layer calls.

#include "McpToolDef.h"
#include "PresetFileParser.h"
#include "../common/ProjectCommands.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/Dx7SysexImport.h"
#include "../engine/FmSynthEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
#include "../model/ProjectModel.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mcp {

enum class PresetRouteKind {
    NordBank,         // Clavia dumps -> NodalRed2x slot (load_nord_bank path)
    VirusRom,         // CC0+PC ROM preset -> gearmulator Virus slot (load_virus_preset path)
    DexedCartridge,   // F0 43 SysEx -> Dexed/DX7-engine plugin slot (load_dexed_cartridge path)
    FmSysex,          // F0 43 SysEx -> internal fm_synth slot (fm_synth_import_sysex path)
    SubSynthVirus,    // Virus F0 00 20 33 -> internal sub_synth slot (sub_synth_import_sysex path)
    PluginPresetFile, // XferJson / CcnK -> any plugin slot (load_plugin_preset_file path)
    None,
};

struct PresetRoute
{
    PresetRouteKind kind = PresetRouteKind::None;
    juce::String error;   // non-empty => dispatch failed, surface to the agent
};

inline bool containsCI(const std::string& haystack, const char* needle)
{
    return juce::String(haystack).containsIgnoreCase(juce::String(needle));
}

inline bool isVirusGearmulatorPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "OsTIrus") || containsCI(pluginId, "Osirus")
        || containsCI(pluginId, "Vavra") || containsCI(pluginId, "Xenia")
        || containsCI(pluginId, "JE8086");
}

inline bool isDexedPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "Dexed");
}

inline bool isNodalRed2xPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "NodalRed2x");
}

inline bool isSubSynthSlot(const std::string& fxType, const std::string& pluginId)
{
    return fxType == "sub_synth" || containsCI(pluginId, "sub_synth");
}

// Pure dispatch table. bytes may be null when no file was supplied (ROM-preset
// case). fileExtension is lowercased with the leading dot ("") when no file.
// Dispatch priority (first match wins):
//   file given:
//     1. fm_synth slot            + F0 43            -> FmSysex
//     2. sub_synth slot           + F0 00 20 33      -> SubSynthVirus
//     3. NodalRed2x plugin slot   + F0 33 or .mid    -> NordBank
//     4. Dexed plugin slot        + F0 43            -> DexedCartridge
//     5. any plugin slot          + XferJson/CcnK or .SerumPreset/.fxp/.fxb -> PluginPresetFile
//   no file:
//     6. Virus gearmulator slot   + program          -> VirusRom (ROM preset, no file)
//   else: None + "cannot determine preset type ..."
inline PresetRoute resolvePresetRoute(const std::string& fxType,
                                      const std::string& pluginId,
                                      const uint8_t* bytes, size_t size,
                                      const juce::String& fileExtension,
                                      bool hasProgram)
{
    if (bytes != nullptr && size >= 2)
    {
        if (fxType == "fm_synth" && bytes[0] == 0xF0 && bytes[1] == 0x43)
            return { PresetRouteKind::FmSysex, {} };

        if (isSubSynthSlot(fxType, pluginId) && size >= 4
            && bytes[0] == 0xF0 && bytes[1] == 0x00 && bytes[2] == 0x20
            && bytes[3] == 0x33)
            return { PresetRouteKind::SubSynthVirus, {} };

        if (fxType == "plugin" && isNodalRed2xPluginId(pluginId)
            && ((size >= 2 && bytes[0] == 0xF0 && bytes[1] == kNordIdClavia)
                || fileExtension == ".mid"))
            return { PresetRouteKind::NordBank, {} };

        if (fxType == "plugin" && isDexedPluginId(pluginId)
            && bytes[0] == 0xF0 && bytes[1] == 0x43)
            return { PresetRouteKind::DexedCartridge, {} };

        if (fxType == "plugin")
        {
            static constexpr char serumMagic[] = "XferJson";
            const bool serumHeader = size >= 8
                && std::memcmp(bytes, serumMagic, 8) == 0;
            const bool fxpHeader = size >= 4
                && bytes[0] == 0x43 && bytes[1] == 0x63 && bytes[2] == 0x6e
                && bytes[3] == 0x4b; // "CcnK"
            const bool presetExt = fileExtension == ".serumpreset"
                || fileExtension == ".fxp" || fileExtension == ".fxb";
            if (serumHeader || fxpHeader || presetExt)
                return { PresetRouteKind::PluginPresetFile, {} };
        }

        return { PresetRouteKind::None,
                 "cannot determine preset type for this file into this slot"
                 " (slot fxType=" + juce::String(fxType) + ")" };
    }

    // No file: ROM presets via CC0+PC are the only file-less route.
    if (fxType == "plugin" && isVirusGearmulatorPluginId(pluginId))
    {
        if (hasProgram)
            return { PresetRouteKind::VirusRom, {} };
        return { PresetRouteKind::None,
                 "apply_preset on a gearmulator Virus slot needs program"
                 " (with optional bank) for a ROM preset, or filePath for a"
                 " file preset" };
    }

    return { PresetRouteKind::None,
             "cannot determine preset type (no filePath given)" };
}

// ---------------------------------------------------------------------------
// Executors — each is the single implementation of one loader; the original
// tool handlers delegate to the same function apply_preset calls.
// ---------------------------------------------------------------------------

/// load_virus_preset: CC0 bank select (0-7 = banks A-H singles) + program change.
inline McpToolResult runVirusRomPreset(AudioEngine& e, int ti, int si,
                                       int bank, int program, int channel,
                                       bool captureToTree)
{
    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, channel, 0, bank});
    p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, channel, program, 0});
    auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return McpToolResult::text(QString::fromStdString(r.error), true);
    return McpToolResult::text(QString("queued bank=%1 program=%2 (banks A-H singles) capturedToTree=%3")
                                   .arg(bank).arg(program).arg(r.capturedToTree ? 1 : 0));
}

/// load_dexed_cartridge: raw DX7 SysEx file -> sendFxMidi SysEx on the slot.
inline McpToolResult runDexedCartridgeFile(AudioEngine& e, int ti, int si,
                                           const QString& path,
                                           bool captureToTree)
{
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return McpToolResult::text("file not found: " + path, true);
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return McpToolResult::text("failed to read file", true);
    if (block.getSize() < 2)
        return McpToolResult::text("file too small", true);
    const auto* bytes = static_cast<const uint8_t*>(block.getData());
    if (bytes[0] != 0xF0 || bytes[1] != 0x43)
        return McpToolResult::text("not a DX7 SysEx file (expected F0 43 header)", true);
    if (block.getSize() > 32768)
        return McpToolResult::text("sysex payload too large (max 32768 bytes)", true);
    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    ProjectCommands::FxMidiEvent ev;
    ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
    ev.sysex.assign(bytes, bytes + block.getSize());
    p.events.push_back(std::move(ev));
    auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return McpToolResult::text(QString::fromStdString(r.error), true);
    return McpToolResult::text(QString("queued sysex %1 bytes (r.queued=%2, track=%3 slot=%4, capturedToTree=%5)")
        .arg(static_cast<int>(block.getSize())).arg(r.queued).arg(ti).arg(si).arg(r.capturedToTree ? 1 : 0));
}

/// load_nord_bank: Clavia .syx/.mid -> validated dumps -> sendFxMidi + CC125.
inline McpToolResult runNordBankFile(AudioEngine& e, int ti, int si,
                                     const QString& path, int program,
                                     bool captureToTree)
{
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return McpToolResult::text("file not found: " + path, true);
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return McpToolResult::text("failed to read file", true);
    const auto suffix = f.getFileExtension().toLowerCase();
    // Normalize to complete F0..F7 dumps (payload coordinates differ
    // between containers; see PresetFileParser.h for the wire format).
    std::vector<std::vector<uint8_t>> dumps;
    if (suffix == ".syx") {
        const auto* b = static_cast<const uint8_t*>(block.getData());
        if (mcp::splitNordSyx(b, block.getSize(), dumps) < 0)
            return McpToolResult::text("truncated SysEx (missing F7)", true);
    } else if (suffix == ".mid") {
        juce::MemoryInputStream in(block, false);
        juce::MidiFile mf;
        if (!mf.readFrom(in))
            return McpToolResult::text("invalid .mid file", true);
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            for (int ev = 0; ev < seq->getNumEvents(); ++ev)
            {
                const auto metadata = seq->getEventPointer(ev);
                if (!metadata->message.isSysEx())
                    continue;
                const auto* raw = metadata->message.getRawData();
                dumps.emplace_back(raw, raw + metadata->message.getRawDataSize());
            }
        }
    } else return McpToolResult::text("unsupported file type (use .syx or .mid)", true);
    if (dumps.empty())
        return McpToolResult::text("no sysex data found in file", true);
    // Validate EVERY dump before queueing anything (no partial bank loads).
    size_t totalBytes = 0;
    for (const auto& d : dumps)
    {
        if (auto err = mcp::validateNordDump(d.data(), d.size()); !err.isEmpty())
            return McpToolResult::text(
                "invalid Nord dump: " + QString::fromStdString(
                    err.toStdString()), true);
        totalBytes += d.size();
    }
    if (program > 127 || program < -1)
        return McpToolResult::text("program must be 0..127", true);
    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    for (const auto& d : dumps) {
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = d;
        p.events.push_back(std::move(ev));
    }
    if (program >= 0)
    {
        // Voice selection AFTER the bank dumps land (BUG-7 plan step 4).
        ProjectCommands::FxMidiEvent pc;
        pc.kind = ProjectCommands::FxMidiEvent::Kind::ProgramChange;
        pc.channel = 1;
        pc.data1 = program;
        p.events.push_back(std::move(pc));
    }
    // Capture-race protocol: append a harmless CC125 (undefined on the
    // NL2x) at the END of the batch. The deferred state capture fires
    // only AFTER the child has consumed the whole bank (see send_fx_midi).
    {
        ProjectCommands::FxMidiEvent cc;
        cc.kind = ProjectCommands::FxMidiEvent::Kind::ControlChange;
        cc.channel = 1;
        cc.data1 = 125; // undefined on the NL2x — the firmware ignores it
        cc.data2 = 0;
        p.events.push_back(std::move(cc));
    }
    auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return McpToolResult::text(QString::fromStdString(r.error), true);
    return McpToolResult::text(QString("queued %1 sysex dumps (%2 bytes)%3 capturedToTree=%4")
        .arg(r.queued)
        .arg(static_cast<int>(totalBytes))
        .arg(program >= 0 ? QString(" program=%1").arg(program) : QString())
        .arg(r.capturedToTree ? 1 : 0));
}

/// fm_synth_import_sysex: DX7 .syx -> fmPatchData via ProjectCommands::setFmPatch.
inline McpToolResult runFmImportSysex(AudioEngine& e, int ti, int si,
                                      const QString& filePath,
                                      int voiceIndexIn)
{
    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return McpToolResult::text("slot not found", true);
    if (fxSlots[si].fxType != "fm_synth")
        return McpToolResult::text("slot is not an FM synth", true);

    if (filePath.isEmpty())
        return McpToolResult::text("filePath required", true);

    juce::File syxFile(filePath.toStdString());
    if (!syxFile.existsAsFile())
        return McpToolResult::text("file not found: " + filePath, true);

    juce::MemoryBlock raw;
    if (!syxFile.loadFileAsData(raw))
        return McpToolResult::text("failed to read file", true);

    auto* bytes = static_cast<const uint8_t*>(raw.getData());
    size_t fileSize = raw.getSize();

    std::optional<HDAW::Dx7Voice> voice;
    std::vector<HDAW::Dx7Voice> voices;
    int resolvedVoiceIndex = 0;

    if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
        voice = HDAW::parseSingleVoiceSysex(bytes, fileSize);
    } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
        voices = HDAW::parseCartridgeSysex(bytes, fileSize);
        int vi = voiceIndexIn;
        if (vi >= 0 && vi < (int)voices.size()) {
            voice = voices[vi];
            resolvedVoiceIndex = vi;
        }
    } else if ((fileSize == 4096 || fileSize == 4097) && !(bytes[0] == 0xF0 && bytes[1] == 0x43)) {
        // Raw 4096-byte VMEM bank (no sysex framing, no checksum;
        // 4097 = trailing F7). Routes through the cartridge parser,
        // which unpacks all 32 voices.
        voices = HDAW::parseCartridgeSysex(bytes, fileSize);
        int vi = voiceIndexIn;
        if (vi >= 0 && vi < (int)voices.size()) {
            voice = voices[vi];
            resolvedVoiceIndex = vi;
        }
    } else {
        return McpToolResult::text(
            "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)", true);
    }

    if (!voice.has_value())
        return McpToolResult::text("failed to parse SysEx data (bad checksum or size)", true);

    // Route through the command: writes fmPatchData to the slot tree
    // (so tree-copy renders and save/load hear it) and applies live
    // best-effort. Works without an audio device.
    juce::MemoryBlock block(voice->patchData.data(), FmSynthEngine::kPatchSize);
    e.getProjectCommands().setFmPatch(ti, si, block.toBase64Encoding().toStdString());

    QJsonObject result;
    result["ok"] = true;
    result["voiceName"] = QString::fromStdString(voice->voiceName);
    result["algorithm"] = voice->algorithm;
    result["feedback"] = voice->feedback;
    if (!voices.empty()) {
        result["totalVoices"] = static_cast<int>(voices.size());
        QJsonArray voicesArr;
        for (int i = 0; i < (int)voices.size(); ++i) {
            QJsonObject v;
            v["index"] = i;
            v["name"] = QString::fromStdString(voices[i].voiceName);
            v["algorithm"] = voices[i].algorithm;
            voicesArr.append(v);
        }
        result["voices"] = voicesArr;
        result["voiceIndex"] = resolvedVoiceIndex;
    }

    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

/// sub_synth_import_sysex: Virus .syx -> AudioEngineCommands::loadVirusPatch.
inline McpToolResult runSubSynthImportSysex(AudioEngine& e, int ti, int si,
                                            const QString& filePath,
                                            int voiceIndex)
{
    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return McpToolResult::text("slot not found", true);
    if (fxSlots[si].fxType != "sub_synth")
        return McpToolResult::text("slot is not a sub_synth", true);

    if (filePath.isEmpty())
        return McpToolResult::text("filePath required", true);
    juce::File syxFile(filePath.toStdString());
    if (!syxFile.existsAsFile())
        return McpToolResult::text("file not found: " + filePath, true);

    auto r = e.getAudioEngineCommands().loadVirusPatch(
        ti, si, filePath.toStdString(), voiceIndex);
    if (!r.ok)
        return McpToolResult::text(QString::fromStdString(r.error), true);

    QJsonObject result;
    result["ok"] = true;
    result["name"] = QString::fromStdString(r.name);
    result["bank"] = r.bank;
    result["program"] = r.program;
    result["mappedCount"] = r.mappedCount;
    QJsonArray unmapped;
    for (const auto& u : r.unmapped)
        unmapped.append(QString::fromStdString(u));
    result["unmapped"] = unmapped;
    return McpToolResult::text(QString::fromUtf8(
        QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

/// load_plugin_preset_file: XferJson/.fxp/.syx -> setStateInformation + tree capture.
inline McpToolResult runLoadPluginPresetFile(AudioEngine& e, int ti, int si,
                                             const QString& filePath)
{
    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= static_cast<int>(fxSlots.size()))
        return McpToolResult::text("slot not found", true);
    if (fxSlots[si].fxType != "plugin")
        return McpToolResult::text("slot is not a plugin", true);

    auto* proc = e.getMainProcessor();
    if (!proc) return McpToolResult::text("audio engine not initialized", true);
    auto* track = proc->getTrack(ti);
    if (!track) return McpToolResult::text("track not found", true);
    auto& chain = track->getFXChain();
    if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si])
        return McpToolResult::text("FX slot not found in chain", true);
    auto* slot = chain[si].get();
    if (!slot->isPlugin() || !slot->getPluginInstance())
        return McpToolResult::text("slot has no plugin instance", true);

    if (filePath.isEmpty())
        return McpToolResult::text("filePath required", true);

    juce::File fxpFile(filePath.toStdString());
    if (!fxpFile.existsAsFile())
        return McpToolResult::text("file not found: " + filePath, true);

    juce::MemoryBlock raw;
    if (!fxpFile.loadFileAsData(raw))
        return McpToolResult::text("failed to read file", true);

    auto parsed = parsePresetFile(raw);
    if (!parsed.ok())
        return McpToolResult::text(QString::fromStdString(parsed.error.toStdString()), true);

    if (parsed.size > static_cast<size_t>(std::numeric_limits<int>::max()))
        return McpToolResult::text("preset payload too large", true);

    slot->getPluginInstance()->setStateInformation(parsed.data, static_cast<int>(parsed.size));

    auto& model = e.getProjectModel();
    auto& um = model.getUndoManager();
    auto slotTree = model.getTrackListTree().getChild(ti)
        .getChildWithName(IDs::FX_CHAIN).getChild(si);
    if (slotTree.isValid()) {
        juce::MemoryBlock stateBlock(parsed.data, parsed.size);
        slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
    }

    return McpToolResult::text("ok");
}

} // namespace mcp
