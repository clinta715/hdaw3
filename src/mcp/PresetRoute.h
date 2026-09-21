#pragma once
// Shared preset-loading dispatch for the MCP FX domain (docs/plans/2026-09-18-apply-preset-mcp-dispatch.md).
//
// resolvePresetRoute() is the pure dispatch table behind the apply_preset MCP
// tool: it maps (slot fxType, slot pluginId, file header bytes) onto the same
// loaders the individual preset tools use. The run* executors below are
// the SINGLE implementations of those loaders — the individual tools
// (load_nord_bank, load_virus_preset, fm_synth_import_
// sysex, sub_synth_import_sysex, load_plugin_preset_file) delegate to them so
// apply_preset never duplicates their logic. (load_dexed_cartridge removed
// 2026-09-14: Dexed ignores injected cartridge state.)
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
    Je8086Patch,      // Roland JP-8080 DT1 dump -> JE8086 slot (load_je8086_preset path)
    WaldorfSysex,     // Waldorf F0 3E dumps -> Xenia/Vavra slots via MIDI SysEx
    VirusRom,         // CC0+PC ROM preset -> gearmulator Virus slot (load_virus_preset path)
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

inline bool isNodalRed2xPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "NodalRed2x");
}

inline bool isJe8086PluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "JE8086");
}

inline bool isWaldorfPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "Xenia") || containsCI(pluginId, "Vavra");
}

inline uint8_t waldorfMachineForPluginId(const std::string& pluginId)
{
    return containsCI(pluginId, "Vavra") ? kWaldorfMachineMicroQ : kWaldorfMachineMw2;
}

inline const char* waldorfNameForMachine(uint8_t machine)
{
    return machine == kWaldorfMachineMicroQ ? "microQ/Vavra" : "Microwave XT/Xenia";
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
//     3. JE8086 plugin slot       + F0 41 .. 00 06 12 -> Je8086Patch
//     4. Xenia/Vavra plugin slot  + F0 3E <machine>  -> WaldorfSysex
//     5. NodalRed2x plugin slot   + F0 33 or .mid    -> NordBank
//     6. any plugin slot          + XferJson/CcnK or .SerumPreset/.fxp/.fxb -> PluginPresetFile
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

        // Roland JP-8080 DT1 patch dumps (F0 41 <dev> 00 06 12 ...) into a
        // JE8086 slot -- the only SysEx that plugin accepts.
        if (fxType == "plugin" && isJe8086PluginId(pluginId)
            && size >= 6 && bytes[0] == 0xF0 && bytes[1] == 0x41
            && bytes[3] == 0x00 && bytes[4] == 0x06 && bytes[5] == 0x12)
            return { PresetRouteKind::Je8086Patch, {} };

        if (fxType == "plugin" && isWaldorfPluginId(pluginId)
            && isWaldorfDumpHeader(bytes, size, waldorfMachineForPluginId(pluginId)))
            return { PresetRouteKind::WaldorfSysex, {} };

        if (fxType == "plugin" && isNodalRed2xPluginId(pluginId)
            && ((size >= 2 && bytes[0] == 0xF0 && bytes[1] == kNordIdClavia)
                || fileExtension == ".mid"))
            return { PresetRouteKind::NordBank, {} };

        // DX7 SysEx into a plugin slot has no reliable route: plugins
        // ignore injected cartridge state (probed silent 2026-09-14, peak 0,
        // state byte-identical). Steer to the working internal engine.
        if (fxType == "plugin" && bytes[0] == 0xF0 && bytes[1] == 0x43)
            return { PresetRouteKind::None,
                     "cannot determine preset type for this file into this slot"
                     " (slot fxType=" + juce::String(fxType) + "): DX7 SysEx"
                     " (F0 43) into plugin slots is unreliable — use"
                     " fm_synth_import_sysex into an fm_synth slot" };

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
    // NL2x) at the END of the batch so the trailing slot state is inert.
    // Delivery is paced by the slot drain (<=1 SysEx per block, order
    // preserved) and the deferred state capture is delayed ~30ms per queued
    // SysEx (see sendFxMidi), then confirmed via get_fx_capture_status.
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

/// load_je8086_preset: Roland JP-8080 DT1 bank (.syx / .mid) -> validated dumps
/// for ONE patch unit -> sendFxMidi.
///
/// Per-patch by design: a 64-patch bank is 128 DT1 messages, while
/// FxMidiParams carries at most 64 events and the proxy forwards SysEx over a
/// single lane (a busy lane DROPS, it does not queue) - so a whole-bank burst
/// would be both over the cap and lossy. One patch = 2 messages.
///
/// The dump is sent with its original UserPatch address; the JE8086 wrapper
/// retargets it to the sounding temp performance (Controller parse path, same
/// transform as the plugin's own patch browser) - see the comment at the
/// sendFxMidi call for why no program-change recall is appended.
inline McpToolResult runJe8086PatchFile(AudioEngine& e, int ti, int si,
                                        const QString& path, int presetIndex,
                                        bool captureToTree)
{
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return McpToolResult::text("file not found: " + path, true);
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return McpToolResult::text("failed to read file", true);

    std::vector<mcp::Jp8080Dump> dumps;
    const auto suffix = f.getFileExtension().toLowerCase();
    if (suffix == ".syx")
    {
        const auto* b = static_cast<const uint8_t*>(block.getData());
        if (mcp::splitJp8080Syx(b, block.getSize(), dumps) < 0)
            return McpToolResult::text("truncated SysEx (missing F7)", true);
    }
    else if (suffix == ".mid")
    {
        juce::MemoryInputStream in(block, false);
        juce::MidiFile mf;
        if (!mf.readFrom(in))
            return McpToolResult::text("invalid .mid file", true);
        std::vector<uint8_t> run;   // re-join the F0 events, then split normally
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            for (int ev = 0; ev < seq->getNumEvents(); ++ev)
            {
                const auto msg = seq->getEventPointer(ev)->message;
                if (!msg.isSysEx())
                    continue;
                const auto* raw = msg.getRawData();
                const auto n = static_cast<size_t>(msg.getRawDataSize());
                run.insert(run.end(), raw, raw + n);
            }
        }
        if (mcp::splitJp8080Syx(run.data(), run.size(), dumps) < 0)
            return McpToolResult::text("truncated SysEx in .mid (missing F7)", true);
    }
    else
        return McpToolResult::text("unsupported file type (use .syx or .mid)", true);

    if (dumps.empty())
        return McpToolResult::text("no JP-8080 DT1 data found in file", true);

    // ATOMIC: validate EVERY message (header, size, checksum) before queueing
    // anything -- a bank with one corrupt patch must not half-load.
    for (const auto& d : dumps)
    {
        if (auto err = mcp::validateJp8080Dump(d.raw.data(), d.raw.size()); !err.isEmpty())
            return McpToolResult::text("invalid JP-8080 dump: "
                + QString::fromStdString(err.toStdString()), true);
    }

    const auto units = mcp::jp8080UnitsInFileOrder(dumps);
    if (units.empty())
        return McpToolResult::text("file carries no JP-8080 patch units", true);
    if (presetIndex <= 0)
        presetIndex = 1;
    if (presetIndex > static_cast<int>(units.size()))
        return McpToolResult::text(QString("preset %1 out of range: file has %2 patch unit(s)")
                                       .arg(presetIndex).arg(static_cast<int>(units.size())), true);

    const auto& unit = units[static_cast<size_t>(presetIndex - 1)];
    std::vector<const mcp::Jp8080Dump*> selected;
    if (mcp::jp8080SelectUnit(dumps, unit, selected) <= 0)
        return McpToolResult::text("selected patch has no DT1 pages", true);

    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    for (const auto* d : selected)
    {
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = d->raw;
        p.events.push_back(std::move(ev));
    }
    // NO program-change recall. The wrapper retargets each UserPatch DT1 to
    // PerformanceTemp | PatchUpper (controller parse path), which is what makes
    // the file's patch sound. A JP-8080 program change does not just select a
    // patch, it LOADS the bank program into the current patch, so appending
    // CC0=1 (USER) + PC overwrites the dump with whatever the emulator's bank
    // holds. Device probe, run6 (docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md):
    // the recall alone changes the sound (rms 0.0043 -> 0.0133), and a recall
    // after the retargeted dump lands back on that recall-only sound
    // (delta vs the retargeted patch 0.00765, delta vs recall-only 0.0000245),
    // discarding the imported patch. `load_je8086_preset` used to send it "so
    // it sounds immediately"; the retarget now provides that.

    if (p.events.size() > 64)
        return McpToolResult::text("too many MIDI events for one injection", true);

    const auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return McpToolResult::text("sendFxMidi failed: " + QString::fromStdString(r.error), true);

    return McpToolResult::text(QString("queued %1 DT1 message(s) for %2 bank %3 slot %4 (patch %5 of %6) capturedToTree=%7")
                                   .arg(static_cast<int>(selected.size()))
                                   .arg(unit.area == 2 ? "patch-user" : "performance")
                                   .arg(unit.bank).arg(unit.slot)
                                   .arg(presetIndex).arg(static_cast<int>(units.size()))
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

/// Xenia/Vavra: Waldorf .syx -> validated dumps -> sendFxMidi.
inline McpToolResult runWaldorfSysexFile(AudioEngine& e, int ti, int si,
                                         const QString& path,
                                         const std::string& pluginId,
                                         bool captureToTree)
{
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return McpToolResult::text("file not found: " + path, true);
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return McpToolResult::text("failed to read file", true);

    const auto* b = static_cast<const uint8_t*>(block.getData());
    const size_t n = block.getSize();
    const uint8_t machine = waldorfMachineForPluginId(pluginId);
    const char* name = waldorfNameForMachine(machine);

    std::vector<std::vector<uint8_t>> dumps;
    const int split = splitWaldorfSyx(b, n, machine, dumps);
    if (split == -1)
        return McpToolResult::text("truncated Waldorf SysEx (missing F7)", true);
    if (split == -2)
        return McpToolResult::text("invalid Waldorf dump for " + QString::fromUtf8(name), true);
    if (dumps.empty())
        return McpToolResult::text("no Waldorf SysEx dumps found in file", true);
    if (dumps.size() > 64)
        return McpToolResult::text("too many Waldorf dumps for one injection (max 64)", true);

    size_t totalBytes = 0;
    for (const auto& d : dumps)
    {
        auto err = validateWaldorfDump(d.data(), d.size(), machine, name);
        if (!err.isEmpty())
            return McpToolResult::text("invalid Waldorf dump: " + QString::fromStdString(err.toStdString()), true);
        totalBytes += d.size();
    }

    // microQ/Vavra edit-buffer retarget (2026-09-18): real bank dumps carry
    // their ORIGINAL buffer/location bytes (0x30 multi-edit / 0x40+ bank
    // slots); injected as-is the OS loads a buffer the current sound does not
    // read, so renders stayed identical ("NOT APPLYING"). q the editor
    // (mqController::sendSingle) and retarget to the single-mode edit buffer,
    // recomputing the Waldorf checksum (sum of [4 .. size-2) & 0x7F;
    // microq_patch.py documents the emulator checks size only, but keep real-
    // hardware-valid files).
    const bool isMicroQ = machine == kWaldorfMachineMicroQ;
    for (auto& d : dumps)
        if (isMicroQ && d.size() == 392)
        {
            d[5] = 0x20;   // MidiBufferNum::SingleEditBufferSingleMode
            d[6] = 0x00;   // MidiSoundLocation::EditBufferCurrentSingle
            uint8_t cs = 0;
            for (size_t i = 4; i + 2 < d.size(); ++i)
                cs += d[i];
            d[d.size() - 2] = cs & 0x7f;
        }

    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    for (const auto& d : dumps)
    {
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = d;
        p.events.push_back(std::move(ev));
    }

    const auto res = e.getProjectCommands().sendFxMidi(p);
    if (!res.ok)
        return McpToolResult::text("sendFxMidi failed: " + QString::fromStdString(res.error), true);
    return McpToolResult::text(QString("queued %1 Waldorf sysex dump(s) (%2 bytes) for %3 capturedToTree=%4")
        .arg(static_cast<int>(dumps.size()))
        .arg(static_cast<int>(totalBytes))
        .arg(name)
        .arg(res.capturedToTree ? 1 : 0));
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
