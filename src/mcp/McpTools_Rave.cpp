#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"
#include "../engine/RaveService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mcp {
namespace {

QJsonObject modelToJson(const HDAW::RaveModelInfo& model)
{
    return QJsonObject{
        { "name", jstr(model.name) },
        { "path", jstr(model.path) },
        { "extension", jstr(model.extension) },
        { "sizeBytes", static_cast<qint64>(model.sizeBytes) },
    };
}

QJsonObject transformToJson(const HDAW::RaveTransformResult& r)
{
    return QJsonObject{
        { "ok", r.ok },
        { "outputPath", jstr(r.outputPath) },
        { "error", jstr(r.error) },
        { "stdoutText", jstr(r.stdoutText) },
        { "stderrText", jstr(r.stderrText) },
        { "exitCode", r.exitCode },
    };
}

} // namespace

void registerRaveTools(McpServer& s, AudioEngine* e)
{
    // RAVE #5: persisted config (QSettings rave/*), shared with the
    // settings.getRaveConfig / settings.setRaveConfig RPC via the
    // HDAW::RaveService helpers so both surfaces return the identical shape:
    // { modelDirs: string[], defaultModel, pythonPath, scriptPath, timeoutMs }.
    s.registerTool({"rave_get_config",
        "Get the persisted RAVE configuration: { modelDirs, defaultModel, pythonPath, scriptPath, timeoutMs }. Sidecar resolution order: explicit request field > these settings > HDAW_RAVE_SCRIPT/HDAW_RAVE_PYTHON/HDAW_RAVE_TIMEOUT_MS env > built-in defaults.",
        objSchema({}),
        "rave",
        [](const QJsonObject&) -> McpToolResult {
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                HDAW::RaveService::persistedConfigJson()).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_set_config",
        "Update the persisted RAVE configuration (partial update: every field optional). timeoutMs, when present, must be > 0. Returns the full configuration after the update, same shape as rave_get_config.",
        objSchema({{"modelDirs", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
                   {"defaultModel", QJsonObject{{"type", "string"}}},
                   {"pythonPath", QJsonObject{{"type", "string"}}},
                   {"scriptPath", QJsonObject{{"type", "string"}}},
                   {"timeoutMs", QJsonObject{{"type", "integer"}}}}),
        "rave",
        [](const QJsonObject& a) -> McpToolResult {
            QString error;
            if (!HDAW::RaveService::savePersistedConfig(a, &error))
                return McpToolResult::text(error.isEmpty() ? QStringLiteral("invalid RAVE config") : error, true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                HDAW::RaveService::persistedConfigJson()).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_list_models",
        "List offline RAVE model files from an explicit directory or configured defaults.",
        objSchema({{"directory", QJsonObject{{"type", "string"}}}}),
        "rave",
        [e](const QJsonObject& a) -> McpToolResult {
            juce::File dir;
            const auto directory = a.value("directory").toString();
            if (!directory.isEmpty())
                dir = juce::File(directory.toStdString());

            QJsonArray models;
            for (const auto& model : e->getRaveService().listModels(dir))
                models.append(modelToJson(model));
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"models", models}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_transform_file",
        "Run an offline RAVE file transform through an external Python sidecar. Does not mutate the project.",
        objSchema({{"inputPath", QJsonObject{{"type", "string"}}},
                   {"modelPath", QJsonObject{{"type", "string"}}},
                   {"outputPath", QJsonObject{{"type", "string"}}},
                   {"pythonPath", QJsonObject{{"type", "string"}}},
                   {"scriptPath", QJsonObject{{"type", "string"}}},
                   {"temperature", QJsonObject{{"type", "number"}}},
                   {"seed", QJsonObject{{"type", "integer"}}}},
                  {"inputPath", "modelPath", "outputPath"}),
        "rave",
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::RaveTransformRequest req;
            req.inputPath = a.value("inputPath").toString().toStdString();
            req.modelPath = a.value("modelPath").toString().toStdString();
            req.outputPath = a.value("outputPath").toString().toStdString();
            req.pythonPath = a.value("pythonPath").toString().toStdString();
            req.scriptPath = a.value("scriptPath").toString().toStdString();
            req.temperature = a.value("temperature").toDouble(1.0);
            req.seed = a.value("seed").toInt(0);

            auto result = e->getRaveService().transformFile(req);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(transformToJson(result)).toJson(QJsonDocument::Compact)), !result.ok);
        }});

    // Shared opt-in "render output -> project" step: import the WAV as a clip
    // (unless noImport) and/or send it to a sampler slot (only when both
    // samplerTrackIndex and samplerSlotIndex are >= 0). startBeats is in
    // BEATS; ProjectCommands::importAudioFile converts to seconds internally.
    // Returns { ok, code, message, clipId, samplerOk, samplerError }: ok=false
    // means a fatal import failure; sampler-slot problems are non-fatal and
    // reported via samplerOk/samplerError.
    struct RaveImportOutcome
    {
        bool ok = false;
        QString message;
        int clipId = -1;
        bool samplerOk = false;
        QString samplerError;
    };
    auto applyRaveOutput = [e](const QJsonObject& a, const QString& outputPath) -> RaveImportOutcome {
        RaveImportOutcome out;
        if (!a.value("noImport").toBool(false))
        {
            const int trackIndex = a.contains("trackIndex") && a.value("trackIndex").isDouble()
                ? a.value("trackIndex").toInt(-1) : -1;
            const double startBeats = a.value("startBeats").toDouble(0.0);
            const bool alignToGrid = a.contains("alignToGrid")
                ? a.value("alignToGrid").toBool(true) : true;

            if (!juce::File(outputPath.toStdString()).existsAsFile())
            {
                out.message = "output file not found: " + outputPath;
                return out;
            }

            auto res = e->getProjectCommands().importAudioFile(
                trackIndex, startBeats, outputPath.toStdString(), alignToGrid);
            if (res.clipId < 0)
            {
                out.message = res.error.empty() ? QString("import failed: ") + outputPath
                                                : QString::fromStdString(res.error);
                return out;
            }
            out.clipId = res.clipId;
        }

        const int samplerTrack = a.value("samplerTrackIndex").toInt(-1);
        const int samplerSlot = a.value("samplerSlotIndex").toInt(-1);
        const int samplerRoot = a.value("samplerRootNote").toInt(60);
        if (samplerTrack >= 0 && samplerSlot >= 0)
        {
            // NOTE: named fxSlots, not slots — Qt defines `slots` as a macro.
            const auto fxSlots = e->getReadModel().getFxSlots(samplerTrack);
            if (samplerSlot >= static_cast<int>(fxSlots.size()))
                out.samplerError = "slot not found";
            else if (fxSlots[samplerSlot].fxType != "sampler")
                out.samplerError = "slot is not a sampler";
            else
            {
                e->getProjectCommands().setSamplerSample(
                    samplerTrack, samplerSlot, outputPath.toStdString(), samplerRoot);
                out.samplerOk = true;
            }
        }

        out.ok = true;
        return out;
    };

    s.registerTool({"rave_import_result",
        "Import an already-rendered RAVE output WAV into the project as a clip (startBeats is in beats) and/or send it to a sampler slot. Both mutations are opt-in: skip the clip import with noImport:true, and the sampler send only happens when samplerTrackIndex and samplerSlotIndex are both >= 0. With no import target the call only validates outputPath.",
        objSchema({{"outputPath", QJsonObject{{"type", "string"}}},
                   {"trackIndex", QJsonObject{{"type", "integer"}}},
                   {"startBeats", QJsonObject{{"type", "number"}}},
                   {"alignToGrid", QJsonObject{{"type", "boolean"}}},
                   {"noImport", QJsonObject{{"type", "boolean"}}},
                   {"samplerTrackIndex", QJsonObject{{"type", "integer"}}},
                   {"samplerSlotIndex", QJsonObject{{"type", "integer"}}},
                   {"samplerRootNote", QJsonObject{{"type", "integer"}}}},
                  {"outputPath"}),
        "rave",
        [e, applyRaveOutput](const QJsonObject& a) -> McpToolResult {
            const QString outputPath = a.value("outputPath").toString();
            if (outputPath.isEmpty())
                return McpToolResult::text("outputPath required", true);
            auto out = applyRaveOutput(a, outputPath);
            if (!out.ok)
                return McpToolResult::text(out.message, true);
            QJsonObject payload{
                {"clipId", out.clipId},
                {"samplerOk", out.samplerOk},
                {"samplerError", out.samplerError},
            };
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_transform_clip",
        "Transform a project audio clip's source file through the offline RAVE sidecar, then opt-in import the rendered WAV as a clip (startBeats is in beats) and/or send it to a sampler slot. Rejects unknown clip ids, MIDI clips, and clips with no audio source file before running the sidecar. On sidecar failure nothing is imported.",
        objSchema({{"clipId", QJsonObject{{"type", "integer"}}},
                   {"modelPath", QJsonObject{{"type", "string"}}},
                   {"outputPath", QJsonObject{{"type", "string"}}},
                   {"pythonPath", QJsonObject{{"type", "string"}}},
                   {"scriptPath", QJsonObject{{"type", "string"}}},
                   {"temperature", QJsonObject{{"type", "number"}}},
                   {"seed", QJsonObject{{"type", "integer"}}},
                   {"trackIndex", QJsonObject{{"type", "integer"}}},
                   {"startBeats", QJsonObject{{"type", "number"}}},
                   {"alignToGrid", QJsonObject{{"type", "boolean"}}},
                   {"noImport", QJsonObject{{"type", "boolean"}}},
                   {"samplerTrackIndex", QJsonObject{{"type", "integer"}}},
                   {"samplerSlotIndex", QJsonObject{{"type", "integer"}}},
                   {"samplerRootNote", QJsonObject{{"type", "integer"}}}},
                  {"clipId", "modelPath", "outputPath"}),
        "rave",
        [e, applyRaveOutput](const QJsonObject& a) -> McpToolResult {
            const int clipId = a.value("clipId").toInt(-1);
            const QString modelPath = a.value("modelPath").toString();
            const QString outputPath = a.value("outputPath").toString();
            if (clipId < 0 || modelPath.isEmpty() || outputPath.isEmpty())
                return McpToolResult::text("clipId, modelPath, and outputPath required", true);

            auto clip = findClip(e, clipId, nullptr);
            if (!clip.isValid())
                return McpToolResult::text("clip not found: " + QString::number(clipId), true);
            if (clip.getProperty("clipType", "audio").toString() == "midi")
                return McpToolResult::text("clip is a MIDI clip (no audio source to transform): "
                                           + QString::number(clipId), true);
            const juce::String sourceFile = clip.getProperty("sourceFile", "").toString();
            if (sourceFile.isEmpty())
                return McpToolResult::text("clip has no audio source file: "
                                           + QString::number(clipId), true);

            HDAW::RaveTransformRequest req;
            req.inputPath = sourceFile.toStdString();
            req.modelPath = modelPath.toStdString();
            req.outputPath = outputPath.toStdString();
            req.pythonPath = a.value("pythonPath").toString().toStdString();
            req.scriptPath = a.value("scriptPath").toString().toStdString();
            req.temperature = a.value("temperature").toDouble(1.0);
            req.seed = a.value("seed").toInt(0);

            auto result = e->getRaveService().transformFile(req);
            if (!result.ok)
                return McpToolResult::text(jstr(result.error), true);

            auto out = applyRaveOutput(a, outputPath);
            if (!out.ok)
                return McpToolResult::text(out.message, true);
            QJsonObject payload{
                {"transform", transformToJson(result)},
                {"clipId", out.clipId},
                {"samplerOk", out.samplerOk},
                {"samplerError", out.samplerError},
            };
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }});

    auto jobStatusToJson = [](int64_t jobId, const HDAW::RaveJobStatus& s) {
        QJsonObject o{
            {"jobId", static_cast<double>(jobId)},
            {"state", s.state},
            {"message", s.message},
        };
        if (s.hasResult)
            o.insert("result", transformToJson(s.result));
        else
            o.insert("result", QJsonValue::Null);
        return o;
    };

    s.registerTool({"rave_start_transform",
        "Start an async offline RAVE file transform through the external Python sidecar and return a job id immediately (never blocks). Takes ONLY sidecar params (input/model/output paths, interpreter, script, sampling controls) — no import options and no beats are involved. Poll rave_job_status until state is finished/failed/cancelled; abort with rave_cancel_job. The worker never touches the project; on terminal success (state finished) call the sync rave_import_result on the message thread to import the rendered WAV as a clip.",
        objSchema({{"inputPath", QJsonObject{{"type", "string"}}},
                   {"modelPath", QJsonObject{{"type", "string"}}},
                   {"outputPath", QJsonObject{{"type", "string"}}},
                   {"pythonPath", QJsonObject{{"type", "string"}}},
                   {"scriptPath", QJsonObject{{"type", "string"}}},
                   {"temperature", QJsonObject{{"type", "number"}}},
                   {"seed", QJsonObject{{"type", "integer"}}}},
                  {"inputPath", "modelPath", "outputPath"}),
        "rave",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString inputPath = a.value("inputPath").toString();
            const QString modelPath = a.value("modelPath").toString();
            const QString outputPath = a.value("outputPath").toString();
            if (inputPath.isEmpty() || modelPath.isEmpty() || outputPath.isEmpty())
                return McpToolResult::text("inputPath, modelPath, and outputPath required", true);
            HDAW::RaveTransformRequest req;
            req.inputPath = inputPath.toStdString();
            req.modelPath = modelPath.toStdString();
            req.outputPath = outputPath.toStdString();
            req.pythonPath = a.value("pythonPath").toString().toStdString();
            req.scriptPath = a.value("scriptPath").toString().toStdString();
            req.temperature = a.value("temperature").toDouble(1.0);
            req.seed = a.value("seed").toInt(0);

            const int64_t jobId = e->getRaveJobManager().startJob(req, {});
            QJsonObject payload{
                {"jobId", static_cast<double>(jobId)},
                {"hint", "poll rave_job_status until terminal; cancel with rave_cancel_job; on finished call rave_import_result"},
            };
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_job_status",
        "Poll an async RAVE transform job. Returns {jobId, state, message, result}: state is running/finished/failed/cancelled (states only — the sidecar reports no percent progress); result carries the sidecar outcome once terminal. Unknown ids are an error.",
        objSchema({{"jobId", QJsonObject{{"type", "integer"}}}},
                  {"jobId"}),
        "rave",
        [e, jobStatusToJson](const QJsonObject& a) -> McpToolResult {
            const int64_t jobId = a.value("jobId").toVariant().toLongLong();
            if (jobId <= 0)
                return McpToolResult::text("jobId required", true);
            HDAW::RaveJobStatus s;
            if (!e->getRaveJobManager().jobStatus(jobId, s))
                return McpToolResult::text("rave job not found: " + QString::number(jobId), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(jobStatusToJson(jobId, s)).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"rave_cancel_job",
        "Cancel a running async RAVE transform job (kills the sidecar process; terminal state becomes cancelled). Idempotent on already-terminal jobs; unknown ids are an error, never silent ok.",
        objSchema({{"jobId", QJsonObject{{"type", "integer"}}}},
                  {"jobId"}),
        "rave",
        [e](const QJsonObject& a) -> McpToolResult {
            const int64_t jobId = a.value("jobId").toVariant().toLongLong();
            if (jobId <= 0)
                return McpToolResult::text("jobId required", true);
            HDAW::RaveJobStatus s;
            if (!e->getRaveJobManager().cancelJob(jobId, s))
                return McpToolResult::text("rave job not found: " + QString::number(jobId), true);
            QJsonObject payload{
                {"jobId", static_cast<double>(s.jobId)},
                {"state", s.state},
                {"message", s.message},
            };
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
