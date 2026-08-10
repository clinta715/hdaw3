#include "Router_Export.h"
#include "RouterHelpers.h"

#include "../FrontendServer.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/ExportManager.h"

#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <string>

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
        int    bitDepth   = optInt(o, "bitDepth", 24, nullptr);
        double startTime  = optDouble(o, "start", 0.0, nullptr);
        double endTime    = optDouble(o, "end", -1.0, nullptr);
        if (endTime <= 0.0)
            endTime = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        double duration = std::max(0.001, endTime - startTime);

        auto* mainProc = engine.getMainProcessor();
        if (mainProc == nullptr)
            return makeError(-32603, "audio engine not initialized");
        auto& em = mainProc->getExportManager();
        if (em.isExporting())
            return makeError(-32603, "export already in progress");

        juce::File outFile(juce::String(path.toUtf8().constData()));
        if (outFile.existsAsFile()) outFile.deleteFile();

        juce::ValueTree projectCopy = engine.getProjectModel().getTree().createCopy();
        auto& formatManager = engine.getProjectPool().getFormatManager();
        auto* pluginManager = &engine.getPluginManager();

        // Progress callback runs on the export worker thread; hop to the main
        // thread before broadcasting so we never touch clients_ off-thread.
        if (server != nullptr) {
            FrontendServer* serverPtr = server;
            em.onProgress = [serverPtr, &em](float prog) {
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
        em.onComplete = [&](bool ok, const juce::String& msg) {
            success = ok;
            message = QString::fromUtf8(msg.toRawUTF8());
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        };

        if (!em.startExport(projectCopy, formatManager, pluginManager, outFile,
                            sampleRate, startTime, duration, fmt, bitDepth)) {
            em.onProgress = nullptr;
            em.onComplete = nullptr;
            return makeError(-32603, "failed to start export");
        }

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
        loop.exec();

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
