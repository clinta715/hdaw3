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
}

} // namespace mcp
