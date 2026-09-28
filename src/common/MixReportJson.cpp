#include "MixReportJson.h"

#include <QJsonValue>
#include <QtGlobal>

#include <algorithm>
#include <stdexcept>

#include <juce_audio_formats/juce_audio_formats.h>

namespace HDAW {

namespace {

QString jstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

} // namespace

MixReportPayloadResult buildMixReportPayload(const QString& filePath,
                                             std::vector<SectionWindow> windows,
                                             double bpm)
{
    MixReportPayloadResult out;

    const juce::File file(filePath.toStdString());
    if (!file.existsAsFile())
    {
        out.error = "file not found: " + filePath;
        return out;
    }
    if (bpm < 0.0)
    {
        out.error = "bpm must be >= 0";
        return out;
    }

    // File duration first: it drives the window clamp below.
    double durationSeconds = 0.0;
    {
        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
        if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
            durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }

    // Clamp / drop the windows against the real file duration. Windows fully outside are
    // dropped and reported; partially overlapping ones are truncated to the end.
    QJsonArray clampedNames;
    std::vector<SectionWindow> kept;
    kept.reserve(windows.size());
    for (auto& w : windows)
    {
        if (!(w.end > w.start))
        {
            out.error = QString("section '%1' has end <= start").arg(jstr(w.name));
            return out;
        }
        if (durationSeconds > 0.0)
        {
            if (w.start >= durationSeconds)
                continue;                       // fully outside -> drop
            if (w.end > durationSeconds)
            {
                clampedNames.append(jstr(w.name));
                w.end = durationSeconds;
            }
        }
        kept.push_back(w);
    }
    if (!windows.empty() && kept.empty())
    {
        out.allWindowsDropped = true;
        return out;
    }

    MixReport rep;
    juce::String err;
    if (!MixReportAnalyzer::analyze(file, kept, bpm, rep, err))
    {
        out.error = jstr(err);
        return out;
    }

    // File-visibility guard (see the header): a first pass that measures silence on a
    // non-trivial file is re-measured once after a short wait.
    if (rep.peak <= 0.0f && rep.duration > 0.5)
    {
        juce::Thread::sleep(3000);
        MixReport retry;
        if (MixReportAnalyzer::analyze(file, kept, bpm, retry, err) && retry.peak > 0.0f)
            rep = retry;
    }
    if (rep.peak <= 0.0f && rep.duration > 0.5)
    {
        // Persistently-zero measurement on a known-rendered file: the wedged-instance state
        // (clears on engine restart via the engine_restart tool).
        rep.measurementSuspicious = true;
    }

    QJsonObject root{
        { "duration", rep.duration },
        { "sampleRate", rep.sampleRate },
        { "peak", rep.peak },
        { "rms", rep.rms },
        { "bands", QJsonObject{
            { "sub", rep.bands[0] },
            { "bass", rep.bands[1] },
            { "body", rep.bands[2] },
            { "high", rep.bands[3] } } },
        { "kickProminence", rep.kickProminence },
        // Clipping verdict: HDAW::MixReport carries no clipping member (BlastReport does), so
        // derive it from peak with the SAME threshold the engine's blast classifier documents
        // (`any bin peak >= 0.999`, MixReport.h). Without it an agent had to interpret a float
        // to notice a slamming mix (2026-09-21 dogfood: peak 1.0, no verdict).
        { "clipping", rep.peak >= 0.999 },
        // B6: per-channel FS-clamp probe — the mono peak/rms above are blind to
        // one-sided clamps ((+1.0, -1.0) averages to 0). See MixReport.h.
        { "ceilingHitPct", rep.ceilingHitPct },
        { "measurementSuspicious", rep.measurementSuspicious } };
    if (rep.hasPumpDepth)
        root["pumpDepth"] = rep.pumpDepth;
    if (!clampedNames.isEmpty())
        root["clampedSections"] = clampedNames;

    QJsonArray sections;
    for (const auto& s : rep.sections)
    {
        sections.append(QJsonObject{
            { "name", jstr(s.name) },
            { "start", s.start },
            { "end", s.end },
            { "rms", s.rms },
            { "peak", s.peak },
            { "boundaryPeak", s.boundaryPeak },
            { "bandEnergy", QJsonObject{
                { "sub", s.bandEnergy[0] },
                { "bass", s.bandEnergy[1] },
                { "body", s.bandEnergy[2] },
                { "high", s.bandEnergy[3] } } } });
    }
    root["sections"] = sections;

    out.payload = root;
    return out;
}

MixReportPayloadResult buildWindowReportPayload(const QString& filePath,
                                                double startSec, double endSec,
                                                double bpm)
{
    MixReportPayloadResult out;

    const juce::File file(filePath.toStdString());
    if (!file.existsAsFile())
    {
        out.error = "file not found: " + filePath;
        return out;
    }
    if (bpm < 0.0)
    {
        out.error = "bpm must be >= 0";
        return out;
    }
    if (!(endSec > startSec))
    {
        out.error = "window has end <= start";
        return out;
    }

    // The window is the ONLY analyzer window: the section row carries the
    // window's rms/peak/bands/kick/clamp stats (MixReport.h SectionReport).
    std::vector<SectionWindow> windows{ SectionWindow{ juce::String("window"), startSec, endSec } };

    MixReport rep;
    juce::String err;
    if (!MixReportAnalyzer::analyze(file, windows, bpm, rep, err))
    {
        out.error = jstr(err);
        return out;
    }

    // The SAME file-visibility guard buildMixReportPayload runs: a just-finished
    // export's writer may still hold the file with unflushed data.
    if (rep.peak <= 0.0f && rep.duration > 0.5)
    {
        juce::Thread::sleep(3000);
        MixReport retry;
        if (MixReportAnalyzer::analyze(file, windows, bpm, retry, err) && retry.peak > 0.0f)
            rep = retry;
    }

    if (rep.sections.empty())
    {
        out.error = "window measurement missing";
        return out;
    }

    // PROMOTE the window's stats to the ROOT: applyTargetGates reads the root,
    // so this is what makes the gates gate the WINDOW (see the header note).
    const SectionReport& w = rep.sections.front();
    const double windowSeconds = w.end - w.start;
    const bool windowSilent = (w.peak <= 0.0 && windowSeconds > 0.5);
    if (windowSilent)
        rep.measurementSuspicious = true;

    QJsonObject root{
        { "duration", windowSeconds },
        { "sampleRate", rep.sampleRate },
        { "peak", w.peak },
        { "rms", w.rms },
        { "bands", QJsonObject{
            { "sub", w.bandEnergy[0] },
            { "bass", w.bandEnergy[1] },
            { "body", w.bandEnergy[2] },
            { "high", w.bandEnergy[3] } } },
        { "kickProminence", w.kickProminence },
        { "clipping", w.peak >= 0.999 },
        { "ceilingHitPct", w.ceilingHitPct },
        { "ceilingHitFrames", static_cast<double>(w.ceilingHitFrames) },
        { "measurementSuspicious", rep.measurementSuspicious } };
    if (rep.hasPumpDepth)
        root["pumpDepth"] = rep.pumpDepth;

    root["sections"] = QJsonArray{ QJsonObject{
        { "name", jstr(w.name) },
        { "start", w.start },
        { "end", w.end },
        { "rms", w.rms },
        { "peak", w.peak },
        { "boundaryPeak", w.boundaryPeak },
        { "ceilingHitPct", w.ceilingHitPct },
        { "ceilingHitFrames", static_cast<double>(w.ceilingHitFrames) },
        { "kickProminence", w.kickProminence },
        { "bandEnergy", QJsonObject{
            { "sub", w.bandEnergy[0] },
            { "bass", w.bandEnergy[1] },
            { "body", w.bandEnergy[2] },
            { "high", w.bandEnergy[3] } } } } };

    out.payload = root;
    return out;
}

void applyTargetGates(QJsonObject& root, const QJsonObject& targets)
{
    if (targets.isEmpty()) return;
    const double rms = root.value("rms").toDouble();
    const double ceilingPct = root.value("ceilingHitPct").toDouble();
    const double kick = root.value("kickProminence").toDouble();
    const double duration = root.value("duration").toDouble();
    QJsonArray rows;
    bool allPass = true;
    auto add = [&](const char* name, double expected, double actual, bool pass,
                   const char* op) {
        rows.append(QJsonObject{ { "target", name }, { "expected", expected },
                                 { "actual", actual }, { "op", op }, { "pass", pass } });
        if (!pass) allPass = false;
    };
    if (targets.contains("masterRms"))
    {
        const double t = targets.value("masterRms").toDouble();
        // ±5% band on the MONO-DOWNMIX rms (convention pinned in brief.schema.json).
        const bool pass = t > 0.0 && std::fabs(rms - t) <= 0.05 * t;
        add("masterRms", t, rms, pass, "within 5%");
    }
    if (targets.contains("ceilingHitPctMax"))
    {
        const double t = targets.value("ceilingHitPctMax").toDouble();
        add("ceilingHitPctMax", t, ceilingPct, ceilingPct <= t, "at most");
    }
    if (targets.contains("kickProminenceMin"))
    {
        const double t = targets.value("kickProminenceMin").toDouble();
        add("kickProminenceMin", t, kick, kick >= t, "at least");
    }
    if (targets.contains("targetDurationSeconds"))
    {
        const double t = targets.value("targetDurationSeconds").toDouble();
        const bool pass = t > 0.0 && std::fabs(duration - t) <= 2.0;
        add("targetDurationSeconds", t, duration, pass, "within 2s");
    }
    if (targets.contains("rmsMin"))
    {
        // A FLOOR on the root RMS (linear amplitude, the SAME units the payload's
        // `rms` field carries and the same convention `masterRms` uses). Silent
        // render: rms 0 fails any positive floor.
        const double t = targets.value("rmsMin").toDouble();
        add("rmsMin", t, rms, rms >= t, "at least");
    }
    root["targetChecks"] = rows;
    root["targetsOk"] = allPass;
}

void applyDropVsBuildGate(QJsonObject& root, const QJsonObject& planKinds, double ratio)
{
    if (planKinds.isEmpty()) return;
    const auto secs = root.value("sections").toArray();
    if (secs.isEmpty()) return;
    const auto isBuild = [](const QString& k) {
        return k.compare("build", Qt::CaseInsensitive) == 0
            || k.compare("build2", Qt::CaseInsensitive) == 0; };
    const auto isDrop = [](const QString& k) {
        return k.compare("mainA", Qt::CaseInsensitive) == 0
            || k.compare("mainB", Qt::CaseInsensitive) == 0
            || k.compare("drop", Qt::CaseInsensitive) == 0; };
    QJsonArray rows, issues;
    QString buildName; double buildRms = 0.0; bool haveBuild = false;
    for (const auto& v : secs)
    {
        const auto o = v.toObject();
        const QString name = o.value("name").toString();
        const QString kind = planKinds.value(name).toString();
        const double rms = o.value("rms").toDouble();
        if (isBuild(kind)) { buildName = name; buildRms = rms; haveBuild = true; continue; }
        if (!isDrop(kind)) continue;
        if (!haveBuild || buildRms <= 0.0) continue;
        const double r = rms / buildRms;
        rows.append(QJsonObject{ { "drop", name }, { "dropRms", rms },
                                 { "build", buildName }, { "buildRms", buildRms },
                                 { "ratio", r }, { "ok", r >= ratio } });
        if (r < ratio)
            issues.append(QString("%1 is %2x its build %3 (build rms %4, drop rms %5) - thin the build cells or lift the drop layers")
                              .arg(name).arg(QString::number(r, 'f', 2)).arg(buildName)
                              .arg(QString::number(buildRms, 'f', 3)).arg(QString::number(rms, 'f', 3)));
    }
    if (rows.isEmpty()) return;
    root["loudnessGates"] = QJsonObject{ { "ratioThreshold", ratio },
                                         { "ok", issues.isEmpty() },
                                         { "dropVsBuild", rows },
                                         { "issues", issues } };
}

} // namespace HDAW
