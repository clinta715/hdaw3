#pragma once

// Async offline RAVE model-training sidecar jobs.
//
// THREADING / PROJECTION INVARIANT: workers run only QProcess and filesystem
// validation. They do not touch ValueTree, ReadModel, processors, routing, or
// any realtime audio path. Progress is state-only; the training sidecar owns
// detailed logging on stdout/stderr.

#include "RaveService.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

class QProcess;

namespace HDAW {

struct RaveTrainingJobStatus
{
    int64_t jobId = 0;
    QString state;
    QString message;
    bool hasResult = false;
    RaveTrainingResult result;
};

class RaveTrainingJobManager
{
public:
    static constexpr const char* kStateRunning = "running";
    static constexpr const char* kStateFinished = "finished";
    static constexpr const char* kStateFailed = "failed";
    static constexpr const char* kStateCancelled = "cancelled";

    using NotifyFn = std::function<void(int64_t jobId, const QString& state, const QString& message)>;

    RaveTrainingJobManager() = default;
    ~RaveTrainingJobManager() { shutdown(); }

    RaveTrainingJobManager(const RaveTrainingJobManager&) = delete;
    RaveTrainingJobManager& operator=(const RaveTrainingJobManager&) = delete;

    int64_t startJob(const RaveTrainingRequest& request, NotifyFn notify = {});
    bool jobStatus(int64_t jobId, RaveTrainingJobStatus& out) const;
    bool cancelJob(int64_t jobId, RaveTrainingJobStatus& out);
    void shutdown();

private:
    struct Job
    {
        mutable std::mutex mutex;
        int64_t id = 0;
        RaveTrainingRequest request;
        QString state = kStateRunning;
        QString message;
        RaveTrainingResult result;
        bool hasResult = false;
        std::atomic<bool> cancelRequested{ false };
        QProcess* process = nullptr;
        std::thread thread;
        NotifyFn notify;
    };

    void runJob(std::shared_ptr<Job> job);
    static void fillSnapshot(const std::shared_ptr<Job>& job, RaveTrainingJobStatus& out);
    static bool failFastError(const RaveTrainingRequest& request, QString& errorOut);

    mutable std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<Job>> jobs_;
    int64_t nextId_ = 1;
    bool stopping_ = false;

    static constexpr size_t kMaxRetainedJobs = 64;
};

} // namespace HDAW
