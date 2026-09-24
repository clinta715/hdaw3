#pragma once
#include "../FrontendRpc.h"

class ProjectCommands;
class QJsonValue;
class QString;

namespace HDAW { class SessionManager; }

namespace frontend {
// `session` is the engine's SessionManager, which is not reachable from
// ProjectCommands; FrontendRouter passes it in so `session.getClipStates` can
// call the same shared payload builder (common/SessionClipStateJson.h) the MCP
// session_get_clip_states tool uses.
DispatchResult dispatchSession(ProjectCommands& cmds, HDAW::SessionManager& session,
                               const QString& subMethod, const QJsonValue& params);
}
