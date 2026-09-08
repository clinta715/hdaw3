#include "Router_Rave.h"
#include "RouterHelpers.h"

#include "../FrontendServer.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/RaveService.h"
#include "../../engine/RaveTrainingJobManager.h"
#include "../../model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QPointer>

#include <cstdint>
#include <optional>

using namespace frontend::router_helpers;

namespace frontend {
namespace {

QJsonObject modelToJson(const HDAW::RaveModelInfo& model)
{
    return QJsonObject{
        { "name", QString::fromUtf8(model.name.toRawUTF8()) },
        { "path", QString::fromUtf8(model.path.toRawUTF8()) },
        { "extension", QString::fromUtf8(model.extension.toRawUTF8()) },
        { "sizeBytes", static_cast<qint64>(model.sizeBytes) },
    };
}

QJsonObject transformToJson(const HDAW::RaveTransformResult& r)
{
    return QJsonObject{
        { "ok", r.ok },
        { "outputPath", QString::fromUtf8(r.outputPath.toRawUTF8()) },
        { "error", QString::fromUtf8(r.error.toRawUTF8()) },
        { "stdoutText", QString::fromUtf8(r.stdoutText.toRawUTF8()) },
        { "stderrText", QString::fromUtf8(r.stderrText.toRawUTF8()) },
        { "exitCode", r.exitCode },
    };
}

QJsonObject trainingResultToJson(const HDAW::RaveTrainingResult& r)
{
    return QJsonObject{
        { "ok", r.ok },
        { "outputModelPath", QString::fromUtf8(r.outputModelPath.toRawUTF8()) },
        { "error", QString::fromUtf8(r.error.toRawUTF8()) },
        { "stdoutText", QString::fromUtf8(r.stdoutText.toRawUTF8()) },
        { "stderrText", QString::fromUtf8(r.stderrText.toRawUTF8()) },
        { "exitCode", r.exitCode },
    };
}

QJsonObject probeToJson(const HDAW::RaveProbeResult& r)
{
    QJsonObject o = r.payload;
    if (!o.contains("ok"))
        o.insert("ok", r.ok);
    if (!o.contains("error"))
        o.insert("error", QString::fromUtf8(r.error.toRawUTF8()));
    o.insert("exitCode", r.exitCode);
    if (!r.stderrText.isEmpty())
        o.insert("stderrText", QString::fromUtf8(r.stderrText.toRawUTF8()));
    return o;
}

// Outcome of the opt-in "render output -> project" step shared by
// importResult and transformClip. samplerOk is false with a samplerError
// message when the sampler send was requested but the slot is missing or is
// not a sampler; that failure is non-fatal (the clip import still stands).
struct RaveImportOutcome
{
    int clipId = -1; // -1 when the clip import was skipped (noImport:true)
    bool samplerOk = false;
    bool samplerRequested = false;
    QString samplerError;
};

// Applies the opt-in import + sampler step for an already-rendered RAVE
// output WAV. Param units: startBeats is in BEATS and is passed straight to
// ProjectCommands::importAudioFile, which converts to seconds internally —
// never convert here (beats-vs-seconds convention).
// Returns nullopt on fatal import failure with *errCode/*errText set
// (missing file -> -32602, failed import -> -32603).
std::optional<RaveImportOutcome> applyRaveOutput(AudioEngine& engine,
                                                 const QJsonObject& o,
                                                 const QString& outputPath,
                                                 int* errCode,
                                                 QString* errText)
{
    RaveImportOutcome outcome;

    const bool noImport = optBool(o, "noImport", false, nullptr);
    if (!noImport)
    {
        const int trackIndex = optInt<int>(o, "trackIndex", -1, nullptr);
        const double startBeats = optDouble(o, "startBeats", 0.0, nullptr);
        const bool alignToGrid = optBool(o, "alignToGrid", true, nullptr);

        if (!juce::File(outputPath.toStdString()).existsAsFile())
        {
            if (errCode) *errCode = -32602;
            if (errText) *errText = "output file not found: " + outputPath;
            return std::nullopt;
        }

        const auto res = engine.getProjectCommands().importAudioFile(
            trackIndex, startBeats, outputPath.toStdString(), alignToGrid);
        if (res.clipId < 0)
        {
            if (errCode) *errCode = -32603;
            if (errText)
                *errText = res.error.empty() ? QString("import failed: ") + outputPath
                                             : QString::fromStdString(res.error);
            return std::nullopt;
        }
        outcome.clipId = res.clipId;
    }

    const int samplerTrack = optInt<int>(o, "samplerTrackIndex", -1, nullptr);
    const int samplerSlot = optInt<int>(o, "samplerSlotIndex", -1, nullptr);
    const int samplerRoot = optInt<int>(o, "samplerRootNote", 60, nullptr);
    outcome.samplerRequested = (samplerTrack >= 0 && samplerSlot >= 0);
    if (!outcome.samplerRequested)
    {
        outcome.samplerError = "not requested";
    }
    else if (!juce::File(outputPath.toStdString()).existsAsFile())
    {
        outcome.samplerError = "output file missing";
    }
    else
    {
        // NOTE: named fxSlots, not slots — Qt defines `slots` as a macro.
        const auto fxSlots = engine.getReadModel().getFxSlots(samplerTrack);
        if (samplerSlot >= static_cast<int>(fxSlots.size()))
        {
            outcome.samplerError = "slot not found";
        }
        else if (fxSlots[samplerSlot].fxType != "sampler")
        {
            outcome.samplerError = "slot is not a sampler";
        }
        else
        {
            engine.getProjectCommands().setSamplerSample(
                samplerTrack, samplerSlot, outputPath.toStdString(), samplerRoot);
            outcome.samplerOk = true;
        }
    }

    return outcome;
}

// Local clip lookup mirroring mcp::findClip (AudioEngineCommands::
// findClipById is private to the command layer, so the router walks the
// track list directly for this read-only lookup).
juce::ValueTree findClipInProject(AudioEngine& engine, int clipId, int* outTrackIdx)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
            continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == clipId)
            {
                if (outTrackIdx)
                    *outTrackIdx = t;
                return clip;
            }
        }
    }
    if (outTrackIdx)
        *outTrackIdx = -1;
    return {};
}

QJsonObject jobStatusToJson(const HDAW::RaveJobStatus& s)
{
    QJsonObject o{
        { "jobId", static_cast<double>(s.jobId) },
        { "state", s.state },
        { "message", s.message },
    };
    if (s.hasResult)
        o.insert("result", transformToJson(s.result));
    else
        o.insert("result", QJsonValue::Null);
    return o;
}

// Sidecar-only params shared with transformFile (input/model/output paths,
// interpreter, script, sampling controls). No import options on purpose:
// async jobs take no beats, no track targets — importing stays a separate
// sync rave.importResult call on the message thread.
bool parseSidecarRequest(const QJsonObject& o, HDAW::RaveTransformRequest& req, DispatchResult* err)
{
    std::string inputPath, modelPath, outputPath;
    if (!requireString(o, "inputPath", inputPath, nullptr) ||
        !requireString(o, "modelPath", modelPath, nullptr) ||
        !requireString(o, "outputPath", outputPath, nullptr))
    {
        if (err)
            *err = makeError(-32602, "inputPath, modelPath, and outputPath required");
        return false;
    }
    req.inputPath = inputPath;
    req.modelPath = modelPath;
    req.outputPath = outputPath;
    req.pythonPath = o.value("pythonPath").toString().toStdString();
    req.scriptPath = o.value("scriptPath").toString().toStdString();
    req.temperature = optDouble(o, "temperature", 1.0, nullptr);
    req.seed = optInt(o, "seed", 0, nullptr);
    return true;
}

QJsonObject trainingJobStatusToJson(const HDAW::RaveTrainingJobStatus& s)
{
    QJsonObject o{
        { "jobId", static_cast<double>(s.jobId) },
        { "state", s.state },
        { "message", s.message },
    };
    if (s.hasResult)
        o.insert("result", trainingResultToJson(s.result));
    else
        o.insert("result", QJsonValue::Null);
    return o;
}

bool parseTrainingRequest(const QJsonObject& o, HDAW::RaveTrainingRequest& req, DispatchResult* err)
{
    std::string datasetPath, outputModelPath;
    if (!requireString(o, "datasetPath", datasetPath, nullptr) ||
        !requireString(o, "outputModelPath", outputModelPath, nullptr))
    {
        if (err)
            *err = makeError(-32602, "datasetPath and outputModelPath required");
        return false;
    }
    req.datasetPath = datasetPath;
    req.outputModelPath = outputModelPath;
    req.name = o.value("name").toString().toStdString();
    req.pythonPath = o.value("pythonPath").toString().toStdString();
    req.scriptPath = o.value("scriptPath").toString().toStdString();
    req.epochs = optInt(o, "epochs", 10, nullptr);
    req.batchSize = optInt(o, "batchSize", 8, nullptr);
    req.sampleRate = optInt(o, "sampleRate", 44100, nullptr);
    if (req.epochs <= 0 || req.batchSize <= 0 || req.sampleRate <= 0)
    {
        if (err)
            *err = makeError(-32602, "epochs, batchSize, and sampleRate must be > 0");
        return false;
    }
    return true;
}

QJsonObject importOutcomeToJson(const RaveImportOutcome& outcome)
{
    return QJsonObject{
        { "clipId", outcome.clipId },
        { "samplerOk", outcome.samplerOk },
        { "samplerRequested", outcome.samplerRequested },
        { "samplerError", outcome.samplerError },
    };
}

} // namespace

DispatchResult dispatchRave(AudioEngine& engine, const QString& m, const QJsonValue& params,
                              FrontendServer* server)
{
    const auto o = paramsObject(params);

    if (m == "listModels")
    {
        juce::File dir;
        const auto directory = o.value("directory").toString();
        if (!directory.isEmpty())
            dir = juce::File(directory.toStdString());

        QJsonArray arr;
        for (const auto& model : engine.getRaveService().listModels(dir))
            arr.append(modelToJson(model));
        return { false, QJsonObject{ { "models", arr } } };
    }

    if (m == "probeModel")
    {
        std::string modelPath;
        if (!requireString(o, "modelPath", modelPath, nullptr))
            return makeError(-32602, "modelPath required");

        HDAW::RaveProbeRequest req;
        req.modelPath = modelPath;
        req.pythonPath = o.value("pythonPath").toString().toStdString();
        req.scriptPath = o.value("scriptPath").toString().toStdString();

        const auto result = engine.getRaveService().probeModel(req);
        if (!result.ok)
            return makeError(-32603, QString::fromUtf8(result.error.toRawUTF8()));
        return { false, probeToJson(result) };
    }

    if (m == "transformFile")
    {
        std::string inputPath, modelPath, outputPath;
        if (!requireString(o, "inputPath", inputPath, nullptr) ||
            !requireString(o, "modelPath", modelPath, nullptr) ||
            !requireString(o, "outputPath", outputPath, nullptr))
            return makeError(-32602, "inputPath, modelPath, and outputPath required");

        HDAW::RaveTransformRequest req;
        req.inputPath = inputPath;
        req.modelPath = modelPath;
        req.outputPath = outputPath;
        req.pythonPath = o.value("pythonPath").toString().toStdString();
        req.scriptPath = o.value("scriptPath").toString().toStdString();
        req.temperature = optDouble(o, "temperature", 1.0, nullptr);
        req.seed = optInt(o, "seed", 0, nullptr);

        const auto result = engine.getRaveService().transformFile(req);
        if (!result.ok)
            return makeError(-32603, QString::fromUtf8(result.error.toRawUTF8()));
        return { false, transformToJson(result) };
    }

    if (m == "importResult")
    {
        std::string outputPath;
        if (!requireString(o, "outputPath", outputPath, nullptr))
            return makeError(-32602, "outputPath required");

        int errCode = -32603;
        QString errText;
        auto outcome = applyRaveOutput(
            engine, o, QString::fromStdString(outputPath), &errCode, &errText);
        if (!outcome)
            return makeError(errCode, errText);
        return { false, importOutcomeToJson(*outcome) };
    }

    if (m == "transformClip")
    {
        int clipId = -1;
        std::string modelPath, outputPath;
        if (!requireInt(o, "clipId", clipId, nullptr) ||
            !requireString(o, "modelPath", modelPath, nullptr) ||
            !requireString(o, "outputPath", outputPath, nullptr))
            return makeError(-32602, "clipId, modelPath, and outputPath required");

        int clipTrack = -1;
        auto clip = findClipInProject(engine, clipId, &clipTrack);
        if (!clip.isValid())
            return makeError(-32602, "clip not found: " + QString::number(clipId));
        if (clip.getProperty(IDs::clipType, "audio").toString() == "midi")
            return makeError(-32602, "clip is a MIDI clip (no audio source to transform): "
                                     + QString::number(clipId));
        const juce::String sourceFile = clip.getProperty(IDs::sourceFile, "").toString();
        if (sourceFile.isEmpty())
            return makeError(-32602, "clip has no audio source file: "
                                     + QString::number(clipId));

        HDAW::RaveTransformRequest req;
        req.inputPath = sourceFile.toStdString();
        req.modelPath = modelPath;
        req.outputPath = outputPath;
        req.pythonPath = o.value("pythonPath").toString().toStdString();
        req.scriptPath = o.value("scriptPath").toString().toStdString();
        req.temperature = optDouble(o, "temperature", 1.0, nullptr);
        req.seed = optInt(o, "seed", 0, nullptr);

        const auto result = engine.getRaveService().transformFile(req);
        if (!result.ok)
            return makeError(-32603, QString::fromUtf8(result.error.toRawUTF8()));

        int errCode = -32603;
        QString errText;
        auto outcome = applyRaveOutput(
            engine, o, QString::fromStdString(outputPath), &errCode, &errText);
        if (!outcome)
            return makeError(errCode, errText);

        QJsonObject payload{
            { "transform", transformToJson(result) },
            { "clipId", outcome->clipId },
            { "samplerOk", outcome->samplerOk },
            { "samplerRequested", outcome->samplerRequested },
            { "samplerError", outcome->samplerError },
        };
        return { false, payload };
    }

    if (m == "startTransform")
    {
        HDAW::RaveTransformRequest req;
        DispatchResult parseErr;
        if (!parseSidecarRequest(o, req, &parseErr))
            return parseErr;

        HDAW::RaveJobManager::NotifyFn notify;
        if (server != nullptr)
        {
            QPointer<FrontendServer> guard(server);
            notify = [guard](int64_t jobId, const QString& state, const QString& message) {
                if (guard.isNull())
                    return;
                // Thread-safe hop (QueuedConnection); never touches clients_
                // off-thread. No percent progress — the sidecar reports none.
                guard->broadcastNotificationFromAnyThread(
                    notify::RaveProgress,
                    QJsonObject{ { "jobId", static_cast<double>(jobId) },
                                 { "state", state },
                                 { "message", message } });
            };
        }

        const int64_t jobId = engine.getRaveJobManager().startJob(req, std::move(notify));
        return { false, QJsonObject{ { "jobId", static_cast<double>(jobId) } } };
    }

    if (m == "startTraining")
    {
        HDAW::RaveTrainingRequest req;
        DispatchResult parseErr;
        if (!parseTrainingRequest(o, req, &parseErr))
            return parseErr;

        HDAW::RaveTrainingJobManager::NotifyFn notify;
        if (server != nullptr)
        {
            QPointer<FrontendServer> guard(server);
            notify = [guard](int64_t jobId, const QString& state, const QString& message) {
                if (guard.isNull())
                    return;
                guard->broadcastNotificationFromAnyThread(
                    notify::RaveTrainingProgress,
                    QJsonObject{ { "jobId", static_cast<double>(jobId) },
                                 { "state", state },
                                 { "message", message } });
            };
        }

        const int64_t jobId = engine.getRaveTrainingJobManager().startJob(req, std::move(notify));
        return { false, QJsonObject{ { "jobId", static_cast<double>(jobId) } } };
    }

    if (m == "trainingJobStatus")
    {
        int64_t jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr) || jobId <= 0)
            return makeError(-32602, "jobId required");
        HDAW::RaveTrainingJobStatus status;
        if (!engine.getRaveTrainingJobManager().jobStatus(jobId, status))
            return makeError(-32602, "rave training job not found: " + QString::number(jobId));
        return { false, trainingJobStatusToJson(status) };
    }

    if (m == "cancelTrainingJob")
    {
        int64_t jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr) || jobId <= 0)
            return makeError(-32602, "jobId required");
        HDAW::RaveTrainingJobStatus status;
        if (!engine.getRaveTrainingJobManager().cancelJob(jobId, status))
            return makeError(-32602, "rave training job not found: " + QString::number(jobId));
        return { false, QJsonObject{ { "jobId", static_cast<double>(status.jobId) },
                                     { "state", status.state },
                                     { "message", status.message } } };
    }

    if (m == "jobStatus")
    {
        int64_t jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr) || jobId <= 0)
            return makeError(-32602, "jobId required");
        HDAW::RaveJobStatus status;
        if (!engine.getRaveJobManager().jobStatus(jobId, status))
            return makeError(-32602, "rave job not found: " + QString::number(jobId));
        return { false, jobStatusToJson(status) };
    }

    if (m == "cancelJob")
    {
        int64_t jobId = 0;
        if (!requireInt(o, "jobId", jobId, nullptr) || jobId <= 0)
            return makeError(-32602, "jobId required");
        HDAW::RaveJobStatus status;
        if (!engine.getRaveJobManager().cancelJob(jobId, status))
            return makeError(-32602, "rave job not found: " + QString::number(jobId));
        return { false, QJsonObject{ { "jobId", static_cast<double>(status.jobId) },
                                     { "state", status.state },
                                     { "message", status.message } } };
    }

    return makeError(-32601, "unknown rave method: " + m);
}

} // namespace frontend
