#pragma once
// resolveMixVerdictInputs — the ONE resolution of a mix_verdict-shaped request
// (fromPlan / sections / bpm / targets / dropBuildRatio / introSeconds) into the
// inputs buildMixVerdict takes. It exists so the MCP `mix_verdict` tool, the RPC
// `audio.mixVerdict` route AND `render_and_verify` (which promised "full render +
// mix_verdict inline", plan §5) cannot drift: today render_and_verify called
// buildMixVerdict with EMPTY planKinds / structureAudit / modulationCoverage, so
// its verdict silently omitted the loudness, structure and modulation gates a
// mix_verdict of the same file would apply (ITEM 4 of the addendum).
//
// READ-ONLY: reads the song plan and the track list. No render, no mutation.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

#include "MixVerdict.h"
#include "ModulationCoverage.h"
#include "ProjectCommands.h"
#include "SongPlanView.h"

#include "../engine/AudioEngine.h"
#include "../engine/SongStructureAudit.h"

namespace HDAW {

struct MixVerdictInputs
{
    std::vector<SectionWindow> windows;
    QJsonObject planKinds;          // section name -> plan kind (the loudness gate)
    QJsonObject structureJson;      // structureAuditJson(auditSongStructure(...)) or {}
    QJsonObject modulationCoverage; // modulationCoverageJson(trackList); ALWAYS present
    double bpm = 0.0;
    double dropBuildRatio = 0.9;
    double introSeconds = 2.0;
    QJsonObject targets;
    bool ok = false;
    int errorCode = -32602;
    QString error;
};

// `requirePlan` is the ONE behavioural switch between the two callers: mix_verdict
// REFUSES "no song plan set (fromPlan)" when fromPlan is asked for without a plan
// (its documented contract); render_and_verify passes false, because it has
// ALREADY spent a render and must still answer — it falls back to the whole-file
// verdict, which is exactly what mix_verdict{fromPlan:false} computes, so the two
// surfaces still agree on a plan-less project.
inline MixVerdictInputs resolveMixVerdictInputs(AudioEngine& engine, bool fromPlan,
                                                const QJsonArray& sections, double bpm,
                                                const QJsonObject& targets,
                                                double dropBuildRatio, double introSeconds,
                                                bool requirePlan)
{
    MixVerdictInputs out;
    out.bpm = bpm;
    out.dropBuildRatio = (dropBuildRatio <= 0.0 || dropBuildRatio > 1.5) ? 0.9 : dropBuildRatio;
    out.introSeconds = introSeconds;
    out.targets = targets;
    // The modulation gate is the global rule the verdict now owns; it is supplied
    // UNCONDITIONALLY (mix_verdict always does, fromPlan or not).
    out.modulationCoverage = modulationCoverageJson(engine.getProjectModel().getTrackListTree());

    if (fromPlan)
    {
        const auto plan = engine.getProjectCommands().getSongPlan();
        if (plan.sections.empty())
        {
            if (requirePlan)
            {
                out.error = "no song plan set (fromPlan)";
                return out;
            }
            // Fall through: empty windows/planKinds/structure == the whole-file verdict.
        }
        else
        {
            if (out.bpm <= 0.0) out.bpm = plan.bpm;
            const double spb = (out.bpm > 0.0) ? 60.0 / out.bpm : 0.5;
            for (const auto& s : plan.sections)
            {
                out.windows.push_back(SectionWindow{ s.name, s.startBeat * spb, s.endBeat * spb });
                out.planKinds.insert(QString::fromStdString(s.name), QString::fromStdString(s.kind));
            }
            out.structureJson = structureAuditJson(
                auditSongStructure(engine.getProjectModel().getTrackListTree(), plan, out.bpm));
        }
    }
    else
    {
        for (const auto& v : sections)
        {
            const auto so = v.toObject();
            out.windows.push_back(SectionWindow{ so.value("name").toString().toStdString(),
                                                 so.value("start").toDouble(),
                                                 so.value("end").toDouble() });
        }
    }

    out.ok = true;
    return out;
}

} // namespace HDAW
