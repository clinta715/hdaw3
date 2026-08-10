#include "Router_Transport.h"
#include "RouterHelpers.h"

#include "../../common/TransportCommands.h"

#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchTransport(TransportCommands& c, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "play")    { c.play();    return { false, QJsonValue::Null }; }
    if (m == "stop")    { c.stop();    return { false, QJsonValue::Null }; }
    if (m == "pause")   { c.pause();   return { false, QJsonValue::Null }; }
    if (m == "rewind")  { c.rewind();  return { false, QJsonValue::Null }; }
    if (m == "toggleLoop") { c.toggleLoop(); return { false, QJsonValue::Null }; }
    if (m == "seekToSample")  { int64_t s; if (!requireInt(o, "sample", s, nullptr)) return makeError(-32602, "sample required"); c.seekToSample(s); return { false, QJsonValue::Null }; }
    if (m == "seekToSeconds") { double s; if (!requireDouble(o, "seconds", s, nullptr)) return makeError(-32602, "seconds required"); c.seekToSeconds(s); return { false, QJsonValue::Null }; }
    if (m == "startRecording") { c.startRecording(); return { false, QJsonValue::Null }; }
    if (m == "stopRecording")  { c.stopRecording();  return { false, QJsonValue::Null }; }
    // transport.record: alias matching the UI semantics (TransportBar's ●
    // button toggles; "R" shortcut triggers). Toggles start/stop based on
    // current state.
    if (m == "record") { if (c.isRecording()) c.stopRecording(); else c.startRecording(); return { false, QJsonValue::Null }; }
    if (m == "isRecording")    { return { false, c.isRecording() }; }
    if (m == "setPunchEnabled") { bool b; if (!requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "enabled required"); c.setPunchEnabled(b); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown transport method: " + m);
}

} // namespace frontend
