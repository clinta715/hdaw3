#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/PsyFmEngine.h"
#include "../engine/PsyFmModMatrix.h"
#include "../engine/PsyFmState.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

void registerPsyFmTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"psy_fm_load_preset",
        "Load a psytrance FM preset routing (growlBass, acidLead, metallicPluck, riser) into a psy_fm FX slot. "
        "Sets algorithm, modulation matrix, and default ratios/feedback/envelopes.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}},
            {"preset",   QJsonObject{{"type","string"},
                {"enum", QJsonArray{"growlBass","acidLead","metallicPluck","riser"}}}}
        }, {"trackId","slotIndex","preset"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QString preset = a.value("preset").toString();
            bool ok = e->getAudioEngineCommands().setFxSlotPsyFmPreset(ti, si, preset.toStdString());
            return ok ? McpToolResult::text("loaded preset: " + preset)
                      : McpToolResult::text("unknown preset: " + preset, true);
        }});

s.registerTool({"psy_fm_get_analysis",
        "Get the current analysis data from a psy_fm FX slot (active voices, per-operator EG levels). "
        "Returns live:false when the audio engine is unavailable (no audio device).",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}}
        }, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QJsonObject state;
            state["live"] = false;
            state["activeVoices"] = 0;
            QJsonArray opLevels;
            for (int op = 0; op < 6; op++)
                opLevels.append(0.0);
            state["opEgLevels"] = opLevels;

            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < (int)chain.size() && chain[si])
                    {
                        auto* engine = chain[si]->psyFmEngine();
                        if (engine)
                        {
                            state["live"] = true;
                            state["activeVoices"] = engine->activeVoiceCount();
                            opLevels = QJsonArray();
                            for (int op = 0; op < 6; op++)
                                opLevels.append(static_cast<double>(engine->getOpEgLevel(op)));
                            state["opEgLevels"] = opLevels;
                        }
                    }
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"psy_fm_set_mod_route",
        "Add or update a modulation route on a psy_fm FX slot's modulation matrix. "
        "Routes are persisted in the project tree and survive save/load/rebuild.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}},
            {"source",    QJsonObject{{"type","string"},
                {"enum", QJsonArray{"ratioSweepLFO","feedbackLFO","modWheel","velocity","barClock"}}}},
            {"dest",      QJsonObject{{"type","string"},
                {"enum", QJsonArray{"op1Ratio","op2Ratio","op3Ratio","op4Ratio","op5Ratio","op6Ratio","op6Feedback"}}}},
            {"depth",     QJsonObject{{"type","number"}}}
        }, {"trackId","slotIndex","source","dest","depth"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            QString srcStr = a.value("source").toString();
            QString destStr = a.value("dest").toString();
            float depth = static_cast<float>(a.value("depth").toDouble());

            e->getAudioEngineCommands().setFxSlotPsyFmModRoute(ti, si,
                srcStr.toStdString(), destStr.toStdString(), depth);
            return McpToolResult::text("ok");
        }});

s.registerTool({"psy_fm_clear_mod_matrix",
        "Clear all modulation routes on a psy_fm FX slot.",
        objSchema({
            {"trackId",  QJsonObject{{"type","integer"}}},
            {"slotIndex", QJsonObject{{"type","integer"}}}
        }, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "psy_fm")
                return McpToolResult::text("slot is not a psy_fm synth", true);

            e->getAudioEngineCommands().clearFxSlotPsyFmModRoutes(ti, si);
            return McpToolResult::text("ok");
        }});

} // namespace registerPsyFmTools

} // namespace mcp
