#pragma once
// Shared preset-loading dispatch for the MCP FX domain (docs/plans/2026-09-18-apply-preset-mcp-dispatch.md).
//
// 2026-09-24 parity wave: the implementation MOVED to src/common/PresetApply.h
// so the JSON-RPC routes (audio.applyPreset / audio.subSynthImportSysex /
// audio.auditionPatch, plugin.loadPresetFile) run the SAME loaders as these
// tools — the AGENTS.md parity contract ("where both surfaces shape the same
// artifact, put the logic in src/common/"). This header is now the MCP-side
// adapter: the dispatch table types alias the HDAW ones, and each run*
// executor wraps the matching HDAW::*ToolText function back into McpToolResult
// — so the individual tool handlers (McpTools_FmSynth.cpp, McpTools_FxSlot.cpp,
// McpTools_FxPreset.cpp) and apply_preset compile UNCHANGED.
//
// resolvePresetRoute() maps (slot fxType, slot pluginId, file header bytes)
// onto the same loaders the individual preset tools use. The run* executors
// are thin wrappers — the SINGLE implementations live in PresetApply.h.
//
// MCP-layer only: no audio-thread code, no graph mutation, no DSP writes
// beyond the pre-existing command-layer calls.

#include "McpToolDef.h"
#include "PresetFileParser.h"
#include "../common/NordBankLoader.h"
#include "../common/PresetApply.h"
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

// Dispatch-table types: aliased to the shared implementation so both spellings
// keep working (tests/unit/mcp/apply_preset_test.cpp pins mcp::PresetRouteKind).
using PresetRouteKind = HDAW::PresetRouteKind;
struct PresetRoute : HDAW::PresetRoute
{
    // Inherit the shared {kind, error}; construction sites keep working.
    PresetRoute() = default;
    PresetRoute(HDAW::PresetRouteKind k, const juce::String& err)
        : HDAW::PresetRoute{ k, err } {}
};

using HDAW::containsCI;
using HDAW::isVirusGearmulatorPluginId;
using HDAW::isNodalRed2xPluginId;
using HDAW::isJe8086PluginId;
using HDAW::isWaldorfPluginId;
using HDAW::waldorfMachineForPluginId;
using HDAW::waldorfNameForMachine;
using HDAW::isSubSynthSlot;
using HDAW::resolvePresetRoute;

// Tool-text wrappers: one per loader, each delegating to the shared HDAW
// entry point and re-shaping its exact text into a McpToolResult.
inline McpToolResult runVirusRomPreset(AudioEngine& e, int ti, int si,
                                       int bank, int program, int channel,
                                       bool captureToTree)
{
    bool ok = false;
    const auto text = HDAW::virusRomPresetToolText(e, ti, si, bank, program, channel, captureToTree, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runNordBankFile(AudioEngine& e, int ti, int si,
                                     const QString& path, int program,
                                     bool captureToTree)
{
    bool ok = false;
    const auto text = HDAW::nordBankFileToolText(e, ti, si, path, program, captureToTree, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runJe8086PatchFile(AudioEngine& e, int ti, int si,
                                        const QString& path, int presetIndex,
                                        bool captureToTree)
{
    bool ok = false;
    const auto text = HDAW::je8086PatchFileToolText(e, ti, si, path, presetIndex, captureToTree, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runFmImportSysex(AudioEngine& e, int ti, int si,
                                      const QString& filePath,
                                      int voiceIndexIn)
{
    bool ok = false;
    const auto text = HDAW::fmImportSysexToolText(e, ti, si, filePath, voiceIndexIn, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runSubSynthImportSysex(AudioEngine& e, int ti, int si,
                                            const QString& filePath,
                                            int voiceIndex)
{
    bool ok = false;
    const auto text = HDAW::subSynthImportSysexToolText(e, ti, si, filePath, voiceIndex, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runWaldorfSysexFile(AudioEngine& e, int ti, int si,
                                         const QString& path,
                                         const std::string& pluginId,
                                         bool captureToTree)
{
    bool ok = false;
    const auto text = HDAW::waldorfSysexFileToolText(e, ti, si, path, pluginId, captureToTree, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runLoadPluginPresetFile(AudioEngine& e, int ti, int si,
                                             const QString& filePath)
{
    bool ok = false;
    const auto text = HDAW::loadPluginPresetFileToolText(e, ti, si, filePath, &ok);
    return McpToolResult::text(text, !ok);
}

// The apply_preset / audition_patch composites: the tools hand their whole
// argument object to the ONE shared body.
inline McpToolResult runApplyPreset(AudioEngine& e, const QJsonObject& args)
{
    bool ok = false;
    const auto text = HDAW::applyPresetToolText(e, args, &ok);
    return McpToolResult::text(text, !ok);
}

inline McpToolResult runAuditionPatch(AudioEngine& e, const QJsonObject& args)
{
    bool ok = false;
    const auto text = HDAW::auditionPatchToolText(e, args, &ok);
    return McpToolResult::text(text, !ok);
}

} // namespace mcp
