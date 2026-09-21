#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchDevice(AudioEngine& engine, const QString& subMethod,
                              const QJsonValue& params);
}
