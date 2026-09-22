// ParamVerity payload — the SINGLE builder behind the MCP `param_verity` tool and
// the RPC `composition.verifyParamSweep` method (AGENTS.md "Feature parity").
// Phase 3 adds the corpus aggregate + sidecar builder (hdaw.param.verity.corpus.v1).
// See ParamVerity.h for the contracts.
#include "ParamVerity.h"

#include <QJsonDocument>

namespace HDAW {

QJsonObject buildParamVerityPayload(const ProjectCommands::ParamVerityResult& r)
{
    QJsonObject root{
        { "ok", r.ok },
        { "trackIndex", r.trackIndex },
        { "slotIndex", r.slotIndex },
        { "paramIndex", r.paramIndex },
        { "fxType", QString::fromStdString(r.fxType) },
        { "paramName", QString::fromStdString(r.paramName) },
        { "baselineRuns", r.baselineRuns },
        { "windowSeconds", r.windowSeconds },
        { "baselineRms", r.baselineRms },
        { "baselinePeak", r.baselinePeak },
        { "band", QJsonArray{ r.band[0], r.band[1], r.band[2], r.band[3] } },
        { "spread", r.spread },
        { "threshold", r.threshold },
        { "separationFactor", kVeritySeparationFactor },
        { "baselineAudible", r.baselineAudible },
        { "inconclusive", r.inconclusive },
        { "restored", r.restored },
        { "anyAudible", r.anyAudible }
    };
    if (!r.error.empty())
        root.insert("error", QString::fromStdString(r.error));

    QJsonArray steps;
    for (const auto& s : r.steps)
    {
        steps.append(QJsonObject{
            { "value", static_cast<double>(s.value) },
            { "rms", s.rms },
            { "peak", s.peak },
            { "band", QJsonArray{ s.band[0], s.band[1], s.band[2], s.band[3] } },
            { "rmsDelta", s.rmsDelta },
            { "bandDelta", QJsonArray{ s.bandDelta[0], s.bandDelta[1],
                                       s.bandDelta[2], s.bandDelta[3] } },
            { "audible", s.audible } });
    }
    root.insert("steps", steps);
    return root;
}

QJsonObject buildParamCorpusPayload(const ProjectCommands::ParamCorpusResult& r)
{
    QJsonArray entries;
    for (const auto& e : r.results)
    {
        QJsonObject o{
            { "paramIndex", e.paramIndex },
            { "paramName", QString::fromStdString(e.paramName) },
            { "ok", e.ok },
            { "anyAudible", e.anyAudible },
            { "baselineRms", e.baselineRms },
            { "spread", e.spread },
            { "maxAbsRmsDelta", e.maxAbsRmsDelta }
        };
        if (!e.error.empty())
            o.insert("error", QString::fromStdString(e.error));
        entries.append(o);
    }

    QJsonObject root{
        { "schema", kParamCorpusSchema },
        { "ok", r.ok },
        { "trackIndex", r.trackIndex },
        { "slotIndex", r.slotIndex },
        { "fxType", QString::fromStdString(r.fxType) },
        { "ran", r.ran },
        { "audibleCount", r.audibleCount },
        { "results", entries }
    };
    if (r.sidecarWritten)
    {
        root.insert("sidecarWritten", true);
        root.insert("outPath", QString::fromStdString(r.outPath));
    }
    if (!r.error.empty())
        root.insert("error", QString::fromStdString(r.error));
    return root;
}

} // namespace HDAW
