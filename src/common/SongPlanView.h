#pragma once
// Song-plan / layer-handoff JSON views — the SINGLE shaping behind both the MCP
// tools (audit_song_structure, get_layer_handoffs) and the RPC methods
// (composition.auditSongStructure, project.getLayerHandoffs), so the two surfaces
// cannot drift (AGENTS.md "Feature parity: MCP + RPC"; the same pattern as
// DeviceParamMap, PsyFmModMatrixView and MatrixPresetService).
//
// Before 2026-09-21 this shaping lived only in src/mcp/McpTools_SongPlan.cpp, which
// is why those two tools had no RPC route.
//
// READ-ONLY shaping: nothing is mutated, no engine state is touched.
//
// Plan: docs/plans/2026-09-21-rpc-parity-retrofit.md (slice 3)

#include <QJsonArray>
#include <QJsonObject>

#include <juce_data_structures/juce_data_structures.h>

#include "ProjectCommands.h"

namespace HDAW {

struct SongStructureAudit;   // engine/SongStructureAudit.h

// One layer handoff as JSON. Only non-empty fields are emitted; `modulation` and
// `verify` are emitted as JSON objects when their strings parse as objects, else
// passed through as plain strings (the pre-extraction MCP behaviour).
QJsonObject layerHandoffJson(const ProjectCommands::LayerHandoff& h);

// The layer-handoff ledger as a JSON array: every track (or just `trackId` when
// >= 0) as {trackId, name, hasHandoff, [role, soundIntent, patternIntent,
// modulation, verify]}. Tracks without a handoff appear with hasHandoff=false and
// no content fields. Out-of-range trackId yields an empty array (the surfaces
// validate the range and report it themselves).
QJsonArray layerHandoffsJson(const juce::ValueTree& trackList, int trackId);

// Arrangement-variety audit payload (Mix Verifier boredom/static-span gates):
// {ok, hasPlan, gates{...}, dropChecks{...}, sections[], spans[]} — or the
// "no song plan set" note when the audit found no plan.
QJsonObject structureAuditJson(const SongStructureAudit& audit);

} // namespace HDAW
