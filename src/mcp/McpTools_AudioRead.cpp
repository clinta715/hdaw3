#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "McpJobs.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ProjectPool.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/Dx7SysexImport.h"
#include "../engine/MidiFx.h"
#include "../engine/MixReport.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/SongStructureAudit.h"
#include "../common/SongPlanView.h"

namespace mcp {

// Drop-vs-build loudness gate (Mix Verifier): a drop section must measure at
// least `ratio` x the RMS of the build that precedes it. Builds are tension
// sections - busy is expected, LOUDER than the payoff is not (the RMS-hotter-
// build case was found by hand on Neon Meridian). Audio-level companion of
// audit_song_structure's dropsAtLeastBuildLoad. No-op unless the plan's section
// kinds are known (fromPlan) and names match the analyzed sections.
static void applyDropVsBuildGate(QJsonObject& root, const QJsonObject& planKinds, double ratio)
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
    root["loudnessGates"] = QJsonObject{ { "ok", issues.isEmpty() },
                                         { "ratioThreshold", ratio },
                                         { "dropVsBuild", rows },
                                         { "issues", issues } };
}

static QJsonObject runMixReportAnalysis(const QString& filePath, double bpm, const QJsonArray& sectionsArg, bool hasSections)
{
    const juce::File file(filePath.toStdString());
    if (!file.existsAsFile())
        throw std::runtime_error(("file not found: " + filePath).toStdString());

    if (bpm < 0.0)
        throw std::runtime_error("bpm must be >= 0");

    std::vector<HDAW::SectionWindow> windows;
    if (hasSections) {
        for (const auto& v : sectionsArg) {
            const auto o = v.toObject();
            const double st = o.value("start").toDouble();
            const double en = o.value("end").toDouble();
            const QString name = o.value("name").toString();
            if (!(en > st))
                throw std::runtime_error(("section '" + name + "' has end <= start").toStdString());
            windows.push_back(HDAW::SectionWindow{
                name.toStdString(), st, en});
        }
    }

    HDAW::MixReport rep;
    juce::String err;
    if (!HDAW::MixReportAnalyzer::analyze(file, windows, bpm, rep, err))
        throw std::runtime_error(jstr(err).toStdString());

    // Windows file-visibility guard: a just-finished export's writer may still
    // hold the file with unflushed data — another handle then reads zeros for
    // the unflushed region (an all-zero measurement for real audio; burned an
    // entire agentic remix session 2026-09-15). When the first pass measures
    // SILENCE on a non-trivial file, wait and re-measure once; a genuinely
    // silent render measures silent twice.
    if (rep.peak <= 0.0f && rep.duration > 0.5)
    {
        juce::Thread::sleep(3000);
        HDAW::MixReport retry;
        if (HDAW::MixReportAnalyzer::analyze(file, windows, bpm, retry, err)
            && retry.peak > 0.0f)
            rep = retry;
    }
    if (rep.peak <= 0.0f && rep.duration > 0.5)
    {
        // Persistently-zero measurement on a known-rendered file: the wedged-
        // instance state (clears on engine restart via the engine_restart tool).
        rep.measurementSuspicious = true;
    }

    QJsonObject root{
        {"duration", rep.duration},
        {"sampleRate", rep.sampleRate},
        {"peak", rep.peak},
        {"rms", rep.rms},
        {"bands", QJsonObject{
            {"sub", rep.bands[0]},
            {"bass", rep.bands[1]},
            {"body", rep.bands[2]},
            {"high", rep.bands[3]}}},
        {"kickProminence", rep.kickProminence},
        // Clipping verdict. HDAW::MixReport carries no clipping member (BlastReport does), so
        // derive it from peak with the SAME threshold the engine's blast classifier documents
        // (`any bin peak >= 0.999`, MixReport.h): the payload used to report peak alone, so an
        // agent had to interpret a float to notice a slamming mix (2026-09-21 dogfood:
        // peak 1.0, no verdict — docs/handoffs/2026-09-21-mcp-dogfood-composition.md).
        {"clipping", rep.peak >= 0.999},
        {"measurementSuspicious", rep.measurementSuspicious}
    };
    if (rep.hasPumpDepth)
        root["pumpDepth"] = rep.pumpDepth;

    QJsonArray sections;
    for (const auto& s : rep.sections) {
        sections.append(QJsonObject{
            {"name", jstr(s.name)},
            {"start", s.start},
            {"end", s.end},
            {"rms", s.rms},
            {"peak", s.peak},
            {"boundaryPeak", s.boundaryPeak},
            {"bandEnergy", QJsonObject{
                {"sub", s.bandEnergy[0]},
                {"bass", s.bandEnergy[1]},
                {"body", s.bandEnergy[2]},
                {"high", s.bandEnergy[3]}}}
        });
    }
    root["sections"] = sections;
    return root;
}

// Shared bin computation for get_waveform_peaks: open the file through the
// project format manager and produce downsampled min/max peak pairs. Both the
// clipId branch and the raw-path branch funnel through here, so the two code
// paths are byte-identical past file resolution. Pure offline file read on
// the MCP thread — no audio-engine or graph state is touched. On failure
// fills *errorOut ("cannot open audio file" / "empty audio") and returns {}.
static QJsonObject computeWaveformPeaks(const juce::File& file, juce::AudioFormatManager& fmtMgr,
                                        int numBins, QString* errorOut = nullptr)
{
    std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
    if (!reader) {
        if (errorOut) *errorOut = "cannot open audio file";
        return {};
    }

    auto totalSamples = reader->lengthInSamples;
    if (totalSamples <= 0) {
        if (errorOut) *errorOut = "empty audio";
        return {};
    }

    int numChannels = static_cast<int>(reader->numChannels);
    double sampleRate = reader->sampleRate;
    numBins = std::clamp(numBins, 100, 10000);
    int64_t samplesPerBin = totalSamples / static_cast<int64_t>(numBins);
    if (samplesPerBin < 1) samplesPerBin = 1;

    juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(samplesPerBin));
    QJsonArray peaks;

    for (int i = 0; i < numBins; ++i) {
        int64_t startSample = static_cast<int64_t>(i) * samplesPerBin;
        int numToRead = static_cast<int>(
            (std::min)(samplesPerBin, totalSamples - startSample));
        if (numToRead <= 0) {
            peaks.append(0.0f);
            peaks.append(0.0f);
            continue;
        }
        buffer.clear();
        reader->read(&buffer, 0, numToRead, startSample, true, true);

        float minVal = 0.0f, maxVal = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* data = buffer.getReadPointer(ch);
            for (int s = 0; s < numToRead; ++s) {
                if (data[s] < minVal) minVal = data[s];
                if (data[s] > maxVal) maxVal = data[s];
            }
        }
        peaks.append(minVal);
        peaks.append(maxVal);
    }

    return QJsonObject{{"peaks", peaks},
                       {"sampleRate", sampleRate},
                       {"numSamples", static_cast<qint64>(totalSamples)}};
}
void registerAudioReadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "audio",
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            QJsonArray arr;
            for (const auto& s2 : fxSlots) {
                bool isPlugin = (s2.fxType == "plugin");
                QJsonObject o{{"slot", s2.slotIndex},
                              {"type", QString::fromStdString(s2.fxType)}};
                if (isPlugin) {
                    o["pluginId"] = QString::fromStdString(s2.pluginId);
                    o["pluginFormat"] = QString::fromStdString(s2.pluginFormat);
                    o["paramCount"] = s2.paramCount;
                }
                o["bypassed"] = s2.bypassed;
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"list_automation_lanes", "List automation lanes on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "audio",
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto al = tl.getChild(ti).getChildWithName(IDs::AUTOMATION_LIST);
            QJsonArray arr;
            for (int i = 0; i < al.getNumChildren(); ++i) {
                auto lane = al.getChild(i);
                arr.append(QJsonObject{
                    {"name", jstr(lane.getProperty(IDs::name).toString())},
                    {"paramID", static_cast<int>(lane.getProperty(IDs::paramID))},
                    {"enabled", static_cast<bool>(lane.getProperty(IDs::automationEnabled))},
                    {"pointCount", lane.getChildWithName(IDs::POINT_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_waveform_peaks",
        "Return downsampled min/max peak pairs for an audio waveform. "
        "Pass clipId to read a project audio clip, or path to read any audio "
        "file on disk (library samples, stems). If both are given, path wins. "
        "Response: {\"peaks\":[min0,max0,min1,max1,...],\"sampleRate\",\"numSamples\"}.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"path", QJsonObject{{"type","string"}}},
                   {"numBins", QJsonObject{{"type","integer"}}}}, {}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString pathArg = a.value("path").toString();
            if (!pathArg.isEmpty()) {
                const juce::File file(pathArg.toStdString());
                if (!file.existsAsFile())
                    return McpToolResult::text("file not found: " + pathArg, true);
                QString error;
                QJsonObject result = computeWaveformPeaks(file,
                                                          e->getProjectPool().getFormatManager(),
                                                          a.value("numBins").toInt(1000), &error);
                if (!error.isEmpty())
                    return McpToolResult::text(error, true);
                result["sourceFile"] = pathArg;
                return McpToolResult::text(QString::fromUtf8(
                    QJsonDocument(result).toJson(QJsonDocument::Compact)));
            }

            int cid = a.value("clipId").toInt(-1);
            auto clip = findClip(e, cid, nullptr);
            if (!clip.isValid())
                return McpToolResult::text(QString("clipId %1 not found").arg(cid), true);
            if (clip.getProperty(IDs::clipType).toString() != juce::String("audio"))
                return McpToolResult::text("not an audio clip", true);

            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                return McpToolResult::text("no source file", true);

            const juce::File file(sourceFile.toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("source file missing", true);

            QString error;
            QJsonObject result = computeWaveformPeaks(file,
                                                      e->getProjectPool().getFormatManager(),
                                                      a.value("numBins").toInt(1000), &error);
            if (!error.isEmpty())
                return McpToolResult::text(error, true);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"pool_list",
        "List all audio files used in the project with usage counts and metadata. "
        "Derived from clips in the arrangement — no persistent pool.",
        objSchema({}, {}),
        "audio",
        [e](const QJsonObject&) {
            auto snapshot = e->getReadModel().snapshot();
            auto& fm = e->getProjectPool().getFormatManager();

            struct PoolEntry { std::string sourceFile, name; int usageCount; double duration; int sampleRate, channels; };
            std::map<std::string, PoolEntry> poolMap;

            for (const auto& clip : snapshot.clips) {
                if (clip.sourceFile.empty()) continue;
                auto it = poolMap.find(clip.sourceFile);
                if (it == poolMap.end()) {
                    PoolEntry entry{clip.sourceFile, clip.name, 1, 0.0, 0, 0};
                    juce::File file(clip.sourceFile);
                    if (file.existsAsFile()) {
                        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
                        if (reader) {
                            entry.duration = reader->lengthInSamples / reader->sampleRate;
                            entry.sampleRate = static_cast<int>(reader->sampleRate);
                            entry.channels = static_cast<int>(reader->numChannels);
                        }
                    }
                    poolMap[clip.sourceFile] = std::move(entry);
                } else {
                    it->second.usageCount++;
                }
            }

            QJsonArray arr;
            for (const auto& [path, entry] : poolMap) {
                (void)path;
                arr.append(QJsonObject{
                    {"sourceFile",  QString::fromStdString(entry.sourceFile)},
                    {"name",        QString::fromStdString(entry.name)},
                    {"usageCount",  entry.usageCount},
                    {"duration",    entry.duration},
                    {"sampleRate",  entry.sampleRate},
                    {"channels",    entry.channels}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"validate_sample",
        "Validate an audio file by opening it with the format manager and reporting basic header / stream properties. Read-only.",
        objSchema({{"path", QJsonObject{{"type","string"}}}}, {"path"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString path = a.value("path").toString();
            const juce::File file(path.toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + path, true);

            auto& fmt = e->getProjectPool().getFormatManager();
            std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
            if (!reader)
                return McpToolResult::text("could not read audio file: " + path, true);

            const double sampleRate = reader->sampleRate;
            const double durationSeconds = sampleRate > 0.0
                ? static_cast<double>(reader->lengthInSamples) / sampleRate
                : 0.0;
            QJsonObject out;
            out["path"] = path;
            out["exists"] = true;
            out["readable"] = true;
            out["sampleRate"] = sampleRate;
            out["channels"] = static_cast<int>(reader->numChannels);
            out["bitsPerSample"] = static_cast<int>(reader->bitsPerSample);
            out["lengthInSamples"] = static_cast<qint64>(reader->lengthInSamples);
            out["durationSeconds"] = durationSeconds;
            out["format"] = jstr(file.getFileExtension().fromFirstOccurrenceOf(".", false, false).toLowerCase());
            out["headerOk"] = true;
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"debug_audio",
        "Read-only audio debug snapshot: transport, meters, track states, and live FX slots.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto snap = e->getReadModel().snapshot();
            const bool filterTrack = a.contains("trackId");
            const int trackFilter = a.value("trackId").toInt(-1);
            if (filterTrack && (trackFilter < 0 || trackFilter >= static_cast<int>(snap.tracks.size())))
                return McpToolResult::text(QString("trackId %1 not found").arg(trackFilter), true);

            auto meterToJson = [](const auto& m) {
                return QJsonObject{
                    {"left", m.leftLevel},
                    {"right", m.rightLevel},
                    {"rmsLeft", m.rmsLeftLevel},
                    {"rmsRight", m.rmsRightLevel},
                    {"lufsMomentary", m.lufsMomentary}
                };
            };

            QJsonObject root;
            root["transport"] = QJsonObject{
                {"bpm", snap.transport.bpm},
                {"isPlaying", snap.transport.isPlaying},
                {"isLooping", snap.transport.isLooping},
                {"isRecording", snap.transport.isRecording},
                {"punchEnabled", snap.transport.punchEnabled},
                {"loopStart", snap.transport.loopStart},
                {"loopEnd", snap.transport.loopEnd},
                {"currentTimeSeconds", snap.transport.currentTimeSeconds},
                {"sampleRate", snap.transport.sampleRate},
                {"timeSigNumerator", snap.transport.timeSigNumerator},
                {"timeSigDenominator", snap.transport.timeSigDenominator}
            };
            root["masterMeter"] = meterToJson(e->getReadModel().getMasterMeter());
            root["trackCount"] = static_cast<int>(snap.tracks.size());
            root["clipCount"] = static_cast<int>(snap.clips.size());
            root["scaleRoot"] = snap.scaleRoot;
            root["scaleMode"] = snap.scaleMode;
            root["masterGain"] = snap.masterGain;

            QJsonArray tracks;
            for (const auto& track : snap.tracks) {
                if (filterTrack && track.index != trackFilter)
                    continue;
                QJsonArray fxSlots;
                for (const auto& fx : e->getReadModel().getFxSlots(track.index)) {
                    fxSlots.append(QJsonObject{
                        {"slotIndex", fx.slotIndex},
                        {"fxType", jstr(fx.fxType)},
                        {"pluginId", jstr(fx.pluginId)},
                        {"pluginName", jstr(fx.pluginName)},
                        {"pluginFormat", jstr(fx.pluginFormat)},
                        {"bypassed", fx.bypassed},
                        {"paramCount", fx.paramCount}
                    });
                }
                tracks.append(QJsonObject{
                    {"index", track.index},
                    {"name", jstr(track.name)},
                    {"color", track.color},
                    {"volume", track.volume},
                    {"pan", track.pan},
                    {"muted", track.muted},
                    {"soloed", track.soloed},
                    {"armed", track.armed},
                    {"inputMonitor", track.inputMonitor},
                    {"trackType", track.trackType},
                    {"isCollapsed", track.isCollapsed},
                    {"isHidden", track.isHidden},
                    {"effectiveMuted", track.effectiveMuted},
                    {"effectiveSoloed", track.effectiveSoloed},
                    {"parentId", track.parentId},
                    {"clipCount", track.clipCount},
                    {"meter", meterToJson(e->getReadModel().getTrackMeter(track.index))},
                    {"fxSlots", fxSlots}
                });
            }
            root["tracks"] = tracks;
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"list_clip_takes",
        "List all takes for an audio clip, showing which is active.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            auto& model = e->getProjectModel();
            auto trackList = model.getTrackListTree();

            for (int t = 0; t < trackList.getNumChildren(); ++t) {
                auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                for (int c = 0; c < clipList.getNumChildren(); ++c) {
                    auto clip = clipList.getChild(c);
                    if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == clipId) {
                        auto takeList = clip.getChildWithName(IDs::TAKE_LIST);
                        int activeIdx = static_cast<int>(clip.getProperty(IDs::activeTake, 0));
                        QJsonArray arr;
                        for (int i = 0; i < takeList.getNumChildren(); ++i) {
                            auto tk = takeList.getChild(i);
                            arr.append(QJsonObject{
                                {"index", i},
                                {"name", jstr(tk.getProperty(IDs::name, "").toString())},
                                {"sourceFile", jstr(tk.getProperty(IDs::sourceFile, "").toString())},
                                {"active", i == activeIdx}
                            });
                        }
                        return McpToolResult::text(QString::fromUtf8(
                            QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                    }
                }
            }
            return McpToolResult::text("clip not found", true);
        }});

    s.registerTool({"switch_clip_take",
        "Switch an audio clip to a specific take index.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"takeIndex", QJsonObject{{"type","integer"}}}}, {"clipId","takeIndex"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            int takeIndex = a.value("takeIndex").toInt();
            e->getAudioGraphCommands().switchClipTakeToIndex(clipId, takeIndex);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"mix_report",
        "Offline mix analysis of a rendered audio file (render verification): overall "
        "peak/RMS, 4-band energies, per-section RMS/peak/boundaryPeak/band details, pump depth and "
        "kick prominence. Section times are SECONDS; windows are [start,end). Band "
        "cutoffs (Hz): sub 40-110, bass 90-300, body 300-2000, high >6000. "
        "kickProminence = E(35-110) / (E(35-110) + E(120-320)), range 0..1 (0 when "
        "either region is silent). pumpDepth = (max-min)/mean of per-beat RMS "
        "(beat = 60/bpm seconds), averaged over sections with >= 8 beats; omitted "
        "when bpm <= 0 or no section qualifies. If sections is omitted, a single "
        "'whole' window [0, duration) is analyzed. bandEnergy is mean power per FFT "
        "window (linear amplitude^2, not dB). Pass fromPlan=true to derive the windows from "
        "the current song plan (section beats converted to seconds via bpm; bpm 0 falls back "
        "to the plan bpm) — callers never do beat math. With fromPlan the response also carries "
        "\"loudnessGates\": each drop section's RMS against the build that precedes it (gate passes "
        "at >= dropBuildRatio, default 0.9) with per-drop rows and issue strings — a build louder "
        "than its payoff is a FAIL (thin the build cells or lift the drop). Optional wait=false returns immediately "
        "with {jobId,state:'running',pollWith:'poll_job'}; poll poll_job for the result.",
        objSchema({
            {"filePath", QJsonObject{{"type","string"}}},
            {"bpm",      QJsonObject{{"type","number"}}},
            {"fromPlan", QJsonObject{{"type","boolean"}}},
            {"wait",     QJsonObject{{"type","boolean"}}},
            {"dropBuildRatio", QJsonObject{{"type","number"}}},
            {"sections", QJsonObject{
                {"type","array"},
                {"items", QJsonObject{
                    {"type","object"},
                    {"properties", QJsonObject{
                        {"name",  QJsonObject{{"type","string"}}},
                        {"start", QJsonObject{{"type","number"}}},
                        {"end",   QJsonObject{{"type","number"}}}}},
                    {"required", QJsonArray{"name","start","end"}}}}}}
        }, {"filePath"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString filePath = a.value("filePath").toString();
            double bpm = a.value("bpm").toDouble(0.0);
            bool hasSections = a.contains("sections");
            QJsonArray sectionsArg = a.value("sections").toArray();
            QJsonObject structureJson;
            bool hasStructure = false;
            QJsonArray clampedJson;
            bool hasClamped = false;
            QJsonObject planKinds;   // section name -> plan kind (for the loudness gate)
            double dropBuildRatio = a.value("dropBuildRatio").toDouble(0.9);
            if (dropBuildRatio <= 0.0 || dropBuildRatio > 1.5) dropBuildRatio = 0.9;
            if (a.value("fromPlan").toBool(false))
            {
                if (!e)
                    return McpToolResult::text("mix_report: engine unavailable", true);
                const auto plan = e->getProjectCommands().getSongPlan();
                if (plan.sections.empty())
                    return McpToolResult::text("mix_report: no song plan set (fromPlan)", true);
                if (bpm <= 0.0) bpm = plan.bpm;
                const double spb = (bpm > 0.0) ? 60.0 / bpm : 0.5;
                // Clamp plan windows to the file's actual duration so a short
                // preview render can be measured with fromPlan without a hard
                // error (fix 2026-09-16: render -> measure loop).
                double dur = 0.0;
                const juce::File pf(filePath.toStdString());
                if (pf.existsAsFile())
                {
                    auto pstream = std::unique_ptr<juce::InputStream>(pf.createInputStream());
                    std::unique_ptr<juce::AudioFormatReader> pread(
                        juce::WavAudioFormat().createReaderFor(pstream.release(), true));
                    if (pread && pread->sampleRate > 0.0 && pread->lengthInSamples > 0)
                        dur = static_cast<double>(pread->lengthInSamples) / pread->sampleRate;
                }
                sectionsArg = QJsonArray();
                for (const auto& s : plan.sections)
                {
                    double st0 = s.startBeat * spb;
                    double en = s.endBeat * spb;
                    if (dur > 0.0)
                    {
                        if (st0 >= dur) continue;               // fully outside -> drop
                        if (en > dur)
                        {
                            clampedJson.append(QString::fromStdString(s.name));
                            hasClamped = true;
                            en = dur;
                        }
                    }
                    sectionsArg.append(QJsonObject{{"name", QString::fromStdString(s.name)},
                                                   {"start", st0},
                                                   {"end", en}});
                    planKinds.insert(QString::fromStdString(s.name),
                                     QString::fromStdString(s.kind));
                }
                if (sectionsArg.isEmpty())
                    return McpToolResult::text(
                        "mix_report: no plan sections fall inside the file duration", true);
                hasSections = true;
                structureJson = HDAW::structureAuditJson(HDAW::auditSongStructure(
                    e->getProjectModel().getTrackListTree(), plan, bpm));
                hasStructure = true;
            }
            const bool wait = a.value("wait").toBool(true);
            if (!wait) {
                const int id = McpJobs::instance().submit("mix_report",
                    [filePath, bpm, sectionsArg, hasSections, structureJson, hasStructure,
                     clampedJson, hasClamped, planKinds, dropBuildRatio]() {
                        QJsonObject root = runMixReportAnalysis(filePath, bpm, sectionsArg, hasSections);
                        if (hasStructure) root["structure"] = structureJson;
                        if (hasClamped) root["clampedSections"] = clampedJson;
                        if (!planKinds.isEmpty()) applyDropVsBuildGate(root, planKinds, dropBuildRatio);
                        return root;
                    });
                QJsonObject payload{{"jobId", id}, {"state", "running"}, {"pollWith", "poll_job"}};
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
            }
            try {
                QJsonObject root = runMixReportAnalysis(filePath, bpm, sectionsArg, hasSections);
                if (hasStructure) root["structure"] = structureJson;
                if (hasClamped) root["clampedSections"] = clampedJson;
                if (!planKinds.isEmpty()) applyDropVsBuildGate(root, planKinds, dropBuildRatio);
                return McpToolResult::text(QString::fromUtf8(
                    QJsonDocument(root).toJson(QJsonDocument::Compact)));
            } catch (const std::exception& ex) {
                return McpToolResult::text(QString::fromUtf8(ex.what()), true);
            }
        }});

    s.registerTool({"mix_diff",
        "Compare two rendered WAVs per-band (render A/B verification): measures both with the mix_report analyzer, then reports rmsDb (rmsA - rmsB in dB), peakRatio (peakA / peakB), per-band energy deltas (linear power, bandA - bandB). Optional sections (seconds, same format as mix_report) add per-section deltas {name,start,end,rmsDb,peakRatio,bandEnergy}. Same band cutoffs as mix_report: sub 40-110, bass 90-300, body 300-2000, high >6000 Hz.",
        objSchema({{"filePathA", QJsonObject{{"type","string"}}},
                   {"filePathB", QJsonObject{{"type","string"}}},
                   {"bpm",      QJsonObject{{"type","number"}}},
                   {"sections", QJsonObject{
                       {"type","array"},
                       {"items", QJsonObject{
                           {"type","object"},
                           {"properties", QJsonObject{
                               {"name",  QJsonObject{{"type","string"}}},
                               {"start", QJsonObject{{"type","number"}}},
                               {"end",   QJsonObject{{"type","number"}}}}},
                           {"required", QJsonArray{"name","start","end"}}}}}}
        }, {"filePathA","filePathB"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString pathA = a.value("filePathA").toString();
            const QString pathB = a.value("filePathB").toString();
            const double bpm = a.value("bpm").toDouble(0.0);
            const bool hasSections = a.contains("sections");
            const QJsonArray sectionsArg = a.value("sections").toArray();
            try {
                const QJsonObject mA = runMixReportAnalysis(pathA, bpm, sectionsArg, hasSections);
                const QJsonObject mB = runMixReportAnalysis(pathB, bpm, sectionsArg, hasSections);
                const auto bandOf = [](const QJsonObject& m, const char* name) {
                    return m.value("bands").toObject().value(name).toDouble();
                };
                const auto rmsDbOf = [](const QJsonObject& m) {
                    const double rms = m.value("rms").toDouble();
                    return rms > 1e-12 ? 20.0 * std::log10(rms) : -240.0;
                };
                const double rmsDbA = rmsDbOf(mA), rmsDbB = rmsDbOf(mB);
                const double peakA = mA.value("peak").toDouble();
                const double peakB = mB.value("peak").toDouble();
                QJsonObject bands;
                bands["sub"]  = bandOf(mA, "sub")  - bandOf(mB, "sub");
                bands["bass"] = bandOf(mA, "bass") - bandOf(mB, "bass");
                bands["body"] = bandOf(mA, "body") - bandOf(mB, "body");
                bands["high"] = bandOf(mA, "high") - bandOf(mB, "high");
                QJsonObject delta;
                delta["rmsDb"] = rmsDbA - rmsDbB;
                delta["peakRatio"] = peakB > 1e-12 ? peakA / peakB : 0.0;
                delta["bands"] = bands;
                QJsonArray sections;
                const auto secsA = mA.value("sections").toArray();
                const auto secsB = mB.value("sections").toArray();
                for (int i = 0; i < secsA.size(); ++i) {
                    const auto sa = secsA.at(i).toObject();
                    const auto sb = i < secsB.size() ? secsB.at(i).toObject() : QJsonObject{};
                    const double rmsAi = sa.value("rms").toDouble();
                    const double rmsBi = sb.value("rms").toDouble();
                    const double peakAi = sa.value("peak").toDouble();
                    const double peakBi = sb.value("peak").toDouble();
                    QJsonObject secBands;
                    secBands["sub"]  = sa.value("bandEnergy").toObject().value("sub").toDouble()
                                     - sb.value("bandEnergy").toObject().value("sub").toDouble();
                    secBands["bass"] = sa.value("bandEnergy").toObject().value("bass").toDouble()
                                     - sb.value("bandEnergy").toObject().value("bass").toDouble();
                    secBands["body"] = sa.value("bandEnergy").toObject().value("body").toDouble()
                                     - sb.value("bandEnergy").toObject().value("body").toDouble();
                    secBands["high"] = sa.value("bandEnergy").toObject().value("high").toDouble()
                                     - sb.value("bandEnergy").toObject().value("high").toDouble();
                    QJsonObject sec;
                    sec["name"] = sa.value("name");
                    sec["start"] = sa.value("start");
                    sec["end"] = sa.value("end");
                    sec["rmsDb"] = (rmsAi > 1e-12 && rmsBi > 1e-12)
                        ? 20.0 * std::log10(rmsAi) - 20.0 * std::log10(rmsBi) : -240.0;
                    sec["peakRatio"] = peakBi > 1e-12 ? peakAi / peakBi : 0.0;
                    sec["bandEnergy"] = secBands;
                    sections.append(sec);
                }
                QJsonObject root;
                root["fileA"] = pathA;
                root["fileB"] = pathB;
                root["delta"] = delta;
                root["sections"] = sections;
                return McpToolResult::text(QString::fromUtf8(
                    QJsonDocument(root).toJson(QJsonDocument::Compact)));
            } catch (const std::exception& ex) {
                return McpToolResult::text(QString::fromUtf8(ex.what()), true);
            }
        }});

    s.registerTool({"diagnose_intro_blast",
        "Diagnose a loud/discordant/noisy INTRO in a rendered audio file (the recurring 'big "
        "loud weird sound at the start' bug class). Windowed trace over the intro: per-bin "
        "peak/RMS/DC/clipping/non-finite; classified into clipping, loudTransient, nonFinite "
        "(NaN/Inf poison), dcOffset, and 'saturation-then-silence' (blast collapses to digital "
        "silence). filePath = rendered master (or a stem). binSeconds default 0.125; "
        "windowSeconds default 8 (the intro span to scan). Long scans cap the emitted bins "
        "(binsSummary: totalBins/emittedBins/truncated/flaggedBins; all flagged bins are kept). "
        "Optional attribution: with maxTracks "
        "> 0 and a live engine, solo-renders each track over the detected blast window "
        "(verifyPart) and reports per-track solo peak/RMS + nonClipping + audible, ranked by "
        "soloRms (top = likely culprit). Read-only; renders tree copies, never mutates.",
        objSchema({ { "filePath", QJsonObject{ { "type", "string" } } },
                    { "windowSeconds", QJsonObject{ { "type", "number" } } },
                    { "binSeconds", QJsonObject{ { "type", "number" } } },
                    { "maxTracks", QJsonObject{ { "type", "integer" } } } },
                  { "filePath" }),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString filePath = a.value("filePath").toString();
            const double windowSeconds = a.value("windowSeconds").toDouble(8.0);
            const double binSeconds = a.value("binSeconds").toDouble(0.125);
            const int maxTracks = a.value("maxTracks").toInt(8);

            HDAW::BlastReport rep;
            juce::String err;
            if (!HDAW::MixReportAnalyzer::analyzeBlast(
                    juce::File(filePath.toStdString()), windowSeconds, binSeconds, rep, err))
                return McpToolResult::text(jstr(err), true);

            QJsonObject root;
            root["ok"] = true;
            root["filePath"] = filePath;
            root["duration"] = rep.duration;
            root["binSeconds"] = rep.binSeconds;
            root["detected"] = rep.detected;
            root["flags"] = QJsonObject{
                { "clipping", rep.clipping },
                { "loudTransient", rep.loudTransient },
                { "nonFinite", rep.nonFinite },
                { "dcOffset", rep.dcOffset },
                { "silenceAfter", rep.silenceAfter } };
            if (rep.loudTransient)
                root["blast"] = QJsonObject{
                    { "start", rep.blastStart }, { "end", rep.blastEnd },
                    { "peak", rep.blastPeak }, { "rms", rep.blastRms },
                    { "preBlastRms", rep.preBlastRms }, { "postBlastRms", rep.postBlastRms } };
            // Cap the emitted bin trace for long scans (300 s @ 0.25 s bins
            // is >1 KB/bin -> tens of KB; a 1200-bin dump tripped the MCP
            // output guard, fix 2026-09-16). Head of the trace plus ALL
            // flagged bins (loud/clipping/non-finite) stay visible; the rest
            // is summarized in binsSummary.
            std::vector<int> flaggedIdx;
            for (int i = 0; i < (int) rep.bins.size(); ++i)
            {
                const auto& b = rep.bins[i];
                if (b.peak >= 0.9 || b.clippingRatio > 0.0 || b.nonFiniteSamples > 0)
                    flaggedIdx.push_back(i);
            }
            const bool binsTruncated = (int) rep.bins.size() > 120;
            QJsonArray bins;
            int emitted = 0;
            auto emitBin = [&bins, &emitted](const HDAW::BlastBin& b) {
                bins.append(QJsonObject{
                    { "start", b.start }, { "end", b.end }, { "peak", b.peak },
                    { "rms", b.rms }, { "mean", b.mean },
                    { "clippingRatio", b.clippingRatio },
                    { "nonFiniteSamples", b.nonFiniteSamples } });
                ++emitted;
            };
            if (!binsTruncated)
            {
                for (const auto& b : rep.bins) emitBin(b);
            }
            else
            {
                for (int i = 0; i < (int) rep.bins.size() && emitted < 96; ++i)
                    emitBin(rep.bins[i]);
                for (const int fi : flaggedIdx)
                {
                    if (emitted >= 200) break;
                    bool dup = false;
                    for (int j = 0; j < emitted; ++j)
                        if (bins[j].toObject().value("start").toDouble()
                            == rep.bins[fi].start) { dup = true; break; }
                    if (!dup) emitBin(rep.bins[fi]);
                }
            }
            root["bins"] = bins;
            root["binsSummary"] = QJsonObject{
                { "totalBins", (int) rep.bins.size() },
                { "emittedBins", emitted },
                { "truncated", binsTruncated },
                { "flaggedBins", (int) flaggedIdx.size() } };

            if (e && rep.detected && maxTracks != 0)
            {
                QJsonArray attribution;
                struct Entry
                {
                    double soloRms = 0.0, soloPeak = 0.0;
                    QString name;
                    int track = -1;
                    bool nonClipping = false, audible = false;
                };
                std::vector<Entry> entries;
                if (rep.loudTransient)
                {
                    const double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 0.0);
                    if (bpm > 0.0)
                    {
                        const double startBeat = rep.blastStart * bpm / 60.0;
                        const double endBeat = rep.blastEnd * bpm / 60.0;
                        const double winSeconds = rep.blastEnd - rep.blastStart;
                        auto trackList = e->getProjectModel().getTrackListTree();
                        int done = 0;
                        for (int t = 0;
                             t < trackList.getNumChildren() && (maxTracks < 0 || done < maxTracks);
                             ++t)
                        {
                            const auto track = trackList.getChild(t);
                            const auto clips = track.getChildWithName(IDs::CLIP_LIST);
                            if (!clips.isValid() || clips.getNumChildren() == 0) continue;
                            auto r = e->getProjectCommands().verifyPart(
                                t, winSeconds, startBeat, endBeat);
                            if (!r.error.empty()) continue;
                            Entry en;
                            en.soloRms = r.soloRms;
                            en.soloPeak = r.soloPeak;
                            en.name = jstr(track.getProperty(IDs::name, "Track").toString());
                            en.track = t;
                            en.nonClipping = r.nonClipping;
                            en.audible = r.audible;
                            entries.push_back(en);
                            ++done;
                        }
                        std::sort(entries.begin(), entries.end(),
                                  [](const Entry& x, const Entry& y) { return x.soloRms > y.soloRms; });
                        bool first = true;
                        for (const auto& en : entries)
                        {
                            QJsonObject o;
                            o["trackId"] = en.track;
                            o["name"] = en.name;
                            o["soloPeak"] = en.soloPeak;
                            o["soloRms"] = en.soloRms;
                            o["nonClipping"] = en.nonClipping;
                            o["audible"] = en.audible;
                            if (first) { o["topContributor"] = true; first = false; }
                            attribution.append(o);
                        }
                    }
                }
                root["attribution"] = attribution;
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
