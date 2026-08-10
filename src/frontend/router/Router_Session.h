#pragma once
#include "../FrontendRpc.h"

class ProjectCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchSession(ProjectCommands& cmds, const QString& subMethod,
                               const QJsonValue& params);
}
