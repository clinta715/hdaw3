#include "FrontendRouter.h"
#include "FrontendServer.h"

#include "../engine/AudioEngine.h"

#include <QJsonArray>
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
        return dispatchProject(engine.getProjectCommands(), m, params);
    }
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
        return dispatchRead(engine.getReadModel(), m, params);
    }
    else if (ns == method::Plugin)      return dispatchPlugin(engine.getPluginService(), m, params, server);
    else if (ns == method::PluginParam) return dispatchPluginParam(engine.getPluginParamService(), m, params);
    else if (ns == method::Audio)       return dispatchAudio(engine, m, params);
    else if (ns == method::Midi)        return dispatchMidi(engine.getMidiService(), m, params);
    else if (ns == method::Export)      return dispatchExport(engine, m, params, server);
    else if (ns == method::Preview)     return dispatchPreview(engine, m, params);
    else if (ns == method::Composition) return dispatchComposition(engine, m, params);
    else if (ns == method::Session)     return dispatchSession(engine.getProjectCommands(), m, params);
    else if (ns == method::Library)     return dispatchLibrary(engine.getFileLibraryManager(), m, params);

    return makeError(-32601, "unknown method namespace: " + ns);
}

} // namespace frontend
