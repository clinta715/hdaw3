#pragma once
#include "../FrontendRpc.h"

class TransportCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchTransport(TransportCommands& cmds, const QString& subMethod,
                                 const QJsonValue& params);
}
