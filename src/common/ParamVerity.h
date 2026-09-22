#pragma once
// ParamVerity — deterministic audibility verdicts for FX parameters (2026-09-22,
// docs/plans/2026-09-22-param-verity-pipeline.md).
//
// Generalizes the AGENTS.md lesson-27 gate discipline ("only multi-x separations
// count as audibility proof") into reusable math + a single JSON payload builder
// shared by the MCP `param_verity` tool and the RPC `composition.verifyParamSweep`
// route (same single-builder contract as MixReportJson).
//
// Method: render the SAME tree-copy window N times at the parameter's current
// value (baseline spread = the harness's own same-input variance, lesson 27),
// then once per swept value. A step is AUDIBLE only when
//     |rms - baselineMean| >= max(kVerityMinSeparation,
//                                 kVeritySeparationFactor * spread)
// with kVeritySeparationFactor = 3.0 (the NodalRed2x 3.1-8.2x precedent) and an
// absolute floor for numerically dead renders. A SILENT baseline is reported
// `inconclusive` — never "no effect" (lesson 25: silence masks every delta).
//
// Deterministic verdicts only: no LLM in this path by design (user decision
// 2026-09-22; prose summarization may consume the payload later).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../engine/TrackFXSlot.h"  // InternalParamDef
#include "ProjectCommands.h"   // ProjectCommands::ParamVerityResult

namespace HDAW {

// Shared param-name resolution for BOTH surfaces (MCP + RPC) — parity by
// construction. Case-insensitive, first match wins, -1 when unknown.
inline int paramIndexByName(const std::vector<HDAW::TrackFXSlot::InternalParamDef>& defs,
                            const QString& name)
{
    for (const auto& d : defs)
        if (QString::fromStdString(d.name.toStdString()).compare(name, Qt::CaseInsensitive) == 0)
            return d.index;
    return -1;
}

// Audibility threshold tuning (see docs/hardware-va-suite.md §9 spreads).
inline constexpr double kVeritySeparationFactor = 3.0;
inline constexpr double kVerityMinSeparation = 1e-4;

// Same-input spread: the largest absolute deviation of any baseline run from
// the baseline mean. With one run the spread is 0 (no variance evidence —
// callers wanting stricter proof should raise baselineRuns).
inline double veritySpread(const std::vector<double>& runs, double mean)
{
    double spread = 0.0;
    for (const double r : runs)
        spread = std::max(spread, std::abs(r - mean));
    return spread;
}

inline double verityThreshold(double spread)
{
    return std::max(kVerityMinSeparation, kVeritySeparationFactor * spread);
}

// The verdict for one swept value. `baselineAudible` must gate the result: a
// silent baseline proves nothing (lesson 25).
inline bool verityAudible(bool baselineAudible, double rms, double baselineMean,
                          double threshold)
{
    if (!baselineAudible) return false;
    return std::abs(rms - baselineMean) >= threshold;
}

// The single payload builder behind MCP `param_verity` and RPC
// `composition.verifyParamSweep` — parity by construction, not by discipline.
QJsonObject buildParamVerityPayload(const ProjectCommands::ParamVerityResult& r);

// Phase 3 corpus: aggregate payload + sidecar schema
// (hdaw.param.verity.corpus.v1) for `param_verity_corpus` /
// `composition.verifyParamCorpus`.
inline constexpr const char* kParamCorpusSchema = "hdaw.param.verity.corpus.v1";
QJsonObject buildParamCorpusPayload(const ProjectCommands::ParamCorpusResult& r);

} // namespace HDAW
