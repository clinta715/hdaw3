#include "Router_Session.h"
#include "RouterHelpers.h"

#include "../../common/ProjectCommands.h"
#include "../../common/SessionClipStateJson.h"

#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchSession(ProjectCommands& c, HDAW::SessionManager& session,
                               const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "setClipScene") { int clipId, scene; if (!requireInt(o, "clipId", clipId, nullptr) || !requireInt(o, "sceneIndex", scene, nullptr)) return makeError(-32602, "clipId and sceneIndex required"); c.setClipScene(clipId, scene); return { false, QJsonValue::Null }; }
    if (m == "createClip") { int track, scene; bool isMidi = true; if (!requireInt(o, "trackIndex", track, nullptr) || !requireInt(o, "sceneIndex", scene, nullptr)) return makeError(-32602, "trackIndex and sceneIndex required"); if (o.contains("isMidi")) { bool b; requireBool(o, "isMidi", b, nullptr); isMidi = b; } return { false, c.createSessionClip(track, scene, isMidi) }; }
    if (m == "launchScene") { int si; if (!requireInt(o, "sceneIndex", si, nullptr)) return makeError(-32602, "sceneIndex required"); c.launchScene(si); return { false, QJsonValue::Null }; }
    if (m == "stopAll") { c.stopAllSessionClips(); return { false, QJsonValue::Null }; }
    // Shared payload builder (common/SessionClipStateJson.h) — the SAME array the
    // MCP session_get_clip_states tool emits; the router hands the client the
    // parsed structure rather than a string-quoted document (read.getFxSlots
    // precedent).
    if (m == "getClipStates") { return { false, QJsonValue(HDAW::sessionClipStatesJson(session.getClipStates())) }; }
    return makeError(-32601, "unknown session method: " + m);
}

} // namespace frontend
