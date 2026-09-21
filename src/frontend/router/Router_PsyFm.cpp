#include "Router_PsyFm.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/AudioEngineCommands.h"
#include "../../engine/MainAudioProcessor.h"
#include "../../engine/TrackFXSlot.h"
#include "../../engine/PsyFmEngine.h"
#include "../../engine/PsyFmState.h"

#include "../../common/PsyFmModMatrixView.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchPsyFm(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    if (m == "getAnalysis") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "psy_fm")
            return makeError(-32602, "slot is not a psy_fm synth");

        QJsonObject result;
        result["live"] = false;
        result["activeVoices"] = 0;
        QJsonArray opLevels;
        for (int op = 0; op < 6; op++)
            opLevels.append(0.0);
        result["opEgLevels"] = opLevels;

        auto* proc = engine.getMainProcessor();
        if (proc) {
            auto* track = proc->getTrack(ti);
            if (track) {
                auto& chain = track->getFXChain();
                if (si < static_cast<int>(chain.size()) && chain[si]) {
                    auto* psyFm = chain[si]->psyFmEngine();
                    if (psyFm) {
                        result["live"] = true;
                        result["activeVoices"] = psyFm->activeVoiceCount();
                        opLevels = QJsonArray();
                        for (int op = 0; op < 6; op++)
                            opLevels.append(static_cast<double>(psyFm->getOpEgLevel(op)));
                        result["opEgLevels"] = opLevels;
                    }
                }
            }
        }
        return { false, result };
    }

    if (m == "loadPreset") {
        int ti, si;
        std::string preset;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "preset", preset, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, and preset required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "psy_fm")
            return makeError(-32602, "slot is not a psy_fm synth");

        bool ok = engine.getAudioEngineCommands().setFxSlotPsyFmPreset(ti, si, preset);
        if (!ok)
            return makeError(-32602, "unknown preset: " + QString::fromStdString(preset));
        return { false, QJsonValue::Null };
    }

    if (m == "setModRoute") {
        int ti, si;
        std::string src, dst;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)
            || !requireString(o, "source", src, nullptr) || !requireString(o, "dest", dst, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, source and dest required");
        if (!o.contains("depth") || !o.value("depth").isDouble())
            return makeError(-32602, "depth (number) required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "psy_fm")
            return makeError(-32602, "slot is not a psy_fm synth");

        engine.getAudioEngineCommands().setFxSlotPsyFmModRoute(
            ti, si, src, dst, static_cast<float>(o.value("depth").toDouble()));
        return { false, QJsonValue::Null };
    }

    if (m == "clearModMatrix") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "psy_fm")
            return makeError(-32602, "slot is not a psy_fm synth");

        engine.getAudioEngineCommands().clearFxSlotPsyFmModRoutes(ti, si);
        return { false, QJsonValue::Null };
    }

    // Same payload as the MCP tool psy_fm_mod_matrix_debug: both delegate to
    // src/common/PsyFmModMatrixView.cpp so they cannot drift.
    if (m == "modMatrixDebug") {
        int ti, si;
        if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackIndex and slotIndex required");
        auto fxSlots = engine.getReadModel().getFxSlots(ti);
        if (si < 0 || si >= static_cast<int>(fxSlots.size()) || fxSlots[si].fxType != "psy_fm")
            return makeError(-32602, "slot is not a psy_fm synth");

        auto view = HDAW::buildPsyFmModMatrixView(
            engine.getProjectModel(), engine.getMainProcessor(), ti, si);
        if (view.isEmpty())
            return makeError(-32602, "slot tree not found");
        return { false, view };
    }

    return makeError(-32601, "unknown psy_fm method: " + m);
}

} // namespace frontend
