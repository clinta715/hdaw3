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

void registerMidiFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_fx",
        "Add a MIDI FX slot to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"fxType", QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"arpeggiator","velocity","chord","scale","notelength",
                                           "transpose","keyfilter","multinote","velocitycurve",
                                           "notechance","mididelay","humanize","strum"}}}},
                   {"position", QJsonObject{{"type","integer"}}}}, {"trackId","fxType"}),
        "midi-fx",
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
        "midi-fx",
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
        "midi-fx",
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
        "midi-fx",
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
        "midi-fx",
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
            "midi-fx",
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

} // namespace mcp
