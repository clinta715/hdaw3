#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

void registerSessionDomain(McpServer& s, AudioEngine* e)
{
    s.registerTool({"session_launch_scene", "Launch all clips in a scene (quantized to next bar).",
        objSchema({{"sceneIndex", QJsonObject{{"type","integer"}}}}, {"sceneIndex"}),
        "session",
        [e](const QJsonObject& a) -> McpToolResult {
            int si = a.value("sceneIndex").toInt();
            if (si < 0 || si > 7) return McpToolResult::text("sceneIndex must be 0-7", true);
            e->getProjectCommands().launchScene(si);
            return McpToolResult::text(QString("launched scene %1").arg(si));
        }});

    s.registerTool({"session_stop_all", "Stop all playing session clips.",
        objSchema({}),
        "session",
        [e](const QJsonObject&) -> McpToolResult {
            e->getProjectCommands().stopAllSessionClips();
            return McpToolResult::text("stopped all session clips");
        }});

    s.registerTool({"session_set_clip_scene", "Assign a clip to a session scene (-1 = remove from session).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"sceneIndex", QJsonObject{{"type","integer"}}}}, {"clipId","sceneIndex"}),
        "session",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            int scene = a.value("sceneIndex").toInt();
            e->getProjectCommands().setClipScene(clipId, scene);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"session_create_clip", "Create an empty clip in a session slot.",
        objSchema({{"trackIndex", QJsonObject{{"type","integer"}}},
                   {"sceneIndex", QJsonObject{{"type","integer"}}},
                   {"isMidi", QJsonObject{{"type","boolean"}}}}, {"trackIndex","sceneIndex"}),
        "session",
        [e](const QJsonObject& a) -> McpToolResult {
            int track = a.value("trackIndex").toInt();
            int scene = a.value("sceneIndex").toInt();
            bool isMidi = a.value("isMidi").toBool(true);
            int clipId = e->getProjectCommands().createSessionClip(track, scene, isMidi);
            if (clipId < 0) return McpToolResult::text("failed to create clip", true);
            return McpToolResult::text(QString("created clip %1 in scene %2").arg(clipId).arg(scene));
        }});

    s.registerTool({"session_get_clip_states", "Get play/stop state for all session clips.",
        objSchema({}),
        "session",
        [e](const QJsonObject&) -> McpToolResult {
            auto states = e->getSessionManager().getClipStates();
            QJsonArray arr;
            for (const auto& st : states) {
                arr.append(QJsonObject{
                    {"clipId", st.clipId},
                    {"sceneIndex", st.sceneIndex},
                    {"isPlaying", st.isPlaying},
                    {"isLaunched", st.isLaunched}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
