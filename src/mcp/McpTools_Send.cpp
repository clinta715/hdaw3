#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
// B2: the stable-id argument helpers (`trackId`/`trackID`, `sendIndex`/`sendID`)
// — thin readers over the ONE shared rule in common/StableRefResolve.h, whose
// error text is what the RPC twin reports for the same request.
#include "McpArgs.h"
#include "../model/ProjectModel.h"
#include "../common/BusInfo.h"
#include "../common/SendJson.h"
// Slice 2 of docs/plans/2026-10-05-param-batch-and-bugfixes.md: the batched bus
// param write — the SAME strict `writes` parser + payload shaper the RPC twin
// (project.setBusFxParams) calls, so both surfaces agree byte-for-byte.
#include "../common/FxParamBatchJson.h"
// The deferred follow-up of that plan: the batched bus/send CREATORS — the SAME
// strict `buses`/`sends` parser + payload shaper the RPC twins
// (project.addBuses / project.addSends) call, so both surfaces agree
// byte-for-byte.
#include "../common/BusSendBatchJson.h"
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
    s.registerTool({"get_track_sends", "List all sends on a track: "
        "[{sendIndex, level, isPreFader, bypassed, sendID}] in SEND_LIST order. "
        "`sendIndex` is the send's ADDRESS (removing one shifts the rest) while "
        "`sendID` is its STABLE identity (design B1), untouched by that splice — "
        "hand the id back to the send tools to address a send you read earlier. "
        "The identical payload RPC read.getTrackSends returns; both read the same "
        "shaping (common/SendJson.h). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"trackID", QJsonObject{{"type","integer"}}}}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            // B2: the track may be named by its stable `trackID`. An out-of-range
            // positional index keeps answering [] — B2 is additive, and only the
            // new argument can fail with "unknown trackID".
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            if (!e->getMainProcessor()) return McpToolResult::text("engine not ready", true);
            return McpToolResult::text(QString::fromStdString(HDAW::shapeSendsJson(
                e->getReadModel().getTrackSends(ti))));
        }});

    // B2: the three send shapers and the two others below take the track by
    // `trackId`/`trackID` and the send by `sendIndex`/`sendID` — the SAME shared
    // resolution the RPC twins run, so an id-holding caller can shape a send it
    // read earlier even after a splice renumbered it.
    s.registerTool({"set_track_send_level", "Set the level of a send. " +
        mcp::stableRefRuleText("trackID", "trackId") + " " +
        mcp::stableRefRuleText("sendID", "sendIndex"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"sendID",    QJsonObject{{"type","integer"}}},
                  {"level",     QJsonObject{{"type","number"}}}}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto trackList = e->getProjectModel().getTrackListTree();
            int ti; std::string refErr;
            if (!trackIndexArg(a, trackList, ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si;
            if (!sendIndexArg(a, trackList, ti, si, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            float lv = static_cast<float>(a.value("level").toDouble());
            e->getProjectCommands().setTrackSendLevel(ti, si, lv);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_mode", "Set send mode: pre or post fader. " +
        mcp::stableRefRuleText("trackID", "trackId") + " " +
        mcp::stableRefRuleText("sendID", "sendIndex"),
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"trackID",     QJsonObject{{"type","integer"}}},
                  {"sendIndex",   QJsonObject{{"type","integer"}}},
                  {"sendID",      QJsonObject{{"type","integer"}}},
                  {"isPreFader",  QJsonObject{{"type","boolean"}}}}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto trackList = e->getProjectModel().getTrackListTree();
            int ti; std::string refErr;
            if (!trackIndexArg(a, trackList, ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si;
            if (!sendIndexArg(a, trackList, ti, si, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            bool pre = a.value("isPreFader").toBool();
            e->getProjectCommands().setTrackSendMode(ti, si, pre);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_track_send_bypassed", "Bypass or unbypass a send. " +
        mcp::stableRefRuleText("trackID", "trackId") + " " +
        mcp::stableRefRuleText("sendID", "sendIndex"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"sendID",    QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto trackList = e->getProjectModel().getTrackListTree();
            int ti; std::string refErr;
            if (!trackIndexArg(a, trackList, ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si;
            if (!sendIndexArg(a, trackList, ti, si, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
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

    s.registerTool({"add_buses", "Batch-create MANY buses in ONE undo unit and one round "
        "trip (the same busType/name/fxType/busTarget semantics as add_bus). `buses` is an "
        "array of {busType, name?, fxType?, busTarget?}; busTarget is the parent bus id "
        "(default 0 = master). PARTIAL-APPLY: a bus that fails (bad busType, an unknown "
        "busTarget, an unsupported fxType) is reported in `errors` (numbered by its index) "
        "and the others still land. Returns "
        "{\"ok\":true,\"created\":N,\"failed\":M,\"busIDs\":[...],\"errors\":[{index,error}]} — "
        "`busIDs` carries -1 for a failed slot. One undo reverts every created bus.",
        objSchema({{"buses", QJsonObject{
                       {"type","array"},
                       {"items", QJsonObject{
                           {"type","object"},
                           {"properties", QJsonObject{
                               {"busType",   QJsonObject{{"type","string"}}},
                               {"name",      QJsonObject{{"type","string"}}},
                               {"fxType",    QJsonObject{{"type","string"}}},
                               {"busTarget", QJsonObject{{"type","integer"},{"default",0}}}}},
                           {"additionalProperties", false},
                           {"required", QJsonArray{"busType"}}}}}}},
                  {"buses"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            std::vector<ProjectCommands::BusCreateSpec> buses; QString perr;
            if (!HDAW::parseBusCreates(a.value("buses"), buses, perr))
                return McpToolResult::text(perr, true);
            std::vector<int> ids;
            std::vector<std::string> errs;
            const auto r = e->getProjectCommands().createBuses(buses, &ids, &errs);
            return McpToolResult::text(
                HDAW::busSendBatchPayloadJson(r, ids, errs, "busIDs"));
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
        "Defaults: level 1.0, post-fader. The returned sendIndex is the new send's "
        "ADDRESS; read the send's stable `sendID` from get_track_sends and hold that "
        "if you mean to shape this send after another one is removed. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"busTarget", QJsonObject{{"type","integer"}}},
                  {"level", QJsonObject{{"type","number"},{"default",1.0}}},
                  {"isPreFader", QJsonObject{{"type","boolean"},{"default",false}}}},
                 {"busTarget"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            // B2: the source track may be named by its stable `trackID`.
            // `busTarget` is a busID (already an identity, not a position).
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            const int busTarget = a.value("busTarget").toInt(-1);
            const float level = static_cast<float>(a.value("level").toDouble(1.0));
            const bool pre = a.value("isPreFader").toBool(false);
            auto r = e->getProjectCommands().createSend(ti, busTarget, level, pre);
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"ok", true}, {"sendIndex", r.sendIndex}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"add_sends", "Batch-create MANY sends in ONE undo unit and one round "
        "trip (the same semantics as add_send). `sends` is an array of "
        "{trackId?|trackID?, busTarget, level?, isPreFader?}; level defaults 1.0 and "
        "isPreFader false. PARTIAL-APPLY: a send that fails (an out-of-range track or an "
        "unknown busTarget) is reported in `errors` (numbered by its index) and the others "
        "still land. Returns "
        "{\"ok\":true,\"created\":N,\"failed\":M,\"sendIndexes\":[...],\"errors\":[{index,error}]} — "
        "`sendIndexes` carries -1 for a failed slot. One undo reverts every created send. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"sends", QJsonObject{
                       {"type","array"},
                       {"items", QJsonObject{
                           {"type","object"},
                           {"properties", QJsonObject{
                               {"trackId",    QJsonObject{{"type","integer"}}},
                               {"trackID",    QJsonObject{{"type","integer"}}},
                               {"busTarget",  QJsonObject{{"type","integer"}}},
                               {"level",      QJsonObject{{"type","number"},{"default",1.0}}},
                               {"isPreFader", QJsonObject{{"type","boolean"},{"default",false}}}}},
                           {"additionalProperties", false},
                           {"required", QJsonArray{"busTarget"}}}}}}},
                  {"sends"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            std::vector<ProjectCommands::SendCreateSpec> sends; QString perr;
            if (!HDAW::parseSendCreates(e->getProjectModel().getTrackListTree(),
                                        a.value("sends"), sends, perr))
                return McpToolResult::text(perr, true);
            std::vector<int> ids;
            std::vector<std::string> errs;
            const auto r = e->getProjectCommands().createSends(sends, &ids, &errs);
            return McpToolResult::text(
                HDAW::busSendBatchPayloadJson(r, ids, errs, "sendIndexes"));
        }});

    s.registerTool({"remove_send",
        "Remove a send from a track by its index. Returns compact JSON "
        "{\"ok\":true,\"removed\":<sendIndex>,\"shifted\":[{\"from\":N,\"to\":N-1},...]}: "
        "`shifted` lists the sends above the removed one re-indexed down by one "
        "([] when the last send was removed) — the identical payload RPC "
        "project.removeSend returns. (Was the bare text \"ok\" before the shift report.) "
        "`removed` is the position that was spliced out, so a send named by its "
        "`sendID` reports the index it still occupied at removal time. " +
        mcp::stableRefRuleText("trackID", "trackId") + " " +
        mcp::stableRefRuleText("sendID", "sendIndex"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"trackID",   QJsonObject{{"type","integer"}}},
                  {"sendIndex", QJsonObject{{"type","integer"}}},
                  {"sendID",    QJsonObject{{"type","integer"}}}}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            // B2: the track by `trackId`/`trackID`, the send by
            // `sendIndex`/`sendID` — the sendID path is what makes a send read
            // before a splice removable after it.
            const auto trackList = e->getProjectModel().getTrackListTree();
            int ti; std::string refErr;
            if (!trackIndexArg(a, trackList, ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            int si;
            if (!sendIndexArg(a, trackList, ti, si, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            std::string error;
            std::vector<std::pair<int, int>> shifted;
            if (!e->getProjectCommands().removeSend(ti, si, error, &shifted))
                return McpToolResult::text(QString::fromStdString(error), true);
            QJsonArray shiftedArr;
            for (const auto& p : shifted)
                shiftedArr.append(QJsonObject{{"from", p.first}, {"to", p.second}});
            const QJsonObject result{{"ok", true}, {"removed", si},
                                     {"shifted", shiftedArr}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
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

    s.registerTool({"set_bus_fx_params",
        "Batch-write MANY FX bus parameters in ONE undo unit and one round trip (the same "
        "real-unit semantics and clamp as set_bus_fx_param). `writes` is an array of "
        "{busID, paramIndex, value}. PARTIAL-APPLY: a write that fails (unknown busID, a "
        "non-fx bus, or an out-of-range paramIndex) is reported in `errors` (numbered by "
        "its index) and the others still land. Returns {ok, written, failed, "
        "errors:[{index, error}]}.",
        objSchema({{"writes", QJsonObject{
                       {"type","array"},
                       {"items", QJsonObject{
                           {"type","object"},
                           {"properties", QJsonObject{
                               {"busID",      QJsonObject{{"type","integer"}}},
                               {"paramIndex", QJsonObject{{"type","integer"}}},
                               {"value",      QJsonObject{{"type","number"}}}}},
                           {"additionalProperties", false},
                           {"required", QJsonArray{"busID","paramIndex","value"}}}}}}},
                  {"writes"}),
        "send",
        [e](const QJsonObject& a) -> McpToolResult {
            std::vector<ProjectCommands::BusFxParamWrite> writes; QString perr;
            if (!HDAW::parseBusFxParamWrites(a.value("writes"), writes, perr))
                return McpToolResult::text(perr, true);
            std::vector<std::string> errs;
            const auto r = e->getProjectCommands().setBusFxParams(writes, &errs);
            return McpToolResult::text(HDAW::fxParamBatchPayloadJson(r, errs));
        }});
}

} // namespace mcp
