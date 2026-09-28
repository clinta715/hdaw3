#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/BatchEnd.h"
#include "../engine/AudioEngine.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

static void registerTransportReadTool(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_transport",
        "Return transport state (bpm, position, isPlaying, isLooping, loopStart, loopEnd, time signature).",
        QJsonObject{{"type","object"}},
        "transport",
        [e](const QJsonObject&) {
            auto tp = e->getProjectModel().getTransportTree();
            QJsonObject o{
                {"bpm", static_cast<double>(e->getProjectModel().getTree().getProperty(IDs::tempo, 120.0))},
                {"position", static_cast<double>(tp.getProperty(IDs::position))},
                {"isPlaying", static_cast<bool>(tp.getProperty(IDs::isPlaying))},
                {"isLooping", static_cast<bool>(tp.getProperty(IDs::isLooping))},
                {"loopStart", static_cast<double>(tp.getProperty(IDs::loopStart))},
                {"loopEnd", static_cast<double>(tp.getProperty(IDs::loopEnd))},
                {"timeSigNumerator", static_cast<int>(tp.getProperty(IDs::timeSigNumerator, 4))},
                {"timeSigDenominator", static_cast<int>(tp.getProperty(IDs::timeSigDenominator, 4))}
            };
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }});
}

static void registerTransportControlTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"transport",
        "Control transport: action in {play,stop,pause,rewind,toggleLoop}; optional loopStart/loopEnd. "
        "Returns {\"ok\": true, \"unit\": <u>} — <u> is the unit the loop window was read in: bare "
        "loopStart/loopEnd default to SECONDS, loopStartBeat/loopEndBeat (or unit:\"beats\") are beats; "
        "with no loop arguments the reply is the bare {\"ok\": true}.",
        objSchema({{"action", QJsonObject{{"type","string"},
            {"enum", QJsonArray{"play","stop","pause","rewind","toggleLoop"}}}},
                  {"loopStart", QJsonObject{{"type","number"}}},
                  {"loopEnd",   QJsonObject{{"type","number"}}}}, {"action"}),
        "transport",
        [e](const QJsonObject& a) -> McpToolResult {
            QString action = a.value("action").toString();
            auto tp = e->getProjectModel().getTransportTree();
            if      (action == "play")  tp.setProperty(IDs::isPlaying, true, nullptr);
            else if (action == "stop")  { tp.setProperty(IDs::isPlaying, false, nullptr);
                                          tp.setProperty(IDs::position, 0.0, nullptr); }
            else if (action == "pause") tp.setProperty(IDs::isPlaying, false, nullptr);
            else if (action == "rewind") tp.setProperty(IDs::position, 0.0, nullptr);
            else if (action == "toggleLoop") {
                bool cur = static_cast<bool>(tp.getProperty(IDs::isLooping));
                tp.setProperty(IDs::isLooping, !cur, nullptr);
            }
            if (a.contains("loopStart")) tp.setProperty(IDs::loopStart, a.value("loopStart").toDouble(), nullptr);
            if (a.contains("loopEnd"))   tp.setProperty(IDs::loopEnd,   a.value("loopEnd").toDouble(), nullptr);
            // S6c payload: a status OBJECT (not bare "ok") so the resolver's
            // `unit` echo has a JSON object to land on when a loop window was
            // given; with no window the dispatch leaves it as the bare ok.
            return McpToolResult::text(QStringLiteral("{\"ok\":true}"));
        }});

    s.registerTool({"seek", "Move the playhead to a position. Returns {\"ok\": true, \"unit\": <u>} "
        "where <u> is the unit the position was read in: bare position defaults to SECONDS, "
        "positionBeat (or unit:\"beats\") are beats.",
        objSchema({{"position", QJsonObject{{"type","number"}}}}, {"position"}),
        "transport",
        [e](const QJsonObject& a) {
            e->getProjectModel().getTransportTree().setProperty(
                IDs::position, a.value("position").toDouble(), nullptr);
            // S6c payload: a status OBJECT (not bare "ok") so the resolver's
            // `unit` echo has a JSON object to land on.
            return McpToolResult::text(QStringLiteral("{\"ok\":true}"));
        }});

    s.registerTool({"undo", "Undo the last N actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        "transport",
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.undo()) break;
            return McpToolResult::text("ok");
        }});

    s.registerTool({"redo", "Redo the last N undone actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        "transport",
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.redo()) break;
            return McpToolResult::text("ok");
        }});
}

// ── Edit batch (S7): begin_batch / end_batch ────────────────────────────────
// The MCP front door for one long-lived engine session: begin_batch opens ONE
// named undo unit, end_batch seals it. While a batch is open, EVERY undo
// boundary a command draws — the command layer's own begin/endTransaction pair,
// and each internal transaction a command opens — is suppressed and the write
// joins the batch (AudioEngineCommands::transactionBoundary), so a single undo
// reverts every edit the batch made.
//
// Engine-global, one-at-a-time, and gated to the stdio transport: the batch
// holds the PROCESS-WIDE undo transaction, so on a shared server another
// client's writes would silently join it. (Relaxing the gate would need a
// per-request ownership token; deliberately not built.)
static void registerBatchTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"begin_batch",
        "Open an EDIT BATCH: every ValueTree write until end_batch — including "
        "commands that open their own internal undo transaction — coalesces into "
        "ONE named undo unit, so a single undo reverts the whole batch. The batch "
        "is ENGINE-GLOBAL and ONE-AT-A-TIME: writers on ANY surface while it is "
        "open join it, so keep batches short, and call end_batch on the failure "
        "path too (a command failure does NOT close the batch). begin_batch is "
        "refused while another batch is open, and on any transport other than "
        "stdio (a batch owns the process-wide undo transaction; the stdio process "
        "is a dedicated engine with this one client). The batch state is reported "
        "by whoami (batchOpen / batchDepth / batchName).",
        objSchema({{"name", QJsonObject{{"type","string"}}}}),
        "transport",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            if (s.transportName() != QLatin1String("stdio"))
                return McpToolResult::text(QString::fromStdString(
                    HDAW::batchStdioRequiredError(s.transportName().toStdString())), true);
            auto& c = e->getProjectCommands();
            const QString name = a.value("name").toString(QStringLiteral("edit"));
            if (!c.beginBatch(name.toStdString()))
                return McpToolResult::text(QString::fromStdString(
                    HDAW::batchAlreadyOpenError(c.batchName())), true);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"end_batch",
        "Close the edit batch opened by begin_batch: seals the batch's writes "
        "into ONE undo unit (a single undo reverts them all) and clears the batch "
        "state. Fails with \"no open batch\" when none is open. ORDERING: the batch "
        "is sealed FIRST, then — only when you ask — the verification runs; a "
        "verification failure NEVER un-seals the batch (the response still carries "
        "the sealed result, with the failure in `verificationError`). Pass "
        "verify:{targets?, outputPath?} to render the whole project and compose the "
        "SAME release verdict render_and_verify/mix_verdict would (cost: one full "
        "render); the result is {ok, sealed, verification:{wavPath, verdict}} when "
        "it succeeds, or {ok, sealed, verificationError} when the render/verdict "
        "fails. With no verify the payload is exactly {ok, sealed}. Call it on the "
        "failure path too — a command failure does not close the batch.",
        objSchema({{"verify", QJsonObject{
                        {"type","object"},
                        {"additionalProperties", false},
                        {"description","Optional verify hook: render the whole project and compose the "
                         "SAME release verdict render_and_verify/mix_verdict would (cost: one full render). "
                         "Keys: targets (B6 expectation object), outputPath (string; omitted => a temp WAV)."},
                        {"properties", QJsonObject{
                            {"outputPath", QJsonObject{{"type","string"}}},
                            {"targets",    QJsonObject{{"type","object"}}}}}}}}),
        "transport",
        [e](const QJsonObject& a) -> McpToolResult {
            // ONE shared implementation with the project.endBatch route
            // (src/common/BatchEnd.h/.cpp): parse → seal → (optional) render + verdict.
            const auto r = HDAW::endBatchAndVerify(*e, a);
            if (!r.ok)
                return McpToolResult::text(r.error, true);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(r.payload).toJson(QJsonDocument::Compact)));
        }});
}

void registerTransportDomain(McpServer& s, AudioEngine* e)
{
    registerTransportReadTool(s, e);
    registerTransportControlTools(s, e);
    registerBatchTools(s, e);
}

} // namespace mcp
