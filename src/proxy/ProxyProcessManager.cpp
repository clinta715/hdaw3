#include "ProxyProcessManager.h"
#include "../common/DebugLog.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#if !defined(_WIN32)
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <cerrno>
extern char** environ;
#endif

namespace proxy {

// Process-wide instance counter backing makeUniqueNamespacePrefix: every
// ProxyProcessManager (live + each offline/export copy) consumes one value,
// so no two managers in one process ever share an OS name namespace.
static std::atomic<uint32_t> gNamespaceInstanceCounter{0};

ProxyProcessManager::ProxyProcessManager() {
    namePrefix = makeUniqueNamespacePrefix("");
}

#if !defined(_WIN32)
// ── Linux process helpers ──────────────────────────────────────────────────
// One WNOHANG waitpid. Returns true when the child is gone (reaped — the raw
// waitpid status is cached into `info` when non-null — or already unknown),
// false when it is still running. Reaping happens EXACTLY ONCE per pid; the
// cached status backs every later health-sweep classification (there is no
// STILL_ACTIVE re-query like GetExitCodeProcess).
static bool reapOnce(pid_t pid, ChildInfo* info) {
    int st = 0;
    pid_t r = ::waitpid(pid, &st, WNOHANG);
    if (r == 0) return false;
    if (info && r > 0) {
        info->reaped = true;
        info->exitStatus = st;
    }
    return true;
}

// Blocking reap (after SIGKILL this returns promptly); caches into `info`.
static void reapBlocking(pid_t pid, ChildInfo* info) {
    int st = 0;
    pid_t r = ::waitpid(pid, &st, 0);
    if (info && r > 0) {
        info->reaped = true;
        info->exitStatus = st;
    }
}

// kill SIGKILL + blocking reap — the TerminateProcess(0)+CloseHandle analog
// for the READY-failure / legacy-v1 / version-mismatch spawn paths.
static void hardKillAndReap(pid_t pid) {
    ::kill(pid, SIGKILL);
    int st = 0;
    ::waitpid(pid, &st, 0);
}

// Graceful when the child exited normally with the shared GRACEFUL_EXIT_CODE
// (both sides include ProxyCommon.h, so the constant cannot drift).
static bool gracefulExit(const ChildInfo& info) {
    return WIFEXITED(info.exitStatus)
        && WEXITSTATUS(info.exitStatus) == proxy::GRACEFUL_EXIT_CODE;
}

// 32-bit exit-code analog for the crash log: signal deaths have no WEXITSTATUS,
// so they map to 0x80000000|WTERMSIG (a code no graceful/normal exit can
// produce); normal exits report WEXITSTATUS verbatim.
static uint32_t exitCodeOf(const ChildInfo& info) {
    if (WIFEXITED(info.exitStatus))
        return static_cast<uint32_t>(WEXITSTATUS(info.exitStatus));
    return 0x80000000u | static_cast<uint32_t>(WTERMSIG(info.exitStatus));
}
#endif

std::string ProxyProcessManager::makeUniqueNamespacePrefix(const std::string& domainLabel) {
    char pidHex[16];
#if defined(_WIN32)
    std::snprintf(pidHex, sizeof(pidHex), "%x", static_cast<unsigned>(::GetCurrentProcessId()));
#else
    std::snprintf(pidHex, sizeof(pidHex), "%x", static_cast<unsigned>(::getpid()));
#endif
    return domainLabel + pidHex + "_" + std::to_string(gNamespaceInstanceCounter.fetch_add(1)) + "_";
}

ProxyProcessManager::~ProxyProcessManager() {
    stopHealthMonitor();
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [id, info] : children) {
        if (info.processHandle != INVALID_HANDLE_VALUE) {
#if defined(_WIN32)
            TerminateProcess(info.processHandle, 0);
            WaitForSingleObject(info.processHandle, 1000);
            CloseHandle(info.processHandle);
#else
            pid_t pid = static_cast<pid_t>(info.processHandle);
            ::kill(pid, SIGKILL);
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(1000);
            while (!reapOnce(pid, &info)) {
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
#endif
        }
    }
}

bool ProxyProcessManager::spawnPluginHost(const std::string& pluginPath, uint32_t slotId, uint32_t* actualSlotId) {
    HDAW_LOG("proxy", "spawnPluginHost: slotId=" + std::to_string(slotId) + " plugin=" + pluginPath);

    auto hostExe = getHostExePath();

    HDAW_LOG("proxy", "spawnPluginHost: hostExe=" + hostExe + " plugin=" + pluginPath);

    // Create pipe and shm outside the lock. The names are derived from the
    // slot id; if a name is still held at spawn time (an orphaned child from a
    // stale engine tree, or a same-slot squatter), creation fails and we bump
    // the slot id and retry with a fresh name, up to a bounded number of
    // attempts. Without this, any held name would fail the whole spawn even
    // though a later slot id is free.
    std::shared_ptr<PipeServer> pipeServer;
    std::shared_ptr<ShmRegion> shmRegion;
    std::string pipeName;
    std::string shmNameStr;

    constexpr int kMaxSpawnNameAttempts = 8;
    for (int attempt = 0; attempt < kMaxSpawnNameAttempts; ++attempt) {
        // Defensively terminate + release any orphaned child/pipe/shm for this
        // slot before creating new ones. killPluginHost takes the mutex
        // internally, so we must NOT already hold it here. Returns false if
        // none — harmless. This guards against a stale child from a previous
        // spawn that was never reaped (e.g. an orphaned process still holding
        // the pipe/shm names), which would otherwise make CreateNamedPipe/
        // ShmRegion::create collide and fail. One defensive kill per attempted
        // slot: preserves the existing behavior for the first attempt and is
        // harmless for bumped attempts.
        killPluginHost(slotId, KillMode::KillHard);

        pipeName = makePipeName(slotId);
        shmNameStr = makeShmName(slotId);

        pipeServer = std::make_shared<PipeServer>(pipeName);
        DWORD pipeErr = 0;
        if (!pipeServer->start(&pipeErr)) {
            HDAW_LOG("proxy", "spawnPluginHost: PipeServer::start() FAILED for " + pipeName + " error=" + std::to_string(static_cast<int>(pipeErr)));
            pipeServer.reset();
            ++slotId;
            continue;
        }

        shmRegion = std::make_shared<ShmRegion>();
        // Size the mapping for the worst-case config (see kMaxShm* in
        // ProxyCommon.h) — the child grows hdr->capacity at PREPARE for
        // multi-channel plugins / large device block sizes, and both sides
        // index the rings with hdr->capacity, so the mapping must cover it.
        uint32_t shmSize = computeShmSize(kMaxShmChannels, kMaxShmBlockSize);
        if (!shmRegion->create(shmNameStr, shmSize)) {
            HDAW_LOG("proxy", "spawnPluginHost: ShmRegion::create() FAILED for " + shmNameStr);
            pipeServer->stop();
            pipeServer.reset();
            shmRegion.reset();
            ++slotId;
            continue;
        }
        break;
    }

    if (!pipeServer || !shmRegion) {
        HDAW_LOG("proxy", "spawnPluginHost: exhausted " + std::to_string(kMaxSpawnNameAttempts) + " attempts creating pipe/shm names");
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

    HANDLE childProc = INVALID_HANDLE_VALUE;

#if defined(_WIN32)
    std::string cmdLine = "\"" + hostExe + "\""
        + " --slot=" + std::to_string(slotId)
        + " --pipe=" + pipeName
        + " --shm=" + shmNameStr
        + " \"--plugin=" + pluginPath + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Diagnostic: redirect the child's stdout to a file when
    // HDAW_PROXY_CHILD_STDOUT is set. Plugins that printf to stdout (e.g.
    // VirtualJV's MAME-core diagnostics: "Not enough samples!", "click")
    // become observable from the parent side. No effect when unset.
    HANDLE childStdoutFile = INVALID_HANDLE_VALUE;
    BOOL inheritHandles = FALSE;
    if (const char* childStdoutPath = getenv("HDAW_PROXY_CHILD_STDOUT");
        childStdoutPath != nullptr && childStdoutPath[0] != '\0')
    {
        SECURITY_ATTRIBUTES sa { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        childStdoutFile = CreateFileA(
            childStdoutPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (childStdoutFile != INVALID_HANDLE_VALUE)
        {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = childStdoutFile;
            si.hStdInput = childStdoutFile;
            si.hStdError = childStdoutFile;
            inheritHandles = TRUE;
        }
    }

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessA(
        nullptr, cmdBuf.data(),
        nullptr, nullptr, inheritHandles,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    if (childStdoutFile != INVALID_HANDLE_VALUE)
        CloseHandle(childStdoutFile);

    if (!ok) {
        HDAW_LOG("proxy", "spawnPluginHost: CreateProcessA FAILED error=" + std::to_string(static_cast<int>(GetLastError())));
        pipeServer->stop();
        return false;
    }

    CloseHandle(pi.hThread);

    childProc = pi.hProcess;

    HDAW_LOG("proxy", "spawnPluginHost: child spawned, waiting for READY");

    // Wait for READY outside the lock (with timeout)
    // Child sends READY as a ProxyResponse. This is an unsolicited await (the
    // parent sends nothing), so the guard's expectation is the READY type
    // itself; nothing else can be in flight on this pipe yet (it is not
    // published to the manager map until below), but running it through the
    // same guard keeps every pipe read on one path. The guard is SCOPED so the
    // exchange lock is released before the ChildInfo is published under the
    // manager's own mutex (lock order: never hold both).
    ProxyResponse readyResp{};
    bool ready = false;
    {
        PipeServer::Exchange readyEx(*pipeServer);
        ready = readyEx.receiveReplyReady(readyResp, MessageType::READY);
    }
    // LEGACY-LAYOUT GUARD (Gate 4/15): a child built against the pre-
    // correlation-id framing answers READY in the OLD layout, which v2 decodes
    // as requestId=1 / result=0 / dataSize=0 — an id no handshake owns. The
    // pipe flags exactly that reply (sawLegacyProtocolReady) instead of
    // discarding it, so we fail IMMEDIATELY with the real diagnosis instead of
    // waiting out the READY budget and reporting a bare timeout.
    if (ready && pipeServer->sawLegacyProtocolReady()) {
        HDAW_LOG("proxy", std::string(proxy::kLegacyV1Diagnosis) + " (slot "
            + std::to_string(slotId) + ")");
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }
    // VERSION GUARD (Gate 4/15): a child that speaks THIS framing but reports a
    // different version in READY.result must never be driven (it would
    // mis-parse the fields it disagrees about). This is the future-version
    // case only — a genuine v1 binary is caught above, where its READY is not
    // even decodable.
    if (ready && !proxy::protocolVersionAccepted(readyResp.result)) {
        HDAW_LOG("proxy", "spawnPluginHost: PROTOCOL VERSION MISMATCH for slot "
            + std::to_string(slotId) + " — child reported " + std::to_string(readyResp.result)
            + ", this engine requires " + std::to_string(proxy::kProtocolVersion)
            + "; refusing to drive a child with a different framing version");
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }
    if (!ready) {
        HDAW_LOG("proxy", "spawnPluginHost: READY timeout or pipe error for slot " + std::to_string(slotId));
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }

#else // !defined(_WIN32) — fork + execve child spawn

    // Diagnostic stdout redirect, mirroring the Windows block above: when
    // HDAW_PROXY_CHILD_STDOUT is set, the child's 0/1/2 all land on the file
    // (append mode). Open failure proceeds without a redirect, exactly like a
    // failed CreateFileA left inheritHandles FALSE above.
    int childStdoutFd = -1;
    if (const char* childStdoutPath = getenv("HDAW_PROXY_CHILD_STDOUT");
        childStdoutPath != nullptr && childStdoutPath[0] != '\0')
    {
        childStdoutFd = ::open(childStdoutPath, O_WRONLY | O_CREAT | O_APPEND, 0640);
    }

    // Unquoted argv: the child parses the same --slot/--pipe/--shm/--plugin
    // tokens (strncmp on the prefixes) as the Windows command line.
    std::string slotArg = "--slot=" + std::to_string(slotId);
    std::string pipeArg = "--pipe=" + pipeName;
    std::string shmArg = "--shm=" + shmNameStr;
    std::string pluginArg = "--plugin=" + pluginPath;

    pid_t pid = ::fork();
    if (pid < 0) {
        if (childStdoutFd >= 0) ::close(childStdoutFd);
        HDAW_LOG("proxy", "spawnPluginHost: fork FAILED errno=" + std::to_string(errno));
        pipeServer->stop();
        return false;
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls between fork and exec.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (::getppid() == 1) ::_exit(127);
        if (childStdoutFd >= 0) {
            ::dup2(childStdoutFd, STDIN_FILENO);
            ::dup2(childStdoutFd, STDOUT_FILENO);
            ::dup2(childStdoutFd, STDERR_FILENO);
        }
        char* argv[] = {
            const_cast<char*>(hostExe.c_str()),
            const_cast<char*>(slotArg.c_str()),
            const_cast<char*>(pipeArg.c_str()),
            const_cast<char*>(shmArg.c_str()),
            const_cast<char*>(pluginArg.c_str()),
            nullptr
        };
        ::execve(hostExe.c_str(), argv, environ);
        ::_exit(127);
    }
    // Parent: drop the redirect fd in all paths (the child holds its dup2s).
    if (childStdoutFd >= 0) ::close(childStdoutFd);

    childProc = static_cast<HANDLE>(pid);

    HDAW_LOG("proxy", "spawnPluginHost: child spawned pid=" + std::to_string(static_cast<int>(pid))
        + ", waiting for READY");

    // Wait for READY outside the lock (with timeout) — same guard-scoped
    // handshake as the Windows branch above (see that comment for the full
    // exchange-lock / map-lock ordering contract).
    ProxyResponse readyResp{};
    bool ready = false;
    {
        PipeServer::Exchange readyEx(*pipeServer);
        ready = readyEx.receiveReplyReady(readyResp, MessageType::READY);
    }
    if (ready && pipeServer->sawLegacyProtocolReady()) {
        HDAW_LOG("proxy", std::string(proxy::kLegacyV1Diagnosis) + " (slot "
            + std::to_string(slotId) + ")");
        hardKillAndReap(pid);
        pipeServer->stop();
        return false;
    }
    if (ready && !proxy::protocolVersionAccepted(readyResp.result)) {
        HDAW_LOG("proxy", "spawnPluginHost: PROTOCOL VERSION MISMATCH for slot "
            + std::to_string(slotId) + " — child reported " + std::to_string(readyResp.result)
            + ", this engine requires " + std::to_string(proxy::kProtocolVersion)
            + "; refusing to drive a child with a different framing version");
        hardKillAndReap(pid);
        pipeServer->stop();
        return false;
    }
    if (!ready) {
        HDAW_LOG("proxy", "spawnPluginHost: READY timeout or pipe error for slot " + std::to_string(slotId));
        hardKillAndReap(pid);
        pipeServer->stop();
        return false;
    }

#endif // _WIN32

    HDAW_LOG("proxy", "spawnPluginHost: READY received for slot " + std::to_string(slotId));

    // Now take the lock to insert the child info
    ChildInfo info;
    info.processHandle = childProc;
    info.pipeName = pipeName;
    info.shmName = shmNameStr;
    info.pipe = std::move(pipeServer);
    info.shm = std::move(shmRegion);
    info.alive.store(true);
    info.lastBlocksSnapshot = 0;
    info.lastSnapshotMs = 0;
    info.crashNotified = false;

    {
        std::lock_guard<std::mutex> lock(mutex);
        children.erase(slotId);
        children.emplace(slotId, std::move(info));
    }

    if (actualSlotId) *actualSlotId = slotId;
    return true;
}

bool ProxyProcessManager::killPluginHost(uint32_t slotId, KillMode mode) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = children.find(slotId);
        if (it == children.end()) return false;
        auto& info = it->second;
        handle = info.processHandle;
        info.alive.store(false);
        if (mode == KillMode::KillHard) {
            // stop() signals + cancels in-flight I/O on the pipe; the erase
            // drops only the MAP's lease — any lease a slot worker still holds
            // keeps the PipeServer (and its handle) alive until that worker
            // finishes, and ~PipeServer closes it then.
            if (info.pipe) info.pipe->stop();
            children.erase(it);
        }
    }

#if defined(_WIN32)
    if (handle != INVALID_HANDLE_VALUE) {
        if (mode == KillMode::KillGraceful) {
            TerminateProcess(handle, proxy::GRACEFUL_EXIT_CODE);
            WaitForSingleObject(handle, 1000);
        } else {
            TerminateProcess(handle, 0);
            WaitForSingleObject(handle, 1000);
        }
        CloseHandle(handle);
    }
#else
    if (handle != INVALID_HANDLE_VALUE) {
        pid_t pid = static_cast<pid_t>(handle);
        if (mode == KillMode::KillGraceful) {
            ::kill(pid, SIGTERM);
            // Wait out the existing 1000ms budget in 20ms WNOHANG steps;
            // cache the exit status into the map entry while it still exists
            // (the graceful erase happens in the block below).
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(1000);
            bool gone = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = children.find(slotId);
                ChildInfo* info = it != children.end() ? &it->second : nullptr;
                for (;;) {
                    if (reapOnce(pid, info)) { gone = true; break; }
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            if (!gone) {
                // Escalate exactly at the WaitForSingleObject timeout: a child
                // that ignored SIGTERM for the full budget must not leak.
                ::kill(pid, SIGKILL);
                reapBlocking(pid, nullptr);
            }
        } else {
            ::kill(pid, SIGKILL);
            reapBlocking(pid, nullptr);
        }
        // Nothing to close on Linux: the pid needs no handle; the reap above
        // is the CloseHandle analog.
    }
#endif

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

#if defined(_WIN32)
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(info.processHandle, &exitCode)) return false;
    if (exitCode != STILL_ACTIVE) {
        info.alive.store(false);
        return false;
    }
    return true;
#else
    // One WNOHANG reap; a reaped child is cached (see ChildInfo::reaped) and
    // every later call sees it gone. waitpid failure (ECHILD — unknown pid)
    // is treated as not-alive, like a failed GetExitCodeProcess.
    if (reapOnce(static_cast<pid_t>(info.processHandle), &info))
    {
        info.alive.store(false);
        return false;
    }
    return true;
#endif
}

const ChildInfo* ProxyProcessManager::getChildInfo(uint32_t slotId) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? &it->second : nullptr;
}

bool ProxyProcessManager::terminateChild(uint32_t slotId, uint32_t /*exitCode*/) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    if (it == children.end()) return false;
    if (it->second.processHandle == INVALID_HANDLE_VALUE) return false;
#if defined(_WIN32)
    return TerminateProcess(it->second.processHandle, exitCode) != 0;
#else
    // SIGKILL regardless of exitCode: Linux cannot inject a 32-bit exit code
    // into a running process; the graceful path is killPluginHost(SIGTERM).
    return ::kill(static_cast<pid_t>(it->second.processHandle), SIGKILL) == 0;
#endif
}

std::shared_ptr<PipeServer> ProxyProcessManager::getPipe(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? it->second.pipe : nullptr;
}

std::shared_ptr<ShmRegion> ProxyProcessManager::getShm(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = children.find(slotId);
    return it != children.end() ? it->second.shm : nullptr;
}

bool ProxyProcessManager::sendHeartbeat(uint32_t slotId) {
    // Matches the legacy receiveResp() budget this call used to inherit
    // (PipeServer::kReadyTimeoutMs on both the connect and the read).
    static constexpr DWORD kHeartbeatTimeoutMs = 8000;

    // Lease held for the WHOLE exchange: a concurrent kill only signals +
    // cancels this pipe; it cannot free it under us.
    auto pipe = getPipe(slotId);
    if (!pipe) return false;

    // ONE-WAY operation under the same exchange lock as every request→response
    // transaction: the heartbeat carries no expectation of its own, but the
    // child still answers it, so the ack is consumed inside the same guard
    // (otherwise the next transaction would read the HEARTBEAT ack as a stale
    // reply). No other pipe op can interleave between the send and the read.
    PipeServer::Exchange ex(*pipe);
    ProxyMessage msg{};
    msg.type = MessageType::HEARTBEAT;
    msg.slotId = slotId;
    if (!ex.sendRequest(msg, kHeartbeatTimeoutMs)) return false;

    ProxyResponse resp{};
    return ex.receiveReply(resp, MessageType::HEARTBEAT, kHeartbeatTimeoutMs);
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

#if defined(_WIN32)
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(info.processHandle, &exitCode)) {
                const DWORD flagError = GetLastError();
                if (!info.crashNotified) {
                    info.crashNotified = true;
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "checkAllChildren: slot %u flagged: GetExitCodeProcess failed, error=%d",
                        id, static_cast<int>(flagError));
                    HDAW_LOG("CrashRecovery", buf);
                    crashedSlots.push_back(id);
                }
                continue;
            }
            if (exitCode == proxy::GRACEFUL_EXIT_CODE) {
                info.alive.store(false);
                continue;
            }
            if (exitCode != STILL_ACTIVE) {
                info.alive.store(false);
                if (!info.crashNotified) {
                    info.crashNotified = true;
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "checkAllChildren: slot %u flagged: exit code 0x%x",
                        id, static_cast<unsigned>(exitCode));
                    HDAW_LOG("CrashRecovery", buf);
                    crashedSlots.push_back(id);
                }
                continue;
            }
#else
            // One WNOHANG reap (cached into ChildInfo on Linux — no
            // STILL_ACTIVE re-query). Still running → fall through to the
            // stall snapshot below, identical to the Windows path.
            if (reapOnce(static_cast<pid_t>(info.processHandle), &info)) {
                info.alive.store(false);
                if (gracefulExit(info)) continue;
                if (!info.crashNotified) {
                    info.crashNotified = true;
                    char buf[160];
                    if (info.reaped)
                        std::snprintf(buf, sizeof(buf), "checkAllChildren: slot %u flagged: exit code 0x%x",
                            id, static_cast<unsigned>(exitCodeOf(info)));
                    else
                        std::snprintf(buf, sizeof(buf), "checkAllChildren: slot %u flagged: waitpid failed errno=%d",
                            id, errno);
                    HDAW_LOG("CrashRecovery", buf);
                    crashedSlots.push_back(id);
                }
                continue;
            }
#endif

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
                    if (!info.crashNotified) {
                        info.crashNotified = true;
                        char buf[160];
                        std::snprintf(buf, sizeof(buf), "checkAllChildren: slot %u flagged: stalled (blocks=%llu frozenMs=%llu thresholdMs=%u)",
                            id, static_cast<unsigned long long>(currentBlocks),
                            static_cast<unsigned long long>(nowMs - info.lastSnapshotMs),
                            static_cast<unsigned>(staleThresholdMs));
                        HDAW_LOG("CrashRecovery", buf);
                        crashedSlots.push_back(id);
                    }
                    continue;
                }
            } else {
                info.lastBlocksSnapshot = currentBlocks;
                info.lastSnapshotMs = nowMs;
            }
        }
    }

    invokeCrashCallbacks(crashedSlots);
}

void ProxyProcessManager::invokeCrashCallbacks(const std::vector<uint32_t>& ids) {
    if (ids.empty()) return;

    // SNAPSHOT under the lock, INVOKE outside it. Copying the std::functions
    // (rather than the map's nodes) is what makes this safe against a
    // concurrent removeSlotCrashCallback from ~PluginProxySlot: the map is
    // never touched again after this block, so no iterator, reference or
    // erased node can be read during the invocation below. Each callback
    // re-enters PluginManager/CrashRecoveryManager, which is exactly why the
    // lock must be released first.
    std::vector<std::pair<uint32_t, CrashCallback>> snapshot;
    snapshot.reserve(ids.size());
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto id : ids) {
            auto it = perSlotCrashCallbacks.find(id);
            if (it != perSlotCrashCallbacks.end())
                snapshot.emplace_back(id, it->second);
        }
    }

    // Test-only seam (the only live-path read of it): runs in the
    // snapshot→invoke window so a test can deterministically remove a callback
    // that was already snapshotted. Null in production.
    if (crashCallbackSnapshotHookForTest)
        crashCallbackSnapshotHookForTest();

    for (auto& entry : snapshot)
        entry.second(entry.first);
}

std::string ProxyProcessManager::getHostExePath() {
#if defined(_WIN32)
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    auto path = std::string(buf);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    return path + "hdaw_plugin_host.exe";
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return "hdaw_plugin_host";
    buf[n] = '\0';
    auto path = std::string(buf, static_cast<size_t>(n));
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        path = path.substr(0, pos + 1);
    return path + "hdaw_plugin_host";
#endif
}

std::string ProxyProcessManager::makePipeName(uint32_t slotId) const {
    return "\\\\.\\pipe\\hdaw_plugin_" + namePrefix + std::to_string(slotId);
}

std::string ProxyProcessManager::makeShmName(uint32_t slotId) const {
    return "hdaw_plugin_shm_" + namePrefix + std::to_string(slotId);
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
