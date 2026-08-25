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
    s.registerTool({"add_automation_lane", "Create an automation lane, optionally bound to a target paramID (1=volume, 2=pan, 3=mute, or 100+slotIndex*100+paramIndex for a plugin FX param).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"laneName",  QJsonObject{{"type","string"}}},
                  {"paramID",   QJsonObject{{"type","integer"}}}}, {"trackId","laneName"}),
        "automation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            QString laneNameQ = a.value("laneName").toString();
            if (laneNameQ.isEmpty()) return McpToolResult::text("laneName required", true);
            int paramID = a.value("paramID").toInt(0);
            bool added = e->getProjectCommands().addAutomationLane(
                trackId, laneNameQ.toUtf8().constData(), paramID);
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
}

} // namespace mcp
