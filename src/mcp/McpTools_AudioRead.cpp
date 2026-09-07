#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
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
#include <algorithm>
#include <optional>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

namespace mcp {

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
        "Return downsampled min/max peak pairs for an audio clip waveform.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"numBins", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            int cid = a.value("clipId").toInt(-1);
            auto clip = findClip(e, cid, nullptr);
            if (!clip.isValid())
                return McpToolResult::text(QString("clipId %1 not found").arg(cid), true);
            if (clip.getProperty(IDs::clipType).toString() != juce::String("audio"))
                return McpToolResult::text("not an audio clip", true);

            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                return McpToolResult::text("no source file", true);

            auto file = juce::File(sourceFile);
            if (!file.existsAsFile())
                return McpToolResult::text("source file missing", true);

            auto& fmtMgr = e->getProjectPool().getFormatManager();
            std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
            if (!reader)
                return McpToolResult::text("cannot open audio file", true);

            auto totalSamples = reader->lengthInSamples;
            if (totalSamples <= 0)
                return McpToolResult::text("empty audio", true);

            int numChannels = static_cast<int>(reader->numChannels);
            double sampleRate = reader->sampleRate;
            int numBins = a.value("numBins").toInt(1000);
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

            QJsonObject result{{"peaks", peaks},
                               {"sampleRate", sampleRate},
                               {"numSamples", static_cast<qint64>(totalSamples)}};
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
        "peak/RMS, 4-band energies, per-section RMS/peak/band details, pump depth and "
        "kick prominence. Section times are SECONDS; windows are [start,end). Band "
        "cutoffs (Hz): sub 40-110, bass 90-300, body 300-2000, high >6000. "
        "kickProminence = E(35-110) / (E(35-110) + E(120-320)), range 0..1 (0 when "
        "either region is silent). pumpDepth = (max-min)/mean of per-beat RMS "
        "(beat = 60/bpm seconds), averaged over sections with >= 8 beats; omitted "
        "when bpm <= 0 or no section qualifies. If sections is omitted, a single "
        "'whole' window [0, duration) is analyzed. bandEnergy is mean power per FFT "
        "window (linear amplitude^2, not dB).",
        objSchema({
            {"filePath", QJsonObject{{"type","string"}}},
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
        }, {"filePath"}),
        "audio",
        [](const QJsonObject& a) -> McpToolResult {
            const juce::File file(a.value("filePath").toString().toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + a.value("filePath").toString(), true);

            const double bpm = a.value("bpm").toDouble(0.0);
            if (bpm < 0.0)
                return McpToolResult::text("bpm must be >= 0", true);

            std::vector<HDAW::SectionWindow> windows;
            if (a.contains("sections")) {
                for (const auto& v : a.value("sections").toArray()) {
                    const auto o = v.toObject();
                    const double st = o.value("start").toDouble();
                    const double en = o.value("end").toDouble();
                    const QString name = o.value("name").toString();
                    if (!(en > st))
                        return McpToolResult::text(
                            "section '" + name + "' has end <= start", true);
                    windows.push_back(HDAW::SectionWindow{
                        name.toStdString(), st, en});
                }
            }

            HDAW::MixReport rep;
            juce::String err;
            if (!HDAW::MixReportAnalyzer::analyze(file, windows, bpm, rep, err))
                return McpToolResult::text(jstr(err), true);

            QJsonObject root{
                {"duration", rep.duration},
                {"sampleRate", rep.sampleRate},
                {"peak", rep.peak},
                {"rms", rep.rms},
                {"bands", QJsonArray{rep.bands[0], rep.bands[1], rep.bands[2], rep.bands[3]}},
                {"bandLabels", QJsonArray{"sub","bass","body","high"}},
                {"kickProminence", rep.kickProminence}
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
                    {"bandEnergy", QJsonArray{s.bandEnergy[0], s.bandEnergy[1],
                                              s.bandEnergy[2], s.bandEnergy[3]}}
                });
            }
            root["sections"] = sections;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
