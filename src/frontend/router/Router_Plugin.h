#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class PluginService;
class PluginParamService;
class QJsonValue;
class QString;

namespace frontend {

class FrontendServer;

DispatchResult dispatchPlugin(PluginService& s, const QString& subMethod,
                               const QJsonValue& params, FrontendServer* server);
// `engine` (not just the param service) is needed so plugin.setParam can also
// persist the write into the slot's offline-replay ledger via the shared
// command layer (RPC/MCP parity by construction for set_fx_param).
DispatchResult dispatchPluginParam(AudioEngine& engine, const QString& subMethod,
                                    const QJsonValue& params);
}
