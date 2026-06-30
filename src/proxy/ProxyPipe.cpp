#include "ProxyPipe.h"
#include <cstring>

namespace proxy {

// --- PipeServer ---

PipeServer::PipeServer(const std::string& pipeName) : name(pipeName) {}

PipeServer::~PipeServer() { stop(); }

bool PipeServer::start() {
    hPipe = CreateNamedPipeA(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(ProxyResponse),
        sizeof(ProxyMessage),
        0,
        nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    running = true;
    return true;
}

void PipeServer::stop() {
    running = false;
    connected = false;
    if (hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeServer::receive(ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    if (!connected) {
        connected = ConnectNamedPipe(hPipe, nullptr) ||
                    GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) return false;
    }
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hPipe, &msg, sizeof(ProxyMessage), &bytesRead, nullptr);
    if (!ok) {
        connected = false;
        return false;
    }
    return bytesRead >= sizeof(ProxyMessage) - sizeof(msg.data);
}

bool PipeServer::send(const ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE || !connected) return false;
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &resp, sizeof(ProxyResponse), &bytesWritten, nullptr);
}

bool PipeServer::sendMsg(const ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE || !connected) return false;
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &msg, sizeof(ProxyMessage), &bytesWritten, nullptr);
}

bool PipeServer::receiveResp(ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    if (!connected) {
        connected = ConnectNamedPipe(hPipe, nullptr) ||
                    GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) return false;
    }
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hPipe, &resp, sizeof(ProxyResponse), &bytesRead, nullptr);
    if (!ok) {
        connected = false;
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

} // namespace proxy
