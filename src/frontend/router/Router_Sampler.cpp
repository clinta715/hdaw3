#include "Router_Sampler.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../common/SamplerStateJson.h"
// The ONE slice-detection payload shaper (bandMasks/strengths/overrideCount
// included) shared with the detect_sampler_slices / recut_sampler_slices tools.
#include "../../common/SamplerSliceShaper.h"
// The ONE sliceMode vocabulary AND the ONE slicePointsOverride element rule
// (common/SamplerSliceModes.h) — the MCP twin refuses with the same bytes.
#include "../../common/SamplerSliceModes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <vector>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchSampler(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& cmds = engine.getAudioEngineCommands();
    // B2: the TRACK_LIST the stable-id track resolution reads (the ONE shared
    // rule in common/StableRefResolve.h; moveTrack/getTrackSends precedent).
    const juce::ValueTree trackList = engine.getProjectModel().getTrackListTree();

    if (m == "setSample") {
        int ti, si; std::string filePath; int root = 60; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "filePath", filePath, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex, filePath required");
        if (o.contains("rootNote") && o.value("rootNote").isDouble())
            root = static_cast<int>(o.value("rootNote").toDouble(60));
        cmds.setSamplerSample(ti, si, filePath, root);
        return { false, QJsonValue::Null };
    }
    if (m == "setParam") {
        int ti, si; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "sampler")
            return makeError(-32602, "slot is not a sampler");

        if (o.contains("property")) {
            std::string prop;
            if (!requireString(o, "property", prop, nullptr) || !o.contains("value"))
                return makeError(-32602, "property and value required");
            // The wire value may arrive as a JSON bool (mono/playReverse) or a
            // number (transpose/baseNote) — QJsonValue's scalar readers return
            // their DEFAULT for the wrong type, so normalize explicitly.
            const QJsonValue v = o.value("value");
            cmds.setSamplerProperty(ti, si, prop,
                v.isBool() ? (v.toBool() ? 1.0 : 0.0) : v.toDouble());
            return { false, QJsonValue::Null };
        }

        int pi; float v;
        if (!requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "value", v, nullptr))
            return makeError(-32602, "paramIndex, value required");
        engine.getProjectCommands().setFxSlotParam(ti, si, pi, v);
        return { false, QJsonValue::Null };
    }
    if (m == "setMode") {
        int ti, si; std::string mode; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "mode", mode, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex, mode required");
        cmds.setSamplerMode(ti, si, mode);
        return { false, QJsonValue::Null };
    }
    if (m == "setSliceMode") {
        int ti, si; std::string sliceMode; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "sliceMode", sliceMode, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex, sliceMode required");
        double grid = optDouble(o, "sliceGrid", 0.25, nullptr);
        double sens = optDouble(o, "sliceSensitivity", 0.5, nullptr);
        // The route reaches the SAME shared validator the engine commands use
        // (common/SamplerSliceModes.h via setSamplerSliceMode) and hands its
        // refusal straight back as -32602 — previously the string was stored
        // verbatim whatever it was.
        const std::string modeError = cmds.setSamplerSliceMode(ti, si, sliceMode, grid, sens);
        if (!modeError.empty())
            return makeError(-32602, QString::fromStdString(modeError));
        return { false, QJsonValue::Null };
    }
    if (m == "detectSlices") {
        int ti, si; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex and slotIndex required");
        // Same slot gate the MCP tool applies, with the same text — the twin
        // surfaces must refuse an unknown slot identically (they used to drift:
        // the tool errored, the route returned an ok:false payload).
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()))
            return makeError(-32602, "slot not found");
        if (fxSlots[si].fxType != "sampler")
            return makeError(-32602, "slot is not a sampler");
        std::string sm = optString(o, "sliceMode", "transient");
        double grid = optDouble(o, "sliceGrid", 0.25, nullptr);
        double sens = optDouble(o, "sliceSensitivity", 0.5, nullptr);
        double fromNorm = optDouble(o, "fromNorm", 0.0, nullptr);
        double toNorm = optDouble(o, "toNorm", 1.0, nullptr);
        auto r = cmds.detectSamplerSlices(ti, si, sm, grid, sens, fromNorm, toNorm);
        // ONE shared shaper (common/SamplerSliceShaper.h) — the route payload is
        // the tool's parsed text by construction.
        return { false, HDAW::samplerSlicePayloadJson(r) };
    }
    if (m == "recutSlices") {
        int ti, si; double fromNorm, toNorm; DispatchResult err;
        // NEW route: its argument vocabulary is the MCP tool's VERBATIM
        // (trackId/trackID — the B2 canonical track pair, StableRefResolve.h),
        // so the twin's names match as the contract requires. The older sampler
        // routes keep their legacy `trackIndex` spelling; this one does not.
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::kTrackRefKeys)
            || !requireInt(o, "slotIndex", si, nullptr))
            return err.isError ? err : makeError(-32602, "trackId and slotIndex required");
        if (!requireDouble(o, "fromNorm", fromNorm, nullptr)
            || !requireDouble(o, "toNorm", toNorm, nullptr))
            return makeError(-32602, "fromNorm and toNorm required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()))
            return makeError(-32602, "slot not found");
        if (fxSlots[si].fxType != "sampler")
            return makeError(-32602, "slot is not a sampler");
        std::string sm = optString(o, "sliceMode", "transient");
        double grid = optDouble(o, "sliceGrid", 0.25, nullptr);
        double sens = optDouble(o, "sliceSensitivity", 0.5, nullptr);
        bool keepOverrides = optBool(o, "keepOverrides", true, nullptr);
        auto r = cmds.recutSamplerSlices(ti, si, sm, grid, sens, fromNorm, toNorm, keepOverrides);
        return { false, HDAW::samplerSlicePayloadJson(r) };
    }
    if (m == "setSliceOverrides") {
        int ti, si; DispatchResult err;
        // Same argument vocabulary as the set_sampler_slice_overrides tool
        // (trackId/trackID + slotIndex + slicePointsOverride) and the same
        // shared HDAW::samplerSliceOverridePayloadJson shaper.
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::kTrackRefKeys)
            || !requireInt(o, "slotIndex", si, nullptr))
            return err.isError ? err : makeError(-32602, "trackId and slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()))
            return makeError(-32602, "slot not found");
        if (fxSlots[si].fxType != "sampler")
            return makeError(-32602, "slot is not a sampler");
        if (!o.value("slicePointsOverride").isArray())
            return makeError(-32602, "slicePointsOverride must be an array");
        const auto arr = o.value("slicePointsOverride").toArray();
        // Same element rule as the set_sampler_slice_overrides tool, from the
        // ONE shared helper (common/SamplerSliceModes.h): every element must be
        // a JSON number, a non-number refuses the WHOLE call with the same
        // bytes the MCP validator emits, and nothing is written. A non-number
        // used to coerce to 0 and be dropped as an implicit endpoint, so
        // `[0.25,"oops"]` SUCCEEDED with one pin.
        std::vector<bool> isNumber;
        isNumber.reserve(static_cast<std::size_t>(arr.size()));
        for (const auto& v : arr)
            isNumber.push_back(v.isDouble());   // true for JSON integers too
        if (const std::string refusal = HDAW::sliceOverrideRefusal(isNumber);
            !refusal.empty())
            return makeError(-32602, QString::fromStdString(refusal));
        std::vector<float> pts;
        pts.reserve(static_cast<std::size_t>(arr.size()));
        for (const auto& v : arr)
            pts.push_back(static_cast<float>(v.toDouble()));
        auto r = cmds.setSamplerSliceOverrides(ti, si, pts);
        return { false, HDAW::samplerSliceOverridePayloadJson(r) };
    }
    if (m == "triggerSlice") {
        int ti, si, idx; float vel = 0.8f; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr)
            || !requireInt(o, "sliceIndex", idx, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex, sliceIndex required");
        double v = optDouble(o, "velocity", 0.8, nullptr);
        auto r = cmds.triggerSamplerSlice(ti, si, idx, static_cast<float>(v));
        return { false, QJsonObject{{"ok", r.ok}, {"totalSlices", r.totalSlices}} };
    }
    if (m == "getState") {
        int ti, si; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex and slotIndex required");
        // B3: ONE shared payload shaper (common/SamplerStateJson.h) so this
        // route, read.getSamplerState and the MCP sampler_get_state emit the
        // identical object by construction.
        return { false,
                 HDAW::samplerStateJson(engine.getReadModel().getSamplerState(ti, si)) };
    }
    if (m == "setKeyRange") {
        int ti, si, keyLow, keyHigh; DispatchResult err;
        if (!trackIndexArg(o, trackList, ti, &err, HDAW::StableRefKeys{"trackIndex", "trackID"})
            || !requireInt(o, "slotIndex", si, nullptr)
            || !requireInt(o, "keyLow", keyLow, nullptr) || !requireInt(o, "keyHigh", keyHigh, nullptr))
            return err.isError ? err : makeError(-32602, "trackIndex, slotIndex, keyLow, keyHigh required");
        cmds.setSamplerKeyRange(ti, si, keyLow, keyHigh);
        return { false, QJsonObject{{"ok", true}, {"keyRangeLow", keyLow}, {"keyRangeHigh", keyHigh}} };
    }

    return makeError(-32601, "unknown sampler method: " + m);
}

} // namespace frontend