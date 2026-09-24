#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class ProjectCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchProject(ProjectCommands& cmds, const QString& subMethod,
                               const QJsonValue& params);
// The two project routes that need engine context (the ProjectModel) rather than
// just the command interface: removeTrack's dryRun/force guard and the
// addTrackWithFx composite. FrontendRouter routes those methods here before
// dispatchProject (see the definitions in Router_Project.cpp).
DispatchResult dispatchRemoveTrack(AudioEngine& engine, const QJsonValue& params);
DispatchResult dispatchAddTrackWithFx(AudioEngine& engine, const QJsonValue& params);
DispatchResult dispatchSettings(AudioEngine& engine, const QString& subMethod,
                               const QJsonValue& params);
}
