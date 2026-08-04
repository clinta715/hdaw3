#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <string>

namespace proxy {

class PipeServer {
public:
    explicit PipeServer(const std::string& pipeName);
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool start();
    void stop();
    bool receive(ProxyMessage& msg);
    bool send(const ProxyResponse& resp);
    bool sendMsg(const ProxyMessage& msg);
    bool sendMsgBounded(const ProxyMessage& msg, DWORD timeoutMs);
    bool receiveResp(ProxyResponse& resp);
    bool receiveRespBounded(ProxyResponse& resp, DWORD timeoutMs);
    bool isConnected() const { return connected; }

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
    bool overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead);
    bool overlappedWrite(const void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesWritten);

    std::string name;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    bool running = false;
    bool connected = false;
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
