#include "ProxyProcessManager.h"
#include <chrono>
#include <cstring>

namespace proxy {

ProxyProcessManager::ProxyProcessManager() = default;

ProxyProcessManager::~ProxyProcessManager() {
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
    std::lock_guard<std::mutex> lock(mutex);

    if (children.count(slotId) && children[slotId].processHandle != INVALID_HANDLE_VALUE)
        return false;

    auto pipeName = makePipeName(slotId);
    auto shmNameStr = makeShmName(slotId);
    auto hostExe = getHostExePath();

    auto pipeServer = std::make_unique<PipeServer>(pipeName);
    if (!pipeServer->start()) return false;

    auto shmRegion = std::make_unique<ShmRegion>();
    uint32_t shmSize = computeShmSize(2, 512);
    if (!shmRegion->create(shmNameStr, shmSize)) {
        pipeServer->stop();
        return false;
    }

    std::string cmdLine = "\"" + hostExe + "\""
        + " --slot=" + std::to_string(slotId)
        + " --pipe=" + pipeName
        + " --shm=" + shmNameStr
        + " --plugin=" + pluginPath;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessA(
        nullptr,
        cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        pipeServer->stop();
        return false;
    }

    CloseHandle(pi.hThread);

    ProxyMessage readyMsg{};
    if (!pipeServer->receive(readyMsg)) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }

    ChildInfo info;
    info.processHandle = pi.hProcess;
    info.pipeName = pipeName;
    info.shmName = shmNameStr;
    info.pipe = std::move(pipeServer);
    info.shm = std::move(shmRegion);
    info.alive.store(true);
    info.lastHeartbeat.store(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    children.erase(slotId);
    children.emplace(slotId, std::move(info));
    return true;
}

bool ProxyProcessManager::killPluginHost(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it == children.end()) return false;

    auto& info = it->second;
    if (info.processHandle != INVALID_HANDLE_VALUE) {
        TerminateProcess(info.processHandle, 0);
        WaitForSingleObject(info.processHandle, 1000);
        CloseHandle(info.processHandle);
        info.processHandle = INVALID_HANDLE_VALUE;
    }
    if (info.pipe) info.pipe->stop();
    info.alive.store(false);
    children.erase(it);
    return true;
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
    if (!pipe->receiveResp(resp)) return false;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it != children.end()) {
        it->second.lastHeartbeat.store(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    return true;
}

bool ProxyProcessManager::checkHealth(uint32_t slotId, uint32_t staleThresholdMs) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it == children.end()) return false;

    auto& info = it->second;
    if (!isAlive(slotId)) return false;

    auto now = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto last = info.lastHeartbeat.load();
    auto elapsed = (now > last) ? (now - last) : 0;
    auto elapsedMs = elapsed / 1000;

    return elapsedMs < staleThresholdMs;
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

} // namespace proxy
