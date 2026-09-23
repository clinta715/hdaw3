#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/AutomationPreset.h"
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

void registerAutomationTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_automation_point", "Add a point to an automation lane (paramID integer preferred; name accepted).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"time",   QJsonObject{{"type","number"}}},
                  {"value",  QJsonObject{{"type","number"}}}}, {"trackId","lane","time","value"}),
        "automation",
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
            "automation",
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
        "automation",
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
        "automation",
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
    s.registerTool({"add_automation_lane", "Create an automation lane, optionally bound to a target paramID (1=volume, 2=pan, 3=mute, or 100+slotIndex*100+paramIndex for a plugin FX param). With replace=true and a nonzero paramID the call instead takes ownership of the lane already bound to that paramID: it is renamed to laneName in place, keeping its points, so a post-arrangement automation pass can re-run and re-assert \"the lane bound to paramID N is mine, named X\" in one call. A laneName already bound to a different paramID still fails.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"laneName",  QJsonObject{{"type","string"}}},
                  {"paramID",   QJsonObject{{"type","integer"}}},
                  {"replace",   QJsonObject{{"type","boolean"}}}}, {"trackId","laneName"}),
        "automation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            QString laneNameQ = a.value("laneName").toString();
            if (laneNameQ.isEmpty()) return McpToolResult::text("laneName required", true);
            int paramID = a.value("paramID").toInt(0);
            bool replace = a.value("replace").toBool(false);
            bool added = e->getProjectCommands().addAutomationLane(
                trackId, laneNameQ.toUtf8().constData(), paramID, replace);
            if (!added)
                return McpToolResult::text("lane name or paramID already exists", true);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_automation_lane", "Remove an automation lane (by paramID integer, or by name string).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}}}, {"trackId","lane"}),
        "automation",
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

    // automation_preset — the P2-3 preset bank. One call writes a named recipe
    // (pump/macro/openClose/riser/sine/square) onto an EXISTING lane over one
    // or more beat windows, as ONE undo unit. Windows/times are beats at this
    // boundary; the tree stores seconds; values are normalized 0..1 (the
    // command converts exactly like generate_automation_envelope). The lane
    // must already exist — create it with add_automation_lane first (the
    // built-in "Volume" lane works by name). Lanes are enabled by this call
    // unless enable=false.
    {
        QJsonObject sectionItem{{"type","object"},
            {"properties", QJsonObject{
                {"start",      QJsonObject{{"type","number"}}},
                {"end",        QJsonObject{{"type","number"}}},
                {"preset",     QJsonObject{{"type","string"}}},
                {"startValue", QJsonObject{{"type","number"}}},
                {"endValue",   QJsonObject{{"type","number"}}}}},
            {"required", QJsonArray{"start","end"}}};
        QJsonObject sectionsSchema{{"type","array"}, {"items", sectionItem}};
        QJsonObject laneSchema{{"oneOf", QJsonArray{
            QJsonObject{{"type","integer"}},
            QJsonObject{{"type","string"}}}}};

        QString presetsDoc;
        for (std::size_t i = 0; i < HDAW::AutomationPreset::kPresetDocumentationCount; ++i)
            presetsDoc += QString(" - %1: %2\n").arg(
                HDAW::AutomationPreset::kPresetDocumentation[i].name,
                HDAW::AutomationPreset::kPresetDocumentation[i].line);
        const QString description =
            QString::fromUtf8(
                "Apply named automation presets to an EXISTING lane over beat windows. Presets:\n") +
            presetsDoc +
            QString::fromUtf8(
                "Windows are BEATS at this boundary; the tree stores SECONDS and values are "
                "normalized 0..1 (converted exactly like generate_automation_envelope; density is "
                "a 0.25-beat grid). The lane must already exist — create it with add_automation_lane "
                "first; built-in lanes like \"Volume\" work by name. With sections, each section's "
                "preset/startValue/endValue override the top-level ones; without sections, preset + "
                "start + end form the single window. clear=true removes existing points inside each "
                "window before writing. seed 0 = non-deterministic (default 12345 = reproducible). "
                "The lane is enabled after this call unless enable=false.");

        s.registerTool({"automation_preset",
            description.toUtf8().constData(),
            objSchema({{"trackId",    QJsonObject{{"type","integer"}}},
                       {"lane",       laneSchema},
                       {"preset",     QJsonObject{{"type","string"}}},
                       {"start",      QJsonObject{{"type","number"}}},
                       {"end",        QJsonObject{{"type","number"}}},
                       {"startValue", QJsonObject{{"type","number"}}},
                       {"endValue",   QJsonObject{{"type","number"}}},
                       {"cycles",     QJsonObject{{"type","number"}}},
                       {"sections",   sectionsSchema},
                       {"clear",      QJsonObject{{"type","boolean"}}},
                       {"seed",       QJsonObject{{"type","integer"}}},
                       {"enable",     QJsonObject{{"type","boolean"}}}},
                      {"trackId","lane"}),
            "automation",
            [e](const QJsonObject& a) -> McpToolResult {
                const int trackId = a.value("trackId").toInt(-1);
                auto lane = findLane(e, trackId, a.value("lane"));
                if (!lane.isValid())
                    return McpToolResult::text(
                        "lane not found; create it with add_automation_lane first "
                        "(built-in lanes like \"Volume\" work by name)", true);
                const std::string laneName =
                    lane.getProperty(IDs::name, "").toString().toStdString();

                std::vector<HDAW::AutomationPreset::PresetWindow> windows;
                QJsonArray applied;
                const auto resolvePreset = [](const QString& s)
                    -> std::optional<HDAW::AutomationPreset::Preset> {
                    return HDAW::AutomationPreset::presetFromName(s.toStdString());
                };

                const auto sections = a.value("sections").toArray();
                if (!sections.isEmpty())
                {
                    for (const auto& sv : sections)
                    {
                        const auto obj = sv.toObject();
                        if (!obj.contains("start") || !obj.contains("end"))
                            return McpToolResult::text("each section requires start and end", true);
                        HDAW::AutomationPreset::PresetWindow w;
                        w.start = obj.value("start").toDouble();
                        w.end = obj.value("end").toDouble();
                        const QString pName = obj.value("preset").toString(a.value("preset").toString());
                        if (pName.isEmpty())
                            return McpToolResult::text(
                                "preset required (pump|macro|openClose|riser|sine|square|subtleLife|randomDrift|steppedGate|phaseSweep|delayThrow)", true);
                        const auto p = resolvePreset(pName);
                        if (!p)
                            return McpToolResult::text("unknown preset: " + pName, true);
                        w.preset = *p;
                        if (obj.contains("startValue"))
                            w.startValue = obj.value("startValue").toDouble();
                        else if (a.contains("startValue"))
                            w.startValue = a.value("startValue").toDouble();
                        if (obj.contains("endValue"))
                            w.endValue = obj.value("endValue").toDouble();
                        else if (a.contains("endValue"))
                            w.endValue = a.value("endValue").toDouble();
                        if (!(w.end > w.start))
                            return McpToolResult::text(
                                QString("bad window: end (%1) must be > start (%2)")
                                    .arg(w.end).arg(w.start), true);
                        windows.push_back(w);
                        applied.append(pName);
                    }
                }
                else
                {
                    const QString pName = a.value("preset").toString();
                    if (pName.isEmpty())
                        return McpToolResult::text(
                            "preset required (pump|macro|openClose|riser|sine|square|subtleLife|randomDrift|steppedGate|phaseSweep|delayThrow) "
                            "when sections is absent", true);
                    const auto p = resolvePreset(pName);
                    if (!p)
                        return McpToolResult::text("unknown preset: " + pName, true);
                    if (!a.contains("start") || !a.contains("end"))
                        return McpToolResult::text(
                            "start and end required when sections is absent", true);
                    HDAW::AutomationPreset::PresetWindow w;
                    w.start = a.value("start").toDouble();
                    w.end = a.value("end").toDouble();
                    w.preset = *p;
                    if (a.contains("startValue")) w.startValue = a.value("startValue").toDouble();
                    if (a.contains("endValue"))   w.endValue   = a.value("endValue").toDouble();
                    if (a.contains("cycles"))     w.cycles     = a.value("cycles").toDouble();
                    if (!(w.end > w.start))
                        return McpToolResult::text(
                            QString("bad window: end (%1) must be > start (%2)")
                                .arg(w.end).arg(w.start), true);
                    windows.push_back(w);
                    applied.append(pName);
                }

                const bool clear = a.value("clear").toBool(false);
                const uint64_t seed = static_cast<uint64_t>(a.value("seed").toInt(12345));
                int pointsAdded = 0;
                const std::string err = e->getProjectCommands().applyAutomationPreset(
                    trackId, laneName, windows, clear, seed, &pointsAdded);
                if (!err.empty())
                    return McpToolResult::text(QString::fromStdString(err), true);

                const bool enable = a.value("enable").toBool(true);
                if (!enable)
                    e->getProjectCommands().setAutomationEnabled(trackId, laneName, false);

                return McpToolResult::text(QString::fromUtf8(
                    QJsonDocument(QJsonObject{
                        {"lane", QString::fromStdString(laneName)},
                        {"presets", applied},
                        {"pointsAdded", pointsAdded}}).toJson(QJsonDocument::Compact)));
            }});

    s.registerTool({"apply_movement_plan",
        "Batch 'movement plan' for the FX & Automation choreography pass: apply automation "
        "presets across MULTIPLE tracks/sections in ONE undo unit. events: [{trackId (req), "
        "preset (req), start, end, paramID, laneName, startValue, endValue, seed}]. Presets: "
        "pump/macro/openClose/riser/sine/square/subtleLife/randomDrift/steppedGate/phaseSweep/"
        "delayThrow. paramID default 1 (volume — reuses the built-in Volume lane); pass the "
        "compound pid (100+slotIndex*100+paramIndex) for plugin FX params. Lane resolution "
        "per event: an explicit laneName is reused when present; if absent OR if that name "
        "would duplicate an existing paramID lane, the lane already bound to paramID is reused "
        "(never two lanes on one parameter), else a lane 'movement-<preset>' or the explicit "
        "name is created and bound. Existing points inside each window are "
        "replaced and the lane enabled. Returns {okCount, failCount, events:[{laneName, "
        "pointsWritten, ok, error?}]} — partial failure keeps the good events. After a plan, "
        "audit_modulation_coverage should be green for the touched sounding tracks. NOTE: Volume "
        "events (paramID 1 — the default) write and ENABLE the track's Volume lane, which makes "
        "automation authoritative for that track: subsequent set_track_volume/fader writes are "
        "overridden (audit_modulation_coverage reports faderOverriddenIds). Call "
        "set_fader_authoritative before post-movement gain staging.",
        objSchema({{"events", QJsonObject{{"type","array"}, {"items", QJsonObject{
            {"type","object"},
            {"properties", QJsonObject{
                {"trackId",    QJsonObject{{"type","integer"}}},
                {"preset",     QJsonObject{{"type","string"}}},
                {"start",      QJsonObject{{"type","number"}}},
                {"end",        QJsonObject{{"type","number"}}},
                {"paramID",    QJsonObject{{"type","integer"}}},
                {"laneName",   QJsonObject{{"type","string"}}},
                {"startValue", QJsonObject{{"type","number"}}},
                {"endValue",   QJsonObject{{"type","number"}}},
                {"seed",       QJsonObject{{"type","integer"}}}}},
            {"required", QJsonArray{"trackId","preset"}}}}}}}, {"events"}),
        "automation",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto eventsArr = a.value("events").toArray();
            if (eventsArr.isEmpty())
                return McpToolResult::text("events array required", true);
            std::vector<ProjectCommands::MovementEvent> events;
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
            const auto res = e->getProjectCommands().applyMovementPlan(events);
            QJsonArray arr;
            for (const auto& r : res.events)
            {
                QJsonObject ro{ { "laneName", QString::fromStdString(r.laneName) },
                                { "pointsWritten", r.pointsWritten },
                                { "ok", r.ok } };
                if (!r.error.empty()) ro["error"] = QString::fromStdString(r.error);
                arr.append(ro);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                { "okCount", res.okCount }, { "failCount", res.failCount },
                { "events", arr }}).toJson(QJsonDocument::Compact)));
        }});
    }
}

} // namespace mcp
