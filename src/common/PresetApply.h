#pragma once
// Preset-loading executors — ONE implementation shared by the MCP tools
// (src/mcp/PresetRoute.h, src/mcp/McpTools_*.cpp) and the JSON-RPC routes
// (audio.applyPreset / audio.subSynthImportSysex / audio.auditionPatch in
// Router_Audio.cpp, plugin.loadPresetFile in Router_Plugin.cpp), per the
// AGENTS.md parity contract: where both surfaces shape the same artifact the
// logic lives in src/common/, so the tool text and the route payload cannot
// drift.
//
// Moved verbatim from src/mcp/PresetRoute.h (2026-09-24 parity wave) with the
// McpToolResult shaping factored out: every entry point returns the EXACT tool
// text (the failure text on the tool's isError paths, the compact-JSON success
// document otherwise) and reports the outcome through *outOk (false <=>
// isError). The MCP layer wraps these back into McpToolResult; the RPC layer
// parses the success text into the reply and maps the failure text onto
// -32602.
//
// Realtime scope: message-thread/file-I/O code only — the engine writes go
// through the pre-existing command-layer calls (ProjectCommands::setFmPatch /
// sendFxMidi, AudioEngineCommands::loadVirusPatch) and the plugin lifecycle
// call (setStateInformation) rides the same path the MCP tool has always used
// (both surfaces run on the host message thread; no audio-thread or isolated-
// child change). Header-only; JUCE + Qt only, no CMake registration.

// Pure format sniffing lives in src/mcp/PresetFileParser.h (juce_core only, no
// MCP types) — reached via ../mcp/ so common/ consumers resolve it too.
#include "../mcp/PresetFileParser.h"
#include "../common/NordBankLoader.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_formats/juce_audio_formats.h>  // MidiFile (je8086 .mid path)

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace HDAW {

inline bool presetContainsCI(const std::string& haystack, const char* needle)
{
    return juce::String(haystack).containsIgnoreCase(juce::String(needle));
}

// Legacy spelling kept as an alias (src/mcp/PresetRoute.h used to own this
// helper; the shared implementation lives here now).
inline constexpr auto& containsCI = presetContainsCI;

inline bool isVirusGearmulatorPluginId(const std::string& pluginId)
{
    return presetContainsCI(pluginId, "OsTIrus") || presetContainsCI(pluginId, "Osirus")
        || presetContainsCI(pluginId, "Vavra") || presetContainsCI(pluginId, "Xenia")
        || presetContainsCI(pluginId, "JE8086");
}

inline bool isNodalRed2xPluginId(const std::string& pluginId)
{
    return presetContainsCI(pluginId, "NodalRed2x");
}

inline bool isJe8086PluginId(const std::string& pluginId)
{
    return presetContainsCI(pluginId, "JE8086");
}

inline bool isWaldorfPluginId(const std::string& pluginId)
{
    return presetContainsCI(pluginId, "Xenia") || presetContainsCI(pluginId, "Vavra");
}

inline uint8_t waldorfMachineForPluginId(const std::string& pluginId)
{
    return presetContainsCI(pluginId, "Vavra") ? mcp::kWaldorfMachineMicroQ : mcp::kWaldorfMachineMw2;
}

inline const char* waldorfNameForMachine(uint8_t machine)
{
    return machine == mcp::kWaldorfMachineMicroQ ? "microQ/Vavra" : "Microwave XT/Xenia";
}

inline bool isSubSynthSlot(const std::string& fxType, const std::string& pluginId)
{
    return fxType == "sub_synth" || presetContainsCI(pluginId, "sub_synth");
}

// ── Pure dispatch table (apply_preset) ───────────────────────────────────────

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
            && mcp::isWaldorfDumpHeader(bytes, size, waldorfMachineForPluginId(pluginId)))
            return { PresetRouteKind::WaldorfSysex, {} };

        if (fxType == "plugin" && isNodalRed2xPluginId(pluginId)
            && ((size >= 2 && bytes[0] == 0xF0 && bytes[1] == mcp::kNordIdClavia)
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

// ── Executors ────────────────────────────────────────────────────────────────
// Each is the single implementation of one loader; the individual MCP tools
// and the apply_preset fan-out both delegate to the same function here.

/// load_virus_preset: CC0 bank select (0-7 = banks A-H singles) + program change.
inline QString virusRomPresetToolText(AudioEngine& e, int ti, int si,
                                      int bank, int program, int channel,
                                      bool captureToTree,
                                      bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    ProjectCommands::FxMidiParams p;
    p.trackIndex = ti;
    p.slotIndex = si;
    p.captureToTree = captureToTree;
    p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, channel, 0, bank});
    p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, channel, program, 0});
    auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return QString::fromStdString(r.error);
    if (outOk) *outOk = true;
    return QString("queued bank=%1 program=%2 (banks A-H singles) capturedToTree=%3")
                       .arg(bank).arg(program).arg(r.capturedToTree ? 1 : 0);
}

/// load_nord_bank: Clavia .syx/.mid -> validated dumps -> sendFxMidi + CC125.
inline QString nordBankFileToolText(AudioEngine& e, int ti, int si,
                                    const QString& path, int program,
                                    bool captureToTree,
                                    bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    // Delegates to the ONE shared loader (src/common/NordBankLoader.cpp) so this path and the
    // matrix tool's loose .syx route cannot drift; the prose output is unchanged.
    const auto r = loadNordBankFile(e, ti, si, path, program, captureToTree);
    if (!r.ok)
        return r.error;
    if (outOk) *outOk = true;
    return QString("queued %1 sysex dumps (%2 bytes)%3 capturedToTree=%4")
        .arg(r.queued)
        .arg(r.totalBytes)
        .arg(r.program >= 0 ? QString(" program=%1").arg(r.program) : QString())
        .arg(r.capturedToTree ? 1 : 0);
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
inline QString je8086PatchFileToolText(AudioEngine& e, int ti, int si,
                                       const QString& path, int presetIndex,
                                       bool captureToTree,
                                       bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return "file not found: " + path;
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return "failed to read file";

    std::vector<mcp::Jp8080Dump> dumps;
    const auto suffix = f.getFileExtension().toLowerCase();
    if (suffix == ".syx")
    {
        const auto* b = static_cast<const uint8_t*>(block.getData());
        if (mcp::splitJp8080Syx(b, block.getSize(), dumps) < 0)
            return "truncated SysEx (missing F7)";
    }
    else if (suffix == ".mid")
    {
        juce::MemoryInputStream in(block, false);
        juce::MidiFile mf;
        if (!mf.readFrom(in))
            return "invalid .mid file";
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
            return "truncated SysEx in .mid (missing F7)";
    }
    else
        return "unsupported file type (use .syx or .mid)";

    if (dumps.empty())
        return "no JP-8080 DT1 data found in file";

    // ATOMIC: validate EVERY message (header, size, checksum) before queueing
    // anything -- a bank with one corrupt patch must not half-load.
    for (const auto& d : dumps)
    {
        if (auto err = mcp::validateJp8080Dump(d.raw.data(), d.raw.size()); !err.isEmpty())
            return "invalid JP-8080 dump: "
                + QString::fromStdString(err.toStdString());
    }

    const auto units = mcp::jp8080UnitsInFileOrder(dumps);
    if (units.empty())
        return "file carries no JP-8080 patch units";
    if (presetIndex <= 0)
        presetIndex = 1;
    if (presetIndex > static_cast<int>(units.size()))
        return QString("preset %1 out of range: file has %2 patch unit(s)")
                                       .arg(presetIndex).arg(static_cast<int>(units.size()));

    const auto& unit = units[static_cast<size_t>(presetIndex - 1)];
    std::vector<const mcp::Jp8080Dump*> selected;
    if (mcp::jp8080SelectUnit(dumps, unit, selected) <= 0)
        return "selected patch has no DT1 pages";

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
        return "too many MIDI events for one injection";

    const auto r = e.getProjectCommands().sendFxMidi(p);
    if (!r.ok)
        return "sendFxMidi failed: " + QString::fromStdString(r.error);

    if (outOk) *outOk = true;
    return QString("queued %1 DT1 message(s) for %2 bank %3 slot %4 (patch %5 of %6) capturedToTree=%7")
                                   .arg(static_cast<int>(selected.size()))
                                   .arg(unit.area == 2 ? "patch-user" : "performance")
                                   .arg(unit.bank).arg(unit.slot)
                                   .arg(presetIndex).arg(static_cast<int>(units.size()))
                                   .arg(r.capturedToTree ? 1 : 0);
}

/// fm_synth_import_sysex: DX7 .syx -> fmPatchData via ProjectCommands::setFmPatch.
inline QString fmImportSysexToolText(AudioEngine& e, int ti, int si,
                                     const QString& filePath,
                                     int voiceIndexIn,
                                     bool* outOk = nullptr)
{
    if (outOk) *outOk = false;

    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return "slot not found";
    if (fxSlots[si].fxType != "fm_synth")
        return "slot is not an FM synth";

    if (filePath.isEmpty())
        return "filePath required";

    juce::File syxFile(filePath.toStdString());
    if (!syxFile.existsAsFile())
        return "file not found: " + filePath;

    juce::MemoryBlock raw;
    if (!syxFile.loadFileAsData(raw))
        return "failed to read file";

    auto* bytes = static_cast<const uint8_t*>(raw.getData());
    size_t fileSize = raw.getSize();

    std::optional<Dx7Voice> voice;
    std::vector<Dx7Voice> voices;
    int resolvedVoiceIndex = 0;

    if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
        voice = parseSingleVoiceSysex(bytes, fileSize);
    } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
        voices = parseCartridgeSysex(bytes, fileSize);
        int vi = voiceIndexIn;
        if (vi >= 0 && vi < (int)voices.size()) {
            voice = voices[vi];
            resolvedVoiceIndex = vi;
        }
    } else if ((fileSize == 4096 || fileSize == 4097) && !(bytes[0] == 0xF0 && bytes[1] == 0x43)) {
        // Raw 4096-byte VMEM bank (no sysex framing, no checksum;
        // 4097 = trailing F7). Routes through the cartridge parser,
        // which unpacks all 32 voices.
        voices = parseCartridgeSysex(bytes, fileSize);
        int vi = voiceIndexIn;
        if (vi >= 0 && vi < (int)voices.size()) {
            voice = voices[vi];
            resolvedVoiceIndex = vi;
        }
    } else {
        return "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)";
    }

    if (!voice.has_value())
        return "failed to parse SysEx data (bad checksum or size)";

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

    if (outOk) *outOk = true;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

/// sub_synth_import_sysex: Virus .syx -> AudioEngineCommands::loadVirusPatch.
inline QString subSynthImportSysexToolText(AudioEngine& e, int ti, int si,
                                           const QString& filePath,
                                           int voiceIndex,
                                           bool* outOk = nullptr)
{
    if (outOk) *outOk = false;

    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return "slot not found";
    if (fxSlots[si].fxType != "sub_synth")
        return "slot is not a sub_synth";

    if (filePath.isEmpty())
        return "filePath required";
    juce::File syxFile(filePath.toStdString());
    if (!syxFile.existsAsFile())
        return "file not found: " + filePath;

    auto r = e.getAudioEngineCommands().loadVirusPatch(
        ti, si, filePath.toStdString(), voiceIndex);
    if (!r.ok)
        return QString::fromStdString(r.error);

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

    if (outOk) *outOk = true;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

/// Xenia/Vavra: Waldorf .syx -> validated dumps -> sendFxMidi.
inline QString waldorfSysexFileToolText(AudioEngine& e, int ti, int si,
                                        const QString& path,
                                        const std::string& pluginId,
                                        bool captureToTree,
                                        bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    const juce::File f(juce::String::fromUTF8(path.toUtf8()));
    if (!f.existsAsFile())
        return "file not found: " + path;
    juce::MemoryBlock block;
    if (!f.loadFileAsData(block))
        return "failed to read file";

    const auto* b = static_cast<const uint8_t*>(block.getData());
    const size_t n = block.getSize();
    const uint8_t machine = waldorfMachineForPluginId(pluginId);
    const char* name = waldorfNameForMachine(machine);

    std::vector<std::vector<uint8_t>> dumps;
    const int split = mcp::splitWaldorfSyx(b, n, machine, dumps);
    if (split == -1)
        return "truncated Waldorf SysEx (missing F7)";
    if (split == -2)
        return "invalid Waldorf dump for " + QString::fromUtf8(name);
    if (dumps.empty())
        return "no Waldorf SysEx dumps found in file";
    if (dumps.size() > 64)
        return "too many Waldorf dumps for one injection (max 64)";

    size_t totalBytes = 0;
    for (const auto& d : dumps)
    {
        auto err = mcp::validateWaldorfDump(d.data(), d.size(), machine, name);
        if (!err.isEmpty())
            return "invalid Waldorf dump: " + QString::fromStdString(err.toStdString());
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
    const bool isMicroQ = machine == mcp::kWaldorfMachineMicroQ;
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
        return "sendFxMidi failed: " + QString::fromStdString(res.error);

    if (outOk) *outOk = true;
    return QString("queued %1 Waldorf sysex dump(s) (%2 bytes) for %3 capturedToTree=%4")
        .arg(static_cast<int>(dumps.size()))
        .arg(static_cast<int>(totalBytes))
        .arg(name)
        .arg(res.capturedToTree ? 1 : 0);
}

/// load_plugin_preset_file: XferJson/.fxp/.syx -> setStateInformation + tree capture.
inline QString loadPluginPresetFileToolText(AudioEngine& e, int ti, int si,
                                            const QString& filePath,
                                            bool* outOk = nullptr)
{
    if (outOk) *outOk = false;

    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= static_cast<int>(fxSlots.size()))
        return "slot not found";
    if (fxSlots[si].fxType != "plugin")
        return "slot is not a plugin";

    auto* proc = e.getMainProcessor();
    if (!proc) return "audio engine not initialized";
    auto* track = proc->getTrack(ti);
    if (!track) return "track not found";
    auto& chain = track->getFXChain();
    if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si])
        return "FX slot not found in chain";
    auto* slot = chain[si].get();
    if (!slot->isPlugin() || !slot->getPluginInstance())
        return "slot has no plugin instance";

    if (filePath.isEmpty())
        return "filePath required";

    juce::File fxpFile(filePath.toStdString());
    if (!fxpFile.existsAsFile())
        return "file not found: " + filePath;

    juce::MemoryBlock raw;
    if (!fxpFile.loadFileAsData(raw))
        return "failed to read file";

    auto parsed = mcp::parsePresetFile(raw);
    if (!parsed.ok())
        return QString::fromStdString(parsed.error.toStdString());

    if (parsed.size > static_cast<size_t>(std::numeric_limits<int>::max()))
        return "preset payload too large";

    // Plugin lifecycle call (setStateInformation). Both surfaces reach this
    // from the host message thread (the MCP server handles requests on the
    // message thread; the frontend router runs on the same thread as the
    // WebSocket server), so the Gate 16 host-thread contract holds by the
    // same argument the MCP tool always made. In-process, non-isolated
    // instances only; isolated children marshal via PluginHost's own
    // lifecycle marshaling and are not touched here.
    slot->getPluginInstance()->setStateInformation(parsed.data, static_cast<int>(parsed.size));

    auto& model = e.getProjectModel();
    auto& um = model.getUndoManager();
    auto slotTree = model.getTrackListTree().getChild(ti)
        .getChildWithName(IDs::FX_CHAIN).getChild(si);
    if (slotTree.isValid()) {
        juce::MemoryBlock stateBlock(parsed.data, parsed.size);
        slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
    }

    if (outOk) *outOk = true;
    return "ok";
}

// ── apply_preset: the agentic front door ─────────────────────────────────────
// ONE tool that dispatches by slot target + file header onto the loaders above
// (resolvePresetRoute). The composite keeps the MCP tool's argument contract —
// trackId/slotIndex/filePath/program/bank/voiceIndex/channel/captureToTree —
// and both surfaces pass their whole argument object here, so the dispatch and
// every loader text are identical by construction.
inline QString applyPresetToolText(AudioEngine& e, const QJsonObject& a,
                                   bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    int ti = a.value("trackId").toInt();
    int si = a.value("slotIndex").toInt();
    auto fxSlots = e.getReadModel().getFxSlots(ti);
    if (si < 0 || si >= (int)fxSlots.size())
        return "slot not found";
    if (fxSlots[si].fxType == "none")
        return "slot is empty";
    const std::string fxType = fxSlots[si].fxType;
    const std::string pluginId = fxSlots[si].pluginId;

    const bool hasFile = a.contains("filePath")
        && !a.value("filePath").toString().isEmpty();
    juce::MemoryBlock raw;
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    juce::String extension;
    if (hasFile)
    {
        const QString path = a.value("filePath").toString();
        const juce::File f(juce::String::fromUTF8(path.toUtf8()));
        if (!f.existsAsFile())
            return "file not found: " + path;
        if (!f.loadFileAsData(raw))
            return "failed to read file";
        bytes = static_cast<const uint8_t*>(raw.getData());
        size = raw.getSize();
        extension = f.getFileExtension().toLowerCase();
    }

    const bool hasProgram = a.contains("program");
    if (hasProgram)
    {
        const int program = a.value("program").toInt(-1);
        if (program < 0 || program > 127)
            return "program must be 0..127";
    }

    const auto route = resolvePresetRoute(
        fxType, pluginId, bytes, size, extension, hasProgram);
    if (route.kind == PresetRouteKind::None)
        return QString::fromStdString(route.error.toStdString());

    const bool capture = a.value("captureToTree").toBool(true);
    const QString path = hasFile ? a.value("filePath").toString() : QString();
    switch (route.kind)
    {
        case PresetRouteKind::NordBank:
        {
            const int program = a.contains("program")
                ? a.value("program").toInt(-1) : -1;
            return nordBankFileToolText(e, ti, si, path, program, capture, outOk);
        }
        case PresetRouteKind::Je8086Patch:
        {
            // program doubles as the 1-based patch unit for JP-8080 files
            const int unit = a.contains("program") ? a.value("program").toInt(1) : 1;
            return je8086PatchFileToolText(e, ti, si, path,
                unit > 0 ? unit : 1, capture, outOk);
        }
        case PresetRouteKind::WaldorfSysex:
            return waldorfSysexFileToolText(e, ti, si, path, pluginId, capture, outOk);
        case PresetRouteKind::VirusRom:
            return virusRomPresetToolText(e, ti, si,
                a.value("bank").toInt(0), a.value("program").toInt(0),
                a.value("channel").toInt(1), capture, outOk);
        case PresetRouteKind::FmSysex:
            return fmImportSysexToolText(e, ti, si, path,
                a.value("voiceIndex").toInt(0), outOk);
        case PresetRouteKind::SubSynthVirus:
            return subSynthImportSysexToolText(e, ti, si, path,
                a.value("voiceIndex").toInt(0), outOk);
        case PresetRouteKind::PluginPresetFile:
            return loadPluginPresetFileToolText(e, ti, si, path, outOk);
        case PresetRouteKind::None:
            break;
    }
    return QString::fromStdString(route.error.toStdString());
}

// ── audition_patch: probe track/clip placement + patch load ─────────────────
// Composite over the SAME primitives the MCP tool uses: the shared loaders
// above (subSynthImportSysexToolText's loadVirusPatch / the fm parse +
// setFmPatch persist path), ProjectCommands::addFxSlot for the probe slot, and
// the role probe phrase clip. Both surfaces pass the whole argument object
// (path/engine/role/root/trackId) so placement and loading cannot drift.
namespace detail {

struct ProbeNote {
    int pitch;
    float velocity;
    double start;
    double duration;
};

inline int patchRoleDefaultRoot(const QString& role)
{
    if (role == "bass") return 36;
    if (role == "lead") return 72;
    if (role == "pad")  return 48;
    if (role == "stab") return 60;
    if (role == "arp")  return 60;
    if (role == "fx")   return 36;
    if (role == "riser") return 36;
    return 48;
}

// Deterministic role probe phrase in clip-local beats — a C++ translation of
// timbre-lib/sweep_dx7_patches.py build_probe_notes (same shapes and role->
// root defaults; the audition window is fixed at 8 beats, seed fixed at 12345).
// Pitches are clamped to 0..127 by skipping out-of-range notes.
inline std::vector<ProbeNote> buildPatchProbeNotes(const QString& role, int root,
                                                   double windowBeats)
{
    std::vector<ProbeNote> notes;
    auto add = [&](int pitch, double start, double dur, float vel) {
        if (pitch >= 0 && pitch <= 127)
            notes.push_back({pitch, vel, start, dur});
    };

    if (role == "bass")
    {
        add(root, 0.0, windowBeats, 100.0f);
    }
    else if (role == "lead")
    {
        add(root, 0.0, windowBeats, 100.0f);
        add(root + 4, 0.0, windowBeats, 90.0f);
    }
    else if (role == "pad")
    {
        add(root, 0.0, windowBeats, 100.0f);
        add(root + 7, 0.0, windowBeats, 90.0f);
        add(root + 12, 0.0, windowBeats, 80.0f);
    }
    else if (role == "stab")
    {
        for (double beat = 0.0; beat < windowBeats; beat += 2.0)
        {
            add(root, beat, 0.5, 100.0f);
            add(root + 7, beat, 0.5, 100.0f);
            add(root + 12, beat, 0.5, 100.0f);
        }
    }
    else if (role == "arp")
    {
        static const int seq[] = { 0, 3, 7, 12, 7, 3 };
        double t = 0.0;
        int i = 0;
        while (t < windowBeats)
        {
            add(root + seq[i % 6], t, 0.4, 100.0f);
            t += 0.5;
            ++i;
        }
    }
    else
    {
        // riser / fx / unknown: 16-step rising gliss (the Python fallback,
        // which also doubles as the percussive-role placeholder).
        for (int k = 0; k < 16; ++k)
        {
            const double t = static_cast<double>(k);
            if (t >= windowBeats) break;
            add(root + k, t, 0.8, 100.0f);
        }
    }
    return notes;
}

} // namespace detail

inline QString auditionPatchToolText(AudioEngine& e, const QJsonObject& a,
                                     bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    QString filePath = a.value("path").toString();
    if (filePath.isEmpty())
        return "path required";
    juce::File patchFile(filePath.toStdString());
    if (!patchFile.existsAsFile())
        return "file not found: " + filePath;

    // ── resolve engine: explicit arg, else sidecar engine key, else header ──
    std::string engine = a.value("engine").toString().toStdString();
    if (engine.empty())
    {
        auto sidecarEngine = [](const juce::File& sc) -> std::string {
            if (!sc.existsAsFile()) return {};
            auto j = juce::JSON::parse(sc.loadFileAsString());
            auto* o = j.getDynamicObject();
            return o ? o->getProperty("engine").toString().toStdString()
                     : std::string();
        };
        engine = sidecarEngine(juce::File(patchFile.getFullPathName() + ".virus.json"));
        if (engine.empty())
            engine = sidecarEngine(juce::File(patchFile.getFullPathName() + ".dx7.json"));
        if (engine.empty())
        {
            juce::MemoryBlock raw;
            if (patchFile.loadFileAsData(raw))
            {
                const auto* b = static_cast<const uint8_t*>(raw.getData());
                const size_t n = raw.getSize();
                if (n >= 2 && b[0] == 0xF0 && b[1] == 0x43)
                    engine = "fm_synth";
                else if (n >= 5 && b[0] == 0xF0 && b[1] == 0x00
                         && b[2] == 0x20 && b[3] == 0x33)
                    engine = "sub_synth";
            }
        }
        if (engine.empty())
            return "could not determine patch engine — pass engine explicitly "
                   "(sub_synth or fm_synth)";
    }

    // ── probe track (or reuse trackId) ──
    auto& m = e.getProjectModel();
    auto tl = m.getTrackListTree();
    int trackId = a.contains("trackId") ? a.value("trackId").toInt() : -1;
    if (trackId < 0 || trackId >= tl.getNumChildren())
    {
        const int idx = tl.getNumChildren();
        juce::ValueTree t(IDs::TRACK);
        // Stable identity (design B1), same tree-derived allocator every
        // other TRACK constructor uses — a probe track is a real track and
        // must not be the one entity in the list without an id.
        t.setProperty(IDs::trackID, m.allocateTrackID(), nullptr);
        t.setProperty(IDs::name, "Patch Probe", nullptr);
        t.setProperty(IDs::volume, 0.85, nullptr);
        t.setProperty(IDs::pan, 0.0, nullptr);
        t.setProperty(IDs::isMuted, false, nullptr);
        t.setProperty(IDs::isSoloed, false, nullptr);
        t.setProperty(IDs::parentBus, 0, nullptr);
        t.setProperty(IDs::color, static_cast<int>(
            ProjectModel::trackColorForIndex(idx)), nullptr);
        t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
        t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, nullptr);
        t.addChild(ProjectModel::createTrackAutomationList(), -1, nullptr);
        tl.addChild(t, -1, &m.getUndoManager());
        trackId = idx;
    }

    // ── synth slot of the engine type ──
    e.getProjectCommands().addFxSlot(trackId, engine, -1, "");
    auto fxChain = tl.getChild(trackId).getChildWithName(IDs::FX_CHAIN);
    const int n = fxChain.isValid() ? fxChain.getNumChildren() : 0;
    const int slotIndex = n > 0 ? n - 1 : 0;

    // ── load the patch ──
    QString name = QString::fromUtf8(patchFile.getFileName().toRawUTF8());
    if (engine == "sub_synth")
    {
        auto r = e.getAudioEngineCommands().loadVirusPatch(
            trackId, slotIndex, filePath.toStdString(), 0);
        if (!r.ok)
            return QString::fromStdString(r.error);
        if (!r.name.empty())
            name = QString::fromStdString(r.name);
    }
    else if (engine == "fm_synth")
    {
        juce::MemoryBlock raw;
        if (!patchFile.loadFileAsData(raw))
            return "failed to read file";
        const auto* bytes = static_cast<const uint8_t*>(raw.getData());
        const size_t fileSize = raw.getSize();
        std::optional<Dx7Voice> voice;
        if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43
            && bytes[3] == 0x00)
            voice = parseSingleVoiceSysex(bytes, fileSize);
        else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43
                 && bytes[3] == 0x09)
        {
            auto voices = parseCartridgeSysex(bytes, fileSize);
            if (!voices.empty()) voice = voices[0];
        }
        else
        {
            return "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)";
        }
        if (!voice.has_value())
            return "failed to parse SysEx data (bad checksum or size)";
        juce::MemoryBlock block(voice->patchData.data(), FmSynthEngine::kPatchSize);
        e.getProjectCommands().setFmPatch(trackId, slotIndex,
            block.toBase64Encoding().toStdString());
        if (!voice->voiceName.empty())
            name = QString::fromStdString(voice->voiceName);
    }
    else
    {
        return "unsupported engine: " + QString::fromStdString(engine);
    }

    // ── role probe phrase clip ──
    QString role = a.value("role").toString("pad");
    const int root = a.contains("root") ? a.value("root").toInt()
                                        : detail::patchRoleDefaultRoot(role);
    constexpr double kWindowBeats = 8.0;
    const double bpm = m.getTree().getProperty(IDs::tempo, 120.0);
    const double durSec = beatsToSeconds(kWindowBeats, bpm);
    auto clip = m.createMidiClipEmpty("Patch Probe", 0.0, durSec);
    clip.setProperty(IDs::color, static_cast<int>(
        ProjectModel::trackColorForIndex(trackId)), nullptr);
    auto nl = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    for (const auto& note : detail::buildPatchProbeNotes(role, root, kWindowBeats))
        nl.addChild(m.createMidiNote(note.pitch, note.velocity / 127.0f,
                                     note.start, note.duration), -1, nullptr);
    tl.getChild(trackId).getChildWithName(IDs::CLIP_LIST).addChild(clip, -1,
        &m.getUndoManager());

    // ── sync the live processor (Gate 2/6): the probe track must be in
    // the routing graph with the loaded slot so pressing play is audible
    // and the live slot values reflect the patch. rebuildRoutingGraph
    // restores param_N / fmPatchData from the tree (Gate 1/10 path).
    if (auto* proc = e.getMainProcessor())
        proc->rebuildRoutingGraph();

    QJsonObject result;
    result["ok"] = true;
    result["trackId"] = trackId;
    result["slotIndex"] = slotIndex;
    result["name"] = name;
    result["engine"] = QString::fromStdString(engine);
    result["role"] = role;

    if (outOk) *outOk = true;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

} // namespace HDAW
