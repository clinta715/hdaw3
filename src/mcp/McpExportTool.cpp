#include "McpExportTool.h"
#include "McpJobs.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/ProjectPool.h"
#include "../engine/PluginManager.h"
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/RenderLaunch.h"
#include "../common/VerifyWindowJson.h"
#include "../common/RenderToolArgs.h"
#include "../common/RenderAndVerify.h"
#include "../common/MixVerdict.h"
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
        "Render the project to an audio file (wav/aiff/flac) asynchronously. The handler returns immediately with \"export started: <path>\"; the render runs on the ExportManager's internal worker thread. Progress is reported via notifications/progress (0.0..1.0); completion via notifications/exportComplete {success, message, outputPath}. Cancellation is explicit via the cancel_export tool, which aborts an in-progress render and deletes the partial file. Optional queue=true waits up to 120s for a prior export to finish instead of immediately rejecting (CAS-guarded; still fails if timeout or cancelled). Optional wait=true blocks until THIS render finishes (bounded by waitTimeoutMs, default 600000, clamped to 1000..1800000) and returns \"export complete: <path>\" on success or a timeout error if the render has not finished within the budget.",
        objSchema({{"outputPath", QJsonObject{{"type","string"}}},
                  {"format",     QJsonObject{{"type","string"},{"enum", QJsonArray{"wav","aiff","flac"}}}},
                  {"start",      QJsonObject{{"type","number"}}},
                  {"end",        QJsonObject{{"type","number"}}},
                  {"sampleRate", QJsonObject{{"type","number"},{"minimum",8000},{"maximum",192000}}},
                  {"bitDepth",   QJsonObject{{"type","integer"},{"enum", QJsonArray{16,24,32}}}},
                  {"trackIds",   QJsonObject{{"type","array"},{"items",QJsonObject{{"type","integer"}}}}},
                  {"dryRun",     QJsonObject{{"type","boolean"}}},
                  {"queue",      QJsonObject{{"type","boolean"}}},
                  {"wait",         QJsonObject{{"type","boolean"}}},
                  {"waitTimeoutMs", QJsonObject{{"type","integer"},{"default",600000},{"minimum",1000},{"maximum",1800000}}}},
                 {"outputPath"}),
        "export",
        [e, &s](const QJsonObject& a) -> McpToolResult {
            QString path = a.value("outputPath").toString();
            if (path.isEmpty()) return McpToolResult::text("outputPath required", true);

            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would export to %1").arg(path));

            // The MCP client re-sends notifications/cancelled routinely between
            // tool calls, so a pre-set flag must not block this export: refuse
            // on a stale flag and export is permanently locked out. Consume the
            // flag and proceed. The flag is no longer polled for mid-render
            // cancel — cancellation is explicit via the cancel_export tool.
            s.resetCancelFlag();

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

            double duration = std::max(0.001, endTime - startTime);

            auto& em = e->getMainProcessor()->getExportManager();
            if (em.isExporting()) {
                bool queue = a.value("queue").toBool(false);
                if (queue) {
                    if (!em.waitForIdle(120000))
                        return McpToolResult::text("timeout waiting for previous export to finish (queue expired or cancelled)", true);
                } else {
                    return McpToolResult::text(
                        "another export is in progress — use cancel_export to abort it first or retry with queue=true to wait", true);
                }
            }

            // Optional track filter: render only the requested track indices.
            // Applied to the offline copy (mute + zero volume on the rest,
            // solo cleared) so a selected track always plays regardless of
            // project solo state and excluded tracks never contribute.
            std::vector<int> trackIds;
            for (const auto& v : a.value("trackIds").toArray())
                trackIds.push_back(v.toInt(-1));

            QPointer<McpServer> serverPtr(&s);
            auto launch = HDAW::launchProjectRender(
                *e, path, sampleRate, bitDepth, fmt, startTime, duration, trackIds,
                [serverPtr](float prog) {
                    if (serverPtr.isNull()) return;
                    QJsonObject params{
                        {"progress", static_cast<double>(prog)},
                        {"message", QString("rendering... %1%").arg(static_cast<int>(prog * 100.0))}
                    };
                    McpNotification n{"notifications/progress", params};
                    QString line = serializeNotification(n);
                    QMetaObject::invokeMethod(serverPtr, "notifyFromBackground",
                        Qt::QueuedConnection, Q_ARG(QString, line));
                },
                [serverPtr, &em, path](bool success, const juce::String& message) {
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
                });
            if (!launch.started)
                return McpToolResult::text(launch.error, true);

            {
                QJsonObject params{{"progress", 0.0},{"message","starting render"}};
                McpNotification n{"notifications/progress", params};
                s.notifyFromBackground(serializeNotification(n));
            }

            // Optional synchronous wait: block until THIS render finishes
            // (bounded), mirroring the queue path's waitForIdle usage.
            if (a.value("wait").toBool(false))
            {
                int waitMs = a.value("waitTimeoutMs").toInt(600000);
                waitMs = std::max(1000, std::min(waitMs, 1800000));
                if (em.waitForIdle(waitMs))
                {
                    // waitForIdle only says the render FINISHED — not how it went.
                    // Surface a failed render as an error via the same mechanism as
                    // the failures above (McpToolResult::text(..., true)), reading
                    // the outcome the way AudioEngineCommands::renderTrackWindow
                    // does: the unconditional "export complete" here was the
                    // `export-dir-must-exist` silent success.
                    const juce::String exportMsg = em.getLastExportMessage();
                    if (!exportMsg.startsWith("Export complete"))
                        return McpToolResult::text(
                            QString("export failed: %1").arg(QString::fromUtf8(exportMsg.toRawUTF8())),
                            true);
                    return McpToolResult::text(QString("export complete: %1").arg(path));
                }
                return McpToolResult::text(
                    QString("export wait timeout after %1ms; render may still be running (poll notifications/exportComplete or the file)").arg(waitMs), true);
            }

            // The async path ALSO submits a McpJobs job that polls the export
            // manager until it leaves the rendering state — agents then get a
            // RELIABLE completion state via poll_job (the writer is joined and
            // the WAV fully flushed before the job completes). Measuring by
            // file-size polling alone silently reads a mid-write file: the
            // artifact that burned the 2026-09-15 remix session.
            const int jobId = McpJobs::instance().submit("export_audio", [&em, path]() -> QJsonObject {
                if (em.waitForIdle(1500000))
                {
                    // Idle != successful — same contract as the wait path above:
                    // a failed render must reach poll_job as success:false, never
                    // as "export complete" with no file on disk.
                    const juce::String exportMsg = em.getLastExportMessage();
                    if (!exportMsg.startsWith("Export complete"))
                        return QJsonObject{
                            {"success", false},
                            {"message", QStringLiteral("export failed: %1")
                                 .arg(QString::fromUtf8(exportMsg.toRawUTF8()))},
                            {"outputPath", path}};
                    return QJsonObject{{"success", true},
                                       {"message", QStringLiteral("export complete: %1").arg(path)},
                                       {"outputPath", path}};
                }
                return QJsonObject{{"success", false},
                                   {"message", QStringLiteral("export wait timeout after 1500000ms")},
                                   {"outputPath", path}};
            });

            return McpToolResult::text(QString("export started: %1 (jobId=%2, format=%3, rate=%4, bits=%5, duration=%6s)")
                .arg(path)
                .arg(jobId)
                .arg(formatStr)
                .arg(sampleRate)
                .arg(bitDepth)
                .arg(duration, 0, 'f', 2));
        }});
}

void registerCancelExportTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"cancel_export", "Cancel an in-progress audio export. No-op if nothing is rendering; aborts the render and deletes the partial file.",
        QJsonObject{{"type","object"}},
        "export",
        [e](const QJsonObject&) -> McpToolResult {
            auto* mainProc = e->getMainProcessor();
            if (mainProc == nullptr)
                return McpToolResult::text("audio engine not initialized", true);
            auto& em = mainProc->getExportManager();
            if (!em.isExporting())
                return McpToolResult::text("no export in progress");
            em.cancel();
            return McpToolResult::text("cancel requested");
        }});
}

// ── S4: verify_window / render_and_verify ───────────────────────────────────
// Render→measure→compare. verify_window renders the WHOLE project through the
// shared export launcher (common/RenderLaunch.h) and measures ONLY the requested
// beat window — the WINDOW's metrics are the report's ROOT, so `targets` gates
// the window, not the file. render_and_verify = full render + buildMixVerdict.
//
// Both WAIT for the render (their contract is measure-and-answer; one
// verify_window costs ~one full export) and both delegate to a ProjectCommands
// entry point, so the RPC twins are byte-identical by construction.
void registerVerifyWindowTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"verify_window",
        "Render → measure → compare in ONE call, for a BEAT window of the WHOLE project. "
        "Renders the ENTIRE project through the export path (a window-ONLY render is not "
        "predictive: it re-bakes plugin state per window and misses sum-peak clamps — a "
        "windowed render measured 0 clamps on a file that carried 32 exact-FS frames), then "
        "measures ONLY [startBeat, endBeat) and reports THE WINDOW's own metrics at the "
        "report ROOT (duration/rms/peak/bands/kickProminence/ceilingHitPct/ceilingHitFrames) "
        "… plus the B6 target gates over them. So a targets:{ceilingHitPctMax:0} check passes "
        "when the clamps are OUTSIDE the window and fails when one is INSIDE it — while "
        "mix_report over the whole file would fail either way. WAITS for the render "
        "(bounded by timeoutMs, default 600000): one call ≈ one full export. Returns "
        "{ok, wavPath, window:{startBeat,endBeat,startSec,endSec,durationSec}, report, "
        "targetChecks, targetsOk}; the rendered WAV is KEPT for A/B and is the CALLER's to "
        "delete (an omitted outputPath lands in the OS temp dir). An inverted/empty window "
        "is refused. The expectation object is STRICT — an unknown key is refused "
        "('unknown expectation key <key>') BEFORE any render; accepted keys are rmsMin "
        "(LINEAR RMS floor, same units as the report's rms, at least), "
        "masterRms (linear mono-downmix RMS, within 5%), ceilingHitPctMax (percent of frames "
        "with any channel |sample| >= 0.999, at most), kickProminenceMin (0..1, at least), "
        "targetDurationSeconds (seconds, within 2s). Optional `expect` is an accepted alias "
        "of `targets`.",
        objSchema({{"startBeat",  QJsonObject{{"type","number"}}},
                   {"endBeat",    QJsonObject{{"type","number"}}},
                   {"targets",    QJsonObject{{"type","object"},
                        {"description","Expectation keys (unknown keys are REFUSED): rmsMin (LINEAR RMS floor, "
                         "same units as the report's rms), masterRms (linear mono-downmix RMS), ceilingHitPctMax "
                         "(percent of frames with any channel |sample| >= 0.999), kickProminenceMin (0..1), "
                         "targetDurationSeconds (seconds)."}}},
                   {"expect",     QJsonObject{{"type","object"},
                        {"description","Alias of targets; same accepted keys, same strictness."}}},
                   {"outputPath", QJsonObject{{"type","string"}}},
                   {"timeoutMs",  QJsonObject{{"type","integer"},{"default",600000}}}},
                 {"startBeat","endBeat"}),
        "audio",
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::VerifyWindowArgs args;
            QString argError;
            if (!HDAW::parseVerifyWindowArgs(a, args, argError))
                return McpToolResult::text(argError, true);
            auto r = e->getProjectCommands().verifyWindow(args.startBeat, args.endBeat, args.targets,
                                                          args.outputPath, args.timeoutMs);
            if (!r.ok)
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(HDAW::buildVerifyWindowPayload(r)).toJson(QJsonDocument::Compact)));
        }});
}

void registerRenderAndVerifyTool(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    s.registerTool({"render_and_verify",
        "Full render + release verdict in ONE call: renders the WHOLE project through the "
        "export path to outputPath (WAITING for completion) and returns the mix_verdict "
        "release-readiness verdict over the produced file — {ok, gates{audible, clipping, "
        "loudness, structure, modulation, introBlast, targets?...}, issues[], warnings[]} plus "
        "the wavPath. The 're-render + re-verdict' loop as one tool, and the verdict IS a "
        "mix_verdict: the SAME input resolution (fromPlan / dropBuildRatio / introSeconds / "
        "targets) and the SAME composer, so `render_and_verify {outputPath}` equals "
        "`mix_verdict {filePath: outputPath}` for the produced file BYTE FOR BYTE. fromPlan "
        "(default FALSE, MIRRORING mix_verdict's default) derives the windows, the loudness "
        "gate and the structure audit from the current song plan; set fromPlan=true to gate the "
        "plan on BOTH surfaces, or leave it false for the whole-file verdict. With fromPlan:true "
        "and NO song plan it falls back to the whole-file verdict (it has already rendered) "
        "instead of refusing. dropBuildRatio (default 0.9) is the loudness "
        "floor; introSeconds (default 2) runs the intro-blast gate; targets adds the same B6 "
        "target gate mix_verdict accepts. An empty or unwritable outputPath is refused.",
        objSchema({{"outputPath",     QJsonObject{{"type","string"}}},
                   {"targets",        QJsonObject{{"type","object"}}},
                   {"timeoutMs",      QJsonObject{{"type","integer"},{"default",600000}}},
                   {"fromPlan",       QJsonObject{{"type","boolean"},{"default",false}}},
                   {"dropBuildRatio", QJsonObject{{"type","number"},{"default",0.9}}},
                   {"introSeconds",   QJsonObject{{"type","number"},{"default",2.0}}}},
                 {"outputPath"}),
        "export",
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::RenderAndVerifyArgs args;
            QString argError;
            if (!HDAW::parseRenderAndVerifyArgs(a, args, argError))
                return McpToolResult::text(argError, true);
            // ONE shared implementation for both surfaces
            // (src/common/RenderAndVerify.h).
            const auto r = HDAW::renderAndVerify(*e, args.outputPath, args.targets, args.timeoutMs,
                                                 args.fromPlan, args.dropBuildRatio,
                                                 args.introSeconds);
            if (!r.ok)
                return McpToolResult::text(r.error, true);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(r.payload).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
