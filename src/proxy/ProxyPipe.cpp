#include "ProxyPipe.h"
#include "../common/DebugLog.h"
#include <cassert>
#include <cstring>
#if !defined(_WIN32)
#include <cerrno>
#include <chrono>
#endif

namespace proxy {

namespace {
const char* replyTypeName(MessageType t) {
    switch (t) {
        case MessageType::READY: return "READY";
        case MessageType::PREPARE: return "PREPARE";
        case MessageType::PREPARE_RESULT: return "PREPARE_RESULT";
        case MessageType::SHUTDOWN: return "SHUTDOWN";
        case MessageType::PROCESS_BLOCK: return "PROCESS_BLOCK";
        case MessageType::SET_STATE: return "SET_STATE";
        case MessageType::GET_STATE: return "GET_STATE";
        case MessageType::GET_STATE_RESULT: return "GET_STATE_RESULT";
        case MessageType::SET_PARAM: return "SET_PARAM";
        case MessageType::GET_PARAM: return "GET_PARAM";
        case MessageType::GET_PARAM_RESULT: return "GET_PARAM_RESULT";
        case MessageType::GET_PARAM_COUNT: return "GET_PARAM_COUNT";
        case MessageType::GET_PARAM_COUNT_RESULT: return "GET_PARAM_COUNT_RESULT";
        case MessageType::GET_PARAM_INFO: return "GET_PARAM_INFO";
        case MessageType::GET_PARAM_INFO_RESULT: return "GET_PARAM_INFO_RESULT";
        case MessageType::SHOW_EDITOR: return "SHOW_EDITOR";
        case MessageType::CLOSE_EDITOR: return "CLOSE_EDITOR";
        case MessageType::EDITOR_CLOSED: return "EDITOR_CLOSED";
        case MessageType::PARAM_CHANGED: return "PARAM_CHANGED";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        case MessageType::STATE_CHUNK: return "STATE_CHUNK";
        case MessageType::GET_PROGRAM_COUNT: return "GET_PROGRAM_COUNT";
        case MessageType::GET_PROGRAM_COUNT_RESULT: return "GET_PROGRAM_COUNT_RESULT";
        case MessageType::GET_PROGRAM_NAME: return "GET_PROGRAM_NAME";
        case MessageType::GET_PROGRAM_NAME_RESULT: return "GET_PROGRAM_NAME_RESULT";
        case MessageType::SET_PROGRAM: return "SET_PROGRAM";
        case MessageType::SET_PROGRAM_RESULT: return "SET_PROGRAM_RESULT";
        case MessageType::GET_CURRENT_PROGRAM: return "GET_CURRENT_PROGRAM";
        case MessageType::GET_CURRENT_PROGRAM_RESULT: return "GET_CURRENT_PROGRAM_RESULT";
    }
    return "?";
}
} // namespace

// --- PipeServer ---
//
// PLATFORM SPLIT: the raw transport layer (dtor/start/stop, the overlapped*
// helpers and the raw ops) exists in two same-file branches. The Windows
// branch (message-mode named pipe + overlapped I/O) is VERBATIM; the Linux
// branch below implements the decided AF_UNIX SOCK_SEQPACKET mapping.
// Everything after the matching #endif (desync bookkeeping, single-op
// wrappers, Exchange correlation) is platform-neutral and shared unchanged.

PipeServer::PipeServer(const std::string& pipeName) : name(pipeName) {}

#if defined(_WIN32)

PipeServer::~PipeServer() {
    // THE ONLY CLOSER. This runs at the LAST lease release, so by construction
    // no pipe operation can still be in flight on this handle (every caller
    // holds a shared_ptr<PipeServer> lease for the whole exchange).
    stop();
    if (HANDLE h = hPipe.exchange(INVALID_HANDLE_VALUE); h != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
}

bool PipeServer::start(DWORD* errorOut) {
    // FILE_FLAG_OVERLAPPED is mandatory for the bounded (WaitForSingleObject)
    // IO used in receiveResp — a synchronous pipe handle cannot be given a
    // per-call timeout. Because the handle is overlapped, EVERY read/write on
    // it must supply an OVERLAPPED structure; see the overlapped* helpers.
    HANDLE h = CreateNamedPipeA(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(ProxyResponse),
        sizeof(ProxyMessage),
        0,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (errorOut) *errorOut = GetLastError();
        return false;
    }
    // start() runs before the server is published to any other thread (the
    // manager stores the lease only after the child answers READY), so this
    // store cannot race a concurrent stop()/~PipeServer.
    hPipe.store(h, std::memory_order_release);
    running.store(true, std::memory_order_relaxed);
    return true;
}

void PipeServer::stop() {
    // Signal + cancel ONLY. The handle is deliberately NOT closed here (and
    // hPipe is NOT written): a single closer is what prevents both a leak and
    // a CloseHandle racing in-flight overlapped I/O. ~PipeServer is that
    // closer, and it runs at the LAST lease release — so by construction no
    // operation can still be in flight when the handle is closed.
    //
    // CancelIoEx(h, nullptr) cancels all outstanding I/O for this handle in
    // THIS process regardless of the issuing thread — exactly the need here,
    // because kills happen on the manager/crash-recovery thread while reads
    // are in flight on a slot's worker threads. A cancelled op completes with
    // ERROR_OPERATION_ABORTED, so the reader's helper returns false cleanly.
    //
    // It deliberately does NOT take the exchange lock: the whole point is that
    // a transaction BLOCKED in overlapped I/O can always be cancelled and then
    // release its guard — taking the lock here would deadlock against exactly
    // the transaction being unblocked.
    //
    // DisconnectNamedPipe deliberately does NOT belong here: it does not
    // cancel pending I/O (its interaction with an in-flight read/connect is
    // unspecified) and it does not free the pipe NAME — a name stays owned
    // until every instance handle is closed. The only thing it would add is
    // refusing new clients on an instance that already refuses all I/O via the
    // stopped_ gate, i.e. no observable effect. The dtor does
    // Cancel -> Disconnect -> Close in the canonical order, with nothing in
    // flight.
    //
    // Consequence for re-spawn: while a lease is still outstanding the NAME is
    // not free, so a same-slot CreateNamedPipe fails — spawnPluginHost already
    // handles that by bumping the slot id and retrying on a fresh name (see
    // kMaxSpawnNameAttempts), and cancel makes the old lease release promptly.
    //
    // A PipeServer is single-use: start() is never called after stop().
    stopped_.store(true, std::memory_order_relaxed);
    running.store(false, std::memory_order_relaxed);
    connected.store(false, std::memory_order_relaxed);
    if (HANDLE h = hPipe.load(std::memory_order_acquire); h != INVALID_HANDLE_VALUE) {
        CancelIoEx(h, nullptr);
    }
}

bool PipeServer::overlappedConnect(DWORD timeoutMs) {
    // stopped_ FIRST: a killed pipe must make new I/O bail immediately.
    if (stopped_.load(std::memory_order_relaxed)) return false;
    // Load the handle ONCE: stop() may cancel (never close) from another
    // thread, and re-reading the member after a multi-second wait is exactly
    // the stale-handle hazard under fix.
    HANDLE h = hPipe.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE) return false;

    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);  // manual-reset
    if (!ov.hEvent) return false;

    BOOL ok = ConnectNamedPipe(h, &ov);
    bool success = false;
    if (ok) {
        // Synchronous completion (rare for a fresh connect).
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            // Client already connected before the call — treat as success.
            success = true;
        } else if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                DWORD transferred = 0;
                success = GetOverlappedResult(h, &ov, &transferred, FALSE) != 0;
            } else {
                // Timeout/abandoned: cancel THIS operation and wait for the
                // cancellation to settle before releasing the event (the
                // OVERLAPPED must not be freed while the op is outstanding).
                CancelIoEx(h, &ov);
                DWORD transferred = 0;
                GetOverlappedResult(h, &ov, &transferred, TRUE);
            }
        }
        // Any other error: leave success == false.
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool PipeServer::overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead,
                                bool* timedOut) {
    bytesRead = 0;
    if (timedOut) *timedOut = false;
    if (stopped_.load(std::memory_order_relaxed)) return false;
    HANDLE h = hPipe.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = ReadFile(h, buf, size, &bytesRead, &ov);
    bool success = false;
    if (ok) {
        // Completed synchronously; bytesRead already filled.
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                // Normal completion OR a concurrent stop()'s CancelIoEx
                // (which surfaces as ERROR_OPERATION_ABORTED -> false).
                success = GetOverlappedResult(h, &ov, &bytesRead, FALSE) != 0;
            } else {
                CancelIoEx(h, &ov);
                DWORD transferred = 0;
                GetOverlappedResult(h, &ov, &transferred, TRUE);
                if (wait == WAIT_TIMEOUT && timedOut)
                    *timedOut = true;
            }
        }
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool PipeServer::overlappedWrite(const void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesWritten) {
    bytesWritten = 0;
    if (stopped_.load(std::memory_order_relaxed)) return false;
    HANDLE h = hPipe.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE) return false;
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = WriteFile(h, buf, size, &bytesWritten, &ov);
    bool success = false;
    if (ok) {
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                success = GetOverlappedResult(h, &ov, &bytesWritten, FALSE) != 0;
            } else {
                CancelIoEx(h, &ov);
                DWORD transferred = 0;
                GetOverlappedResult(h, &ov, &transferred, TRUE);
            }
        }
    }

    CloseHandle(ov.hEvent);
    return success;
}

// ---------------------------------------------------------------------------
// RAW OPERATIONS (no lock — the caller holds exchangeMutex_)
// ---------------------------------------------------------------------------

bool PipeServer::receiveRaw(ProxyMessage& msg) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(INFINITE)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&msg, sizeof(ProxyMessage), INFINITE, bytesRead)) {
        connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyMessage) - sizeof(msg.data);
}

bool PipeServer::sendRaw(const ProxyResponse& resp) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&resp, sizeof(ProxyResponse), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgRaw(const ProxyMessage& msg) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgBoundedRaw(const ProxyMessage& msg, DWORD timeoutMs) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), timeoutMs, bytesWritten);
}

bool PipeServer::receiveRespRaw(ProxyResponse& resp) {
    // Bounded READY wait: a hung child must not hang the engine forever.
    // Connect is normally near-instant (child connects right after spawn); the
    // dominant cost is the child's plugin init before it writes READY, so the
    // read gets the full kReadyTimeoutMs budget.
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(kReadyTimeoutMs)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), kReadyTimeoutMs, bytesRead)) {
        connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyResponse) - sizeof(resp.data);
}

bool PipeServer::receiveRespBoundedRaw(ProxyResponse& resp, DWORD timeoutMs) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(timeoutMs)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    bool timedOut = false;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), timeoutMs, bytesRead, &timedOut)) {
        // A bounded-receive TIMEOUT is not a broken pipe: the child is still
        // there and its late response stays queued in the message-mode pipe.
        // Only a genuine read error tears the connection state down.
        if (!timedOut)
            connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyResponse) - sizeof(resp.data);
}

#else
// ---------------------------------------------------------------------------
// LINUX BRANCH — AF_UNIX SOCK_SEQPACKET transport.
//
// Mapping from the Win32 message-mode named pipe (the Windows branch above
// stays verbatim):
//   * SOCK_SEQPACKET preserves the 256-byte frame boundaries exactly like
//     PIPE_TYPE_MESSAGE: one frame per send/recv — never split, never
//     coalesced. A discarded 256-byte record therefore cannot corrupt the
//     framing, so the platform-neutral Exchange discard loop stays correct
//     unchanged.
//   * start() = socket() + bind(socketPathForPipeName(name)) + listen(1)
//     (single instance — the analogue of a single-instance named pipe). The
//     listen fd lives in hPipe until overlappedConnect() accepts; then the
//     ACCEPTED fd replaces it and the listen fd is closed. Both writes happen
//     here, under the exchange lock the raw ops hold.
//   * overlappedConnect = accept() with a bounded poll(); overlappedRead/
//     Write = poll() + recv()/send(MSG_NOSIGNAL) of ONE whole frame.
//   * INFINITE (0xFFFFFFFF) maps to poll(-1) blocking.
//   * stop() = shutdown(fd, SHUT_RDWR), which unblocks a poll/recv/accept in
//     flight — the CancelIoEx role. ~PipeServer remains the ONLY closer.
// ---------------------------------------------------------------------------
namespace {
constexpr DWORD kInfiniteTimeoutMs = 0xFFFFFFFF;
// The Linux raw ops keep the Windows branch's literal `INFINITE` call sites
// verbatim, so the constant keeps its name here (0xFFFFFFFF == blocking).
constexpr DWORD INFINITE = kInfiniteTimeoutMs;

// poll() against a steady_clock deadline taken at entry. EINTR retries
// recompute the remaining time from the ORIGINAL budget, so an interrupted
// wait can never exceed the caller's timeout. Returns >0 when ready, 0 on
// timeout, <0 on a real poll error.
int pollDeadline(int fd, short events, DWORD timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        int timeoutMsec = -1; // INFINITE: block.
        if (timeoutMs != kInfiniteTimeoutMs) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (elapsed >= static_cast<long long>(timeoutMs)) return 0;
            timeoutMsec = static_cast<int>(timeoutMs - elapsed);
        }
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = events;
        const int rc = ::poll(&pfd, 1, timeoutMsec);
        if (rc >= 0)
            return (rc > 0 && (pfd.revents & POLLNVAL)) ? -1 : rc;
        if (errno != EINTR) return -1;
    }
}
} // namespace

PipeServer::~PipeServer() {
    // THE ONLY CLOSER (lesson 40) — same contract as the Windows branch: this
    // runs at the LAST lease release, so by construction no pipe operation can
    // still be in flight on this fd. Handles the INVALID_HANDLE_VALUE (-1)
    // sentinel.
    stop();
    if (HANDLE h = hPipe.exchange(INVALID_HANDLE_VALUE); h != INVALID_HANDLE_VALUE) {
        ::close(h);
    }
}

bool PipeServer::start(DWORD* errorOut) {
    const std::string path = socketPathForPipeName(name);
    HANDLE h = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (h == INVALID_HANDLE_VALUE) {
        if (errorOut) *errorOut = static_cast<DWORD>(errno);
        return false;
    }
    // A stale socket file left by a previous crashed session would fail
    // bind() (the analogue of the pipe NAME still being owned); unlink it
    // first so a fresh start() on a reclaimed slot name works.
    ::unlink(path.c_str());
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // sun_path is bounded; a pathological name must fail start() (==
    // CreateNamedPipeA failing on Windows), never truncate into another path.
    if (path.size() >= sizeof(addr.sun_path)) {
        if (errorOut) *errorOut = static_cast<DWORD>(ENAMETOOLONG);
        ::close(h);
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(h, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errorOut) *errorOut = static_cast<DWORD>(errno);
        ::close(h);
        return false;
    }
    // User-only: the socket carries the plugin control protocol.
    ::chmod(path.c_str(), 0600);
    if (::listen(h, 1) != 0) {
        if (errorOut) *errorOut = static_cast<DWORD>(errno);
        ::close(h);
        return false;
    }
    // start() runs before the server is published to any other thread (the
    // manager stores the lease only after the child answers READY), so this
    // store cannot race a concurrent stop()/~PipeServer.
    hPipe.store(h, std::memory_order_release);
    running.store(true, std::memory_order_relaxed);
    return true;
}

void PipeServer::stop() {
    // Signal + cancel ONLY, same contract as the Windows branch: the fd is
    // deliberately NOT closed here (hPipe is NOT written) — ~PipeServer is
    // the single closer, and it never takes the exchange lock, so a
    // transaction blocked in I/O can always be unblocked and then release its
    // own guard. shutdown(SHUT_RDWR) unblocks a poll/recv/accept in flight on
    // the fd (the CancelIoEx role); on a still-listening fd it is a harmless
    // no-op error. A PipeServer is single-use: start() is never called after
    // stop().
    stopped_.store(true, std::memory_order_relaxed);
    running.store(false, std::memory_order_relaxed);
    connected.store(false, std::memory_order_relaxed);
    if (HANDLE h = hPipe.load(std::memory_order_acquire); h != INVALID_HANDLE_VALUE) {
        ::shutdown(h, SHUT_RDWR);
    }
}

bool PipeServer::overlappedConnect(DWORD timeoutMs) {
    // stopped_ FIRST: a killed pipe must make new I/O bail immediately.
    if (stopped_.load(std::memory_order_relaxed)) return false;
    // Load the handle ONCE: stop() may cancel (never close) from another
    // thread, and re-reading the member after a multi-second wait is exactly
    // the stale-handle hazard under fix.
    HANDLE listenFd = hPipe.load(std::memory_order_acquire);
    if (listenFd == INVALID_HANDLE_VALUE) return false;

    if (pollDeadline(listenFd, POLLIN, timeoutMs) <= 0)
        return false; // timeout or poll error — still listening, nothing to clean up
    HANDLE accepted = INVALID_HANDLE_VALUE;
    for (;;) {
        accepted = ::accept(listenFd, nullptr, nullptr);
        if (accepted != INVALID_HANDLE_VALUE) break;
        if (errno != EINTR) return false;
    }
    // hPipe transitions LISTEN fd -> ACCEPTED fd (the I/O fd). Single instance
    // = backlog 1, so this happens once; the listen fd is closed immediately
    // after the accept succeeds. Both writes happen here, under the exchange
    // lock every raw op holds.
    hPipe.store(accepted, std::memory_order_release);
    ::close(listenFd);
    return true;
}

bool PipeServer::overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead,
                                bool* timedOut) {
    bytesRead = 0;
    if (timedOut) *timedOut = false;
    if (stopped_.load(std::memory_order_relaxed)) return false;
    HANDLE h = hPipe.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE) return false;
    if (pollDeadline(h, POLLIN, timeoutMs) <= 0) {
        // Timeout (or poll error): a bounded-receive timeout must NOT mark the
        // pipe disconnected — report it distinctly and fail.
        if (timedOut) *timedOut = true;
        return false;
    }
    // SEQPACKET: one recv returns exactly one whole 256-byte record (or a
    // peer close, or an error).
    const ssize_t n = ::recv(h, buf, size, 0);
    if (n <= 0) {
        // 0 = orderly peer close (the ERROR_BROKEN_PIPE analogue);
        // EAGAIN/EWOULDBLOCK/EINTR-after-deadline = budget exhausted, which is
        // a timeout, not a broken pipe. Anything else is a real error.
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            && timedOut)
            *timedOut = true;
        return false;
    }
    bytesRead = static_cast<DWORD>(n);
    // A short SEQPACKET frame (possible only for payload > 240 B, which the
    // fixed 256-byte framing makes impossible) fails right here — the same
    // all-or-nothing guarantee the Windows message-mode read had.
    return n == static_cast<ssize_t>(size);
}

bool PipeServer::overlappedWrite(const void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesWritten) {
    bytesWritten = 0;
    if (stopped_.load(std::memory_order_relaxed)) return false;
    HANDLE h = hPipe.load(std::memory_order_acquire);
    if (h == INVALID_HANDLE_VALUE) return false;
    if (pollDeadline(h, POLLOUT, timeoutMs) <= 0)
        return false; // timeout or poll error
    // MSG_NOSIGNAL: a dead child must surface as a false return (EPIPE), not
    // as SIGPIPE taking the engine down.
    const ssize_t n = ::send(h, buf, size, MSG_NOSIGNAL);
    if (n <= 0) return false;
    bytesWritten = static_cast<DWORD>(n);
    // A SEQPACKET record is sent whole or not at all; a partial send can only
    // be a real error surface, which the == check treats as failure.
    return n == static_cast<ssize_t>(size);
}

// ---------------------------------------------------------------------------
// RAW OPERATIONS (no lock — the caller holds exchangeMutex_). The connect
// gating, the stopped_/INVALID_HANDLE_VALUE checks and the size checks are
// the same shape as the Windows branch; only the transport calls differ.
// ---------------------------------------------------------------------------

bool PipeServer::receiveRaw(ProxyMessage& msg) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(INFINITE)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&msg, sizeof(ProxyMessage), INFINITE, bytesRead)) {
        connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyMessage) - sizeof(msg.data);
}

bool PipeServer::sendRaw(const ProxyResponse& resp) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&resp, sizeof(ProxyResponse), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgRaw(const ProxyMessage& msg) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgBoundedRaw(const ProxyMessage& msg, DWORD timeoutMs) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), timeoutMs, bytesWritten);
}

bool PipeServer::receiveRespRaw(ProxyResponse& resp) {
    // Bounded READY wait: a hung child must not hang the engine forever.
    // Connect is normally near-instant (child connects right after spawn); the
    // dominant cost is the child's plugin init before it writes READY, so the
    // read gets the full kReadyTimeoutMs budget.
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(kReadyTimeoutMs)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), kReadyTimeoutMs, bytesRead)) {
        connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyResponse) - sizeof(resp.data);
}

bool PipeServer::receiveRespBoundedRaw(ProxyResponse& resp, DWORD timeoutMs) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE) return false;
    if (!connected.load(std::memory_order_relaxed)) {
        if (!overlappedConnect(timeoutMs)) {
            connected.store(false, std::memory_order_relaxed);
            return false;
        }
        connected.store(true, std::memory_order_relaxed);
    }
    DWORD bytesRead = 0;
    bool timedOut = false;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), timeoutMs, bytesRead, &timedOut)) {
        // A bounded-receive TIMEOUT is not a broken pipe: the child is still
        // there and its late response stays queued in the message-mode pipe.
        // Only a genuine read error tears the connection state down.
        if (!timedOut)
            connected.store(false, std::memory_order_relaxed);
        return false;
    }
    return bytesRead >= sizeof(ProxyResponse) - sizeof(resp.data);
}

#endif // defined(_WIN32)

// ---------------------------------------------------------------------------
// DESYNC BOOKKEEPING (callers hold exchangeMutex_)
// ---------------------------------------------------------------------------

void PipeServer::noteDesync(const char* why) {
    desyncEvents_.fetch_add(1, std::memory_order_relaxed);
    // Log only on the TRANSITION into desynced: a bounded receive that returns
    // false is the normal idle outcome for the editor watcher's await loop, so
    // a line per occurrence would flood the log.
    if (!desynced_.exchange(true, std::memory_order_relaxed)) {
        HDAW_LOG("proxy", "pipe desynced: " + std::string(why) + " (pipe=" + name
            + ") — a late reply may still be queued; the next exchange discards "
              "any reply whose correlation id is not its current request's");
    }
}

void PipeServer::noteInSync() {
    desynced_.store(false, std::memory_order_relaxed);
}

void PipeServer::noteUnexpectedReply(MessageType got, MessageType expected,
                                     uint32_t gotId, uint32_t myId) {
    // Always a DISCARD now: the id proves the reply is not this exchange's
    // answer (a late reply from an earlier request, or a protocol error). The
    // counter is the observable proof tests assert on.
    staleDiscards_.fetch_add(1, std::memory_order_relaxed);
    HDAW_LOG("proxy", std::string("STALE/UNEXPECTED reply type=") + replyTypeName(got)
        + " requestId=" + std::to_string(gotId)
        + " (mine=" + std::to_string(myId) + ", expected=" + replyTypeName(expected) + ")"
        + " — discarded" + " (pipe=" + name + ")");
}

uint32_t PipeServer::allocateRequestId() {
    // fetch_add yields the previous value; +1 makes the first id 1 (0 is
    // reserved). On wrap the atomic returns 0xFFFFFFFF -> +1 == 0, which is
    // reserved, so skip it and take the next.
    uint32_t id = requestIdCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (id == kUnsolicitedRequestId)
        id = requestIdCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
    return id;
}

void PipeServer::routeUnsolicitedEditorClosed() {
    HDAW_LOG("proxy", "unsolicited EDITOR_CLOSED routed to the editor-closed handler (pipe=" + name + ")");
    if (editorClosedHandler_)
        editorClosedHandler_();
}

void PipeServer::setEditorClosedHandler(std::function<void()> cb) {
    // Taken under the exchange lock: the reader (Exchange::receiveReply) holds
    // it, so the std::function is never read while it is being replaced.
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    editorClosedHandler_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// SINGLE-OP WRAPPERS — the exchange lock is held for exactly one operation.
// (They carry no expectation, so they cannot classify an unexpected reply;
// use Exchange to build a request→response transaction.)
// ---------------------------------------------------------------------------

bool PipeServer::receive(ProxyMessage& msg) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return receiveRaw(msg);
}

bool PipeServer::send(const ProxyResponse& resp) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return sendRaw(resp);
}

bool PipeServer::sendMsg(const ProxyMessage& msg) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return sendMsgRaw(msg);
}

bool PipeServer::sendMsgBounded(const ProxyMessage& msg, DWORD timeoutMs) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return sendMsgBoundedRaw(msg, timeoutMs);
}

bool PipeServer::receiveResp(ProxyResponse& resp) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return receiveRespRaw(resp);
}

bool PipeServer::receiveRespBounded(ProxyResponse& resp, DWORD timeoutMs) {
    std::lock_guard<std::mutex> lock(exchangeMutex_);
    return receiveRespBoundedRaw(resp, timeoutMs);
}

// ---------------------------------------------------------------------------
// Exchange — the RAII guard owning one complete request→response transaction.
// ---------------------------------------------------------------------------

PipeServer::Exchange::Exchange(PipeServer& server) : srv_(server) {
#ifndef NDEBUG
    assert(srv_.lockOwner_.load(std::memory_order_relaxed) != std::this_thread::get_id()
           && "PipeServer exchange lock re-entered on the same thread: a nested "
              "request->response transaction would deadlock the non-recursive "
              "exchange mutex (see the RE-ENTRANCY note in ProxyPipe.h)");
#endif
    srv_.exchangeMutex_.lock();
    acquired_ = true;
#ifndef NDEBUG
    srv_.lockOwner_.store(std::this_thread::get_id(), std::memory_order_relaxed);
#endif
}

PipeServer::Exchange::Exchange(PipeServer& server, std::try_to_lock_t) : srv_(server) {
    acquired_ = srv_.exchangeMutex_.try_lock();
#ifndef NDEBUG
    if (acquired_)
        srv_.lockOwner_.store(std::this_thread::get_id(), std::memory_order_relaxed);
#else
    (void) srv_;
#endif
}

PipeServer::Exchange::~Exchange() {
    if (!acquired_) return;
#ifndef NDEBUG
    srv_.lockOwner_.store(std::thread::id{}, std::memory_order_relaxed);
#endif
    srv_.exchangeMutex_.unlock();
}

bool PipeServer::Exchange::sendRequest(const ProxyMessage& msg, DWORD timeoutMs) {
    if (!acquired_) return false;
    ProxyMessage stamped = msg;
    stamped.requestId = srv_.allocateRequestId();
    if (!srv_.sendMsgBoundedRaw(stamped, timeoutMs)) return false;
    currentRequestId_ = stamped.requestId;
    haveRequest_ = true;
    return true;
}

bool PipeServer::Exchange::sendRequestUnbounded(const ProxyMessage& msg) {
    if (!acquired_) return false;
    ProxyMessage stamped = msg;
    stamped.requestId = srv_.allocateRequestId();
    if (!srv_.sendMsgRaw(stamped)) return false;
    currentRequestId_ = stamped.requestId;
    haveRequest_ = true;
    return true;
}

bool PipeServer::Exchange::sendContinuation(const ProxyMessage& msg, DWORD timeoutMs) {
    if (!acquired_) return false;
    if (!haveRequest_) {
        HDAW_LOG("proxy", "sendContinuation with no current request — programming "
                          "error (a continuation must share its request's id)");
        return false;
    }
    ProxyMessage stamped = msg;
    stamped.requestId = currentRequestId_;
    return srv_.sendMsgBoundedRaw(stamped, timeoutMs);
}

bool PipeServer::Exchange::receiveReply(ProxyResponse& out, MessageType expected,
                                       DWORD timeoutMs, bool timeoutIsDesync) {
    return receiveReplyImpl(out, expected, timeoutMs, /*readyBudget=*/false, timeoutIsDesync);
}

bool PipeServer::Exchange::receiveReplyReady(ProxyResponse& out, MessageType expected) {
    return receiveReplyImpl(out, expected, kReadyTimeoutMs, /*readyBudget=*/true, /*timeoutIsDesync=*/true);
}

bool PipeServer::Exchange::receiveReplyImpl(ProxyResponse& out, MessageType expected,
                                           DWORD timeoutMs, bool readyBudget, bool timeoutIsDesync) {
    if (!acquired_) return false;
#ifndef NDEBUG
    // Programming-error tripwire: an exchange that expects a SOLICITED reply
    // must have sent its request first. The two legal requestless awaits are
    // the spawn READY handshake and the editor watcher's EDITOR_CLOSED wait.
    if (!haveRequest_ && expected != MessageType::EDITOR_CLOSED
        && expected != MessageType::READY) {
        HDAW_LOG("proxy", std::string("receiveReply(") + replyTypeName(expected)
            + ") with no request sent on this exchange — programming error (pipe="
            + srv_.name + ")");
    }
#endif
    for (;;) {
        ProxyResponse r{};
        const bool ok = readyBudget ? srv_.receiveRespRaw(r)
                                    : srv_.receiveRespBoundedRaw(r, timeoutMs);
        if (!ok) {
            if (timeoutIsDesync)
                srv_.noteDesync(readyBudget
                                    ? "handshake receive returned false (timeout or pipe error)"
                                    : "bounded receive returned false (timeout or pipe error)");
            return false;
        }

        // UNSOLICITED (id 0 by design): the child's editor window closed on
        // its own. No request produces this type; routing it to the handler
        // and continuing keeps it from being handed to a caller that never
        // asked for it.
        if (r.requestId == kUnsolicitedRequestId
            && r.type == MessageType::EDITOR_CLOSED) {
            srv_.routeUnsolicitedEditorClosed();
            continue;
        }

        // LEGACY v1 CHILD (Gate 4/15): a child built before the correlation-id
        // framing answers READY in the OLD {type, result, dataSize, data}
        // layout, so its reply is NOT id 0 — the id match below would DISCARD
        // it and the spawn would die by TIMEOUT, hiding the real cause. Only a
        // requestless await may take this path (a solicited exchange must
        // still prove the reply is its own by id), and the caller
        // (ProxyProcessManager::spawnPluginHost) turns the flag into an
        // immediate, explicit spawn failure.
        if (!haveRequest_ && isLegacyV1ReadyReply(r)) {
            srv_.legacyProtocolReady_.store(true, std::memory_order_relaxed);
            out = r;
            return true;
        }

        // OUR reply: the id matches this guard's CURRENT request AND the type
        // is either the expected one or a STATE_CHUNK continuation (the child
        // continues a chunked reply with the same id; the caller's own length
        // checks decide how many it consumes).
        //
        // The first clause covers the REQUESTLESS AWAITS (the spawn READY
        // handshake): the parent sends nothing, so the child's reply carries
        // kUnsolicitedRequestId and there is no id to match. Only legal while
        // no request has been sent on this exchange — once one has, a reply
        // must carry ITS id (the generic id-0 rule cannot shadow it).
        if (!haveRequest_ && r.requestId == kUnsolicitedRequestId && r.type == expected) {
            srv_.noteInSync();
            out = r;
            return true;
        }

        if (haveRequest_ && r.requestId == currentRequestId_
            && (r.type == expected || r.type == MessageType::STATE_CHUNK)) {
            // We just read the reply we asked for: the stream is provably in
            // sync at this point, so the desync hint can be cleared.
            srv_.noteInSync();
            out = r;
            return true;
        }

        // A reply for some OTHER request (a late reply from an exchange that
        // already timed out, or an unexpected type for our id). Message-mode
        // named pipes deliver whole messages (PIPE_TYPE_MESSAGE |
        // PIPE_READMODE_MESSAGE), so dropping one cannot corrupt the framing of
        // the ones still queued behind it. Discarding is now CORRECT, not
        // best-effort: the correlation id proves this reply is not ours, and
        // it stays discarded for every future exchange too (its id can never
        // match again).
        srv_.noteUnexpectedReply(r.type, expected, r.requestId, currentRequestId_);
        continue;
    }
}

// --- PipeClient ---

#if defined(_WIN32)

PipeClient::PipeClient(const std::string& pipeName) : name(pipeName) {}

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    hPipe = CreateFileA(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);
    return true;
}

void PipeClient::disconnect() {
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::send(const ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &msg, sizeof(ProxyMessage), &bytesWritten, nullptr);
}

bool PipeClient::receive(ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    DWORD bytesRead = 0;
    return ReadFile(hPipe, &resp, sizeof(ProxyResponse), &bytesRead, nullptr);
}

bool PipeClient::sendResp(const ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &resp, sizeof(ProxyResponse), &bytesWritten, nullptr);
}

bool PipeClient::receiveMsg(ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    DWORD bytesRead = 0;
    return ReadFile(hPipe, &msg, sizeof(ProxyMessage), &bytesRead, nullptr);
}

#else
// LINUX BRANCH — the child side of the AF_UNIX SOCK_SEQPACKET transport.
// connect() maps CreateFileA: socket() + connect() to
// socketPathForPipeName(name); ECONNREFUSED/ENOENT simply return false, the
// same "server not there yet" outcome a failed CreateFileA produced. Each
// send/recv moves exactly one whole 256-byte frame (SEQPACKET record).
PipeClient::PipeClient(const std::string& pipeName) : name(pipeName) {}

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    disconnect();
    HANDLE h = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    const std::string path = socketPathForPipeName(name);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(h);
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(h, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(h);
        return false;
    }
    hPipe = h;
    return true;
}

void PipeClient::disconnect() {
    if (hPipe != INVALID_HANDLE_VALUE) {
        ::close(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::send(const ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    return ::send(hPipe, &msg, sizeof(ProxyMessage), MSG_NOSIGNAL)
           == static_cast<ssize_t>(sizeof(ProxyMessage));
}

bool PipeClient::receive(ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    const ssize_t n = ::recv(hPipe, &resp, sizeof(ProxyResponse), 0);
    return n == static_cast<ssize_t>(sizeof(ProxyResponse));
}

bool PipeClient::sendResp(const ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    return ::send(hPipe, &resp, sizeof(ProxyResponse), MSG_NOSIGNAL)
           == static_cast<ssize_t>(sizeof(ProxyResponse));
}

bool PipeClient::receiveMsg(ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    const ssize_t n = ::recv(hPipe, &msg, sizeof(ProxyMessage), 0);
    return n == static_cast<ssize_t>(sizeof(ProxyMessage));
}

#endif // defined(_WIN32)

} // namespace proxy
