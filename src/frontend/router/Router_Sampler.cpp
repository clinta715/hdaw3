#include "Router_Sampler.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchSampler(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& cmds = engine.getAudioEngineCommands();

    if (m == "setSample") {
        int ti, si; std::string filePath; int root = 60;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, filePath required");
        if (o.contains("rootNote") && o.value("rootNote").isDouble())
            root = static_cast<int>(o.value("rootNote").toDouble(60));
        cmds.setSamplerSample(ti, si, filePath, root);
        return { false, QJsonValue::Null };
    }
    if (m == "setParam") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex, slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "sampler")
            return makeError(-32602, "slot is not a sampler");

        if (o.contains("property")) {
            std::string prop;
            if (!requireString(o, "property", prop, nullptr) || !o.contains("value"))
                return makeError(-32602, "property and value required");
            cmds.setSamplerProperty(ti, si, prop, o.value("value").toBool());
            return { false, QJsonValue::Null };
        }

        int pi; float v;
        if (!requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "value", v, nullptr))
            return makeError(-32602, "paramIndex, value required");
        engine.getProjectCommands().setFxSlotParam(ti, si, pi, v);
        return { false, QJsonValue::Null };
    }
    if (m == "setMode") {
        int ti, si; std::string mode;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "mode", mode, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, mode required");
        cmds.setSamplerMode(ti, si, mode);
        return { false, QJsonValue::Null };
    }
    if (m == "setSliceMode") {
        int ti, si; std::string sliceMode;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "sliceMode", sliceMode, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, sliceMode required");
        double grid = optDouble(o, "sliceGrid", 0.25, nullptr);
        double sens = optDouble(o, "sliceSensitivity", 0.5, nullptr);
        cmds.setSamplerSliceMode(ti, si, sliceMode, grid, sens);
        return { false, QJsonValue::Null };
    }
    if (m == "detectSlices") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        std::string sm = optString(o, "sliceMode", "transient");
        double grid = optDouble(o, "sliceGrid", 0.25, nullptr);
        double sens = optDouble(o, "sliceSensitivity", 0.5, nullptr);
        auto r = cmds.detectSamplerSlices(ti, si, sm, grid, sens);
        QJsonArray pts;
        for (float p : r.slicePoints)
            pts.append(static_cast<double>(p));
        return { false, QJsonObject{{"ok", r.ok},
                                    {"totalSlices", r.totalSlices},
                                    {"slicePoints", pts}} };
    }
    if (m == "triggerSlice") {
        int ti, si, idx; float vel = 0.8f;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireInt(o, "sliceIndex", idx, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, sliceIndex required");
        double v = optDouble(o, "velocity", 0.8, nullptr);
        auto r = cmds.triggerSamplerSlice(ti, si, idx, static_cast<float>(v));
        return { false, QJsonObject{{"ok", r.ok}, {"totalSlices", r.totalSlices}} };
    }
    if (m == "getState") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto s = engine.getReadModel().getSamplerState(ti, si);
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
        obj["sliceMode"] = QString::fromStdString(s.sliceMode);
        obj["sliceGrid"] = static_cast<double>(s.sliceGrid);
        obj["sliceSensitivity"] = static_cast<double>(s.sliceSensitivity);
        QJsonArray slicePoints;
        for (float p : s.slicePoints)
            slicePoints.append(static_cast<double>(p));
        obj["slicePoints"] = slicePoints;
        return { false, obj };
    }

    return makeError(-32601, "unknown sampler method: " + m);
}

} // namespace frontend