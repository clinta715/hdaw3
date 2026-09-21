#pragma once

#include <QJsonObject>
#include <QString>

class MainAudioProcessor;
class ProjectModel;

namespace HDAW {

/// Canonical read-only psy_fm modulation-matrix view.
///
/// Shared by the MCP tool `psy_fm_mod_matrix_debug` (src/mcp/McpTools_PsyFm.cpp)
/// and the frontend RPC method `psy_fm.modMatrixDebug`
/// (src/frontend/router/Router_PsyFm.cpp), so the two surfaces cannot drift
/// (AGENTS.md "RPC parity by construction"). Previously only the MCP tool had it,
/// which is exactly how the payload would have diverged.
///
/// Reports, per route: the source name/value, the raw contribution
/// (sourceValue * depth) and the budget-scaled contribution actually applied.
/// Op6Feedback routes share a per-destination budget: when the summed |depth|
/// exceeds 1.0 every feedback contribution is scaled by 1/total. Also returns the
/// base vs computed (simulated apply()) ratios/feedback and the live source
/// values.
///
/// Read-only: no mutation, no audio render. When the plugin processor is absent
/// (no audio device / headless) it falls back to the persisted `psyFmMatrix`
/// routes so a patch designer still gets a usable view.
///
/// The CALLER validates that the track/slot exists and is a `psy_fm` slot; this
/// returns an empty object when the slot's ValueTree is missing.
QJsonObject buildPsyFmModMatrixView (ProjectModel& model,
                                     MainAudioProcessor* processor,
                                     int trackIndex,
                                     int slotIndex);

} // namespace HDAW
