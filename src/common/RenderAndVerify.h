#pragma once
// render_and_verify — the ONE implementation behind the MCP `render_and_verify`
// tool and its RPC twin `export.renderAndVerify` (S4 of
// docs/plans/2026-09-28-agent-mechanization.md §5).
//
// It is the "re-render + re-verdict" loop as one call: render the WHOLE project
// through the shared export launcher (common/RenderLaunch.h — the same tree-copy
// / startExport path export_audio uses), WAIT for it, then compose the release
// verdict with the EXISTING buildMixVerdict (common/MixVerdict.h) over the
// produced file. Both surfaces call this function, so neither the payload nor
// any refusal text can drift.
//
// "mix_verdict inline" (plan §5) is literal: the verdict inputs are resolved by
// the SAME helper the mix_verdict tool/route run (common/MixVerdictInputs.h), so
// the loudness (fromPlan), structure-variety, modulation-coverage, intro-blast
// and targets gates all appear — `render_and_verify {outputPath}` and
// `mix_verdict {filePath: outputPath}` produce BYTE-IDENTICAL verdicts. `fromPlan`
// defaults to FALSE, MIRRORING the mix_verdict tool/route default, so the
// no-args calls agree (a song plan only enters the verdict when a caller asks
// for it with fromPlan:true on BOTH surfaces).
//
// READ-ONLY with respect to the live project: only the export tree copy and the
// ExportManager are touched (the written WAV is the caller's file).

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <memory>

#include "MixVerdict.h"
#include "MixVerdictInputs.h"
#include "RenderLaunch.h"

#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../model/ProjectModel.h"

namespace HDAW {

struct RenderAndVerifyResult
{
    bool ok = false;
    int errorCode = -32603;   // JSON-RPC code the route reports (the tool just reports text)
    QString error;            // non-empty on failure; byte-identical on both surfaces
    QJsonObject payload;      // {wavPath, verdict}
};

inline RenderAndVerifyResult renderAndVerify(AudioEngine& engine,
                                            const QString& outputPath,
                                            const QJsonObject& targets,
                                            uint32_t timeoutMs = 600000,
                                            bool fromPlan = false,
                                            double dropBuildRatio = 0.9,
                                            double introSeconds = 2.0)
{
    RenderAndVerifyResult out;

    if (outputPath.isEmpty())
    {
        out.errorCode = -32602;
        out.error = "outputPath required";
        return out;
    }
    if (engine.getMainProcessor() == nullptr)
    {
        out.error = "audio engine not initialized";
        return out;
    }

    // The SAME resolution mix_verdict runs. requirePlan=false: a render was already
    // going to happen, so a plan-less project falls back to the whole-file verdict
    // (mix_verdict's fromPlan:false path) instead of refusing after the render.
    const auto inputs = resolveMixVerdictInputs(engine, fromPlan, QJsonArray{}, 0.0,
                                                targets, dropBuildRatio, introSeconds,
                                                /*requirePlan=*/false);
    if (!inputs.ok)
    {
        out.errorCode = inputs.errorCode;
        out.error = inputs.error;
        return out;
    }

    const double seconds = ExportManager::calculateProjectDuration(engine.getProjectModel());
    auto rendered = renderProjectAndWait(engine, outputPath, 48000.0, 24,
                                         ExportManager::WAV, 0.0, seconds, {}, timeoutMs);
    if (!rendered.ok)
    {
        out.error = rendered.error;
        return out;
    }

    // The verdict composer mix_verdict uses, with the SAME inputs it resolves — so
    // the verdict is byte-identical to `mix_verdict {filePath: outputPath, ...}`.
    const auto verdict = buildMixVerdict(outputPath, inputs.windows, inputs.planKinds,
                                         inputs.bpm, inputs.dropBuildRatio,
                                         inputs.structureJson, inputs.modulationCoverage,
                                         inputs.introSeconds, inputs.targets);
    if (!verdict.error.isEmpty())
    {
        out.error = verdict.error;
        return out;
    }

    out.payload = QJsonObject{ { "wavPath", outputPath }, { "verdict", verdict.verdict } };
    out.ok = true;
    return out;
}

} // namespace HDAW
