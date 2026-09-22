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
            || k.compare("finale", Qt::CaseInsensitive) == 0
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
