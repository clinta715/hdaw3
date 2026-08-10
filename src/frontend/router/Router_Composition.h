#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchComposition(AudioEngine& engine, const QString& subMethod,
                                    const QJsonValue& params);
}
