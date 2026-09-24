#pragma once
// apply_movement_plan request shaping + result payload, shared by the MCP tool
// (src/mcp/McpTools_Automation.cpp) and the JSON-RPC route so both surfaces
// produce byte-identical text BY CONSTRUCTION (AGENTS.md feature-parity
// contract). Header-only; JUCE + Qt only.
#include "ProjectCommands.h"   // ProjectCommands::MovementEvent / MovementPlanResult

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace HDAW {

// Parse the `events` array into ProjectCommands::MovementEvent values. `error`
// receives the exact tool text ("events array required") on failure. Every
// per-event default (trackId -1, start 0.0, end 16.0, paramID -1, seed 12345,
// startValue/endValue only when present) is preserved verbatim.
inline bool parseMovementPlan(const QJsonObject& args,
                              std::vector<ProjectCommands::MovementEvent>& events,
                              std::string& error)
{
    events.clear();
    error.clear();
    const auto eventsArr = args.value("events").toArray();
    if (eventsArr.isEmpty())
    {
        error = "events array required";
        return false;
    }
    for (const auto& ev : eventsArr)
    {
        const auto o = ev.toObject();
        ProjectCommands::MovementEvent me;
        me.trackIndex = o.value("trackId").toInt(-1);
        me.startBeats = o.value("start").toDouble(0.0);
        me.endBeats   = o.value("end").toDouble(16.0);
        me.preset     = o.value("preset").toString().toStdString();
        me.paramID    = o.value("paramID").toInt(-1);
        me.laneName   = o.value("laneName").toString().toStdString();
        if (o.contains("startValue")) me.startValue = o.value("startValue").toDouble();
        if (o.contains("endValue"))   me.endValue   = o.value("endValue").toDouble();
        me.seed = static_cast<uint64_t>(o.value("seed").toInt(12345));
        events.push_back(me);
    }
    return true;
}

// Result payload text (compact JSON):
// {okCount, failCount, events:[{laneName, pointsWritten, ok, error?}]}.
// `error` is only present when non-empty.
inline QString movementPlanResultText(const ProjectCommands::MovementPlanResult& res)
{
    QJsonArray arr;
    for (const auto& r : res.events)
    {
        QJsonObject ro{ { "laneName", QString::fromStdString(r.laneName) },
                        { "pointsWritten", r.pointsWritten },
                        { "ok", r.ok } };
        if (! r.error.empty()) ro["error"] = QString::fromStdString(r.error);
        arr.append(ro);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        { "okCount", res.okCount }, { "failCount", res.failCount },
        { "events", arr }}).toJson(QJsonDocument::Compact));
}

// ONE entry point for apply_movement_plan: parse + apply + report. Returns the
// exact tool text (error text when *outOk is left false).
inline QString applyMovementPlanToolText(ProjectCommands& commands,
                                         const QJsonObject& args,
                                         bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    std::vector<ProjectCommands::MovementEvent> events;
    std::string error;
    if (! parseMovementPlan(args, events, error))
        return QString::fromStdString(error);

    const auto res = commands.applyMovementPlan(events);
    if (outOk) *outOk = true;
    return movementPlanResultText(res);
}

} // namespace HDAW
