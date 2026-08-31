#pragma once
#include "../FrontendRouter.h"
#include <QJsonValue>

namespace frontend {
DispatchResult dispatchPsyFm(AudioEngine& engine, const QString& m, const QJsonValue& params);
}
