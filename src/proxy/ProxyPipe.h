#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace proxy {

// The parent-side end of one isolated plugin's control pipe.
//
// THREADING CONTRACT (see `Exchange` below): EVERY request→response
// transaction on this object runs inside an `Exchange` guard, which owns
// `exchangeMutex_` for the whole transaction. Nothing else serializes the
// protocol — the raw single-op helpers are private and only the guard (and the
// public single-op wrappers, which take the lock for exactly one op) may call
// them.
class PipeServer {
public:
    explicit PipeServer(const std::string& pipeName);
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool start(DWORD* errorOut = nullptr);
    // CONTRACT (lease-based lifetime):
    //  * OBJECT memory lifetime = the shared_ptr<PipeServer> LEASE held by the
    //    caller for the whole exchange (ProxyProcessManager::getPipe returns
    //    one). The manager map entry is NOT a lifetime guarantee — any
    //    killPluginHost erases it.
    //  * HANDLE lifetime = until ~PipeServer, the SINGLE closer. Because the
    //    dtor runs at the last lease release, no operation can be in flight
    //    against a closed handle by construction.
    //  * stop() only signals + cancels: it sets stopped_ (every subsequent I/O
    //    call bails immediately) and CancelIoEx's any in-flight overlapped I/O
    //    so an in-progress read/write returns false promptly. It NEVER writes
    //    hPipe, NEVER calls CloseHandle, and — deliberately — NEVER takes the
    //    exchange lock: an exchange blocked in I/O is released by the cancel
    //    and then drops its own guard.
    void stop();

    // ---------------------------------------------------------------------
    // EXCHANGE SERIALIZATION
    // ---------------------------------------------------------------------
    // A request→response TRANSACTION on this pipe must be atomic with respect
    // to every other transaction on the SAME PipeServer: a slot's pipe is used
    // from the message thread (timerCallback → pollProgramCount; the UI's
    // showEditor/closeEditor + ProxyEditor), from the background state-retry
    // worker (sendStateInternal/verifyStateApplied) and from the editor
    // watcher (waitForEditorClosed). Without serialization `A-send, B-send,
    // A-receive` lets one caller consume the other's reply — both waits are
    // bounded, so the symptom is a wrong/failed reply rather than a hang.
    //
    // `Exchange` is the RAII guard that owns `exchangeMutex_` for the WHOLE
    // transaction and is the ONLY way to run one: every request→response site
    // (including chunked continuations) creates ONE Exchange spanning its
    // complete send/receive sequence.
    //
    // LOCK ORDER (both rules hold, always):
    //   1. A caller FIRST takes the shared_ptr<PipeServer> LEASE from
    //      ProxyProcessManager::getPipe(), THEN constructs the Exchange.
    //   2. While holding an Exchange, NEVER call back into ProxyProcessManager
    //      (getPipe / killPluginHost / setSlotCrashCallback / …): the manager's
    //      `mutex` and this object's `exchangeMutex_` must never be held
    //      simultaneously in opposite orders.
    //   stop() takes NEITHER lock (see above), so it can always unblock a
    //   stuck exchange.
    //
    // RE-ENTRANCY: `exchangeMutex_` is deliberately NON-recursive and no path
    // nests one exchange inside another — `verifyStateApplied` calls
    // `getStateInformation`, but only AFTER `sendStateInternal`'s Exchange has
    // been destroyed, so the nested GET takes its own guard sequentially. A
    // debug-only owner-thread tripwire (`lockOwner_`) asserts that, so a future
    // nested transaction fails loudly instead of silently deadlocking.
    //
    // DESYNC (observability only — NOT a correctness mechanism): a bounded
    // receive that times out does not tear the connection down, so the child's
    // late reply stays queued in the message-mode pipe. `desynced_` records
    // that and the counter below proves a stale reply was seen. Correctness no
    // longer depends on it: every request carries a correlation id
    // (ProxyMessage::requestId) and every reply echoes it, so an exchange
    // delivers only a reply bearing ITS current request's id and DISCARDS
    // anything else (a late reply from an earlier request cannot be mistaken
    // for a fresh one, even of the SAME type — the old expected-type-only
    // heuristic is superseded).
    class Exchange;

    // ---------------------------------------------------------------------
    // SINGLE-OP WRAPPERS
    // ---------------------------------------------------------------------
    // Each takes the exchange lock for exactly ONE operation. They carry no
    // expectation (nothing tells them which reply type is "theirs"), so they
    // cannot classify an unexpected reply — they must NOT be used to assemble
    // a request→response transaction; use `Exchange` for that. No production
    // call site uses them any more (all of them run through `Exchange`); they
    // remain as the public single-op/one-way surface for tests, and as the
    // documented one-op boundary a future caller must not widen.
    bool receive(ProxyMessage& msg);
    bool send(const ProxyResponse& resp);
    bool sendMsg(const ProxyMessage& msg);
    bool sendMsgBounded(const ProxyMessage& msg, DWORD timeoutMs);
    bool receiveResp(ProxyResponse& resp);
    bool receiveRespBounded(ProxyResponse& resp, DWORD timeoutMs);

    // Advisory only: a bounded-receive TIMEOUT deliberately does not clear it
    // (the connection is healthy; a late response is still consumable), so
    // false positives are possible. The authoritative error signal is the
    // return value of the I/O call itself.
    bool isConnected() const { return connected.load(std::memory_order_relaxed); }

    // ---------------------------------------------------------------------
    // UNSOLICITED REPLY ROUTING + DESYNC OBSERVABILITY
    // ---------------------------------------------------------------------
    // The child emits EDITOR_CLOSED with no request behind it (its editor
    // window was closed). No exchange ever expects that type, so an Exchange
    // that reads one routes it HERE instead of handing it to the caller as the
    // caller's reply. Set by PluginProxySlot once the slot has an
    // editor-closed callback; the handler must not call back into the pipe.
    void setEditorClosedHandler(std::function<void()> cb);
    // Observability only (see the DESYNC note above): correctness is id-based.
    bool isDesynced() const { return desynced_.load(std::memory_order_relaxed); }
    uint64_t staleRepliesDiscarded() const { return staleDiscards_.load(std::memory_order_relaxed); }
    uint64_t desyncEvents() const { return desyncEvents_.load(std::memory_order_relaxed); }

    // TRUE once a reply proved the child speaks the pre-correlation-id (v1)
    // framing: only the requestless READY handshake can prove it (a v1 child
    // answers READY in the old layout, which v2 decodes as requestId=1 /
    // result=0 / dataSize=0 — see isLegacyV1ReadyReply in ProxyCommon.h).
    // The handshake returns that reply instead of discarding it, and
    // spawnPluginHost turns this flag into an immediate, non-timeout spawn
    // failure with kLegacyV1Diagnosis.
    bool sawLegacyProtocolReady() const { return legacyProtocolReady_.load(std::memory_order_relaxed); }

private:
    // Monotonic correlation-id allocator: returns 1, 2, 3, … (never 0 —
    // kUnsolicitedRequestId is reserved for requestless notifications such as
    // EDITOR_CLOSED). Called by Exchange::sendRequest, once per REQUEST (not
    // once per guard): a guard spanning many sequential requests (e.g.
    // fetchParamMetadata) must be able to tell a late reply from an earlier
    // request apart from the current one. Wraps at 2^32 skipping 0.
    uint32_t allocateRequestId();
    // Bounded READY wait. Heavy plugins (e.g. Vital) can take several seconds
    // to initialise, so the default is generous.
    static constexpr DWORD kReadyTimeoutMs = 8000;

    // OVERLAPPED connect with a bounded wait. Returns true on success (client
    // connected, or was already connected). On timeout/error: cancels the IO,
    // leaves connected=false.
    bool overlappedConnect(DWORD timeoutMs);
    // OVERLAPPED read/write with a bounded wait. INFINITE preserves the prior
    // blocking behavior for non-READY exchanges. Return true on completion with
    // bytesTransferred filled; false on timeout/error (IO cancelled).
    // timedOut (optional) reports WAIT_TIMEOUT distinctly from real pipe
    // errors: a bounded-receive timeout must NOT mark the pipe disconnected
    // (the connection is healthy; a late response is still consumable), while
    // a genuine read error must.
    bool overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead,
                        bool* timedOut = nullptr);
    bool overlappedWrite(const void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesWritten);

    // ---------------------------------------------------------------------
    // RAW OPERATIONS — NO LOCK. The caller MUST hold `exchangeMutex_` (an
    // `Exchange` guard, or one of the single-op wrappers above). These are the
    // only places that touch hPipe for I/O, and — because every public entry
    // point takes the lock first — connection establishment and every
    // `connected` transition therefore happen under the exchange lock: the
    // transport state is owned by the serialized transaction, never by a racy
    // single-op peek.
    // ---------------------------------------------------------------------
    bool receiveRaw(ProxyMessage& msg);
    bool sendRaw(const ProxyResponse& resp);
    bool sendMsgRaw(const ProxyMessage& msg);
    bool sendMsgBoundedRaw(const ProxyMessage& msg, DWORD timeoutMs);
    // The legacy `receiveResp` shape: kReadyTimeoutMs on BOTH the connect and
    // the read (that is what every pre-serialization handshake used).
    bool receiveRespRaw(ProxyResponse& resp);
    bool receiveRespBoundedRaw(ProxyResponse& resp, DWORD timeoutMs);

    // --- desync bookkeeping (all callers hold `exchangeMutex_`) ----------
    void noteDesync(const char* why);
    void noteInSync();
    void noteUnexpectedReply(MessageType got, MessageType expected,
                             uint32_t gotId, uint32_t myId);
    void routeUnsolicitedEditorClosed();

    std::string name;
    // Each I/O helper loads this ONCE into a local at entry and never re-reads
    // the member mid-operation: stop() may run concurrently on another thread
    // (cancel-only, so the loaded handle stays valid for the whole operation).
    // The HANDLE is written only by start() and ~PipeServer (the single closer).
    std::atomic<HANDLE> hPipe{ INVALID_HANDLE_VALUE };
    // Set by stop(): every new I/O call bails immediately. Distinct from
    // `connected`, which is a transport state that a bounded-receive timeout
    // deliberately does NOT clear.
    std::atomic<bool> stopped_{ false };
    std::atomic<bool> running{ false };
    std::atomic<bool> connected{ false };

    // ONE lock per complete request→response transaction (see the contract in
    // the public section above). Non-recursive by design: no path nests.
    std::mutex exchangeMutex_;
#ifndef NDEBUG
    // Debug-only re-entrancy tripwire for the lock above. Read relaxed before
    // acquiring and written while holding the lock; it exists to turn a future
    // nested transaction (which would deadlock a non-recursive mutex) into a
    // loud assertion at the offending call site.
    std::atomic<std::thread::id> lockOwner_{};
#endif

    // Monotonic correlation-id source (see allocateRequestId). Only Exchange
    // touches it, and only while holding the exchange lock.
    std::atomic<uint32_t> requestIdCounter_{ 0 };

    // Set by the READY handshake when a reply matched the LEGACY v1 signature
    // (see sawLegacyProtocolReady). Only ever true on a spawn-handshake pipe.
    std::atomic<bool> legacyProtocolReady_{ false };

    // Desync hint + its observability counters (see the DESYNC note above).
    std::atomic<bool> desynced_{ false };
    std::atomic<uint64_t> staleDiscards_{ 0 };
    std::atomic<uint64_t> desyncEvents_{ 0 };

    // Unsolicited EDITOR_CLOSED destination. Read/written under exchangeMutex_
    // (the reader is Exchange::receiveReply, which holds it) so the routing is
    // race-free against setEditorClosedHandler.
    std::function<void()> editorClosedHandler_;
};

// RAII guard covering ONE complete request→response transaction.
//
// Every request→response site creates exactly one Exchange and drives the
// whole transaction through it:
//   PipeServer::Exchange ex(pipe);
//   ex.sendRequest(msg, timeout);
//   ProxyResponse resp{};
//   ex.receiveReply(resp, MessageType::EXPECTED_RESULT, timeout);
// Chunked replies (a header plus N STATE_CHUNK continuations) stay inside the
// SAME guard — the caller keeps calling receiveReply for the continuations.
class PipeServer::Exchange {
public:
    // Blocking acquisition: the transaction waited for the pipe. Every
    // request→response site uses this form.
    explicit Exchange(PipeServer& server);
    // Non-blocking acquisition for AWAIT loops that must never starve a real
    // exchange (the editor watcher). `acquired()` reports whether the loop won
    // the lock; when it did not, the await loop must release everything and
    // retry rather than queue up behind a multi-second transaction.
    Exchange(PipeServer& server, std::try_to_lock_t);
    ~Exchange();

    Exchange(const Exchange&) = delete;
    Exchange& operator=(const Exchange&) = delete;

    bool acquired() const noexcept { return acquired_; }

    // The correlation id of this guard's CURRENT request: valid after a
    // successful sendRequest/sendRequestUnbounded (and reused by every
    // sendContinuation); kUnsolicitedRequestId before any request is sent.
    // Exposed for observability/tests — production matching uses it internally.
    uint32_t currentRequestId() const noexcept { return currentRequestId_; }

    // Request half. Every request of the transaction goes through here — a
    // chunked SET_STATE sends N requests before its single reply. Each call
    // allocates a FRESH correlation id (from PipeServer's monotonic allocator),
    // stamps it into the message and records it as this guard's CURRENT
    // request id; receiveReply then accepts only replies bearing that id.
    // Allocating per request (not per guard) is what lets a guard that spans
    // many sequential requests — fetchParamMetadata, the GET_STATE retry loop —
    // discard a late reply from an EARLIER request of the same guard.
    bool sendRequest(const ProxyMessage& msg, DWORD timeoutMs);
    bool sendRequestUnbounded(const ProxyMessage& msg);

    // Continuation half of a multi-message request: sends with the SAME id as
    // the current request (the child echoes it on the reply), so a chunked
    // SET_STATE's first message and its STATE_CHUNKs share one correlation id.
    // Requires a prior sendRequest on this guard (logs + returns false
    // otherwise — a continuation with no request is a programming error).
    bool sendContinuation(const ProxyMessage& msg, DWORD timeoutMs);

    // Reply half, bounded. Loops over the raw reads so that replies which are
    // NOT this exchange's answer never reach the caller:
    //   * requestId == this guard's current request id AND
    //     (type == `expected` or STATE_CHUNK continuation) → returned to the
    //     caller (and clears the desync hint)
    //   * requestId == kUnsolicitedRequestId AND type == `expected`, on a guard
    //     that has sent NO request → returned: the requestless await (the spawn
    //     READY handshake, whose reply carries id 0 by design)
    //   * requestId == kUnsolicitedRequestId AND type == EDITOR_CLOSED
    //     → UNSOLICITED: routed to the editor-closed handler and skipped
    //     (never handed to a caller that never asked for it); waiting
    //     continues inside the same budget
    //   * anything else (wrong id, or an unexpected type for our id)
    //     → counted + logged as STALE/UNEXPECTED and DISCARDED; waiting
    //     continues inside the same budget. This discard is now CORRECT (not
    //     best-effort): the id proves the reply is not ours.
    // timeoutIsDesync=false is for await loops whose timeout is a NORMAL idle
    // condition (the editor watcher polls a 500 ms budget forever); such a
    // timeout must not mark the pipe desynced.
    bool receiveReply(ProxyResponse& out, MessageType expected, DWORD timeoutMs,
                      bool timeoutIsDesync = true);

    // Reply half on the legacy handshake budget (PipeServer::kReadyTimeoutMs,
    // applied to both the connect and the read). This is the shape every
    // pre-existing `receiveResp` call site had — the UI's showEditor/
    // closeEditor handshake and the spawn READY wait — and it is preserved
    // verbatim: a heavy plugin (Vital) legitimately needs seconds to become
    // ready, but a hung child still times out instead of blocking forever.
    bool receiveReplyReady(ProxyResponse& out, MessageType expected);

private:
    bool receiveReplyImpl(ProxyResponse& out, MessageType expected,
                          DWORD timeoutMs, bool readyBudget, bool timeoutIsDesync);

    PipeServer& srv_;
    bool acquired_ = false;
    // Correlation id of this guard's CURRENT request (set by sendRequest /
    // sendRequestUnbounded, reused by sendContinuation). Replies are matched
    // against it. haveRequest_ is false until the first request is sent — the
    // editor-closed watcher never sends one (it waits for an unsolicited
    // EDITOR_CLOSED), so a receive with no request is only legal for that
    // unsolicited type (and the READY spawn await); see receiveReplyImpl.
    uint32_t currentRequestId_ = kUnsolicitedRequestId;
    bool haveRequest_ = false;
};

class PipeClient {
public:
    explicit PipeClient(const std::string& pipeName);
    ~PipeClient();

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    bool connect();
    void disconnect();
    bool send(const ProxyMessage& msg);
    bool receive(ProxyResponse& resp);
    bool sendResp(const ProxyResponse& resp);
    bool receiveMsg(ProxyMessage& msg);

private:
    std::string name;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
};

} // namespace proxy
