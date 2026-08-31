#include "Router_PsyFm.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/AudioEngineCommands.h"
#include "../../engine/MainAudioProcessor.h"
#include "../../engine/TrackFXSlot.h"
#include "../../engine/PsyFmEngine.h"
#include "../../engine/PsyFmState.h"

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

    return makeError(-32601, "unknown psy_fm method: " + m);
}

} // namespace frontend
