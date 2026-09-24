#pragma once
// session_get_clip_states — the SINGLE payload builder behind the MCP
// `session_get_clip_states` tool and (by the AGENTS.md parity rule) any future
// RPC twin.
//
// Maps SessionManager::getClipStates() to the {clipId, sceneIndex, isPlaying,
// isLaunched} array. The caller wraps it: QString::fromUtf8(
// QJsonDocument(arr).toJson(QJsonDocument::Compact)) — that wording stays on
// the surface.

#include <QJsonArray>
#include <QJsonObject>

#include "../engine/SessionManager.h"   // HDAW::SessionManager::SessionClipState

#include <vector>

namespace HDAW {

inline QJsonArray sessionClipStatesJson(
    const std::vector<SessionManager::SessionClipState>& states)
{
    QJsonArray arr;
    for (const auto& st : states) {
        arr.append(QJsonObject{
            {"clipId", st.clipId},
            {"sceneIndex", st.sceneIndex},
            {"isPlaying", st.isPlaying},
            {"isLaunched", st.isLaunched}
        });
    }
    return arr;
}

} // namespace HDAW
