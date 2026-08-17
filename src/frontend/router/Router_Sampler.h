#pragma once
#include "../FrontendRouter.h"
#include <QJsonValue>
#include <QString>
namespace frontend {
DispatchResult dispatchSampler(AudioEngine& engine, const QString& m, const QJsonValue& params);
} // namespace frontend