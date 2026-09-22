#pragma once
#include "../FrontendRpc.h"

class AudioEngine;
class PluginService;
class PluginParamService;
class QJsonValue;
class QString;

namespace frontend {

class FrontendServer;

DispatchResult dispatchPlugin(PluginService& s, AudioEngine& engine, const QString& subMethod,
                               const QJsonValue& params, FrontendServer* server);
// `engine` is needed (alongside the service) for the preset-loading routes, which go through the
// shared command layer — plugin.loadNordBank calls HDAW::loadNordBankFile, the same loader the MCP
// load_nord_bank tool uses (RPC/MCP parity by construction).
// `engine` (not just the param service) is needed so plugin.setParam can also
// persist the write into the slot's offline-replay ledger via the shared
// command layer (RPC/MCP parity by construction for set_fx_param).
DispatchResult dispatchPluginParam(AudioEngine& engine, const QString& subMethod,
                                    const QJsonValue& params);
}
