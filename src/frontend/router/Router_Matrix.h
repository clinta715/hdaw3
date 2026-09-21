#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchMatrix(AudioEngine& engine, const QString& subMethod,
                              const QJsonValue& params);
}
