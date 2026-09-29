#pragma once
#include "ProxyCommon.h"
#include "ProxyPipe.h"
#include "ProxySharedMemory.h"
#include <windows.h>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace proxy {

struct ChildInfo {
    HANDLE processHandle = INVALID_HANDLE_VALUE;
    std::string pipeName;
    std::string shmName;
    // LEASE-BASED OWNERSHIP. The map holds the owning reference; getPipe()/
    // getShm() hand out a shared_ptr COPY taken under the mutex, so a caller
    // can hold the object alive across a whole exchange even if a concurrent
    // killPluginHost() erases the map entry. The object (and its OS handle) is
    // destroyed at the LAST lease release — killPluginHost only signals +
    // cancels (PipeServer::stop) and drops the map's own reference.
    std::shared_ptr<PipeServer> pipe;
    std::shared_ptr<ShmRegion> shm;
    std::atomic<bool> alive{false};
    uint64_t lastBlocksSnapshot{0};
    uint64_t lastSnapshotMs{0};
    bool crashNotified = false;

    ChildInfo() = default;
    ChildInfo(ChildInfo&& o) noexcept
        : processHandle(o.processHandle)
        , pipeName(std::move(o.pipeName))
        , shmName(std::move(o.shmName))
        , pipe(std::move(o.pipe))
        , shm(std::move(o.shm))
        , alive(o.alive.load())
        , lastBlocksSnapshot(o.lastBlocksSnapshot)
        , lastSnapshotMs(o.lastSnapshotMs)
        , crashNotified(o.crashNotified)
    {
        o.processHandle = INVALID_HANDLE_VALUE;
    }
    ChildInfo& operator=(ChildInfo&& o) noexcept {
        if (this != &o) {
            processHandle = o.processHandle;
            pipeName = std::move(o.pipeName);
            shmName = std::move(o.shmName);
            pipe = std::move(o.pipe);
            shm = std::move(o.shm);
            alive.store(o.alive.load());
            lastBlocksSnapshot = o.lastBlocksSnapshot;
            lastSnapshotMs = o.lastSnapshotMs;
            crashNotified = o.crashNotified;
            o.processHandle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    ChildInfo(const ChildInfo&) = delete;
    ChildInfo& operator=(const ChildInfo&) = delete;
};

using CrashCallback = std::function<void(uint32_t slotId)>;

enum class KillMode {
    KillGraceful,
    KillHard
};

class ProxyProcessManager {
public:
    ProxyProcessManager();
    ~ProxyProcessManager();

    ProxyProcessManager(const ProxyProcessManager&) = delete;
    ProxyProcessManager& operator=(const ProxyProcessManager&) = delete;

    bool spawnPluginHost(const std::string& pluginPath, uint32_t slotId, uint32_t* actualSlotId = nullptr);
    bool killPluginHost(uint32_t slotId, KillMode mode);
    bool isAlive(uint32_t slotId);
    bool isChildAlive(uint32_t slotId) const;

    // RAW pointer into the child map — NOT a lease. It is valid only while no
    // concurrent killPluginHost()/erase for this slot can run; once the mutex
    // is released the entry (and the ChildInfo, and its process handle) may be
    // freed by another thread. Production code must use the lock-scoped helper
    // terminateChild() or the leases (getPipe/getShm); this accessor remains
    // for tests that need the raw process handle to simulate an external kill
    // while holding the manager exclusively.
    const ChildInfo* getChildInfo(uint32_t slotId) const;

    // Terminates the child process for `slotId` under the manager's own lock
    // (the out-of-band "external kill" seam) — no handle is handed out and
    // nothing can race the close. The map entry intentionally stays: the
    // health sweep observes the exit and flags the crash, exactly as before.
    // Returns false when the slot is unknown or has no process handle.
    bool terminateChild(uint32_t slotId, uint32_t exitCode = 0);

    // Lease copy taken under the mutex; null when the slot is unknown. Hold the
    // returned lease for the WHOLE exchange (a kill on another thread must not
    // free the pipe/shm under the caller).
    std::shared_ptr<PipeServer> getPipe(uint32_t slotId);
    std::shared_ptr<ShmRegion> getShm(uint32_t slotId);

    bool sendHeartbeat(uint32_t slotId);
    bool checkHealth(uint32_t slotId, uint32_t staleThresholdMs = 2000);

    void setSlotCrashCallback(uint32_t slotId, CrashCallback cb);
    void removeSlotCrashCallback(uint32_t slotId);
    void checkAllChildren(uint32_t staleThresholdMs = 2000);

    // Invoke the per-slot crash callbacks for `ids`, OUTSIDE the manager
    // mutex. `perSlotCrashCallbacks` is mutated under `mutex` by
    // setSlotCrashCallback/removeSlotCrashCallback, and the latter runs from
    // ~PluginProxySlot on the message thread while the health-monitor thread
    // sweeps the map — so the sweep must NOT hold iterators/references into it
    // while invoking (the callbacks re-enter PluginManager /
    // CrashRecoveryManager, so the lock must not be held during the call
    // either).
    //
    // CONTRACT (snapshot semantics): the callback set is snapshotted under the
    // lock (each std::function is COPIED) and then invoked after the lock is
    // released. A callback removed concurrently with a sweep may therefore
    // still be invoked ONCE — that is intended: the map itself is never read
    // while another thread mutates it, so the sweep can neither crash nor
    // observe a half-erased entry. For the same reason a callback registered
    // after the snapshot is not invoked by that sweep; the next sweep sees it.
    void invokeCrashCallbacks(const std::vector<uint32_t>& ids);

    // TEST-ONLY seam, read on the LIVE invoke path: called exactly once by
    // invokeCrashCallbacks AFTER the callback snapshot has been taken under the
    // lock and BEFORE any callback is invoked. It lets a test deterministically
    // erase a callback in the snapshot→invoke window (the window the snapshot
    // exists to make safe) instead of trying to hit a probabilistic race.
    // Production never sets it.
    std::function<void()> crashCallbackSnapshotHookForTest;

    void startHealthMonitor(uint32_t intervalMs = 2000);
    void stopHealthMonitor();

    // Per-domain namespace for the OS named objects (pipes/shm) this manager
    // creates. The constructor AUTO-GENERATES a unique prefix (pid hex +
    // process-wide instance counter) via makeUniqueNamespacePrefix, so every
    // ProxyProcessManager owns a distinct OS name namespace by construction
    // and the old "must be set before any spawnPluginHost call" contract is
    // already satisfied from the ctor. This raw setter remains an escape hatch
    // for callers that need a specific prefix verbatim; for domain labels
    // (e.g. "export_") prefer PluginManager::setProxyNamespacePrefix, which
    // ALWAYS appends uniqueness on top of the label. Children are agnostic:
    // they receive the exact pipe/shm names on their command line.
    void setNamePrefix(const std::string& prefix) { namePrefix = prefix; }
    const std::string& getNamePrefix() const { return namePrefix; }

    // Returns domainLabel + "<pid-hex>_" + "<process-wide instance counter>_" so
    // every ProxyProcessManager gets a unique OS name namespace by construction.
    static std::string makeUniqueNamespacePrefix(const std::string& domainLabel);

    static std::string getHostExePath();

    std::string makePipeName(uint32_t slotId) const;
    std::string makeShmName(uint32_t slotId) const;

    std::string namePrefix;

    std::unordered_map<uint32_t, ChildInfo> children;
    mutable std::mutex mutex;
    std::unordered_map<uint32_t, CrashCallback> perSlotCrashCallbacks;

    std::thread healthThread;
    std::atomic<bool> healthMonitorRunning{false};
    uint32_t healthMonitorIntervalMs = 2000;
};

} // namespace proxy
