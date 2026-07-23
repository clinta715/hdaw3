#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/ProjectPool.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>

namespace mcp {

static void registerAudioReadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("engine not ready", true);
            auto* tr = proc->getTrack(ti);
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            QJsonArray arr;
            for (int i = 0; i < static_cast<int>(chain.size()); ++i) {
                auto& s2 = chain[i]; if (!s2) continue;
                QJsonObject o{{"slot", i},{"type", jstr(s2->getType())}};
                if (s2->isPlugin()) {
                    o["pluginId"] = jstr(s2->getPluginID());
                    o["paramCount"] = static_cast<int>(s2->getAutomatableParams().size());
                }
                o["bypassed"] = s2->isBypassed();
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
}

static void registerFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_fx",
        "Add an FX slot. fxType in {eq,compressor,reverb,delay}, OR a pluginId.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"fxType",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"eq","compressor","reverb","delay"}}}},
                  {"pluginId", QJsonObject{{"type","string"}}},
                  {"position", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* rm = e->getMainProcessor()->getRoutingManager();
            if (!rm) return McpToolResult::text("routing manager not ready", true);
            auto* tr = rm->getTrackNode(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            if (type.empty() && a.contains("pluginId")) type = "plugin";
            int pos = a.value("position").toInt(-1);
            int idx = tr->addFXSlotAt(type, pos);
            if (a.contains("pluginId"))
                tr->setFXSlotPluginID(idx, a.value("pluginId").toString().toStdString());
            return McpToolResult::text(QString("slot=%1").arg(idx));
        }});

    s.registerTool({"remove_fx", "Remove an FX slot (destructive).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"dryRun",    QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto* tr = e->getMainProcessor()->getTrack(ti);
            if (!tr) return McpToolResult::text("track not found", true);
            int s = a.value("slotIndex").toInt();
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove FX slot %1 on track %2").arg(s).arg(ti));
            tr->removeFXSlot(s);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_plugin_presets",
        "List all preset/program names of a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            int si = a.value("slotIndex").toInt();
            if (si < 0 || si >= (int)chain.size())
                return McpToolResult::text("slot not found", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin())
                return McpToolResult::text("slot is not a plugin", true);
            int num = slot->getNumPrograms();
            QJsonArray arr;
            for (int i = 0; i < num; ++i)
                arr.append(QJsonObject{{"index", i}, {"name", jstr(slot->getProgramName(i))}});
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"load_plugin_preset",
        "Load a preset/program by index on a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"programIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex","programIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            int si = a.value("slotIndex").toInt();
            if (si < 0 || si >= (int)chain.size())
                return McpToolResult::text("slot not found", true);
            int pi = a.value("programIndex").toInt();
            chain[si]->setCurrentProgram(pi);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_fx_bypass", "Bypass or unbypass an FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            tr->setFXBypassed(a.value("slotIndex").toInt(), a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_fx_params", "List all automatable parameters of a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            int si = a.value("slotIndex").toInt();
            if (si < 0 || si >= (int)chain.size())
                return McpToolResult::text("slot not found", true);
            auto& slot = chain[si];
            if (!slot || !slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot has no plugin", true);
            auto& params = slot->getAutomatableParams();
            auto* plugin = slot->getPluginInstance();
            QJsonArray arr;
            for (const auto& pi : params) {
                QJsonObject o;
                o["index"] = pi.index;
                o["name"] = jstr(pi.name);
                o["automatable"] = pi.automatable;
                if (pi.index >= 0 && pi.index < plugin->getParameters().size()) {
                    auto* p = plugin->getParameters()[pi.index];
                    o["value"] = static_cast<double>(p->getValue());
                    o["defaultValue"] = static_cast<double>(p->getDefaultValue());
                    o["text"] = jstr(p->getText(p->getValue(), 128));
                }
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{{"params", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_fx_param", "Set a plugin FX parameter value (normalized 0..1).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"paramIndex",QJsonObject{{"type","integer"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramIndex","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            int si = a.value("slotIndex").toInt();
            if (si < 0 || si >= (int)chain.size())
                return McpToolResult::text("slot not found", true);
            auto& slot = chain[si];
            if (!slot || !slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot has no plugin", true);
            int pi = a.value("paramIndex").toInt();
            auto& params = slot->getPluginInstance()->getParameters();
            if (pi < 0 || pi >= params.size())
                return McpToolResult::text("param index out of range", true);
            float v = static_cast<float>(a.value("value").toDouble());
            v = std::clamp(v, 0.0f, 1.0f);
            params[pi]->setValue(v);
            slot->setAutomationParam(pi, v);
            return McpToolResult::text("ok");
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
            pt.setProperty(IDs::startTime, a.value("time").toDouble(), &um);
            pt.setProperty(IDs::gain, a.value("value").toDouble(), &um);
            pl.addChild(pt, -1, &um);
            if (auto* proc = e->getMainProcessor())
                proc->rebuildAutomationCache(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

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
}

void registerAudioDomain(McpServer& s, AudioEngine* e)
{
    registerAudioReadTools(s, e);
    registerFxTools(s, e);
    registerAutomationTools(s, e);
}

} // namespace mcp
