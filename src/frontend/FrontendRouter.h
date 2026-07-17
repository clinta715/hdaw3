#pragma once

#include <QString>
#include <QJsonValue>
#include "FrontendRpc.h"

class AudioEngine;

namespace frontend {

// The dispatch core. Routes a fully-parsed JSON-RPC method to the matching
// abstract-interface call on the engine. Main-thread-only (matches the
// project's single-thread rule; the WebSocket server hops inbound messages
// to the main thread via Qt::QueuedConnection).
//
// Returns a DispatchResult: on success, payload is the bare result value
// (object/array/primitive/null); on error, payload is {code, message}.
DispatchResult dispatch(AudioEngine& engine, const QString& method, const QJsonValue& params);

} // namespace frontend
