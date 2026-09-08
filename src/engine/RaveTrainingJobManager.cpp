#include "RaveTrainingJobManager.h"

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

juce::String stdoutOf(QProcess& process)
{
    return juce::String(process.readAllStandardOutput().constData());
}

juce::String stderrOf(QProcess& process)
{
    return juce::String(process.readAllStandardError().constData());
}

void emitNotify(const RaveTrainingJobManager::NotifyFn& notify, int64_t jobId,
                const QString& state, const QString& message)
{
    if (notify)
        notify(jobId, state, message);
}

bool containsWavFile(const juce::File& dir)
{
    if (!dir.isDirectory())
        return false;
    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, true, "*.wav");
    return files.size() > 0;
}

} // namespace

bool RaveTrainingJobManager::failFastError(const RaveTrainingRequest& request, QString& errorOut)
{
    const juce::File dataset(request.datasetPath);
    if (request.datasetPath.isEmpty() || !dataset.isDirectory())
    {
        errorOut = qstr("dataset directory not found: " + request.datasetPath);
        return true;
    }
    if (!containsWavFile(dataset))
    {
        errorOut = qstr("dataset contains no .wav files: " + request.datasetPath);
        return true;
    }
    if (request.outputModelPath.isEmpty())
    {
        errorOut = "outputModelPath required";
        return true;
    }
    const juce::File output(request.outputModelPath);
    auto parent = output.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
    {
        errorOut = qstr("output model directory could not be created: " + parent.getFullPathName());
        return true;
    }
    const QString scriptPath = qstr(RaveService::resolveTrainScriptPath(request.scriptPath));
    if (scriptPath.isEmpty())
    {
        errorOut = "RAVE training script not configured (set scriptPath or HDAW_RAVE_TRAIN_SCRIPT)";
        return true;
    }
    if (!juce::File(scriptPath.toStdString()).existsAsFile())
    {
        errorOut = "RAVE training script not found: " + scriptPath;
        return true;
    }
    if (request.epochs <= 0)
    {
        errorOut = "epochs must be > 0";
        return true;
    }
    if (request.batchSize <= 0)
    {
        errorOut = "batchSize must be > 0";
        return true;
    }
    if (request.sampleRate <= 0)
    {
        errorOut = "sampleRate must be > 0";
        return true;
    }
    return false;
}

void RaveTrainingJobManager::fillSnapshot(const std::shared_ptr<Job>& job, RaveTrainingJobStatus& out)
{
    std::lock_guard<std::mutex> lock(job->mutex);
    out.jobId = job->id;
    out.state = job->state;
    out.message = job->message;
    out.hasResult = job->hasResult;
    out.result = job->result;
}

int64_t RaveTrainingJobManager::startJob(const RaveTrainingRequest& request, NotifyFn notify)
{
    auto job = std::make_shared<Job>();
    job->request = request;
    job->result.outputModelPath = request.outputModelPath;
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
                break;
            if (it->second->thread.joinable())
                it->second->thread.join();
            jobs_.erase(it);
        }
        jobs_[job->id] = job;
    }

    if (invalid)
    {
        emitNotify(job->notify, job->id, kStateRunning, "validating");
        emitNotify(job->notify, job->id, kStateFailed, error);
        return job->id;
    }

    job->thread = std::thread([this, job] { runJob(job); });
    return job->id;
}

bool RaveTrainingJobManager::jobStatus(int64_t jobId, RaveTrainingJobStatus& out) const
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

bool RaveTrainingJobManager::cancelJob(int64_t jobId, RaveTrainingJobStatus& out)
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
            if (job->process != nullptr)
                job->process->kill();
        }
    }
    fillSnapshot(job, out);
    return true;
}

void RaveTrainingJobManager::shutdown()
{
    std::map<int64_t, std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs = jobs_;
        for (const auto& [id, job] : jobs_)
        {
            std::lock_guard<std::mutex> jobLock(job->mutex);
            job->cancelRequested.store(true);
            if (job->process != nullptr)
                job->process->kill();
        }
    }
    for (const auto& [id, job] : jobs)
    {
        if (job->thread.joinable())
            job->thread.join();
    }
}

void RaveTrainingJobManager::runJob(std::shared_ptr<Job> job)
{
    emitNotify(job->notify, job->id, kStateRunning, "training sidecar started");

    const RaveTrainingRequest request = job->request;
    RaveTrainingResult result;
    result.outputModelPath = request.outputModelPath;

    const QString scriptPath = qstr(RaveService::resolveTrainScriptPath(request.scriptPath));
    const QString pythonPath = qstr(RaveService::resolvePythonPath(request.pythonPath));

    QStringList args;
    args << scriptPath
         << "--dataset" << qstr(request.datasetPath)
         << "--output-model" << qstr(request.outputModelPath)
         << "--name" << qstr(request.name)
         << "--epochs" << QString::number(request.epochs)
         << "--batch-size" << QString::number(request.batchSize)
         << "--sample-rate" << QString::number(request.sampleRate);

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
        RaveTrainingJobStatus snap;
        fillSnapshot(job, snap);
        emitNotify(job->notify, job->id, kStateCancelled, snap.message);
        return;
    }

    auto finishTerminal = [&](const QString& state, const QString& message, const RaveTrainingResult& r) {
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
        result.error = jstr("failed to start RAVE training sidecar: " + QString(process.errorString().toUtf8().constData()));
        result.stderrText = stderrOf(process);
        result.stdoutText = stdoutOf(process);
        finishTerminal(kStateFailed, qstr(result.error), result);
        return;
    }

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
            result.error = "RAVE training sidecar timed out";
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
    if (result.ok && !juce::File(request.outputModelPath).existsAsFile())
    {
        result.ok = false;
        result.error = "RAVE training sidecar completed successfully but did not create output model: " + request.outputModelPath;
        finishTerminal(kStateFailed, qstr(result.error), result);
    }
    else if (!result.ok)
    {
        result.error = result.stderrText.isNotEmpty()
            ? result.stderrText
            : jstr("RAVE training sidecar failed with exit code " + QString::number(result.exitCode));
        finishTerminal(kStateFailed, qstr(result.error), result);
    }
    else
    {
        finishTerminal(kStateFinished, "training finished", result);
    }
}

} // namespace HDAW
