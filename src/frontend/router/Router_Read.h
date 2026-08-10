#pragma once
#include "../FrontendRpc.h"

class ReadModel;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchRead(ReadModel& r, const QString& subMethod,
                            const QJsonValue& params);
}
