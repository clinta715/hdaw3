#include "McpExportTool.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/ProjectPool.h"
#include "../engine/PluginManager.h"
#include "../model/ProjectModel.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPointer>
#include <QMetaObject>

namespace mcp {

static QJsonObject objSchema(const QJsonObject& props, const QJsonArray& required = {})
{
    QJsonObject s{{"type","object"},{"properties", props},{"additionalProperties", false}};
    if (!required.isEmpty()) s["required"] = required;
    return s;
}

void registerExportTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"export_audio",
        "Render the project to an audio file (wav/aiff/flac) asynchronously. The handler returns immediately with \"export started: <path>\"; the render runs on the ExportManager's internal worker thread. Progress is reported via notifications/progress (0.0..1.0); completion via notifications/exportComplete {success, message, outputPath}. Cooperative cancellation: a notifications/cancelled received before the export starts skips it entirely; a notifications/cancelled received while the export is running is picked up by the render thread at the next progress tick (the per-block onProgress callback checks the cancel flag) and aborts the render, deleting the partial file.",
        objSchema({{"outputPath", QJsonObject{{"type","string"}}},
                  {"format",     QJsonObject{{"type","string"},{"enum", QJsonArray{"wav","aiff","flac"}}}},
                  {"start",      QJsonObject{{"type","number"}}},
                  {"end",        QJsonObject{{"type","number"}}},
                  {"sampleRate", QJsonObject{{"type","number"},{"minimum",8000},{"maximum",192000}}},
                  {"bitDepth",   QJsonObject{{"type","integer"},{"enum", QJsonArray{16,24,32}}}},
                  {"trackIds",   QJsonObject{{"type","array"},{"items",QJsonObject{{"type","integer"}}}}},
                  {"dryRun",     QJsonObject{{"type","boolean"}}}},
                 {"outputPath"}),
        [e, &s](const QJsonObject& a) -> McpToolResult {
            QString path = a.value("outputPath").toString();
            if (path.isEmpty()) return McpToolResult::text("outputPath required", true);

            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would export to %1").arg(path));

            if (s.isCancelRequested()) {
                s.resetCancelFlag();
                return McpToolResult::text("export cancelled (flag was already set)", true);
            }

            QString formatStr = a.value("format").toString("wav").toLower();
            HDAW::ExportManager::Format fmt = HDAW::ExportManager::WAV;
            if      (formatStr == "aiff") fmt = HDAW::ExportManager::AIFF;
            else if (formatStr == "flac") fmt = HDAW::ExportManager::FLAC;
            else if (formatStr != "wav")  fmt = HDAW::ExportManager::WAV;

            double sampleRate = a.value("sampleRate").toDouble(48000.0);
            int bitDepth = a.value("bitDepth").toInt(24);

            double startTime = a.value("start").toDouble(0.0);
            double endTime = a.value("end").toDouble(-1.0);
            if (endTime <= 0.0)
                endTime = HDAW::ExportManager::calculateProjectDuration(e->getProjectModel());

            juce::File outFile(juce::String(path.toUtf8().constData()));
            if (outFile.existsAsFile()) outFile.deleteFile();
            double duration = std::max(0.001, endTime - startTime);

            auto& em = e->getMainProcessor()->getExportManager();
            if (em.isExporting()) {
                return McpToolResult::text("export already in progress", true);
            }

            juce::ValueTree projectCopy = e->getProjectModel().getTree().createCopy();
            auto& formatManager = e->getProjectPool().getFormatManager();
            auto* pluginManager = &e->getPluginManager();

            QPointer<McpServer> serverPtr(&s);
            em.onProgress = [serverPtr, &em](float prog) {
                if (serverPtr.isNull()) return;
                if (serverPtr->isCancelRequested()) {
                    em.cancel();
                }
                QJsonObject params{
                    {"progress", static_cast<double>(prog)},
                    {"message", QString("rendering... %1%").arg(static_cast<int>(prog * 100.0))}
                };
                McpNotification n{"notifications/progress", params};
                QString line = serializeNotification(n);
                QMetaObject::invokeMethod(serverPtr, "notifyFromBackground",
                    Qt::QueuedConnection, Q_ARG(QString, line));
            };

            em.onComplete = [serverPtr, &em, path](bool success, const juce::String& message) {
                if (!serverPtr.isNull()) {
                    QJsonObject params{{"success", success},
                                       {"message", QString::fromUtf8(message.toRawUTF8())},
                                       {"outputPath", path}};
                    McpNotification n{"notifications/exportComplete", params};
                    QMetaObject::invokeMethod(serverPtr, "notifyFromBackground",
                        Qt::QueuedConnection, Q_ARG(QString, serializeNotification(n)));
                }
                em.onProgress = nullptr;
                em.onComplete = nullptr;
                if (!serverPtr.isNull())
                    serverPtr->resetCancelFlag();
            };

            if (!em.startExport(projectCopy, formatManager, pluginManager, outFile,
                                sampleRate, startTime, duration, fmt, bitDepth)) {
                em.onProgress = nullptr;
                em.onComplete = nullptr;
                return McpToolResult::text("failed to start export", true);
            }

            {
                QJsonObject params{{"progress", 0.0},{"message","starting render"}};
                McpNotification n{"notifications/progress", params};
                s.notifyFromBackground(serializeNotification(n));
            }

            return McpToolResult::text(QString("export started: %1").arg(path));
        }});
}

} // namespace mcp
