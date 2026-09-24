#include "FrontendRouter.h"
#include "FrontendServer.h"

#include "../common/FxPluginIdCheck.h"
#include "../common/FmSynthStateJson.h"
#include "../engine/AudioEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include "router/RouterHelpers.h"
#include "router/Router_Project.h"
#include "router/Router_Session.h"
#include "router/Router_Transport.h"
#include "router/Router_AudioGraph.h"
#include "router/Router_Read.h"
#include "router/Router_Plugin.h"
#include "router/Router_Midi.h"
#include "router/Router_Audio.h"
#include "router/Router_Export.h"
#include "router/Router_Preview.h"
#include "router/Router_Composition.h"
#include "router/Router_Library.h"
#include "router/Router_Sampler.h"
#include "router/Router_Pool.h"
#include "router/Router_PsyFm.h"
#include "router/Router_Rave.h"
#include "router/Router_Device.h"
#include "router/Router_Matrix.h"
#include "router/Router_Tuning.h"
#include "router/Router_Modulation.h"

using namespace frontend::router_helpers;

namespace frontend {

// ---- Public entry point ----------------------------------------------------

DispatchResult dispatch(AudioEngine& engine, const QString& method, const QJsonValue& params,
                        FrontendServer* server) {
    const int dot = method.indexOf('.');
    if (dot < 0) return makeError(-32601, "method must be 'namespace.method': " + method);
    const QString ns = method.left(dot);
    const QString m  = method.mid(dot + 1);

    if (ns == method::Project) {
        if (m == "importMidiFile") {
            const auto o = paramsObject(params);
            std::string filePath;
            if (!requireString(o, "filePath", filePath, nullptr))
                return makeError(-32602, "filePath required");
            int trackIndex = optInt(o, "trackIndex", -1, nullptr);
            auto clipIds = engine.getProjectCommands().importMidiFile(filePath, trackIndex);
            QJsonArray arr;
            for (int id : clipIds) arr.append(id);
            return { false, QJsonObject{ { "clipIds", arr }, { "trackCount", static_cast<int>(clipIds.size()) } } };
        }
        // RPC twin of the MCP add_fx pluginId gate: dispatchProject receives
        // only ProjectCommands& (no plugin/model access), so the check runs
        // here — the same engine-context precedent as importMidiFile above.
        // HDAW::fxPluginIdError is shared with src/mcp/McpTools_FxSlot.cpp,
        // so both surfaces fail with byte-identical text (add_fx parity test).
        if (m == "addFxSlot") {
            const auto pluginId = optString(paramsObject(params), "pluginId", "");
            if (const auto err = HDAW::fxPluginIdError(pluginId, engine.getProjectModel());
                !err.empty())
                return makeError(-32602, QString::fromStdString(err));
        }
        // Two more project routes need engine context and run the SHARED body
        // their MCP twin runs (the same precedent as addFxSlot above):
        //   removeTrack    — the dryRun/force guard MCP remove_track has
        //                    (src/common/TrackRemoveGuard.h)
        //   addTrackWithFx — the composite MCP add_track_with_fx is
        //                    (src/common/AddTrackWithFx.h), pluginId gate included
        if (m == "removeTrack")    return dispatchRemoveTrack(engine, params);
        if (m == "addTrackWithFx") return dispatchAddTrackWithFx(engine, params);
        return dispatchProject(engine.getProjectCommands(),
                               engine.getProjectModel().getTrackListTree(), m, params);
    }
    else if (ns == method::Settings) return dispatchSettings(engine, m, params);
    else if (ns == method::Transport)   return dispatchTransport(engine.getTransportCommands(), m, params);
    else if (ns == method::AudioGraph)  return dispatchAudioGraph(engine.getAudioGraphCommands(), m, params);
    else if (ns == method::Read) {
        // getWaveformPeaks needs AudioEngine (for ProjectPool), not just ReadModel
        if (m == "getWaveformPeaks") {
            const auto o = paramsObject(params);
            int clipId = 0;
            if (!requireInt(o, "clipId", clipId, nullptr))
                return makeError(-32602, "clipId required");
            int numBins = optInt(o, "numBins", 1000, nullptr);

            auto peaks = engine.getWaveformPeaks(clipId, numBins);
            if (!peaks.ok)
                return makeError(peaks.errorCode, QString::fromStdString(peaks.error));

            QJsonArray arr;
            for (double v : peaks.peaks) arr.append(v);
            QJsonObject result{{"peaks", arr},
                               {"sampleRate", peaks.sampleRate},
                               {"numSamples", static_cast<qint64>(peaks.numSamples)}};
            return { false, result };
        }
        // getFmSynthState needs AudioEngine (the live processor's
        // activeVoiceCount for the caller-chosen slot), not just ReadModel —
        // the SAME shared read the MCP fm_synth_get_state tool runs
        // (HDAW::fmSynthStateToolText, src/common/FmSynthStateJson.h).
        // Deliberately NOT read.getFmAnalysis: different sources (the
        // analysis voice count + the first non-bypassed slot's engine
        // algorithm), documented in the ledger note.
        if (m == "getFmSynthState") {
            const auto o = paramsObject(params);
            int ti, si;
            if (!requireInt(o, "trackId", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
                return makeError(-32602, "trackId and slotIndex required");
            bool ok = false;
            const QString text = HDAW::fmSynthStateToolText(engine, ti, si, &ok);
            if (!ok)
                return makeError(-32602, text);
            return { false, QJsonDocument::fromJson(text.toUtf8()).object() };
        }
        return dispatchRead(engine.getReadModel(),
                            engine.getProjectModel().getTrackListTree(),
                            engine.getProjectModel().getBusListTree(), m, params);
    }
    else if (ns == method::Plugin)      return dispatchPlugin(engine.getPluginService(), engine, m, params, server);
    else if (ns == method::PluginParam) return dispatchPluginParam(engine, m, params);
    else if (ns == method::Audio)       return dispatchAudio(engine, m, params);
    else if (ns == method::Midi)        return dispatchMidi(engine.getMidiService(), m, params);
    else if (ns == method::Export)      return dispatchExport(engine, m, params, server);
    else if (ns == method::Preview)     return dispatchPreview(engine, m, params);
    else if (ns == method::Composition) return dispatchComposition(engine, m, params);
    else if (ns == method::Session)     return dispatchSession(engine.getProjectCommands(), engine.getSessionManager(), m, params);
    else if (ns == method::Library)     return dispatchLibrary(engine.getFileLibraryManager(), m, params);
    else if (ns == method::Sampler)     return dispatchSampler(engine, m, params);
    else if (ns == method::Pool)        return dispatchPool(engine.getReadModel(), engine.getProjectPool().getFormatManager(), m, params);
    else if (ns == method::PsyFm)       return dispatchPsyFm(engine, m, params);
    else if (ns == method::Rave)        return dispatchRave(engine, m, params, server);
    else if (ns == method::Device)      return dispatchDevice(engine, m, params);
    else if (ns == method::Matrix)      return dispatchMatrix(engine, m, params);
    else if (ns == method::Tuning)      return dispatchTuning(engine, m, params);
    else if (ns == method::Modulation)  return dispatchModulation(engine, m, params);

    return makeError(-32601, "unknown method namespace: " + ns);
}

} // namespace frontend
