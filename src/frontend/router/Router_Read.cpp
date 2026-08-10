#include "Router_Read.h"
#include "RouterHelpers.h"

#include "../../common/ReadModel.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchRead(ReadModel& r, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "snapshot")         { return { false, toJson(r.snapshot()) }; }
    if (m == "getTrackCount")    { return { false, r.getTrackCount() }; }
    if (m == "getTrack")         { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, toJson(r.getTrack(i)) }; }
    if (m == "getClip")          { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); return { false, toJson(r.getClip(i)) }; }
    if (m == "getNotes") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        QJsonArray arr; for (const auto& n : r.getNotes(i)) arr.append(toJson(n));
        return { false, arr };
    }
    if (m == "getCcPoints") {
        int i, cc; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "controllerNumber", cc, nullptr)) return makeError(-32602, "clipId and controllerNumber required");
        QJsonArray arr; for (const auto& p : r.getCcPoints(i, cc)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getClipGainEnvelope") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        QJsonArray arr; for (const auto& p : r.getClipGainEnvelope(i)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getTransport")     { return { false, toJson(r.getTransport()) }; }
    if (m == "getScaleRoot")     { return { false, r.getScaleRoot() }; }
    if (m == "getScaleMode")     { return { false, r.getScaleMode() }; }
    if (m == "getFxSlots") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& f : r.getFxSlots(i)) arr.append(toJson(f));
        return { false, arr };
    }
    if (m == "getMidiFxSlots") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& f : r.getMidiFxSlots(i)) arr.append(toJson(f));
        return { false, arr };
    }
    if (m == "getAutomationLanes") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& l : r.getAutomationLanes(i)) arr.append(toJson(l));
        return { false, arr };
    }
    if (m == "getAutomationPoints") {
        int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required");
        QJsonArray arr; for (const auto& p : r.getAutomationPoints(i, lane)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getMarkers") {
        QJsonArray arr; for (const auto& mk : r.getMarkers()) arr.append(toJson(mk));
        return { false, arr };
    }
    if (m == "getArrangerRegions") {
        QJsonArray arr;
        for (const auto& rs : r.getArrangerRegions())
            arr.append(toJson(rs));
        return { false, arr };
    }
    if (m == "getArrangerChains") {
        QJsonArray arr;
        for (const auto& cs : r.getArrangerChains())
            arr.append(toJson(cs));
        return { false, arr };
    }
    if (m == "getTempoPoints") {
        QJsonArray arr; for (const auto& t : r.getTempoPoints()) arr.append(toJson(t));
        return { false, arr };
    }
    if (m == "getInternalFxParams") {
        int ti, si; if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)) return makeError(-32602, "trackIndex and slotIndex required");
        QJsonArray arr; for (const auto& p : r.getInternalFxParams(ti, si)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getAutomatableParams") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& a : r.getAutomatableParams(i)) arr.append(toJson(a));
        return { false, arr };
    }
    if (m == "getModulationLfos") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& l : r.getModulationLfos(i)) arr.append(toJson(l));
        return { false, arr };
    }
    if (m == "getTrackMeter")   { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, toJson(r.getTrackMeter(i)) }; }
    if (m == "getMasterMeter")  { return { false, toJson(r.getMasterMeter()) }; }
    if (m == "getTrackSends") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& s : r.getTrackSends(i)) arr.append(toJson(s));
        return { false, arr };
    }
    if (m == "isDirty")         { return { false, r.isDirty() }; }
    return makeError(-32601, "unknown read method: " + m);
}

} // namespace frontend
