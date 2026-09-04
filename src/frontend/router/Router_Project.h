#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class ProjectCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchProject(ProjectCommands& cmds, const QString& subMethod,
                               const QJsonValue& params);
DispatchResult dispatchSettings(AudioEngine& engine, const QString& subMethod,
                               const QJsonValue& params);
}
