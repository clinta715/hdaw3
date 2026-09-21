// McpTools_Tuning.cpp — spectral tuning analysis (analyze_tuning).
//
// The analysis lives in src/common/TuningAnalysis.cpp, shared with the RPC method
// `tuning.analyze` (and the async `tuning.jobStatus`), so the payload cannot drift
// between the surfaces — parity by construction (AGENTS.md "Feature parity: MCP + RPC",
// docs/plans/2026-09-21-rpc-parity-retrofit.md slice 4). Before this the analysis was
// static helpers in THIS file, which is why the only tuning tool had no RPC route.

#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "McpJobs.h"
#include "../engine/AudioEngine.h"
#include "../common/TuningAnalysis.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <exception>

namespace mcp {

void registerTuningTools(McpServer& s, AudioEngine* e)
{
    (void)e;
    s.registerTool({"analyze_tuning",
        "Analyze a rendered WAV file's spectral tuning per role (kick/bass/arp/lead/hat/pad). "
        "Computes centroid, rolloff85, mel_low/mid/high via timbre-lib style descriptors, "
        "compares to per-role targets, and returns pass/fail + deterministic suggestions "
        "(rootNote +/-12, filter cutoff, OctaveRange). "
        "Use to verify psytrance tuning: kick <120Hz, bass 60-250Hz, arp/lead 400-3000Hz, hat >6kHz. "
        "Offline analysis+suggestion only; re-render via export then re-analyze (loop up to 3 times). "
        "unknown role values return skipped:true and are not evaluated. "
        "Optional wait=false returns immediately with {jobId,state:'running',pollWith:'poll_job'}; poll poll_job for the result. "
        "RPC twin: tuning.analyze returns the same payload (poll tuning.jobStatus when wait=false).",
        objSchema({
            {"wavPath", QJsonObject{{"type","string"}}},
            {"role", QJsonObject{{"type","string"}}},
            {"wait", QJsonObject{{"type","boolean"}}}
        }, {"wavPath"}),
        "audio",
        [](const QJsonObject& a) -> McpToolResult {
            const QString wavPath = a.value("wavPath").toString();
            const QString role = a.value("role").toString();
            const bool wait = a.value("wait").toBool(true);
            if (!wait) {
                const int id = McpJobs::instance().submit("analyze_tuning", [wavPath, role]() {
                    return HDAW::analyzeTuning(wavPath, role).object;
                });
                QJsonObject payload{{"jobId", id}, {"state", "running"}, {"pollWith", "poll_job"}};
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            }
            try {
                // rawJson preserves the pre-extraction output byte-for-byte (the analysis
                // serializes Indented, and the Python sidecar's key order is kept).
                return McpToolResult::text(HDAW::analyzeTuning(wavPath, role).rawJson);
            } catch (const std::exception& ex) {
                return McpToolResult::text(QString::fromUtf8(ex.what()), true);
            }
        }});
}

} // namespace mcp
