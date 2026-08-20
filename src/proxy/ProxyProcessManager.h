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

namespace proxy {

struct ChildInfo {
    HANDLE processHandle = INVALID_HANDLE_VALUE;
    std::string pipeName;
    std::string shmName;
    std::unique_ptr<PipeServer> pipe;
    std::unique_ptr<ShmRegion> shm;
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

    const ChildInfo* getChildInfo(uint32_t slotId) const;

    PipeServer* getPipe(uint32_t slotId);
    ShmRegion* getShm(uint32_t slotId);

    bool sendHeartbeat(uint32_t slotId);
    bool checkHealth(uint32_t slotId, uint32_t staleThresholdMs = 2000);

    void setSlotCrashCallback(uint32_t slotId, CrashCallback cb);
    void removeSlotCrashCallback(uint32_t slotId);
    void checkAllChildren(uint32_t staleThresholdMs = 2000);

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
