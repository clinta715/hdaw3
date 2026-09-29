#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <atomic>
#include <string>

namespace proxy {

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
    //    hPipe and NEVER calls CloseHandle.
    void stop();
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

private:
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
