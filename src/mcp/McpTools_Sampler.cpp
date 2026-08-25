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

void registerSamplerTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"sampler_set_sample",
        "Load an audio file into a sampler FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"filePath",  QJsonObject{{"type","string"}}},
                  {"rootNote",  QJsonObject{{"type","integer"}}}},
                  {"trackId","slotIndex","filePath"}),
        "fx",
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
        "fx",
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
        "fx",
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
        "fx",
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
        "fx",
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
        "fx",
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

}

} // namespace mcp
