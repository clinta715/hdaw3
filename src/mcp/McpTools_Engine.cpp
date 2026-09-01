#include "McpTools_Engine.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "McpTools_Private.h"
#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/MainAudioProcessor.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QDebug>

namespace mcp {

// ============================================================================
// engine_info — read-only binary/process report.
//
// The update flow this supports (see docs/testing-mcp.md):
//   rebuild -> engine_info(buildBinaryPath) to check stale -> engine_restart
//   -> client mcp.reload -> launcher re-copies + size-verifies the fresh
//   binary (lesson 21: a stale binary looks healthy but contains none of the
//   fixes).
// Never mutates anything.
// ============================================================================
void registerEngineInfoTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"engine_info",
        "Read-only engine process/binary report: running binary path, mtime (epoch "
        "seconds), size, app version and export status. Optional buildBinaryPath "
        "compares against a freshly built binary and returns buildMtime/buildSize "
        "plus stale=true when the build tree is newer than the running engine. "
        "Use before engine_restart.",
        objSchema({{"buildBinaryPath", QJsonObject{{"type","string"}}}}),
        "engine",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            QJsonObject out;

            QString running;
            if (QCoreApplication::instance() != nullptr)
                running = QCoreApplication::applicationFilePath();
            out.insert("runningBinaryPath", running);
            const QFileInfo rf(running);
            const QDateTime runningModified = rf.lastModified();
            out.insert("runningMtime", runningModified.isValid()
                ? (double) runningModified.toSecsSinceEpoch() : 0.0);
            out.insert("runningSize", (double) rf.size());

            const QString buildPath = a.value("buildBinaryPath").toString();
            if (!buildPath.isEmpty()) {
                out.insert("buildBinaryPath", buildPath);
                const QFileInfo bf(buildPath);
                if (bf.exists()) {
                    const QDateTime buildModified = bf.lastModified();
                    const double buildM = buildModified.toSecsSinceEpoch();
                    out.insert("buildMtime", buildM);
                    out.insert("buildSize", (double) bf.size());
                    out.insert("stale", runningModified.isValid() && buildM > runningModified.toSecsSinceEpoch());
                } else {
                    out.insert("buildExists", false);
                }
            }

            bool exporting = false;
            if (auto* mainProc = e->getMainProcessor())
                exporting = mainProc->getExportManager().isExporting();
            out.insert("exporting", exporting);

            out.insert("version", s.serverVersion());

            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});
}

// ============================================================================
// engine_restart — intentional engine process exit for binary updates.
//
// Exit code 42 = intentional restart; the launcher (mcp-launch.bat) already
// propagates exit codes and re-copies + size-verifies the fresh binary on
// relaunch. The exit is scheduled 300 ms AFTER the tool response is flushed
// so the MCP client receives the reply before the transport dies. Never
// silently cancels a running export.
// ============================================================================
void registerEngineRestartTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"engine_restart",
        "Intentionally exit the engine process so the launcher re-copies the "
        "freshly built binary and the client reconnects (exit code 42 = "
        "intentional restart; the client must reconnect via mcp.reload / "
        "mcp-launch relaunch). Refuses while an export is rendering unless "
        "force=true — a long render is never silently cancelled.",
        objSchema({{"force", QJsonObject{{"type","boolean"}}}}),
        "engine",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            auto* mainProc = e->getMainProcessor();
            const bool exporting = mainProc != nullptr
                && mainProc->getExportManager().isExporting();
            const bool force = a.value("force").toBool(false);
            if (exporting && !force)
                return McpToolResult::text(
                    "export in progress — call cancel_export first or pass force:true", true);

            // Context object + QPointer guard: if the server is torn down
            // before the timer fires, the exit is dropped instead of firing
            // on a dangling server.
            QPointer<McpServer> serverPtr(&s);
            QTimer::singleShot(300, &s, [serverPtr]() {
                if (serverPtr.isNull()) return;
                qWarning() << "engine_restart: exiting for binary update";
                QCoreApplication::exit(42);
            });

            return McpToolResult::text(
                "restarting engine on updated binary — client must reconnect "
                "(mcp.reload / mcp-launch relaunch); launcher re-copies and "
                "size-verifies the fresh binary");
        }});
}

} // namespace mcp
