#include "ProxyPipe.h"
#include <cstring>

namespace proxy {

// --- PipeServer ---

PipeServer::PipeServer(const std::string& pipeName) : name(pipeName) {}

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

bool PipeServer::receive(ProxyMessage& msg) {
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

bool PipeServer::send(const ProxyResponse& resp) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&resp, sizeof(ProxyResponse), INFINITE, bytesWritten);
}

bool PipeServer::sendMsg(const ProxyMessage& msg) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgBounded(const ProxyMessage& msg, DWORD timeoutMs) {
    if (stopped_.load(std::memory_order_relaxed)) return false;
    if (hPipe.load(std::memory_order_acquire) == INVALID_HANDLE_VALUE
        || !connected.load(std::memory_order_relaxed)) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), timeoutMs, bytesWritten);
}

bool PipeServer::receiveResp(ProxyResponse& resp) {
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

bool PipeServer::receiveRespBounded(ProxyResponse& resp, DWORD timeoutMs) {
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

// --- PipeClient ---

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

} // namespace proxy
