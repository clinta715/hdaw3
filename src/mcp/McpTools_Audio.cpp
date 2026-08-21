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

static std::optional<HDAW::EnvelopeGenerator::Shape> parseEnvelopeShape(const QString& s) {
    using S = HDAW::EnvelopeGenerator::Shape;
    if (s == "ramp") return S::Ramp;
    if (s == "adsr") return S::ADSR;
    if (s == "sine") return S::Sine;
    if (s == "triangle") return S::Triangle;
    if (s == "saw") return S::Saw;
    if (s == "square") return S::Square;
    if (s == "pulse") return S::Pulse;
    if (s == "staircase") return S::Staircase;
    if (s == "sCurve") return S::SCurve;
    if (s == "randomWalk") return S::RandomWalk;
    if (s == "noise") return S::Noise;
    return std::nullopt;
}

static void registerAudioReadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
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
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            int takeIndex = a.value("takeIndex").toInt();
            e->getAudioGraphCommands().switchClipTakeToIndex(clipId, takeIndex);
            return McpToolResult::text("ok");
        }});
}

static void registerFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_fx",
        "Add an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser,sampler,fm_synth}, OR a pluginId.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"fxType",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","sampler","fm_synth"}}}},
                  {"pluginId", QJsonObject{{"type","string"}}},
                  {"position", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            if (type.empty() && a.contains("pluginId")) type = "plugin";
            std::string pluginId;
            if (a.contains("pluginId")) pluginId = a.value("pluginId").toString().toStdString();
            int pos = a.value("position").toInt(-1);
            auto fxChain = tl.getChild(ti).getChildWithName(IDs::FX_CHAIN);
            int n = fxChain.isValid() ? fxChain.getNumChildren() : 0;
            int idx = (pos < 0 || pos > n) ? n : pos;
            e->getProjectCommands().addFxSlot(ti, type, pos, pluginId);
            return McpToolResult::text(QString("slot=%1").arg(idx));
        }});

    s.registerTool({"remove_fx", "Remove an FX slot (destructive).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"dryRun",    QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            int s = a.value("slotIndex").toInt();
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove FX slot %1 on track %2").arg(s).arg(ti));
            e->getProjectCommands().removeFxSlot(ti, s);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_plugin_presets",
        "List all preset/program names of a plugin FX slot. Uses the preset cache when available (populated during plugin scanning); falls back to querying the live plugin instance.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            // Try cache first (by pluginId)
            auto* presetInfo = e->getPluginManager().getPresetInfo(juce::String(fxSlots[si].pluginId));
            if (presetInfo && presetInfo->numPrograms > 1)
            {
                QJsonArray arr;
                for (int i = 0; i < presetInfo->numPrograms; ++i)
                {
                    juce::String name = i < presetInfo->programNames.size()
                        ? presetInfo->programNames[i]
                        : juce::String("Preset ") + juce::String(i);
                    arr.append(QJsonObject{{"index", i}, {"name", QString::fromStdString(name.toStdString())}});
                }
                return McpToolResult::text(
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            }

            // Fallback: live query from instantiated plugin
            auto progs = e->getFxProgramList(ti, si);
            QJsonArray arr;
            for (const auto& p : progs)
                arr.append(QJsonObject{{"index", p.index}, {"name", QString::fromStdString(p.name)}});
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"search_plugin_presets",
        "Search for presets across all scanned plugins by name (case-insensitive substring match).",
        objSchema({{"query", QJsonObject{{"type","string"}}},
                   {"limit", QJsonObject{{"type","integer"}}}}, {"query"}),
        [e](const QJsonObject& a) -> McpToolResult {
            QString query = a.value("query").toString().toLower();
            if (query.isEmpty()) return McpToolResult::text("query required", true);
            int limit = a.value("limit").toInt(50);
            if (limit < 1) limit = 1;
            if (limit > 200) limit = 200;
            auto& pm = e->getPluginManager();
            QJsonArray matches;
            for (const auto& pd : pm.getPlugins()) {
                if (static_cast<int>(matches.size()) >= limit) break;
                juce::String pluginId = pd.createIdentifierString();
                auto* presetInfo = pm.getPresetInfo(pluginId);
                if (!presetInfo || presetInfo->numPrograms <= 1) continue;
                QString pluginName = QString::fromStdString(pd.name.toStdString());
                for (int i = 0; i < presetInfo->numPrograms; ++i) {
                    if (static_cast<int>(matches.size()) >= limit) break;
                    juce::String name = i < presetInfo->programNames.size()
                        ? presetInfo->programNames[i]
                        : juce::String("Preset ") + juce::String(i);
                    QString presetName = QString::fromStdString(name.toStdString());
                    if (presetName.toLower().contains(query)) {
                        matches.append(QJsonObject{
                            {"pluginId", QString::fromStdString(pluginId.toStdString())},
                            {"pluginName", pluginName},
                            {"presetIndex", i},
                            {"presetName", presetName}
                        });
                    }
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(matches).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"load_plugin_preset",
        "Load a preset/program by index on a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"programIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex","programIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);
            int pi = a.value("programIndex").toInt();
            e->getPluginParamService().setCurrentProgram(ti, fxSlots[si].pluginId, pi);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_fx_bypass", "Bypass or unbypass an FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().setFxSlotBypassed(ti, si, a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"restart_fx", "Restart a crashed isolated plugin FX slot.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}}}, {"trackIndex","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackIndex").toInt();
            int si = a.value("slotIndex").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            e->getProjectCommands().respawnFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_fx_params", "List all automatable parameters of an FX slot. Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, sampler).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is empty", true);

            QJsonArray arr;
            if (fxSlots[si].fxType == "plugin")
            {
                auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
                for (const auto& pi : params) {
                    QJsonObject o;
                    o["index"] = pi.index;
                    o["name"] = QString::fromStdString(pi.name);
                    o["automatable"] = pi.automatable;
                    o["value"] = static_cast<double>(pi.value);
                    o["text"] = QString::fromStdString(pi.text);
                    arr.append(o);
                }
            }
            else
            {
                // Internal FX: enumerate from param definitions
                auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
                for (const auto& def : defs) {
                    QJsonObject o;
                    o["index"] = def.index;
                    o["name"] = QString::fromUtf8(def.name.toRawUTF8());
                    o["automatable"] = true;
                    o["minValue"] = static_cast<double>(def.minValue);
                    o["maxValue"] = static_cast<double>(def.maxValue);
                    o["defaultValue"] = static_cast<double>(def.defaultValue);
                    arr.append(o);
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"params", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_fx_param", "Set an FX parameter value (normalized 0..1). Works for both plugin and internal FX (eq, compressor, reverb, delay, chorus, flanger, phaser, sampler).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is empty", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            v = std::clamp(v, 0.0f, 1.0f);

            if (fxSlots[si].fxType == "plugin")
            {
                auto params = e->getPluginParamService().getParams(ti, fxSlots[si].pluginId);
                if (pi < 0 || pi >= static_cast<int>(params.size()))
                    return McpToolResult::text("param index out of range", true);
                e->getPluginParamService().setParam(ti, fxSlots[si].pluginId, pi, v);
            }
            else
            {
                // Internal FX: route through the command layer which sets the
                // ValueTree property, triggering the listener to apply to DSP.
                // The ValueTree stores real values, so denormalize first.
                auto defs = HDAW::TrackFXSlot::getParamDefsForType(fxSlots[si].fxType);
                if (pi < 0 || pi >= static_cast<int>(defs.size()))
                    return McpToolResult::text("param index out of range", true);
                float realValue = defs[static_cast<size_t>(pi)].minValue
                    + v * (defs[static_cast<size_t>(pi)].maxValue - defs[static_cast<size_t>(pi)].minValue);
                e->getProjectCommands().setFxSlotParam(ti, si, pi, realValue);
            }
            return McpToolResult::text("ok");
        }});

    s.registerTool({"load_plugin_preset_file",
        "Load a preset file into a plugin FX slot via setStateInformation. "
        "Supports .fxp (standard VST2 FPCh, Serum 2 layout) and .syx (DX7 SysEx) files.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}}},
                  {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot has no plugin instance", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File fxpFile(filePath.toStdString());
            if (!fxpFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!fxpFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto* bytes = static_cast<const uint8_t*>(raw.getData());

            // SysEx file (DX7 format: starts with F0 43)
            if (bytes[0] == 0xF0 && bytes[1] == 0x43) {
                slot->getPluginInstance()->setStateInformation(raw.getData(), static_cast<int>(raw.getSize()));

                auto& model = e->getProjectModel();
                auto& um = model.getUndoManager();
                auto slotTree = model.getTrackListTree().getChild(ti)
                    .getChildWithName(IDs::FX_CHAIN).getChild(si);
                if (slotTree.isValid()) {
                    slotTree.setProperty(IDs::pluginState, raw.toBase64Encoding(), &um);
                }
                return McpToolResult::text("ok");
            }

            // FXP file (starts with CcnK)
            if (raw.getSize() < 60)
                return McpToolResult::text("file too small for FXP header", true);

            uint32_t chunkMagic = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            uint32_t fxMagic    = (bytes[8] << 24) | (bytes[9] << 16) | (bytes[10] << 8) | bytes[11];

            if (chunkMagic != 0x43636e4b)
                return McpToolResult::text("not a valid FXP or SysEx file", true);
            if (fxMagic != 0x46504368)
                return McpToolResult::text("not a chunk-based FXP (bad FPCh magic)", true);

            const uint8_t* chunkData = nullptr;
            size_t chunkSize = 0;

            auto readBE32 = [](const uint8_t* p) -> uint32_t {
                return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            };

            if (raw.getSize() >= 60) {
                uint32_t cs2 = readBE32(bytes + 56);
                if (cs2 > 0 && 60 + cs2 == raw.getSize()) {
                    chunkData = bytes + 60;
                    chunkSize = cs2;
                }
            }

            if (!chunkData && raw.getSize() >= 56) {
                uint32_t cs1 = readBE32(bytes + 52);
                if (cs1 > 0 && 56 + cs1 == raw.getSize()) {
                    chunkData = bytes + 56;
                    chunkSize = cs1;
                }
            }

            if (!chunkData || chunkSize == 0)
                return McpToolResult::text("could not locate chunk data in FXP file", true);

            slot->getPluginInstance()->setStateInformation(chunkData, static_cast<int>(chunkSize));

            auto& model = e->getProjectModel();
            auto& um = model.getUndoManager();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (slotTree.isValid()) {
                juce::MemoryBlock stateBlock(chunkData, chunkSize);
                slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
            }

            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_internal_fx_param",
        "Set an internal (non-plugin) FX parameter value. Works for eq, compressor, reverb, delay, chorus, flanger, phaser, and sampler.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType == "plugin" || fxSlots[si].fxType == "none")
                return McpToolResult::text("slot is not an internal FX", true);
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"sampler_set_sample",
        "Load an audio file into a sampler FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"filePath",  QJsonObject{{"type","string"}}},
                  {"rootNote",  QJsonObject{{"type","integer"}}}},
                  {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);
            juce::File file(filePath.toStdString());
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            int root = a.value("rootNote").toInt(60);
            e->getProjectCommands().setSamplerSample(ti, si, filePath.toStdString(), root);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"sampler_get_state",
        "Get the current state of a sampler FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                  {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);

            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (!slotTree.isValid())
                return McpToolResult::text("slot tree not found", true);

            QJsonObject state;
            state["sampleFile"] = QString::fromStdString(slotTree.getProperty("sampleFile", "").toString().toStdString());
            state["mode"] = QString::fromStdString(slotTree.getProperty("mode", "classic").toString().toStdString());
            state["rootNote"] = static_cast<int>(slotTree.getProperty("rootNote", 60));
            state["transpose"] = static_cast<int>(slotTree.getProperty("transpose", 0));
            state["mono"] = static_cast<bool>(slotTree.getProperty("mono", false));
            state["playReverse"] = static_cast<bool>(slotTree.getProperty("playReverse", false));

            QJsonObject env;
            env["attack"] = static_cast<double>(slotTree.getProperty("param_0", 0.005));
            env["decay"] = static_cast<double>(slotTree.getProperty("param_1", 0.1));
            env["sustain"] = static_cast<double>(slotTree.getProperty("param_2", 0.9));
            env["release"] = static_cast<double>(slotTree.getProperty("param_3", 0.1));
            state["envelope"] = env;

            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < static_cast<int>(chain.size()) && chain[si])
                    {
                        auto* engine = chain[si]->samplerEngineForTest();
                        state["hasSound"] = (engine != nullptr && engine->currentSound() != nullptr);
                        if (engine)
                            state["activeVoices"] = engine->activeVoiceCount();
                    }
                }
            }

            state["sliceMode"] = QString::fromStdString(slotTree.getProperty("sliceMode", "transient").toString().toStdString());
            state["sliceGrid"] = static_cast<double>(slotTree.getProperty("sliceGrid", 0.25));
            state["sliceSensitivity"] = static_cast<double>(slotTree.getProperty("sliceSensitivity", 0.5));
            QJsonArray slicePoints;
            juce::StringArray sliceTokens = juce::StringArray::fromTokens(
                slotTree.getProperty("slicePoints", "").toString(), ",", "");
            for (const auto& tok : sliceTokens)
                slicePoints.append(tok.trim().getDoubleValue());
            state["slicePoints"] = slicePoints;

            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_sampler_param",
        "Set a sampler FX slot parameter. Either a named slot property ({property, value}: mono, playReverse, transpose, baseNote) or a real parameter value by paramIndex.",
        objSchema({{"trackId",    QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}},
                  {"paramIndex", QJsonObject{{"type","integer"}}},
                  {"property",   QJsonObject{{"type","string"}}},
                  {"value",      QJsonObject{{"type","number"}}}},
                  {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            if (a.contains("property"))
            {
                QString prop = a.value("property").toString();
                e->getAudioEngineCommands().setSamplerProperty(
                    ti, si, prop.toStdString(), a.value("value").toBool(false));
                return McpToolResult::text(QJsonDocument(QJsonObject{{"ok", true}})
                    .toJson(QJsonDocument::Compact));
            }
            int pi = a.value("paramIndex").toInt();
            float v = static_cast<float>(a.value("value").toDouble());
            e->getProjectCommands().setFxSlotParam(ti, si, pi, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_sampler_mode",
        "Set a sampler FX slot's playback mode. mode in {classic, one-shot, slice}.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"mode",      QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"classic","one-shot","slice"}}}}},
                  {"trackId","slotIndex","mode"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            QString mode = a.value("mode").toString();
            e->getAudioEngineCommands().setSamplerMode(ti, si, mode.toStdString());
            return McpToolResult::text(QJsonDocument(QJsonObject{{"ok", true}})
                .toJson(QJsonDocument::Compact));
        }});

    s.registerTool({"detect_sampler_slices",
        "Detect slice points for a sampler FX slot (transient or grid mode) and store them. Returns {ok, totalSlices, slicePoints} (normalized 0..1).",
        objSchema({{"trackId",          QJsonObject{{"type","integer"}}},
                  {"slotIndex",        QJsonObject{{"type","integer"}}},
                  {"sliceMode",        QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"transient","grid"}}}},
                  {"sliceGrid",        QJsonObject{{"type","number"}}},
                  {"sliceSensitivity", QJsonObject{{"type","number"}}}},
                  {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            std::string sm = a.value("sliceMode").toString("transient").toStdString();
            double grid = a.value("sliceGrid").toDouble(0.25);
            double sens = a.value("sliceSensitivity").toDouble(0.5);
            auto r = e->getAudioEngineCommands().detectSamplerSlices(ti, si, sm, grid, sens);
            QJsonArray pts;
            for (float p : r.slicePoints)
                pts.append(static_cast<double>(p));
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"ok", r.ok},
                            {"totalSlices", r.totalSlices},
                            {"slicePoints", pts}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"trigger_sampler_slice",
        "Audition one slice of a sampler FX slot in slice mode. Returns {ok, totalSlices}.",
        objSchema({{"trackId",    QJsonObject{{"type","integer"}}},
                  {"slotIndex",  QJsonObject{{"type","integer"}}},
                  {"sliceIndex", QJsonObject{{"type","integer"}}},
                  {"velocity",   QJsonObject{{"type","number"}}}},
                  {"trackId","slotIndex","sliceIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "sampler")
                return McpToolResult::text("slot is not a sampler", true);
            int idx = a.value("sliceIndex").toInt();
            float vel = static_cast<float>(a.value("velocity").toDouble(0.8));
            auto r = e->getAudioEngineCommands().triggerSamplerSlice(ti, si, idx, vel);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"ok", r.ok},
                            {"totalSlices", r.totalSlices}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"fm_synth_load_preset",
        "Load a raw DX7 patch (156 bytes hex string) into an FM synth FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"patchData", QJsonObject{{"type","string"}}}}, {"trackId","slotIndex","patchData"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QString hex = a.value("patchData").toString();
            if (hex.isEmpty() || hex.size() != 312)
                return McpToolResult::text("patchData must be 312 hex characters (156 bytes)", true);

            uint8_t patch[156];
            for (int i = 0; i < 156; i++)
            {
                bool ok;
                patch[i] = static_cast<uint8_t>(hex.mid(i * 2, 2).toInt(&ok, 16));
                if (!ok) return McpToolResult::text("invalid hex at offset " + QString::number(i), true);
            }

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si >= (int)chain.size() || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->fmSynthEngine())
                return McpToolResult::text("FM synth engine not initialized", true);

            slot->fmSynthEngine()->loadPatch(patch);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"fm_synth_import_sysex",
        "Import a DX7 .syx file into an FM synth FX slot. Supports single voice dumps "
        "(163 bytes) and 32-voice cartridge dumps (4104 bytes). For cartridges, loads "
        "voice index 0 (first voice). Returns the voice name if available.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File syxFile(filePath.toStdString());
            if (!syxFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!syxFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto* bytes = static_cast<const uint8_t*>(raw.getData());
            size_t fileSize = raw.getSize();

            std::optional<HDAW::Dx7Voice> voice;
            std::vector<HDAW::Dx7Voice> voices;
            int resolvedVoiceIndex = 0;

            if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
                voice = HDAW::parseSingleVoiceSysex(bytes, fileSize);
            } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
                voices = HDAW::parseCartridgeSysex(bytes, fileSize);
                int vi = a.value("voiceIndex").toInt(0);
                if (vi >= 0 && vi < (int)voices.size()) {
                    voice = voices[vi];
                    resolvedVoiceIndex = vi;
                }
            } else {
                return McpToolResult::text(
                    "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)", true);
            }

            if (!voice.has_value())
                return McpToolResult::text("failed to parse SysEx data (bad checksum or size)", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si >= (int)chain.size() || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->fmSynthEngine())
                return McpToolResult::text("FM synth engine not initialized", true);

            slot->fmSynthEngine()->loadPatch(voice->patchData.data());

            QJsonObject result;
            result["ok"] = true;
            result["voiceName"] = QString::fromStdString(voice->voiceName);
            result["algorithm"] = voice->algorithm;
            result["feedback"] = voice->feedback;
            if (!voices.empty()) {
                result["totalVoices"] = static_cast<int>(voices.size());
                QJsonArray voicesArr;
                for (int i = 0; i < (int)voices.size(); ++i) {
                    QJsonObject v;
                    v["index"] = i;
                    v["name"] = QString::fromStdString(voices[i].voiceName);
                    v["algorithm"] = voices[i].algorithm;
                    voicesArr.append(v);
                }
                result["voices"] = voicesArr;
                result["voiceIndex"] = resolvedVoiceIndex;
            }

            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"fm_synth_get_state",
        "Get the current state of an FM synth FX slot (active voices, algorithm).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QJsonObject state;
            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < (int)chain.size() && chain[si])
                    {
                        auto* engine = chain[si]->fmSynthEngine();
                        if (engine)
                            state["activeVoices"] = engine->activeVoiceCount();
                    }
                }
            }
            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            state["algorithm"] = slotTree.isValid()
                ? static_cast<int>(slotTree.getProperty("param_0", 0)) : 0;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});
}

static void registerAutomationTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_automation_point", "Add a point to an automation lane (paramID integer preferred; name accepted).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"time",   QJsonObject{{"type","number"}}},
                  {"value",  QJsonObject{{"type","number"}}}}, {"trackId","lane","time","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(e, a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            auto pl = lane.getChildWithName(IDs::POINT_LIST);
            if (!pl.isValid()) { pl = juce::ValueTree(IDs::POINT_LIST); lane.addChild(pl, -1, &um); }
            juce::ValueTree pt(IDs::POINT);
            // MCP boundary speaks beats; the ValueTree stores seconds.
            double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
            pt.setProperty(IDs::startTime, HDAW::beatsToSeconds(a.value("time").toDouble(), bpm), &um);
            pt.setProperty(IDs::gain, a.value("value").toDouble(), &um);
            pl.addChild(pt, -1, &um);
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

    {
        QJsonObject pointProps{{"time", QJsonObject{{"type","number"}}},
                               {"value", QJsonObject{{"type","number"}}}};
        QJsonObject pointItem{{"type","object"}, {"properties", pointProps}};
        QJsonObject pointsSchema{{"type","array"}, {"items", pointItem}};
        QJsonObject laneSchema{{"oneOf", QJsonArray{QJsonObject{{"type","integer"}}, QJsonObject{{"type","string"}}}}};
        QJsonObject modeSchema{{"type","string"}, {"enum", QJsonArray{"replace","append"}}};
        QJsonObject props{{"trackId", QJsonObject{{"type","integer"}}},
                          {"lane", laneSchema},
                          {"points", pointsSchema},
                          {"mode", modeSchema}};
        s.registerTool({"set_automation_points",
            "Set multiple automation points on a lane at once (bulk). Replaces all existing points or appends.",
            objSchema(props, QJsonArray{"trackId","lane","points"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt();
            auto lane = findLane(e, trackId, a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            auto pl = lane.getChildWithName(IDs::POINT_LIST);
            if (!pl.isValid()) { pl = juce::ValueTree(IDs::POINT_LIST); lane.addChild(pl, -1, &um); }
            double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
            QString mode = a.value("mode").toString("replace");
            if (mode == "replace") {
                while (pl.getNumChildren() > 0)
                    pl.removeChild(0, &um);
            }
            auto pointsArray = a.value("points").toArray();
            for (const auto& ptVal : pointsArray) {
                auto pt = ptVal.toObject();
                juce::ValueTree p(IDs::POINT);
                p.setProperty(IDs::startTime, HDAW::beatsToSeconds(pt.value("time").toDouble(), bpm), &um);
                p.setProperty(IDs::gain, pt.value("value").toDouble(), &um);
                pl.addChild(p, -1, &um);
            }
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(trackId);
            return McpToolResult::text(QString("%1 points set").arg(pointsArray.size()));
        }});
    }

    s.registerTool({"set_automation_enabled", "Enable or disable an automation lane.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"enabled",QJsonObject{{"type","boolean"}}}}, {"trackId","lane","enabled"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(e, a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            lane.setProperty(IDs::automationEnabled, a.value("enabled").toBool(),
                             &e->getProjectModel().getUndoManager());
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_fader_authoritative",
        "Disable (or re-enable) ALL Volume automation lanes on a track so the fader is authoritative in playback/export. trackId -1 = every track. Automation points are kept; only the enabled flag toggles. Mirrors project.setFaderAuthoritative (one shared command path).",
        objSchema({{"trackId",        QJsonObject{{"type","integer"}}},
                  {"authoritative",  QJsonObject{{"type","boolean"}}}}, {"trackId","authoritative"}),
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().setFaderAuthoritative(
                a.value("trackId").toInt(-1), a.value("authoritative").toBool());
            return McpToolResult::text("ok");
        }});

    // add_automation_lane / remove_automation_lane â€” the lane-authoring surface.
    // paramID 0 leaves the lane unbound (legacy default); for FX-parameter
    // automation pass the compound id (100 + slotIndex*100 + paramIndex).
    // Mirrors project.addAutomationLane / project.removeAutomationLane so the
    // UI and MCP share one command path (AGENTS.md feature-parity contract).
    s.registerTool({"add_automation_lane", "Create an automation lane, optionally bound to a target paramID (1=volume, 2=pan, 3=mute, or 100+slotIndex*100+paramIndex for a plugin FX param).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"laneName",  QJsonObject{{"type","string"}}},
                  {"paramID",   QJsonObject{{"type","integer"}}}}, {"trackId","laneName"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            QString laneNameQ = a.value("laneName").toString();
            if (laneNameQ.isEmpty()) return McpToolResult::text("laneName required", true);
            int paramID = a.value("paramID").toInt(0);
            e->getProjectCommands().addAutomationLane(
                trackId, laneNameQ.toUtf8().constData(), paramID);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_automation_lane", "Remove an automation lane (by paramID integer, or by name string).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}}}, {"trackId","lane"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto ref = a.value("lane");
            // Resolve the lane by paramID/name, then delete by its name (the
            // command path addresses lanes by name; findLane handles both).
            auto lane = findLane(e, trackId, ref);
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            std::string name = lane.getProperty(IDs::name, "").toString().toStdString();
            e->getProjectCommands().removeAutomationLane(trackId, name);
            return McpToolResult::text("ok");
        }});
}

static void registerSendTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_track_sends", "List all sends on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            if (!e->getMainProcessor()) return McpToolResult::text("engine not ready", true);
            auto sends = e->getReadModel().getTrackSends(ti);
            QJsonArray arr;
            for (const auto& s : sends) {
                arr.append(QJsonObject{
                    {"sendIndex", s.sendIndex},
                    {"level", static_cast<double>(s.level)},
                    {"isPreFader", s.isPreFader},
                    {"bypassed", s.bypassed},
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_track_send_level", "Set the level of a send.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"level", QJsonObject{{"type","number"}}}}, {"trackId","sendIndex","level"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            float lv = static_cast<float>(a.value("level").toDouble());
            e->getProjectCommands().setTrackSendLevel(ti, si, lv);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_mode", "Set send mode: pre or post fader.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"isPreFader", QJsonObject{{"type","boolean"}}}}, {"trackId","sendIndex","isPreFader"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            bool pre = a.value("isPreFader").toBool();
            e->getProjectCommands().setTrackSendMode(ti, si, pre);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_bypassed", "Bypass or unbypass a send.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed", QJsonObject{{"type","boolean"}}}}, {"trackId","sendIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setTrackSendBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});
}

static void registerMidiFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_fx",
        "Add a MIDI FX slot to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"fxType", QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"arpeggiator","velocity","chord","scale","notelength",
                                           "transpose","keyfilter","multinote","velocitycurve",
                                           "notechance","mididelay","humanize","strum"}}}},
                   {"position", QJsonObject{{"type","integer"}}}}, {"trackId","fxType"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            int pos = a.value("position").toInt(-1);
            e->getProjectCommands().addMidiFxSlot(ti, type, pos);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_midi_fx",
        "Remove a MIDI FX slot from a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().removeMidiFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_bypass",
        "Bypass or unbypass a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"bypassed", QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setMidiFxSlotBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_param",
        "Set a parameter on a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"paramName", QJsonObject{{"type","string"}}},
                   {"value", QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramName","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            std::string pn = a.value("paramName").toString().toStdString();
            double v = a.value("value").toDouble();
            e->getProjectCommands().setMidiFxSlotParam(ti, si, pn, v);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_param_normalized",
        "Set a MIDI FX parameter by normalized value (0..1) for real-time modulation. "
        "Bypasses the ValueTree — use set_midi_fx_param for persistent changes.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                   {"slotIndex",   QJsonObject{{"type","integer"}}},
                   {"paramIndex",  QJsonObject{{"type","integer"}}},
                   {"value",       QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}},
                  {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt();
            int slotIndex = a.value("slotIndex").toInt();
            int paramIndex = a.value("paramIndex").toInt();
            float value = static_cast<float>(a.value("value").toDouble());

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("No audio processor", true);
            auto* track = proc->getTrack(trackId);
            if (!track) return McpToolResult::text("Track not found", true);

            auto& chain = track->getMidiFxChain();
            if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size()) || !chain[slotIndex])
                return McpToolResult::text("MIDI FX slot not found", true);

            chain[slotIndex]->setAutomationParam(paramIndex, value);
            return McpToolResult::text("ok");
        }});

    {
        QJsonObject midiFxProps{{"trackId", QJsonObject{{"type","integer"}}},
                                {"slotIndex", QJsonObject{{"type","integer"}}}};
        s.registerTool({"list_midi_fx_params",
            "List all parameters of a MIDI FX slot with their names, ranges, and current values.",
            objSchema(midiFxProps, QJsonArray{"trackId","slotIndex"}),
            [e](const QJsonObject& a) -> McpToolResult {
                int ti = a.value("trackId").toInt();
                int si = a.value("slotIndex").toInt();
                auto allSlots = e->getReadModel().getMidiFxSlots(ti);
                int numSlots = static_cast<int>(allSlots.size());
                if (si < 0 || si >= numSlots)
                    return McpToolResult::text("MIDI FX slot not found", true);
                const MidiFxSlotSnapshot& slot = allSlots[static_cast<size_t>(si)];
                auto defs = HDAW::getMidiFxParamDefs(juce::String(slot.fxType));
                QJsonArray params;
                for (size_t i = 0; i < defs.size(); ++i) {
                    const auto& d = defs[i];
                    QJsonObject p;
                    p["index"] = static_cast<int>(i);
                    p["name"] = QString::fromUtf8(d.name);
                    p["defaultValue"] = d.defaultValue;
                    p["minValue"] = d.minValue;
                    p["maxValue"] = d.maxValue;
                    auto it = slot.params.find(d.name);
                    if (it != slot.params.end()) {
                        if (std::holds_alternative<double>(it->second))
                            p["value"] = std::get<double>(it->second);
                        else
                            p["value"] = d.defaultValue;
                    } else {
                        p["value"] = static_cast<double>(d.defaultValue);
                    }
                    params.append(p);
                }
                QJsonObject result;
                result["fxType"] = QString::fromStdString(slot.fxType);
                result["bypassed"] = slot.bypassed;
                result["params"] = params;
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
            }});
    }
}

static void registerEnvelopeTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_envelope_shapes",
        "List all available envelope generator shapes.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            auto addShape = [&](const char* name, const char* desc) {
                arr.append(QJsonObject{{"name", name}, {"description", desc}});
            };
            addShape("ramp", "Linear ramp between two values");
            addShape("adsr", "Attack-Decay-Sustain-Release envelope");
            addShape("sine", "Sine wave LFO");
            addShape("triangle", "Triangle wave LFO");
            addShape("saw", "Sawtooth wave LFO");
            addShape("square", "Square wave LFO");
            addShape("pulse", "Pulse wave (50% duty)");
            addShape("staircase", "Stepped quantization");
            addShape("sCurve", "S-curve (smooth step)");
            addShape("randomWalk", "Seeded random walk");
            addShape("noise", "Seeded uniform noise");
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"shapes", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"generate_automation_envelope",
        "Generate an envelope shape on an automation lane.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                   {"lane",     QJsonObject{{"oneOf", QJsonArray{
                       QJsonObject{{"type","integer"}},
                       QJsonObject{{"type","string"}}}}}},
                   {"shape",    QJsonObject{{"type","string"}}},
                   {"start",    QJsonObject{{"type","number"}}},
                   {"end",      QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue", QJsonObject{{"type","number"}}},
                   {"cycles",   QJsonObject{{"type","number"}}},
                   {"steps",    QJsonObject{{"type","integer"}}},
                   {"phase",    QJsonObject{{"type","number"}}},
                   {"density",  QJsonObject{{"type","number"}}},
                   {"smooth",   QJsonObject{{"type","number"}}},
                   {"seed",     QJsonObject{{"type","integer"}}}},
                  {"trackId","lane","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto laneRef = a.value("lane");
            auto lane = findLane(e, trackId, laneRef);
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);
            std::string laneName = lane.getProperty(IDs::name, "").toString().toStdString();

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(1.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateAutomationEnvelope(trackId, laneName, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_gain_envelope",
        "Generate an envelope shape on a clip's gain envelope.",
        objSchema({{"clipId",     QJsonObject{{"type","integer"}}},
                   {"shape",      QJsonObject{{"type","string"}}},
                   {"start",      QJsonObject{{"type","number"}}},
                   {"end",        QJsonObject{{"type","number"}}},
                   {"startValue", QJsonObject{{"type","number"}}},
                   {"endValue",   QJsonObject{{"type","number"}}},
                   {"cycles",     QJsonObject{{"type","number"}}},
                   {"steps",      QJsonObject{{"type","integer"}}},
                   {"phase",      QJsonObject{{"type","number"}}},
                   {"density",    QJsonObject{{"type","number"}}},
                   {"smooth",     QJsonObject{{"type","number"}}},
                   {"seed",       QJsonObject{{"type","integer"}}}},
                  {"clipId","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(2.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipGainEnvelope(clipId, params);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"generate_clip_cc_lane",
        "Generate an envelope shape on a MIDI clip's CC lane.",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                   {"controllerNumber", QJsonObject{{"type","integer"}}},
                   {"shape",            QJsonObject{{"type","string"}}},
                   {"start",            QJsonObject{{"type","number"}}},
                   {"end",              QJsonObject{{"type","number"}}},
                   {"startValue",       QJsonObject{{"type","number"}}},
                   {"endValue",         QJsonObject{{"type","number"}}},
                   {"cycles",           QJsonObject{{"type","number"}}},
                   {"steps",            QJsonObject{{"type","integer"}}},
                   {"phase",            QJsonObject{{"type","number"}}},
                   {"density",          QJsonObject{{"type","number"}}},
                   {"smooth",           QJsonObject{{"type","number"}}},
                   {"seed",             QJsonObject{{"type","integer"}}}},
                  {"clipId","controllerNumber","shape"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt(-1);
            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid()) return McpToolResult::text("clip not found", true);
            int controllerNumber = a.value("controllerNumber").toInt(-1);
            if (controllerNumber < 0 || controllerNumber > 127)
                return McpToolResult::text("controllerNumber must be 0-127", true);
            QString shapeStr = a.value("shape").toString();
            auto shape = parseEnvelopeShape(shapeStr);
            if (!shape) return McpToolResult::text("unknown shape: " + shapeStr, true);

            HDAW::EnvelopeGenerator::Params params;
            params.shape = *shape;
            params.startTime = a.value("start").toDouble(0.0);
            params.endTime = a.value("end").toDouble(16.0);
            params.startValue = a.value("startValue").toDouble(0.0);
            params.endValue = a.value("endValue").toDouble(127.0);
            params.cycles = a.value("cycles").toDouble(1.0);
            params.steps = a.value("steps").toInt(8);
            params.phase = a.value("phase").toDouble(0.0);
            params.densityPerSec = a.value("density").toDouble(8.0);
            params.smooth = a.value("smooth").toDouble(0.0);
            params.seed = static_cast<uint64_t>(a.value("seed").toInt(0));

            e->getProjectCommands().generateClipCcLane(clipId, controllerNumber, params);
            return McpToolResult::text("ok");
        }});
}

void registerAudioDomain(McpServer& s, AudioEngine* e)
{
    registerAudioReadTools(s, e);
    registerFxTools(s, e);
    registerMidiFxTools(s, e);
    registerAutomationTools(s, e);
    registerSendTools(s, e);
    registerEnvelopeTools(s, e);
}

} // namespace mcp
