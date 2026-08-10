#pragma once
#include "../FrontendRpc.h"

class ProjectCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchProject(ProjectCommands& cmds, const QString& subMethod,
                               const QJsonValue& params);
}
