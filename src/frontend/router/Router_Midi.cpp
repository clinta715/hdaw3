#include "Router_Midi.h"
#include "RouterHelpers.h"

#include "../../common/MidiService.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchMidi(MidiService& s, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "getAvailableDevices") {
        QJsonArray arr; for (const auto& d : s.getAvailableDevices()) arr.append(QString::fromStdString(d));
        return { false, arr };
    }
    if (m == "openDevice")  { std::string id; if (!requireString(o, "identifier", id, nullptr)) return makeError(-32602, "identifier required"); return { false, s.openDevice(id) }; }
    if (m == "closeDevice") { s.closeDevice(); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown midi method: " + m);
}

} // namespace frontend
