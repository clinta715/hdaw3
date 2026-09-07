#pragma once

// Async RAVE sidecar jobs (RAVE #3).
//
// RaveService::transformFile blocks the calling thread for up to 10 minutes
// (QProcess::waitForFinished). Calling it on the RPC/message thread stalls
// every other RPC. RaveJobManager moves the sidecar launch onto a per-job
// worker thread: clients submit (startJob), poll (jobStatus), and cancel
// (cancelJob), with started/terminal notifications for the frontend.
//
// THREADING / PROJECTION INVARIANT (see AGENTS.md lessons 11-13):
// The worker thread runs ONLY the sidecar QProcess plus the notify callback.
// It NEVER touches the ValueTree, ReadModel, commands, processors, or the
// AudioEngine. Project mutation stays on the message thread: on terminal
// success the client calls the existing sync rave.importResult RPC, which
// runs on the message thread like every other dispatch.
//
// Why the launch lives here instead of calling RaveService::transformFile:
// transformFile is blocking with no cancel hook and RaveService.* is frozen,
// so cancel could never kill its internal QProcess. The worker below hosts an
// equivalent cancellable launch with IDENTICAL sidecar semantics (same fail-
// fast validation order/messages, same python/script/timeout resolution via
// the shared RaveService::resolve* helpers — request > QSettings > env —
// same argv, same result mapping) but waits in
// 100 ms slices so cancel/timeout/shutdown kill the process promptly.
// QProcess is created, started, waited on, and killed on the SAME worker
// thread; cancelJob only sets an atomic flag and calls kill() on the live
// process pointer under the job lock (kill() is a direct OS-level call with
// no event-loop involvement).

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

struct RaveJobStatus
{
    int64_t jobId = 0;
    // One of "running", "finished", "failed", "cancelled".
    // (The sidecar gives no percent progress, so states only — no fake %.)
    QString state;
    QString message;
    bool hasResult = false;
    // Valid when hasResult (finished/failed carry the sidecar result;
    // cancelled carries the partial output captured before the kill).
    RaveTransformResult result;
};

class RaveJobManager
{
public:
    static constexpr const char* kStateRunning = "running";
    static constexpr const char* kStateFinished = "finished";
    static constexpr const char* kStateFailed = "failed";
    static constexpr const char* kStateCancelled = "cancelled";

    // (jobId, state, message). Invoked from the worker thread for started +
    // terminal states, and from the calling thread for fail-fast completions.
    // Must be thread-safe (the frontend routes it through
    // FrontendServer::broadcastNotificationFromAnyThread); may be empty.
    using NotifyFn = std::function<void(int64_t jobId, const QString& state, const QString& message)>;

    RaveJobManager() = default;
    ~RaveJobManager() { shutdown(); }

    RaveJobManager(const RaveJobManager&) = delete;
    RaveJobManager& operator=(const RaveJobManager&) = delete;

    // Validate (fail-fast, no thread/process on invalid input) then run the
    // sidecar on a new worker thread. Returns the job id. Terminal results
    // are retained (bounded: last 64) so jobStatus-after-finish works.
    int64_t startJob(const RaveTransformRequest& request, NotifyFn notify = {});

    // False when the id is unknown.
    bool jobStatus(int64_t jobId, RaveJobStatus& out) const;

    // Flag a running job for cancellation and kill its live process.
    // Idempotent on terminal jobs (returns their current snapshot).
    // False ONLY when the id is unknown (clear not-found, never silent ok).
    bool cancelJob(int64_t jobId, RaveJobStatus& out);

    // Cancel all jobs and join all workers. Bounded: each worker wakes from
    // its 100 ms wait slice, kills its process, and exits — never the full
    // 10-minute sidecar timeout. Engine teardown relies on this.
    void shutdown();

private:
    struct Job
    {
        mutable std::mutex mutex;
        int64_t id = 0;
        RaveTransformRequest request;
        QString state = kStateRunning;
        QString message;
        RaveTransformResult result;
        bool hasResult = false;
        std::atomic<bool> cancelRequested{ false };
        // Owned by the worker thread's stack frame; non-null only while the
        // sidecar process is live. Cross-thread access under mutex, and only
        // for kill() — never wait/read/start.
        QProcess* process = nullptr;
        std::thread thread;
        NotifyFn notify;
    };

    void runJob(std::shared_ptr<Job> job);
    static void fillSnapshot(const std::shared_ptr<Job>& job, RaveJobStatus& out);
    static bool failFastError(const RaveTransformRequest& request, QString& errorOut);

    mutable std::mutex mutex_;
    std::map<int64_t, std::shared_ptr<Job>> jobs_;
    int64_t nextId_ = 1;
    bool stopping_ = false;

    static constexpr size_t kMaxRetainedJobs = 64;
};

} // namespace HDAW
