#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {
class FrontendServer;

DispatchResult dispatchRave(AudioEngine& engine, const QString& subMethod,
                            const QJsonValue& params, FrontendServer* server = nullptr);
}
