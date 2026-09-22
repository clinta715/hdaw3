#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchModulation(AudioEngine& engine, const QString& subMethod,
                                  const QJsonValue& params);
}
