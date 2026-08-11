#include "ProxyProcessManager.h"
#include "../common/DebugLog.h"
#include <chrono>
#include <cstring>

namespace proxy {

ProxyProcessManager::ProxyProcessManager() = default;

ProxyProcessManager::~ProxyProcessManager() {
    stopHealthMonitor();
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [id, info] : children) {
        if (info.processHandle != INVALID_HANDLE_VALUE) {
            TerminateProcess(info.processHandle, 0);
            WaitForSingleObject(info.processHandle, 1000);
            CloseHandle(info.processHandle);
        }
    }
}

bool ProxyProcessManager::spawnPluginHost(const std::string& pluginPath, uint32_t slotId) {
    HDAW_LOG("proxy", "spawnPluginHost: slotId=" + std::to_string(slotId) + " plugin=" + pluginPath);

    // Defensively terminate + release any orphaned child/pipe/shm for this slot
    // before creating new ones. killPluginHost takes the mutex internally, so we
    // must NOT already hold it here. Returns false if none — harmless. This
    // guards against a stale child from a previous spawn that was never reaped
    // (e.g. an orphaned process still holding the pipe/shm names), which would
    // otherwise make CreateNamedPipe/ShmRegion::create collide and fail.
    // fullCleanup=true: the old pipe/shm names must be freed before new ones
    // can be created with the same slot id. Safe because spawnPluginHost runs
    // on the message thread during graph rebuild (audio callback completed).
    killPluginHost(slotId, KillMode::KillHard);

    // Create pipe and shm outside the lock
    auto pipeName = makePipeName(slotId);
    auto shmNameStr = makeShmName(slotId);
    auto hostExe = getHostExePath();

    HDAW_LOG("proxy", "spawnPluginHost: hostExe=" + hostExe + " plugin=" + pluginPath);

    auto pipeServer = std::make_unique<PipeServer>(pipeName);
    if (!pipeServer->start()) {
        HDAW_LOG("proxy", "spawnPluginHost: PipeServer::start() FAILED for " + pipeName);
        return false;
    }

    auto shmRegion = std::make_unique<ShmRegion>();
    // Size the mapping for the worst-case config (see kMaxShm* in
    // ProxyCommon.h) — the child grows hdr->capacity at PREPARE for
    // multi-channel plugins / large device block sizes, and both sides
    // index the rings with hdr->capacity, so the mapping must cover it.
    uint32_t shmSize = computeShmSize(kMaxShmChannels, kMaxShmBlockSize);
    if (!shmRegion->create(shmNameStr, shmSize)) {
        HDAW_LOG("proxy", "spawnPluginHost: ShmRegion::create() FAILED for " + shmNameStr);
        pipeServer->stop();
        return false;
    }

    // Initialize the ring buffer capacity (power of 2 >= blockSize * numChannels)
    {
        auto* hdr = shmRegion->getHeader();
        if (hdr) {
            uint32_t cap = 1;
            while (cap < 512u * 2u) cap <<= 1;
            hdr->capacity = cap;
        }
    }

    std::string cmdLine = "\"" + hostExe + "\""
        + " --slot=" + std::to_string(slotId)
        + " --pipe=" + pipeName
        + " --shm=" + shmNameStr
        + " \"--plugin=" + pluginPath + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessA(
        nullptr, cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        HDAW_LOG("proxy", "spawnPluginHost: CreateProcessA FAILED error=" + std::to_string(static_cast<int>(GetLastError())));
        pipeServer->stop();
        return false;
    }

    CloseHandle(pi.hThread);

    HDAW_LOG("proxy", "spawnPluginHost: child spawned, waiting for READY");

    // Wait for READY outside the lock (with timeout)
    // Child sends READY as a ProxyResponse
    ProxyResponse readyResp{};
    if (!pipeServer->receiveResp(readyResp)) {
        HDAW_LOG("proxy", "spawnPluginHost: READY timeout or pipe error for slot " + std::to_string(slotId));
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }

    HDAW_LOG("proxy", "spawnPluginHost: READY received for slot " + std::to_string(slotId));

    // Now take the lock to insert the child info
    ChildInfo info;
    info.processHandle = pi.hProcess;
    info.pipeName = pipeName;
    info.shmName = shmNameStr;
    info.pipe = std::move(pipeServer);
    info.shm = std::move(shmRegion);
    info.alive.store(true);

    {
        std::lock_guard<std::mutex> lock(mutex);
        children.erase(slotId);
        children.emplace(slotId, std::move(info));
    }
    return true;
}

bool ProxyProcessManager::killPluginHost(uint32_t slotId, KillMode mode) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    PipeServer* pipe = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = children.find(slotId);
        if (it == children.end()) return false;
        auto& info = it->second;
        handle = info.processHandle;
        pipe = info.pipe.get();
        info.alive.store(false);
        if (mode == KillMode::KillHard) {
            if (info.pipe) info.pipe->stop();
            children.erase(it);
        }
    }

    if (handle != INVALID_HANDLE_VALUE) {
        if (mode == KillMode::KillGraceful) {
            TerminateProcess(handle, proxy::GRACEFUL_EXIT_CODE);
        } else {
            TerminateProcess(handle, 0);
            WaitForSingleObject(handle, 1000);
        }
        CloseHandle(handle);
    }

    if (mode == KillMode::KillGraceful) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = children.find(slotId);
        if (it != children.end()) {
            if (it->second.pipe) it->second.pipe->stop();
            children.erase(it);
        }
    }
    return true;
}

bool ProxyProcessManager::isChildAlive(uint32_t slotId) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it == children.end()) return false;
    return it->second.alive.load();
}

bool ProxyProcessManager::isAlive(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it == children.end()) return false;

    auto& info = it->second;
    if (info.processHandle == INVALID_HANDLE_VALUE) return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(info.processHandle, &exitCode)) return false;
    if (exitCode != STILL_ACTIVE) {
        info.alive.store(false);
        return false;
    }
    return true;
}

const ChildInfo* ProxyProcessManager::getChildInfo(uint32_t slotId) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? &it->second : nullptr;
}

PipeServer* ProxyProcessManager::getPipe(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? it->second.pipe.get() : nullptr;
}

ShmRegion* ProxyProcessManager::getShm(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? it->second.shm.get() : nullptr;
}

bool ProxyProcessManager::sendHeartbeat(uint32_t slotId) {
    auto* pipe = getPipe(slotId);
    if (!pipe) return false;

    ProxyMessage msg{};
    msg.type = MessageType::HEARTBEAT;
    msg.slotId = slotId;
    if (!pipe->sendMsg(msg)) return false;

    ProxyResponse resp{};
    return pipe->receiveResp(resp);
}

bool ProxyProcessManager::checkHealth(uint32_t slotId, uint32_t /*staleThresholdMs*/) {
    return isAlive(slotId);
}

void ProxyProcessManager::checkAllChildren(uint32_t staleThresholdMs) {
    std::vector<uint32_t> crashedSlots;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& [id, info] : children) {
            if (info.processHandle == INVALID_HANDLE_VALUE) continue;

            DWORD exitCode = 0;
            if (!GetExitCodeProcess(info.processHandle, &exitCode)) {
                crashedSlots.push_back(id);
                continue;
            }
            if (exitCode == proxy::GRACEFUL_EXIT_CODE) {
                info.alive.store(false);
                continue;
            }
            if (exitCode != STILL_ACTIVE) {
                info.alive.store(false);
                crashedSlots.push_back(id);
                continue;
            }

            uint64_t currentBlocks = 0;
            bool inputPending = false;
            if (info.shm && info.shm->getHeader()) {
                auto* hdr = info.shm->getHeader();
                currentBlocks = hdr->audioBlocksProcessed.load(std::memory_order_relaxed);
                const uint32_t w = hdr->inputWritePos.load(std::memory_order_acquire);
                const uint32_t r = hdr->inputReadPos.load(std::memory_order_acquire);
                inputPending = (w != r);
            }

            auto nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            if (currentBlocks == info.lastBlocksSnapshot) {
                if (!inputPending) {
                    // Idle, not hung: the parent isn't feeding blocks (e.g.
                    // transport stopped → graph not processed → nothing written
                    // to the input ring), so no progress is expected. Keep the
                    // stall timer reset so a healthy idle child is never killed.
                    info.lastSnapshotMs = nowMs;
                } else if (info.lastSnapshotMs == 0) {
                    info.lastSnapshotMs = nowMs;
                } else if (nowMs - info.lastSnapshotMs > staleThresholdMs) {
                    crashedSlots.push_back(id);
                    continue;
                }
            } else {
                info.lastBlocksSnapshot = currentBlocks;
                info.lastSnapshotMs = nowMs;
            }
        }
    }

    for (auto id : crashedSlots) {
        auto it = perSlotCrashCallbacks.find(id);
        if (it != perSlotCrashCallbacks.end())
            it->second(id);
    }
}

std::string ProxyProcessManager::getHostExePath() {
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    auto path = std::string(buf);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    return path + "hdaw_plugin_host.exe";
}

std::string ProxyProcessManager::makePipeName(uint32_t slotId) {
    return "\\\\.\\pipe\\hdaw_plugin_" + std::to_string(slotId);
}

std::string ProxyProcessManager::makeShmName(uint32_t slotId) {
    return "hdaw_plugin_shm_" + std::to_string(slotId);
}

void ProxyProcessManager::setSlotCrashCallback(uint32_t slotId, CrashCallback cb) {
    std::lock_guard<std::mutex> lock(mutex);
    perSlotCrashCallbacks[slotId] = std::move(cb);
}

void ProxyProcessManager::removeSlotCrashCallback(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    perSlotCrashCallbacks.erase(slotId);
}

void ProxyProcessManager::startHealthMonitor(uint32_t intervalMs) {
    if (healthMonitorRunning.load()) return;
    healthMonitorIntervalMs = intervalMs;
    healthMonitorRunning.store(true);
    healthThread = std::thread([this]() {
        while (healthMonitorRunning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(healthMonitorIntervalMs));
            if (healthMonitorRunning.load())
                checkAllChildren(healthMonitorIntervalMs * 2);
        }
    });
}

void ProxyProcessManager::stopHealthMonitor() {
    healthMonitorRunning.store(false);
    if (healthThread.joinable())
        healthThread.join();
}

} // namespace proxy
