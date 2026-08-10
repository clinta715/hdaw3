#pragma once
#include "../FrontendRpc.h"

class AudioGraphCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchAudioGraph(AudioGraphCommands& cmds, const QString& subMethod,
                                   const QJsonValue& params);
}
