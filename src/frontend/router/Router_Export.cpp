#include "Router_Export.h"
#include "RouterHelpers.h"

#include "../FrontendServer.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/ExportManager.h"
#include "../../common/RenderLaunch.h"
#include "../../common/RenderToolArgs.h"
#include "../../common/RenderAndVerify.h"
#include "../../common/MixVerdict.h"
#include "../../model/ProjectModel.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace frontend::router_helpers;

namespace frontend {

// Render the project to an audio file. The ExportManager runs the render on
// its own worker thread; this handler spins a local QEventLoop (processing
// queued cross-thread invocations) until onComplete fires, broadcasting
// notify.exportProgress along the way. Mirrors the MCP export_audio tool
// (src/mcp/McpExportTool.cpp) but uses the frontend's own
// notify.exportProgress channel (not notifications/progress).
DispatchResult dispatchExport(AudioEngine& engine, const QString& m,
                              const QJsonValue& params, FrontendServer* server) {
    const auto o = paramsObject(params);

    if (m == "temporaryRender") {
        // Convenience route for the Compose tab energy arc: render the whole
        // project into the system temp dir and hand the path back (the caller
        // then feeds it to audio.mixReport { fromPlan: true }). Deliberately
        // forwards to the audio handler — one pipeline, progress
        // notifications and cancel semantics included.
        QJsonObject fwd = o;
        const juce::File outFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile(juce::String("hdaw_energy_")
                                                     + juce::String(juce::Time::currentTimeMillis())
                                                     + ".wav");
        fwd["outputPath"] = QString::fromUtf8(outFile.getFullPathName().toRawUTF8());
        fwd["format"] = "wav";
        return dispatchExport(engine, "audio", fwd, server);
    }

    if (m == "audio") {
        std::string pathStr;
        if (!requireString(o, "outputPath", pathStr, nullptr))
            return makeError(-32602, "outputPath required");
        QString path = QString::fromStdString(pathStr);
        if (path.isEmpty())
            return makeError(-32602, "outputPath required");

        QString formatStr = optString(o, "format", "wav").c_str();
        formatStr = formatStr.toLower();
        HDAW::ExportManager::Format fmt = HDAW::ExportManager::WAV;
        if      (formatStr == "aiff") fmt = HDAW::ExportManager::AIFF;
        else if (formatStr == "flac") fmt = HDAW::ExportManager::FLAC;

        double sampleRate = optDouble(o, "sampleRate", 48000.0, nullptr);
        DispatchResult intErr;
        int    bitDepth;
        if (!optInt(o, "bitDepth", bitDepth, 24, &intErr)) return intErr;
        double startTime  = optDouble(o, "start", 0.0, nullptr);
        double endTime    = optDouble(o, "end", -1.0, nullptr);
        if (endTime <= 0.0)
            endTime = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        double duration = std::max(0.001, endTime - startTime);

        auto* mainProc = engine.getMainProcessor();
        if (mainProc == nullptr)
            return makeError(-32603, "audio engine not initialized");
        auto& em = mainProc->getExportManager();
        if (em.isExporting()) {
            bool queue = optBool(o, "queue", false, nullptr);
            if (queue) {
                // Poll with event processing so progress notifications and
                // cancel requests keep flowing; bounded 120s wait.
                const auto deadline = juce::Time::getMillisecondCounter() + 120000u;
                while (em.isExporting()) {
                    if (juce::Time::getMillisecondCounter() >= deadline)
                        return makeError(-32603, "timeout waiting for previous export to finish (queue expired or cancelled)");
                    // Keep Qt event loop alive so cancel/progress still dispatch.
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    juce::Thread::sleep(10);
                }
            } else {
                return makeError(-32603, "export already in progress — retry with queue=true to wait");
            }
        }

        // Optional track filter (mirrors the MCP export_audio tool): render
        // only the requested track indices. Applied to the offline copy by the
        // shared launcher (HDAW::applyTrackFilterToRenderCopy) — mute + zero
        // volume on the rest, solo cleared — so the live project and routing
        // graph are untouched.
        std::vector<int> trackIds;
        for (const auto& v : o.value("trackIds").toArray())
            trackIds.push_back(v.toInt(-1));

        // Progress callback runs on the export worker thread; hop to the main
        // thread before broadcasting so we never touch clients_ off-thread.
        std::function<void(float)> onProgress;
        if (server != nullptr) {
            FrontendServer* serverPtr = server;
            onProgress = [serverPtr](float prog) {
                QJsonObject payload{
                    { "progress", static_cast<double>(prog) },
                    { "message", QString("rendering... %1%").arg(static_cast<int>(prog * 100.0)) },
                };
                serverPtr->broadcastNotificationFromAnyThread(notify::ExportProgress, payload);
            };
        }

        // The export worker runs on its own thread and hops back here via
        // QMetaObject::invokeMethod(..., QueuedConnection) for progress and
        // completion. Blocking on doneFuture.get() would stall the Qt event
        // loop, which (a) prevents the progress hops from firing until after
        // the export finishes, defeating the live progress notifications,
        // and (b) prevents aboutToQuit from firing, so a Ctrl-C during export
        // hangs the process. Spin a local event loop instead so queued
        // invocations are processed; quit when onComplete fires.
        QEventLoop loop;
        bool success = false;
        QString message;
        auto onComplete = [&](bool ok, const juce::String& msg) {
            success = ok;
            message = QString::fromUtf8(msg.toRawUTF8());
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        };

        // The render LAUNCH — tree copy (lesson 27), `trackIds` filter, and
        // ExportManager::startExport — is the ONE shared helper the MCP
        // export_audio tool and the waiting verify_window / render_and_verify
        // paths also use (common/RenderLaunch.h), so the surfaces cannot drift.
        auto launch = HDAW::launchProjectRender(engine, path, sampleRate, bitDepth, fmt,
                                               startTime, duration, trackIds,
                                               std::move(onProgress), onComplete);
        if (!launch.started)
            return makeError(-32603, launch.error);

        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ExportProgress,
                QJsonObject{ { "progress", 0.0 }, { "message", "starting render" } });
        }

        // Process events until the worker's onComplete hops back here and
        // quits the loop. This keeps progress notifications streaming live
        // and lets aboutToQuit fire if the app is asked to exit mid-export.
        // This dispatch call is still the only one in flight — every other
        // WebSocket request is queued behind it — but the event loop now
        // turns over, so the UI and other Qt timers keep working.
        //
        // Socket notifiers are EXCLUDED from this nested loop on purpose: a
        // client disconnect processed re-entrantly inside it frees the
        // accepted QTcpSocket (QWebSocketServerPrivate::onSocketDisconnected
        // deleteLater) while QWebSocketPrivate::processData is mid-iteration,
        // which NULL-derefs in Qt 6.11.1 (qwebsocket_p.cpp:1374). Deferring
        // socket events to the outer loop tears the connection down cleanly
        // after the handler unwinds. Progress notifications are queued
        // invocations, not socket events, so they still stream live.
        loop.exec(QEventLoop::ExcludeSocketNotifiers);

        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ExportProgress,
                QJsonObject{ { "progress", success ? 1.0 : 0.0 }, { "message", message } });
        }

        em.onProgress = nullptr;
        em.onComplete = nullptr;

        if (!success)
            return makeError(-32603, QString("export failed: %1").arg(message));

        return { false, QJsonObject{
            { "outputPath", path },
            { "message", message },
        } };
    }

    if (m == "renderAndVerify") {
        // MCP twin of render_and_verify (McpExportTool.cpp): render the WHOLE
        // project through the SAME shared launcher (common/RenderLaunch.h),
        // WAIT for it, then build the release verdict with the existing
        // buildMixVerdict (src/common/MixVerdict.h). Payload {wavPath, verdict};
        // refusals are byte-identical to the tool's because both surfaces run
        // the same launcher and the same verdict composer.
        // The SAME argument parser the tool runs (common/RenderToolArgs.h), so
        // a missing/ill-typed outputPath fails with the MCP validator's exact
        // bytes instead of a hand-rolled message.
        HDAW::RenderAndVerifyArgs parsed;
        QString argError;
        if (!HDAW::parseRenderAndVerifyArgs(o, parsed, argError))
            return makeError(-32602, argError);
        // ONE shared implementation for both surfaces (common/RenderAndVerify.h):
        // full render through the shared launcher, then the existing verdict. The
        // verdict inputs resolve through the SAME helper mix_verdict uses, so the
        // verdict equals mix_verdict's for the produced file.
        const auto r = HDAW::renderAndVerify(engine, parsed.outputPath, parsed.targets,
                                             parsed.timeoutMs, parsed.fromPlan,
                                             parsed.dropBuildRatio, parsed.introSeconds);
        if (!r.ok)
            return makeError(r.errorCode, r.error);
        return { false, r.payload };
    }

    if (m == "isExporting") {
        auto* mainProc = engine.getMainProcessor();
        bool exporting = (mainProc != nullptr) && mainProc->getExportManager().isExporting();
        return { false, exporting };
    }
    if (m == "cancel") {
        auto* mainProc = engine.getMainProcessor();
        if (mainProc != nullptr && mainProc->getExportManager().isExporting())
            mainProc->getExportManager().cancel();
        return { false, QJsonValue::Null };
    }

    return makeError(-32601, "unknown export method: " + m);
}

} // namespace frontend
