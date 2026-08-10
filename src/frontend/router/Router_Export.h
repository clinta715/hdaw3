#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {

class FrontendServer;

DispatchResult dispatchExport(AudioEngine& engine, const QString& subMethod,
                               const QJsonValue& params, FrontendServer* server);
}
