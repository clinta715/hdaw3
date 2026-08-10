#pragma once
#include "../FrontendRpc.h"

class PluginService;
class PluginParamService;
class QJsonValue;
class QString;

namespace frontend {

class FrontendServer;

DispatchResult dispatchPlugin(PluginService& s, const QString& subMethod,
                               const QJsonValue& params, FrontendServer* server);
DispatchResult dispatchPluginParam(PluginParamService& s, const QString& subMethod,
                                    const QJsonValue& params);
}
