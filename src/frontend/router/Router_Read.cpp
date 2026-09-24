#include "Router_Read.h"
#include "RouterHelpers.h"

#include "../../common/ReadModel.h"
#include "../../common/BusInfo.h"
#include "../../common/SendJson.h"
// Shared shaping for the two 2026-09-24 read routes — the SAME entry points
// the MCP twins call (get_master_fx_params / list_clip_takes).
#include "../../common/MasterFxAccess.h"
#include "../../common/ClipTakesJson.h"
#include "../../model/ProjectModel.h"   // IDs:: namespace (MASTER_FX)

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchRead(ReadModel& r, const juce::ValueTree& trackList,
                            const juce::ValueTree& busList,
                            const QString& m, const QJsonValue& params) {
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
        // The SAME shaping list_fx emits (common/SendJson.h) — the router hands the
        // client the parsed structure rather than a string-quoted document.
        return { false, QJsonDocument::fromJson(
            QString::fromStdString(HDAW::shapeFxSlotsJson(r.getFxSlots(i))).toUtf8()).array() };
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
    if (m == "getFmAnalysis")   { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, toJson(r.getFmAnalysis(i)) }; }
    if (m == "getTrackSends") {
        // B2: `trackId` (index) or `trackID` (stable id, design B1) — the ONE
        // shared rule (common/StableRefResolve.h), so this route and the MCP
        // get_track_sends resolve, succeed and FAIL with the same text.
        int i; DispatchResult err;
        if (!trackIndexArg(o, trackList, i, &err)) return err;
        // The SAME shaping get_track_sends emits (common/SendJson.h).
        return { false, QJsonDocument::fromJson(
            QString::fromStdString(HDAW::shapeSendsJson(r.getTrackSends(i))).toUtf8()).array() };
    }

    // --- MASTER-bus FX + clip takes (2026-09-24 parity wave) ---
    // Both routes are thin hand-offs to the ONE shared shaping the MCP twins
    // (get_master_fx_params / list_clip_takes) run:
    //   getMasterFxParams — HDAW::masterFxParamsToolText (common/MasterFxAccess.h)
    //   getClipTakes      — HDAW::clipTakesToolText      (common/ClipTakesJson.h)
    // so the payloads match the tools' text byte-for-byte (parsed into the
    // reply, listBusFxParams precedent). getMasterFxParams reads the
    // MASTER_FX node — the TRACK_LIST's sibling under the project root, the
    // same hop dispatchProject's master-FX routes take.
    if (m == "getMasterFxParams") {
        const juce::ValueTree masterFx = trackList.getParent().getChildWithName(IDs::MASTER_FX);
        bool ok = false;
        const QString text = HDAW::masterFxParamsToolText(masterFx, &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, QJsonDocument::fromJson(text.toUtf8()).object() };
    }
    if (m == "getClipTakes") {
        int clipId;
        if (!requireInt(o, "clipId", clipId, nullptr))
            return makeError(-32602, "clipId required");
        bool ok = false;
        const QString text = HDAW::clipTakesToolText(trackList, clipId, &ok);
        if (!ok)
            return makeError(-32602, text);
        // The takes payload is a JSON ARRAY (the tool's shape) — parse to
        // .array(), not .object() (an object() wrap of an array document
        // silently yields {}).
        return { false, QJsonDocument::fromJson(text.toUtf8()).array() };
    }

    // --- Buses (docs/plans/2026-09-22-bus-fx-params.md, slice C) ---
    // common/BusInfo.h holds the ONE shaping and the ONE read-side validation, so these
    // return exactly what the MCP twins (list_buses / list_bus_fx_params in
    // McpTools_Send.cpp) return — including the failure text. The shaping is a compact
    // JSON document (what an MCP text payload is); the router hands the client the
    // parsed structure rather than a string-quoted document.
    if (m == "listBuses") {
        return { false, QJsonDocument::fromJson(
            QString::fromStdString(HDAW::shapeBusesJson(HDAW::readBuses(busList))).toUtf8()).array() };
    }
    if (m == "listBusFxParams") {
        int id; if (!requireInt(o, "busID", id, nullptr)) return makeError(-32602, "busID required");
        const auto target = HDAW::findFxBusForRead(busList, id);
        if (!target.bus.isValid())
            return makeError(-32602, QString::fromStdString(target.error));
        return { false, QJsonDocument::fromJson(
            QString::fromStdString(HDAW::shapeBusFxParamsJson(target.bus)).toUtf8()).object() };
    }
    if (m == "isDirty")         { return { false, r.isDirty() }; }
    if (m == "sampler.getState") {
        int ti, si; if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)) return makeError(-32602, "trackIndex and slotIndex required");
        auto s = r.getSamplerState(ti, si);
        QJsonObject obj;
        obj["sampleFile"] = QString::fromStdString(s.sampleFile);
        obj["mode"] = QString::fromStdString(s.mode);
        obj["rootNote"] = s.rootNote;
        obj["transpose"] = s.transpose;
        obj["mono"] = s.mono;
        obj["playReverse"] = s.playReverse;
        QJsonObject env;
        env["attack"] = static_cast<double>(s.attack);
        env["hold"] = static_cast<double>(s.hold);
        env["decay"] = static_cast<double>(s.decay);
        env["sustain"] = static_cast<double>(s.sustain);
        env["release"] = static_cast<double>(s.release);
        obj["envelope"] = env;
        obj["sampleStart"] = static_cast<double>(s.sampleStart);
        obj["sampleEnd"] = static_cast<double>(s.sampleEnd);
        obj["glide"] = static_cast<double>(s.glide);
        obj["hasSound"] = s.hasSound;
        obj["activeVoices"] = s.activeVoices;
        obj["keyRangeLow"] = s.keyRangeLow;
        obj["keyRangeHigh"] = s.keyRangeHigh;
        return { false, obj };
    }
    return makeError(-32601, "unknown read method: " + m);
}

} // namespace frontend
