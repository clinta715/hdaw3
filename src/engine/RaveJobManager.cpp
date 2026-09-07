#include "RaveJobManager.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

namespace HDAW {
namespace {

QString qstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

juce::String jstr(const QString& s)
{
    return juce::String(s.toUtf8().constData());
}

bool isSupportedModelExtension(const QString& extension)
{
    const auto e = extension.toLower();
    return e == ".ts" || e == ".pt" || e == ".pth" || e == ".rave" || e == ".onnx";
}

juce::String stdoutOf(QProcess& process)
{
    return juce::String(process.readAllStandardOutput().constData());
}

juce::String stderrOf(QProcess& process)
{
    return juce::String(process.readAllStandardError().constData());
}

void emitNotify(const RaveJobManager::NotifyFn& notify, int64_t jobId,
                const QString& state, const QString& message)
{
    if (notify)
        notify(jobId, state, message);
}

} // namespace

bool RaveJobManager::failFastError(const RaveTransformRequest& request, QString& errorOut)
{
    // Same order and messages as RaveService::transformFile so sync and async
    // failures read identically.
    const juce::File input(request.inputPath);
    if (request.inputPath.isEmpty() || !input.existsAsFile())
    {
        errorOut = qstr("input file not found: " + request.inputPath);
        return true;
    }

    const juce::File model(request.modelPath);
    if (request.modelPath.isEmpty() || !model.existsAsFile())
    {
        errorOut = qstr("model file not found: " + request.modelPath);
        return true;
    }

    if (!isSupportedModelExtension(qstr(model.getFileExtension())))
    {
        errorOut = qstr("unsupported RAVE model extension: " + model.getFileExtension());
        return true;
    }

    if (request.outputPath.isEmpty())
    {
        errorOut = "outputPath required";
        return true;
    }

    // RAVE #5: shared resolution (request > QSettings > env > zero-config
    // dev/packaged fallbacks) with RaveService.
    const QString scriptPath = qstr(RaveService::resolveScriptPath(request.scriptPath));
    if (scriptPath.isEmpty())
    {
        errorOut = "RAVE transform script not configured (set scriptPath or HDAW_RAVE_SCRIPT)";
        return true;
    }

    if (!juce::File(scriptPath.toStdString()).existsAsFile())
    {
        errorOut = "RAVE transform script not found: " + scriptPath;
        return true;
    }

    return false;
}

void RaveJobManager::fillSnapshot(const std::shared_ptr<Job>& job, RaveJobStatus& out)
{
    std::lock_guard<std::mutex> lock(job->mutex);
    out.jobId = job->id;
    out.state = job->state;
    out.message = job->message;
    out.hasResult = job->hasResult;
    out.result = job->result;
}

int64_t RaveJobManager::startJob(const RaveTransformRequest& request, NotifyFn notify)
{
    auto job = std::make_shared<Job>();
    job->request = request;
    job->result.outputPath = request.outputPath;
    job->notify = std::move(notify);

    QString error;
    const bool invalid = failFastError(request, error);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->id = nextId_++;
        if (invalid)
        {
            job->state = kStateFailed;
            job->message = error;
            job->result.ok = false;
            job->result.error = jstr(error);
            job->hasResult = true;
        }
        // Bound the retained-terminal map so status-after-finish works without
        // unbounded growth. Evict oldest TERMINAL jobs only — never a running
        // job (its worker thread is still joined on erase/shutdown).
        while (jobs_.size() >= kMaxRetainedJobs)
        {
            auto it = jobs_.begin();
            for (; it != jobs_.end(); ++it)
            {
                std::lock_guard<std::mutex> jobLock(it->second->mutex);
                if (it->second->state != kStateRunning)
                    break;
            }
            if (it == jobs_.end())
                break; // all running: transient overgrowth, never drop live work
            if (it->second->thread.joinable())
                it->second->thread.join(); // terminal: worker already exited
            jobs_.erase(it);
        }
        jobs_[job->id] = job;
    }

    if (invalid)
    {
        // Fail-fast: no thread, no process. Emit started + terminal so
        // notification-only clients see the same shape as a live job.
        emitNotify(job->notify, job->id, kStateRunning, "validating");
        emitNotify(job->notify, job->id, kStateFailed, error);
        return job->id;
    }

    job->thread = std::thread([this, job] { runJob(job); });
    return job->id;
}

bool RaveJobManager::jobStatus(int64_t jobId, RaveJobStatus& out) const
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end())
            return false;
        job = it->second;
    }
    fillSnapshot(job, out);
    return true;
}

bool RaveJobManager::cancelJob(int64_t jobId, RaveJobStatus& out)
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end())
            return false;
        job = it->second;
    }
    {
        std::lock_guard<std::mutex> jobLock(job->mutex);
        if (job->state == kStateRunning)
        {
            job->cancelRequested.store(true);
            // Cross-thread kill: kill() is a direct OS-level call (SIGKILL /
            // TerminateProcess) with no event-loop involvement. The worker
            // also kills same-thread from its wait slice — double-kill is
            // harmless and whichever lands first wakes waitForFinished.
            if (job->process != nullptr)
                job->process->kill();
        }
    }
    fillSnapshot(job, out);
    return true;
}

void RaveJobManager::shutdown()
{
    std::map<int64_t, std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
            jobs = jobs_;
        else
        {
            stopping_ = true;
            jobs = jobs_;
        }
        for (const auto& [id, job] : jobs_)
        {
            std::lock_guard<std::mutex> jobLock(job->mutex);
            job->cancelRequested.store(true);
            if (job->process != nullptr)
                job->process->kill();
        }
    }
    // Join outside both locks. Each worker wakes from its 100 ms wait slice
    // (or its 5 s post-kill grace wait), finalizes, and exits — bounded,
    // never the 10-minute sidecar timeout.
    for (const auto& [id, job] : jobs)
    {
        if (job->thread.joinable())
            job->thread.join();
    }
}

void RaveJobManager::runJob(std::shared_ptr<Job> job)
{
    emitNotify(job->notify, job->id, kStateRunning, "sidecar started");

    const RaveTransformRequest request = job->request; // local copy; touch no engine state
    RaveTransformResult result;
    result.outputPath = request.outputPath;

    // RAVE #5: shared resolution (request > QSettings > env > zero-config
    // fallbacks > default) with
    // RaveService::transformFile so sync and async paths never diverge.
    const QString scriptPath = qstr(RaveService::resolveScriptPath(request.scriptPath));
    const QString pythonPath = qstr(RaveService::resolvePythonPath(request.pythonPath));

    QStringList args;
    args << scriptPath
         << "--input" << qstr(request.inputPath)
         << "--model" << qstr(request.modelPath)
         << "--output" << qstr(request.outputPath)
         << "--temperature" << QString::number(request.temperature, 'g', 12)
         << "--seed" << QString::number(request.seed);

    QProcess process;
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    {
        std::lock_guard<std::mutex> jobLock(job->mutex);
        if (job->cancelRequested.load())
        {
            job->state = kStateCancelled;
            job->message = "cancelled";
            job->result = result;
            job->result.ok = false;
            job->result.error = "cancelled";
            job->hasResult = true;
        }
        else
        {
            job->process = &process;
        }
    }
    if (job->cancelRequested.load())
    {
        RaveJobStatus snap;
        fillSnapshot(job, snap);
        emitNotify(job->notify, job->id, kStateCancelled, snap.message);
        return;
    }

    auto finishTerminal = [&](const QString& state, const QString& message, const RaveTransformResult& r) {
        {
            std::lock_guard<std::mutex> jobLock(job->mutex);
            job->process = nullptr;
            job->state = state;
            job->message = message;
            job->result = r;
            job->hasResult = true;
        }
        emitNotify(job->notify, job->id, state, message);
    };

    process.start(pythonPath, args);
    if (!process.waitForStarted(5000))
    {
        result.error = jstr("failed to start RAVE sidecar: " + QString(process.errorString().toUtf8().constData()));
        result.stderrText = stderrOf(process);
        result.stdoutText = stdoutOf(process);
        finishTerminal(kStateFailed, qstr(result.error), result);
        return;
    }

    // Slice-wait (event-driven, NOT a sleep poll): each wait returns within
    // 100 ms so cancel/timeout/shutdown take effect promptly.
    QElapsedTimer elapsed;
    elapsed.start();
    const int timeoutMs = RaveService::resolveTimeoutMs();
    while (!process.waitForFinished(100))
    {
        if (job->cancelRequested.load())
        {
            process.kill();
            process.waitForFinished(5000);
            result.exitCode = process.exitCode();
            result.stderrText = stderrOf(process);
            result.stdoutText = stdoutOf(process);
            result.ok = false;
            result.error = "cancelled";
            finishTerminal(kStateCancelled, "cancelled", result);
            return;
        }
        if (elapsed.elapsed() >= timeoutMs)
        {
            process.kill();
            process.waitForFinished(5000);
            result.exitCode = process.exitCode();
            result.error = "RAVE sidecar timed out";
            result.stderrText = stderrOf(process);
            result.stdoutText = stdoutOf(process);
            finishTerminal(kStateFailed, qstr(result.error), result);
            return;
        }
    }

    result.exitCode = process.exitCode();
    result.stderrText = stderrOf(process);
    result.stdoutText = stdoutOf(process);

    if (job->cancelRequested.load())
    {
        result.ok = false;
        result.error = "cancelled";
        finishTerminal(kStateCancelled, "cancelled", result);
        return;
    }

    result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;
    if (result.ok && !juce::File(request.outputPath).existsAsFile())
    {
        result.ok = false;
        result.error = "RAVE sidecar completed successfully but did not create output: " + request.outputPath;
        finishTerminal(kStateFailed, qstr(result.error), result);
    }
    else if (!result.ok)
    {
        result.error = result.stderrText.isNotEmpty()
            ? result.stderrText
            : jstr("RAVE sidecar failed with exit code " + QString::number(result.exitCode));
        finishTerminal(kStateFailed, qstr(result.error), result);
    }
    else
    {
        finishTerminal(kStateFinished, "sidecar finished", result);
    }
}

} // namespace HDAW
