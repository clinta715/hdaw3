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
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>

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
}

} // namespace mcp
