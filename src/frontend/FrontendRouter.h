#pragma once

#include <QString>
#include <QJsonValue>
#include "FrontendRpc.h"

class AudioEngine;

namespace frontend {

class FrontendServer;

// The dispatch core. Routes a fully-parsed JSON-RPC method to the matching
// abstract-interface call on the engine. Main-thread-only (matches the
// project's single-thread rule; the WebSocket server hops inbound messages
// to the main thread via Qt::QueuedConnection).
//
// `server` is optional: when non-null, long-running handlers (e.g.
// `export.audio`) use it to broadcast progress notifications while the
// dispatch call is still blocked waiting for completion. Tests can pass
// nullptr to exercise the router without a live server.
//
// Returns a DispatchResult: on success, payload is the bare result value
// (object/array/primitive/null); on error, payload is {code, message}.
DispatchResult dispatch(AudioEngine& engine, const QString& method, const QJsonValue& params,
                        FrontendServer* server = nullptr);

} // namespace frontend
