// Router_Matrix.cpp — RPC surface for the matrix-preset domain.
//
// matrix.listPresets / matrix.applyPreset mirror the MCP tools list_matrix_presets /
// apply_matrix_preset 1:1 — same artifact, same payload object, same error text and
// the same error class — by both calling the shared HDAW::MatrixPresetService. This
// is the RPC half of the project's "maintain RPC parity" rule (AGENTS.md); before
// 2026-09-21 the whole domain was reachable over MCP only.
//
// Engine surface ONLY: bounded file reads + JSON shaping + dispatch onto the existing
// engine entry points (the same code path the MCP tools run). No DSP, no audio
// thread, no plugin instantiation, no ValueTree schema changes.
//
// Plan: docs/plans/2026-09-21-rpc-parity-retrofit.md (slice 2)

#include "Router_Matrix.h"
#include "RouterHelpers.h"

#include "../../common/MatrixPresetService.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

namespace {

// The service carries the JSON-RPC code hint so both surfaces classify a failure
// identically: -32602 argument problems, -32603 environment/artifact problems.
DispatchResult fromOp(const HDAW::MatrixOpResult& r) {
    if (r.ok)
        return { false, r.payload };
    return makeError(r.errorCode, QString::fromStdString(r.error));
}

} // namespace

DispatchResult dispatchMatrix(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    if (m == "listPresets") {
        // Omit engine for the available-engines error path: an unknown engine lists
        // what IS available (same message the MCP tool produces).
        const QString engineId = QString::fromStdString(optString(o, "engine", ""));
        return fromOp(HDAW::listMatrixPresets(engineId));
    }

    if (m == "applyPreset") {
        std::string engineId, id;
        if (!requireString(o, "engine", engineId, nullptr))
            return makeError(-32602, "missing or non-string param: engine");
        if (!requireString(o, "id", id, nullptr))
            return makeError(-32602, "missing or non-string param: id");
        int ti = -1;
        if (!requireInt(o, "trackId", ti, nullptr))
            return makeError(-32602, "missing or non-numeric param: trackId");
        int si = -1;
        if (!requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "missing or non-numeric param: slotIndex");
        const bool capture = optBool(o, "captureToTree", true, nullptr);
        return fromOp(HDAW::applyMatrixPreset(engine, QString::fromStdString(engineId),
                                              QString::fromStdString(id), ti, si, capture));
    }

    return makeError(-32601, "unknown matrix method: " + m);
}

} // namespace frontend
