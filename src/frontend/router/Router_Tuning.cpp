// Router_Tuning.cpp — RPC surface for the spectral tuning analysis.
//
// tuning.analyze / tuning.jobStatus mirror the MCP tool analyze_tuning (and poll_job)
// by calling the shared HDAW::analyzeTuning in src/common/TuningAnalysis.cpp, so the
// payload cannot drift between the surfaces — parity by construction (AGENTS.md
// "Feature parity: MCP + RPC"). This was the last confirmed RPC gap: the analysis used
// to be static helpers inside src/mcp/McpTools_Tuning.cpp and there was no `tuning`
// namespace at all (docs/plans/2026-09-21-rpc-parity-retrofit.md slice 4).
//
// Async mode (`wait:false`) submits to the process-wide MCP job registry (McpJobs) — the
// SAME registry `poll_job` reads — and exposes it as `tuning.jobStatus`, the per-domain
// status convention already used by `rave.jobStatus` / `rave.trainingJobStatus`. The
// generic `poll_job` tool therefore stays an MCP-side convenience rather than becoming a
// cross-domain RPC concept. The frontend already depends on the mcp layer for its
// JSON-RPC envelope (FrontendServer.cpp includes mcp/McpJsonRpc.h), so this is an
// existing boundary, not a new one.
//
// READ-ONLY: the analysis decodes a WAV from disk. No DSP, no audio thread, no graph
// mutation, no ValueTree access, no engine state read.

#include "Router_Tuning.h"
#include "RouterHelpers.h"

#include "../../common/TuningAnalysis.h"
#include "../../mcp/McpJobs.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <exception>
#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchTuning(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    (void)engine;   // the analysis is file-based; no engine state is read

    const auto o = paramsObject(params);

    if (m == "analyze") {
        std::string wavPath;
        if (!requireString(o, "wavPath", wavPath, nullptr))
            return makeError(-32602, "missing or non-string param: wavPath");
        const QString role = QString::fromStdString(optString(o, "role", ""));
        const bool wait = optBool(o, "wait", true, nullptr);

        if (!wait) {
            const QString wav = QString::fromStdString(wavPath);
            const int id = mcp::McpJobs::instance().submit("analyze_tuning", [wav, role]() {
                return HDAW::analyzeTuning(wav, role).object;
            });
            return { false, QJsonObject{ { "jobId", id }, { "state", "running" },
                                         { "pollWith", "tuning.jobStatus" } } };
        }

        try {
            return { false, HDAW::analyzeTuning(QString::fromStdString(wavPath), role).object };
        } catch (const std::exception& ex) {
            // An unusable path/file is an argument problem (the caller named it), not an
            // environment failure.
            return makeError(-32602, QString::fromUtf8(ex.what()));
        }
    }

    if (m == "jobStatus") {
        int jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr))
            return makeError(-32602, "missing or non-numeric param: jobId");
        const auto status = mcp::McpJobs::instance().status(jobId);
        if (status.isEmpty())
            return makeError(-32602, "unknown jobId");
        return { false, status };
    }

    return makeError(-32601, "unknown tuning method: " + m);
}

} // namespace frontend
