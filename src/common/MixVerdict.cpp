#include "MixVerdict.h"

#include "MixReportJson.h"

#include <QJsonArray>
#include <QJsonValue>

#include <juce_audio_formats/juce_audio_formats.h>

namespace HDAW {

namespace {

// one gate row: {ok, detail...}
QJsonObject gate(bool ok, QJsonObject detail)
{
    QJsonObject o{ { "ok", ok } };
    for (auto it = detail.begin(); it != detail.end(); ++it)
        o.insert(it.key(), it.value());
    return o;
}

} // namespace

MixVerdictResult buildMixVerdict(const QString& filePath,
                                 std::vector<SectionWindow> windows,
                                 const QJsonObject& planKinds,
                                 double bpm,
                                 double dropBuildRatio,
                                 const QJsonObject& structureAudit,
                                 const QJsonObject& modulationCoverage,
                                 double introSeconds)
{
    MixVerdictResult out;

    // The one shared builder behind mix_report (guard, clamping, shape) — see MixReportJson.h.
    auto built = buildMixReportPayload(filePath, windows, bpm);
    if (!built.error.isEmpty())
    {
        out.error = built.error;
        return out;
    }
    if (built.allWindowsDropped)
    {
        out.error = "no plan sections fall inside the file duration";
        return out;
    }
    QJsonObject report = built.payload;

    QJsonObject gates;
    QJsonArray issues, warnings;

    // 1) audible — a silent render makes every other gate vacuous (lesson 25).
    const double rms = report.value("rms").toDouble();
    gates["audible"] = gate(rms > 1e-4, QJsonObject{ { "rms", rms } });
    if (rms <= 1e-4)
        issues.append("silent render (rms <= 1e-4): nothing was captured — check the clips, "
                      "the instrument and the export range before judging anything else");

    // 2) clipping — the verdict the mix_report payload now carries (P1).
    const bool clipping = report.value("clipping").toBool();
    gates["clipping"] = gate(!clipping, QJsonObject{ { "peak", report.value("peak") } });
    if (clipping)
        issues.append("clipping: peak reaches full scale — run auto_gain_tracks (batch "
                      "gain-staging) or lower the master gain, then re-render");

    // 3) loudness — drop vs build, only when the caller supplied the plan's kind map.
    if (!planKinds.isEmpty())
    {
        applyDropVsBuildGate(report, planKinds, dropBuildRatio);
        const auto lg = report.value("loudnessGates").toObject();
        if (!lg.isEmpty())
        {
            const bool ok = lg.value("ok").toBool();
            gates["loudness"] = gate(ok, QJsonObject{ { "ratioThreshold", lg.value("ratioThreshold") },
                                                      { "dropVsBuild", lg.value("dropVsBuild") } });
            for (const auto& i : lg.value("issues").toArray())
                issues.append(i);
        }
    }

    // 4) structure — arrangement variety, when the caller ran the audit.
    if (!structureAudit.isEmpty())
    {
        const bool ok = structureAudit.value("ok").toBool();
        gates["structure"] = gate(ok, QJsonObject{ { "gates", structureAudit.value("gates") },
                                                   { "dropChecks", structureAudit.value("dropChecks") } });
        if (structureAudit.value("hasPlan").toBool() && !ok)
        {
            const auto g = structureAudit.value("gates").toObject();
            if (g.value("boredomSpans").toInt() > 0)
                issues.append(QString("structure: %1 boredom span(s) — consecutive non-build "
                                      "sections with no melodic and no backbeat role")
                                  .arg(g.value("boredomSpans").toInt()));
            if (!g.value("allDropsHaveBackbeat").toBool())
                issues.append("structure: a drop has no clap/snare backbeat");
            if (!g.value("firstDropHasMotif").toBool())
                issues.append("structure: the first drop carries no lead/stab/motif");
            if (!g.value("dropsAtLeastBuildLoad").toBool())
                issues.append("structure: a drop is thinner than its build");
        }
    }

    // 5) modulation — the global rule (every sounding track must carry movement), when the
    // caller supplied the audit. This is the gate the verdict used to have to exclude.
    if (!modulationCoverage.isEmpty())
    {
        const auto sum = modulationCoverage.value("summary").toObject();
        const auto attention = sum.value("attentionRequiredIds").toArray();
        const int withClips = sum.value("tracksWithClips").toInt();
        const bool ok = attention.isEmpty();
        gates["modulation"] = gate(ok, QJsonObject{ { "tracksWithClips", withClips },
                                                    { "fullyCovered", sum.value("fullyCovered") },
                                                    { "attentionRequiredIds", attention } });
        if (!ok)
            issues.append(QString("modulation: %1 of %2 sounding track(s) carry no movement "
                                  "(no enabled LFO, no enabled automation lane with >= 3 "
                                  "points, no sub_synth internal LFO) — apply_movement_plan "
                                  "writes movement for many tracks in one undo unit")
                              .arg(attention.size()).arg(withClips));
        if (!sum.value("faderOverriddenIds").toArray().isEmpty())
            warnings.append("faderOverridden: an enabled Volume lane makes automation "
                            "authoritative on some track(s) — call set_fader_authoritative "
                            "before gain staging");
    }

    // 6) intro blast — the recurring "big loud weird sound at the start" class.
    if (introSeconds > 0.0)
    {
        BlastReport blast;
        juce::String err;
        if (MixReportAnalyzer::analyzeBlast(juce::File(filePath.toStdString()), introSeconds,
                                            0.125, blast, err))
        {
            gates["introBlast"] = gate(!blast.detected,
                                       QJsonObject{ { "detected", blast.detected },
                                                    { "clipping", blast.clipping },
                                                    { "loudTransient", blast.loudTransient },
                                                    { "silenceAfter", blast.silenceAfter },
                                                    { "dcOffset", blast.dcOffset },
                                                    { "blastStart", blast.blastStart },
                                                    { "blastPeak", blast.blastPeak } });
            if (blast.detected)
                issues.append(QString("intro blast at %1s (peak %2) — diagnose with "
                                      "diagnose_intro_blast before shipping")
                                  .arg(QString::number(blast.blastStart, 'f', 2))
                                  .arg(QString::number(blast.blastPeak, 'f', 3)));
        }
        else
        {
            // Not a failure of the mix: report that the gate could not be evaluated rather
            // than silently dropping it.
            warnings.append("introBlast gate skipped: " + QString::fromUtf8(err.toRawUTF8()));
        }
    }

    if (report.value("measurementSuspicious").toBool())
        warnings.append("measurementSuspicious: the file measured silent and stayed silent "
                        "after a re-read — a wedged instance can read zeros for real audio "
                        "(engine_restart clears it)");

    bool ok = true;
    for (auto it = gates.begin(); it != gates.end(); ++it)
        if (!it.value().toObject().value("ok").toBool())
            ok = false;

    out.verdict = QJsonObject{
        { "ok", ok },
        { "file", filePath },
        { "gates", gates },
        { "issues", issues },
        { "warnings", warnings } };
    return out;
}

} // namespace HDAW
