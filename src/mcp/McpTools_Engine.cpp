#include "McpTools_Engine.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "McpTools_Private.h"
#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/MainAudioProcessor.h"
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/ToolUnits.h"
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

// ============================================================================
// tool_help — the ONE-CALL form of the tools/list entry for one tool
// (docs/plans/2026-09-28-agent-mechanization.md §1, S1b).
//
// Payload == the tools/list entry verbatim: {name, description, category,
// inputSchema (x-unit annotations + the standard `examples` array included)}.
// Both read the SAME stored McpToolDef (registerTool enriched the schema once
// at registration, examples included), so the two can never drift — which is
// what the ToolRegistry test asserts by construction. There is NO ad-hoc
// top-level `example` key (ITEM B: clean cutover to inputSchema.examples).
//
// MCP_ONLY: it describes the MCP tool registry; the RPC surface has no such
// registry (the engine_info / whoami precedent). An unknown name is refused with
// the shared unknownToolHelpText.
// ============================================================================
void registerToolHelpTool(McpServer& s) {
    s.registerTool({"tool_help",
        "Return the tools/list entry for ONE tool by name: {name, description, "
        "category, inputSchema (with x-unit unit annotations and the standard "
        "`examples` array)} — exactly "
        "what tools/list reports for it, so an agent can read one tool's contract "
        "in one call instead of scanning the whole list. An unknown name is "
        "refused with \"unknown tool <name>\".",
        objSchema({{"name", QJsonObject{{"type","string"}}}}, {"name"}),
        "engine",
        [&s](const QJsonObject& a) -> McpToolResult {
            const QString name = a.value("name").toString();
            const auto it = s.tools().find(name);
            if (it == s.tools().constEnd())
                return McpToolResult::text(unknownToolHelpText(name), true);
            const McpToolDef& t = it.value();
            const QJsonObject entry{
                {"name", t.name},
                {"description", t.description},
                {"inputSchema", t.inputSchema},
                {"category", t.category}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(entry).toJson(QJsonDocument::Compact)));
        }});
}

// The ONE field builder behind engine_info AND whoami (see McpTools_Engine.h).
// whoami's payload is this object PLUS session context, so the two can never
// disagree about the engine they describe.
QJsonObject buildEngineInfoPayload(McpServer& s, AudioEngine* e, const QJsonObject& a)
{
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
    if (e) {
        if (auto* mainProc = e->getMainProcessor())
            exporting = mainProc->getExportManager().isExporting();
    }
    out.insert("exporting", exporting);

    out.insert("version", s.serverVersion());

    // Source-vs-binary version guard: when the caller states the
    // version it expects (e.g. the source tree's CMake project
    // version), report whether the RUNNING binary actually matches.
    // These three keys appear ONLY when expectedVersion is passed, so
    // bare calls stay backward compatible.
    const QString expectedVersion = a.value("expectedVersion").toString();
    if (a.contains("expectedVersion") && !expectedVersion.isEmpty()) {
        out.insert("expectedVersion", expectedVersion);
        const QString actualVersion = s.serverVersion();
        out.insert("actualVersion", actualVersion);
        out.insert("versionMismatch", expectedVersion != actualVersion);
    }

    return out;
}

void registerEngineInfoTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"engine_info",
        "Read-only engine process/binary report: running binary path, mtime (epoch "
        "seconds), size, app version and export status. Optional buildBinaryPath "
        "compares against a freshly built binary and returns buildMtime/buildSize "
        "plus stale=true when the build tree is newer than the running engine. "
        "Optional expectedVersion cross-checks the RUNNING binary's actual "
        "version (actualVersion) and reports versionMismatch=true when they "
        "differ - the source-vs-binary guard so a stale engine built before a "
        "version bump is detected instead of silently running without the "
        "expected tools. Use before engine_restart.",
        objSchema({{"buildBinaryPath",  QJsonObject{{"type","string"}}},
                   {"expectedVersion", QJsonObject{{"type","string"}}}}),
        "engine",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            const QJsonObject out = buildEngineInfoPayload(s, e, a);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});
}

// ============================================================================
// whoami — ONE call answering "what is running?" (docs/plans/
// 2026-09-28-agent-mechanization.md §6). Pure introspection: the engine_info
// report (same builder, so no drift) plus the session context — transport,
// loaded project path/name, track/clip counts.
// ============================================================================
void registerWhoamiTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"whoami",
        "One call answering \"what is running?\": the engine_info report (running "
        "binary path/mtime/size, version, exporting; plus the buildBinaryPath "
        "stale check and the expectedVersion cross-check when those optional "
        "args are given) PLUS the session context: transport (stdio/http), the "
        "project file path loaded/saved this session (\"\" when none), the "
        "project name, its track and clip counts, and the S7 edit-batch state "
        "(batchOpen / batchDepth / batchName — see begin_batch / end_batch). "
        "Superset of engine_info - every engine_info key appears here with the "
        "same value.",
        objSchema({{"buildBinaryPath",  QJsonObject{{"type","string"}}},
                   {"expectedVersion", QJsonObject{{"type","string"}}}}),
        "engine",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            QJsonObject out = buildEngineInfoPayload(s, e, a);

            out.insert("transport", s.transportName());

            const std::string path = e->getProjectFilePath();
            out.insert("projectPath", QString::fromStdString(path));

            auto& m = e->getProjectModel();
            out.insert("projectName", jstr(m.getTree().getProperty(IDs::name).toString()));

            const juce::ValueTree tl = m.getTrackListTree();
            const int tracks = tl.getNumChildren();
            int clips = 0;
            for (int i = 0; i < tracks; ++i)
                clips += tl.getChild(i).getChildWithName(IDs::CLIP_LIST).getNumChildren();
            out.insert("trackCount", tracks);
            out.insert("clipCount", clips);

            // S7 edit-batch state (begin_batch / end_batch): whether a batch is
            // open, its depth (0/1 — a batch is a flag, not a counter), and its
            // name ("" when none). Null-safe like getProjectFilePath: an
            // introspection call before initialize() reports "no batch".
            out.insert("batchOpen", e->batchActive());
            out.insert("batchDepth", e->batchActive() ? 1 : 0);
            out.insert("batchName", QString::fromStdString(e->batchName()));

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
