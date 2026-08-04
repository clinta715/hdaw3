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
    std::atomic<uint32_t> lastHeartbeat{0};

    ChildInfo() = default;
    ChildInfo(ChildInfo&& o) noexcept
        : processHandle(o.processHandle)
        , pipeName(std::move(o.pipeName))
        , shmName(std::move(o.shmName))
        , pipe(std::move(o.pipe))
        , shm(std::move(o.shm))
        , alive(o.alive.load())
        , lastHeartbeat(o.lastHeartbeat.load())
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
            lastHeartbeat.store(o.lastHeartbeat.load());
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

    bool spawnPluginHost(const std::string& pluginPath, uint32_t slotId);
    bool killPluginHost(uint32_t slotId, KillMode mode);
    bool isAlive(uint32_t slotId);
    bool isChildAlive(uint32_t slotId) const;

    const ChildInfo* getChildInfo(uint32_t slotId) const;

    PipeServer* getPipe(uint32_t slotId);
    ShmRegion* getShm(uint32_t slotId);

    bool sendHeartbeat(uint32_t slotId);
    bool checkHealth(uint32_t slotId, uint32_t staleThresholdMs = 2000);

    void setCrashCallback(CrashCallback cb) { crashCallback = std::move(cb); }
    void setSlotCrashCallback(uint32_t slotId, CrashCallback cb);
    void removeSlotCrashCallback(uint32_t slotId);
    void checkAllChildren(uint32_t staleThresholdMs = 2000);

    void startHealthMonitor(uint32_t intervalMs = 2000);
    void stopHealthMonitor();

    static std::string getHostExePath();

private:
    std::string makePipeName(uint32_t slotId);
    std::string makeShmName(uint32_t slotId);

    std::unordered_map<uint32_t, ChildInfo> children;
    mutable std::mutex mutex;
    CrashCallback crashCallback;
    std::unordered_map<uint32_t, CrashCallback> perSlotCrashCallbacks;

    std::thread healthThread;
    std::atomic<bool> healthMonitorRunning{false};
    uint32_t healthMonitorIntervalMs = 2000;
};

} // namespace proxy
