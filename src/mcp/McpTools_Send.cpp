#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../common/BusInfo.h"
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

void registerSendTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_track_sends", "List all sends on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "send",
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
        "send",
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
        "send",
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
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt(-1);
            int si = a.value("sendIndex").toInt(-1);
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setTrackSendBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});

    // --- Bus / send creation (docs/plans/2026-09-22-bus-send-surface.md, slice B) ---
    // The four mutators above can only shape sends that already exist; these create
    // them. Both surfaces (these tools and the project.* routes in
    // Router_Project.cpp) call the SAME ProjectCommands entry points and shape the
    // SAME payloads, which is what the twin test asserts.

    s.registerTool({"add_bus", "Create a bus (busType 'fx' or 'group') and return its busID. "
        "busTarget is the parent bus id (default 0 = master) — an fx bus may target another fx "
        "bus, which chains them (create the filter bus first, then the delay bus targeting it, "
        "for a high-passed delay return). An fx bus requires fxType one of "
        "reverb|delay|eq|compressor|filter; anything else is rejected by name.",
        objSchema({{"busType", QJsonObject{{"type","string"}}},
                  {"name", QJsonObject{{"type","string"}}},
                  {"fxType", QJsonObject{{"type","string"}}},
                  {"busTarget", QJsonObject{{"type","integer"},{"default",0}}}}, {"busType"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            auto r = e->getProjectCommands().createBus(
                a.value("busType").toString().toStdString(),
                a.value("name").toString().toStdString(),
                a.value("fxType").toString().toStdString(),
                a.value("busTarget").toInt(0));
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"ok", true}, {"busID", r.busID}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_bus", "Remove a bus by id; sends targeting it are removed with it.",
        objSchema({{"busID", QJsonObject{{"type","integer"}}}}, {"busID"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            std::string error;
            if (!e->getProjectCommands().removeBus(a.value("busID").toInt(-1), error))
                return McpToolResult::text(QString::fromStdString(error), true);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_bus_target", "Re-parent a bus: busTarget becomes the bus's parent "
        "bus id (0 = master). Only the moved bus changes — its own children keep feeding it — so "
        "an existing return can be routed through a filter bus added later (reverb -> HPF -> "
        "master) instead of being recreated in the right order. Errors (with NO change) for an "
        "unknown busID, the master bus, busTarget equal to busID, an unknown busTarget, and any "
        "busTarget whose own parent chain reaches this bus (re-parenting could otherwise close a "
        "cycle several hops deep).",
        objSchema({{"busID", QJsonObject{{"type","integer"}}},
                  {"busTarget", QJsonObject{{"type","integer"}}}}, {"busID","busTarget"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            auto r = e->getProjectCommands().setBusTarget(a.value("busID").toInt(-1),
                                                          a.value("busTarget").toInt(-1));
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"ok", true}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"add_send", "Create a send from a track to a bus and return its sendIndex. "
        "Defaults: level 1.0, post-fader.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"busTarget", QJsonObject{{"type","integer"}}},
                  {"level", QJsonObject{{"type","number"},{"default",1.0}}},
                  {"isPreFader", QJsonObject{{"type","boolean"},{"default",false}}}},
                 {"trackId","busTarget"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const int ti = a.value("trackId").toInt(-1);
            const int busTarget = a.value("busTarget").toInt(-1);
            const float level = static_cast<float>(a.value("level").toDouble(1.0));
            const bool pre = a.value("isPreFader").toBool(false);
            auto r = e->getProjectCommands().createSend(ti, busTarget, level, pre);
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"ok", true}, {"sendIndex", r.sendIndex}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_send", "Remove a send from a track by its index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}}}, {"trackId","sendIndex"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const int ti = a.value("trackId").toInt(-1);
            const int si = a.value("sendIndex").toInt(-1);
            std::string error;
            if (!e->getProjectCommands().removeSend(ti, si, error))
                return McpToolResult::text(QString::fromStdString(error), true);
            return McpToolResult::text("ok");
        }});

    // --- Bus FX params + bus discovery (docs/plans/2026-09-22-bus-fx-params.md, slice C) ---
    // The creators above make a return reachable; these make it SHAPABLE (set_bus_fx_param)
    // and VISIBLE (list_buses / list_bus_fx_params). Both surfaces read the same
    // ROUTING_GRAPH/BUS_LIST tree through the shared shaping in common/BusInfo.h and write
    // through the same ProjectCommands::setBusFxParam, so MCP and RPC cannot drift —
    // tests/unit/frontend/bus_send_rpc_test.cpp asserts that by driving both.

    s.registerTool({"list_buses", "List every bus in the project: "
        "[{busID, name, busType, fxType, busTarget}], sorted by busID. busType is "
        "'fx' | 'group' | 'master'; an fx bus is the usable send target (read this before "
        "add_send) and the one set_bus_fx_param / list_bus_fx_params accept.",
        objSchema({}, {}),
        "send",
        [e](const QJsonObject&) -> McpToolResult {
            return McpToolResult::text(QString::fromStdString(HDAW::shapeBusesJson(
                HDAW::readBuses(e->getProjectModel().getBusListTree()))));
        }});

    s.registerTool({"list_bus_fx_params", "List the FX parameters a bus return honors: "
        "{busID, name, busType, fxType, params:[{index, name, minValue, maxValue, defaultValue, "
        "value}]}. Settable with set_bus_fx_param (same param space list_fx_params reports for a "
        "track FX slot). Reports ONLY the parameters the bus DSP actually applies. Errors for an "
        "unknown busID, a non-fx bus, or an fxType the bus chain cannot build.",
        objSchema({{"busID", QJsonObject{{"type","integer"}}}}, {"busID"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const int busID = a.value("busID").toInt(-1);
            const auto target =
                HDAW::findFxBusForRead(e->getProjectModel().getBusListTree(), busID);
            if (!target.bus.isValid())
                return McpToolResult::text(QString::fromStdString(target.error), true);
            return McpToolResult::text(
                QString::fromStdString(HDAW::shapeBusFxParamsJson(target.bus)));
        }});

    s.registerTool({"set_bus_fx_param", "Set an FX bus parameter in REAL units (the index / "
        "minValue / maxValue list_bus_fx_params reports; values clamp to that range). Shapes the shared "
        "send return — reverb Room Size / Wet Level, eq Gain, compressor Threshold, delay Delay "
        "Time (seconds) / Feedback / Mix / SyncToTempo / Division, filter Cutoff (Hz) / Mode "
        "(0=lowpass, 1=highpass, 2=bandpass) / Resonance. Errors (with no mutation) for an "
        "unknown busID, a non-fx bus, or a paramIndex the bus's fxType does not have. Durable: the "
        "value lives on the BUS node, so it survives save/load, rebuildRoutingGraph() and "
        "prepareToPlay.",
        objSchema({{"busID", QJsonObject{{"type","integer"}}},
                  {"paramIndex", QJsonObject{{"type","integer"}}},
                  {"value", QJsonObject{{"type","number"}}}}, {"busID","paramIndex","value"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const int busID = a.value("busID").toInt(-1);
            const int paramIndex = a.value("paramIndex").toInt(-1);
            const float value = static_cast<float>(a.value("value").toDouble());
            std::string error;
            if (!e->getProjectCommands().setBusFxParam(busID, paramIndex, value, error))
                return McpToolResult::text(QString::fromStdString(error), true);
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
