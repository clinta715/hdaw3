// McpTools_Matrix.cpp — matrix-preset MCP tools (R3, docs/plans/2026-09-16-matrix-presets.md).
//
// list_matrix_presets / apply_matrix_preset — MCP surface for the per-plugin
// matrix-preset pipeline. The implementation lives in the shared
// HDAW::MatrixPresetService (src/common/MatrixPresetService.h), which the RPC
// surface (src/frontend/router/Router_Matrix.cpp) calls too: same payloads, same
// dispatch routes, same error text — parity by construction (AGENTS.md "Feature
// parity: MCP + RPC"). Before 2026-09-21 this logic lived here only, so the whole
// domain was MCP-only.
//
// Engine surface ONLY: this file shapes the tool schemas and serializes the
// service's payload as compact JSON. No DSP, no audio-thread code, no ValueTree
// schema changes, no plugin instantiation.

#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../common/MatrixPresetService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace mcp {

namespace {

// Success payloads are the service's QJsonObject verbatim, compact-serialized —
// identical to what the RPC returns as the JSON-RPC result object.
McpToolResult matrixPayloadText(const QJsonObject& payload)
{
    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

} // namespace

void registerMatrixTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({ "list_matrix_presets",
        "List the harvested matrix presets + morph chains for ONE core plugin engine"
        " (je8086, virus, nodalred2x, xenia, vavra). Sheets are read from"
        " timbre-lib/matrix_presets/ (HDAW_MATRIX_PRESETS_DIR overrides the location)."
        " Returns {engine, sheet, presets:[{id,name,role,appliesVia,evidence}],"
        " morphs:[{pair,distance,steps,apply}]} where morph apply is 'sysex'"
        " (injectable SysEx steps), 'file' (.syx step files, nord), or 'params'."
        " Same payload as the matrix.listPresets RPC method.",
        objSchema({ { "engine", QJsonObject{ { "type", "string" } } } },
                  QJsonArray{ "engine" }),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto r = HDAW::listMatrixPresets(a.value("engine").toString());
            if (!r.ok)
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return matrixPayloadText(r.payload);
        } });

    s.registerTool({ "apply_matrix_preset",
        "Apply ONE matrix preset or morph step (ids from list_matrix_presets) to a plugin FX"
        " slot. Dispatch: parameter-level ids (je8086 presets / param-carrying morph steps) go"
        " through the set_fx_param engine path, resolving decoder names to live param indexes"
        " via <engine>_param_index_map.json, and return {applied,skipped,unmapped} plus the"
        " deferred plugin-state capture info (captureToTree, default true — the send_fx_midi"
        " trigger that snapshots the applied params into the tree for offline renders; poll"
        " get_fx_capture_status to confirm; captureToTree:false skips) — param writes reach the"
        " LIVE child only, so without the capture offline renders boot the init patch; morph"
        " steps carrying SysEx queue through the send_fx_midi path and return"
        " {queued,captureDeferred:true}; nodalred2x morph steps load their .syx file through the"
        " load_nord_bank path. Realtime mutation: not undoable; capture via project save."
        " Same payloads as the matrix.applyPreset RPC method.",
        objSchema({ { "engine", QJsonObject{ { "type", "string" } } },
                   { "id", QJsonObject{ { "type", "string" } } },
                   { "trackId", QJsonObject{ { "type", "integer" } } },
                   { "slotIndex", QJsonObject{ { "type", "integer" } } },
                   { "captureToTree", QJsonObject{ { "type", "boolean" } } } },
                  QJsonArray{ "engine", "id", "trackId", "slotIndex" }),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            // Required-argument shape stays a surface concern (MCP reports it as tool
            // text, the RPC as -32602); everything semantic is the service's.
            if (!a.contains("trackId") || !a.contains("slotIndex"))
                return McpToolResult::text("trackId and slotIndex required", true);
            const auto r = HDAW::applyMatrixPreset(
                *e, a.value("engine").toString(), a.value("id").toString(),
                a.value("trackId").toInt(-1), a.value("slotIndex").toInt(-1),
                a.value("captureToTree").toBool(true));
            if (!r.ok)
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return matrixPayloadText(r.payload);
        } });
}

} // namespace mcp
