#include "ProxyPipe.h"
#include "../common/DebugLog.h"
#include <cassert>
#include <cstring>

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
              "reply types it does not expect (best effort: the protocol has no "
              "correlation id)");
    }
}

void PipeServer::noteInSync() {
    desynced_.store(false, std::memory_order_relaxed);
}

void PipeServer::noteUnexpectedReply(MessageType got, MessageType expected, bool discarded) {
    if (discarded)
        staleDiscards_.fetch_add(1, std::memory_order_relaxed);
    HDAW_LOG("proxy", std::string("STALE/UNEXPECTED reply type=") + replyTypeName(got)
        + " expected=" + replyTypeName(expected)
        + (discarded ? " — discarded" : " — returned to caller (pipe believed in sync)")
        + " (pipe=" + name + ")");
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
    return srv_.sendMsgBoundedRaw(msg, timeoutMs);
}

bool PipeServer::Exchange::sendRequestUnbounded(const ProxyMessage& msg) {
    if (!acquired_) return false;
    return srv_.sendMsgRaw(msg);
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

        if (r.type == expected) {
            // We just read the reply we asked for: the stream is provably in
            // sync at this point, so the desync hint can be cleared.
            srv_.noteInSync();
            out = r;
            return true;
        }

        if (r.type == MessageType::STATE_CHUNK) {
            // Continuation of a chunked reply: the child answers a chunked GET
            // with a header plus N of these, and the caller's own length
            // checks decide how many it consumes. Never a stray type.
            out = r;
            return true;
        }

        if (r.type == MessageType::EDITOR_CLOSED) {
            // UNSOLICITED: the child's editor window closed on its own. No
            // request produces this type, so handing it to the caller would
            // give the caller an "answer" it never asked for AND swallow a real
            // editor-close notification. Route it to the handler and keep
            // waiting inside the same budget.
            srv_.routeUnsolicitedEditorClosed();
            continue;
        }

        // A reply for some OTHER transaction: a late reply from an exchange
        // that already timed out, or a genuine protocol error. Message-mode
        // named pipes deliver whole messages (PIPE_TYPE_MESSAGE |
        // PIPE_READMODE_MESSAGE), so dropping one cannot corrupt the framing of
        // the ones still queued behind it — which is what makes discarding
        // safe. Discard it only while the pipe is known desynced (best effort:
        // with no correlation id, a stale reply of the EXPECTED type remains
        // indistinguishable); otherwise return it so the caller's own type
        // check keeps reporting the error.
        if (srv_.desynced_.load(std::memory_order_relaxed)) {
            srv_.noteUnexpectedReply(r.type, expected, /*discarded=*/true);
            continue;
        }
        srv_.noteUnexpectedReply(r.type, expected, /*discarded=*/false);
        srv_.noteDesync("unexpected reply type inside an in-sync exchange");
        out = r;
        return true;
    }
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
