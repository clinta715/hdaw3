#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

static void registerTransportReadTool(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_transport",
        "Return transport state (position, isPlaying, isLooping, loopStart, loopEnd).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto tp = e->getProjectModel().getTransportTree();
            QJsonObject o{
                {"position", static_cast<double>(tp.getProperty(IDs::position))},
                {"isPlaying", static_cast<bool>(tp.getProperty(IDs::isPlaying))},
                {"isLooping", static_cast<bool>(tp.getProperty(IDs::isLooping))},
                {"loopStart", static_cast<double>(tp.getProperty(IDs::loopStart))},
                {"loopEnd", static_cast<double>(tp.getProperty(IDs::loopEnd))}
            };
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }});
}

static void registerTransportControlTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"transport",
        "Control transport: action in {play,stop,pause,rewind,toggleLoop}; optional loopStart/loopEnd (seconds).",
        objSchema({{"action", QJsonObject{{"type","string"},
            {"enum", QJsonArray{"play","stop","pause","rewind","toggleLoop"}}}},
                  {"loopStart", QJsonObject{{"type","number"}}},
                  {"loopEnd",   QJsonObject{{"type","number"}}}}, {"action"}),
        [e](const QJsonObject& a) -> McpToolResult {
            QString action = a.value("action").toString();
            auto tp = e->getProjectModel().getTransportTree();
            if      (action == "play")  tp.setProperty(IDs::isPlaying, true, nullptr);
            else if (action == "stop")  { tp.setProperty(IDs::isPlaying, false, nullptr);
                                          tp.setProperty(IDs::position, 0.0, nullptr); }
            else if (action == "pause") tp.setProperty(IDs::isPlaying, false, nullptr);
            else if (action == "rewind") tp.setProperty(IDs::position, 0.0, nullptr);
            else if (action == "toggleLoop") {
                bool cur = static_cast<bool>(tp.getProperty(IDs::isLooping));
                tp.setProperty(IDs::isLooping, !cur, nullptr);
            }
            if (a.contains("loopStart")) tp.setProperty(IDs::loopStart, a.value("loopStart").toDouble(), nullptr);
            if (a.contains("loopEnd"))   tp.setProperty(IDs::loopEnd,   a.value("loopEnd").toDouble(), nullptr);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"seek", "Move the playhead to a position (in seconds).",
        objSchema({{"position", QJsonObject{{"type","number"}}}}, {"position"}),
        [e](const QJsonObject& a) {
            e->getProjectModel().getTransportTree().setProperty(
                IDs::position, a.value("position").toDouble(), nullptr);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"undo", "Undo the last N actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.undo()) break;
            return McpToolResult::text("ok");
        }});

    s.registerTool({"redo", "Redo the last N undone actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.redo()) break;
            return McpToolResult::text("ok");
        }});
}

void registerTransportDomain(McpServer& s, AudioEngine* e)
{
    registerTransportReadTool(s, e);
    registerTransportControlTools(s, e);
}

} // namespace mcp
