#pragma once
// Spectral tuning analysis — the SINGLE implementation behind the MCP tool
// `analyze_tuning` and the RPC method `tuning.analyze`.
//
// Parity by construction: before 2026-09-21 this lived as static helpers inside
// src/mcp/McpTools_Tuning.cpp, which is why the only tuning tool had no RPC route
// (docs/plans/2026-09-21-rpc-parity-retrofit.md slice 4). The same pattern as
// DeviceParamMap / PsyFmModMatrixView / MatrixPresetService / SongPlanView.
//
// READ-ONLY and engine-free: it decodes a WAV from disk and computes descriptors. No
// engine state, no DSP, no graph mutation, no ValueTree access.
//
// Two analysis paths, in order (unchanged from the pre-extraction behaviour):
//   1. the Python sidecar `timbre-lib/tune_roles.py` (highest fidelity — tried via
//      python/python3/py, then via `wsl <venv python>` with /mnt path translation);
//   2. a lightweight pure-C++ fallback (centroid/mel bands are approximate — the
//      hardcoded values are deliberate, see computeDescriptors).
// A JSON parser must never see a different answer depending on which path ran: both
// surfaces call THIS function, so they always agree.

#include <QJsonObject>
#include <QString>

namespace HDAW {

struct TuningAnalysisResult
{
    QJsonObject object;   // structured payload — the RPC result object
    QString rawJson;      // the exact JSON text the analysis produced (MCP tool output)
};

// Analyze `wavPath` for `role` (kick/bass/arp/lead/hat/pad; empty = per-role checks for
// all role targets).
//
// Throws std::runtime_error on: empty wavPath, wav not found, undecodable/empty audio,
// or an analysis that did not produce a JSON object.
// An UNKNOWN role is not an error — the check reports `skipped: true`.
TuningAnalysisResult analyzeTuning(const QString& wavPath, const QString& role);

} // namespace HDAW
