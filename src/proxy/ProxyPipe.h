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
    bool isConnected() const { return connected; }

private:
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

private:
    std::string name;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
};

} // namespace proxy
