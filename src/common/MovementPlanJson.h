#pragma once
// apply_movement_plan request shaping + result payload, shared by the MCP tool
// (src/mcp/McpTools_Automation.cpp) and the JSON-RPC route so both surfaces
// produce byte-identical text BY CONSTRUCTION (AGENTS.md feature-parity
// contract). Header-only; JUCE + Qt only.
#include "ProjectCommands.h"   // ProjectCommands::MovementEvent / MovementPlanResult
#include "StableRefResolve.h"  // resolveTrackRef (the ONE shared rule)

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
//
// B2b split: WITH the stable `trackID` the ONE shared rule
// (common/StableRefResolve.h) resolves strictly and a bad ref (unknown id,
// disagreement) fails the WHOLE plan naming the id, before anything is
// applied: a stale id after a reorder is exactly the caller bug the rule
// refuses to guess around, and "nothing mutated on a malformed argument" is
// the single-track tools' contract too. WITHOUT `trackID` the per-event parse
// is BYTE-FOR-BYTE the pre-B2b one: positional `trackId` with its historical
// -1 default, so a track the apply layer then reports per-event ("track not
// found") — never a whole-plan refusal. The per-event error channel stays for
// apply-level misses (lane conflicts, bad windows) — its existing purpose.
inline bool parseMovementPlan(const juce::ValueTree& trackList,
                              const QJsonObject& args,
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
        if (o.contains("trackID"))
        {
            int index = HDAW::kNoRef, stableID = 0;
            if (o.contains("trackId"))
            {
                if (! o.value("trackId").isDouble())
                {
                    error = "missing or non-numeric param: trackId";
                    return false;
                }
                index = static_cast<int>(o.value("trackId").toDouble());
            }
            if (! o.value("trackID").isDouble())
            {
                error = "missing or non-numeric param: trackID";
                return false;
            }
            stableID = static_cast<int>(o.value("trackID").toDouble());
            const auto ref = resolveTrackRef(trackList, index, stableID);
            if (! ref.ok)
            {
                error = ref.error;
                return false;
            }
            me.trackIndex = ref.index;
        }
        else
        {
            // Pre-B2b parse, byte-for-byte: `toInt(-1)` — an absent OR
            // non-numeric positional is the "no track" default the apply
            // layer reports per-event.
            me.trackIndex = o.value("trackId").toInt(-1);
        }
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
                                         const juce::ValueTree& trackList,
                                         const QJsonObject& args,
                                         bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    std::vector<ProjectCommands::MovementEvent> events;
    std::string error;
    if (! parseMovementPlan(trackList, args, events, error))
        return QString::fromStdString(error);

    const auto res = commands.applyMovementPlan(events);
    if (outOk) *outOk = true;
    return movementPlanResultText(res);
}

} // namespace HDAW
