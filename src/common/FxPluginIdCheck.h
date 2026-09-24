#pragma once
// FxPluginIdCheck — the shared add_fx pluginId gate (fix 2026-09-23, handoff
// "*FX CLAP editions silently silence a track / bare ids leave an inert slot").
//
// BOTH add_fx surfaces call this so the failure text stays byte-identical
// (MCP-RPC parity; asserted by tests/unit/frontend/add_fx_parity_test.cpp):
//   - MCP `add_fx`            — src/mcp/McpTools_FxSlot.cpp
//   - RPC `project.addFxSlot` — intercept in src/frontend/FrontendRouter.cpp
//     (its route, dispatchProject, receives only ProjectCommands& and cannot
//     reach the scan cache; FrontendRouter intercepts project.* methods that
//     need engine context — `project.importMidiFile` is the precedent).
//
// Returns "" when pluginId is acceptable for an FX slot, otherwise the exact
// error both surfaces must report (MCP isError text / RPC -32602 message).
//
// Sentinel analysis (where pluginId is actually consumed):
//   - src/mcp/McpTools_FxSlot.cpp:147-148 — the MCP surface derives
//     type="plugin" ONLY when fxType is absent and pluginId is present; the
//     internal kinds (eq/compressor/...) are chosen exclusively by the
//     schema-enum `fxType` argument.
//   - src/frontend/router/Router_Project.cpp:405-413,421 — the route reads
//     `type`/`fxType` and `pluginId` independently; pluginId defaults to "".
//   - src/model/ProjectModel.cpp:419-424 — pluginId is stored only when
//     type == "plugin" AND pluginId is non-empty, so a non-empty id on an
//     internal-fxType call is dead weight; validating it is safe.
//   - Empty pluginId is a legitimate sentinel (no external plugin: internal
//     fxType slots and the arg-less `add_fx {trackId}` pinned non-error by
//     tests/integration/mcp/mcp_coverage_test.cpp "Empty/unknown FX type") —
//     accepted here. No 'none' sentinel travels in pluginId anywhere.
//
// Order matters: the shadow-edition predicate fires BEFORE resolvability —
// "CLAP-VavraFX-a405fdaa-0" DOES resolve against a cache that contains the
// plugin, and it must still be rejected. Pure + cache-independent either way.

#include <string>

#include "../engine/PluginManager.h"
#include "../model/ProjectModel.h"

namespace HDAW {

inline std::string fxPluginIdError(const std::string& pluginId,
                                   const ProjectModel& model)
{
    if (pluginId.empty())
        return {};
    if (PluginManager::isShadowFxEdition(juce::String(pluginId)))
        return "pluginId \"" + pluginId + "\": *FX shadow edition — a synth-only "
               "build that cannot process track audio (it would silence the "
               "slot); excluded from list_plugins and rejected by add_fx";
    if (model.resolvePluginFormat(pluginId).empty())
        return "pluginId \"" + pluginId + "\" does not resolve to a scanned "
               "plugin or a .clap/.vst3 path (run scan_plugins; use ids from "
               "list_plugins)";
    return {};
}

} // namespace HDAW
