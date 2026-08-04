#include "ProxyPipe.h"
#include <cstring>

namespace proxy {

// --- PipeServer ---

PipeServer::PipeServer(const std::string& pipeName) : name(pipeName) {}

PipeServer::~PipeServer() { stop(); }

bool PipeServer::start() {
    // FILE_FLAG_OVERLAPPED is mandatory for the bounded (WaitForSingleObject)
    // IO used in receiveResp — a synchronous pipe handle cannot be given a
    // per-call timeout. Because the handle is overlapped, EVERY read/write on
    // it must supply an OVERLAPPED structure; see the overlapped* helpers.
    hPipe = CreateNamedPipeA(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
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

bool PipeServer::overlappedConnect(DWORD timeoutMs) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);  // manual-reset
    if (!ov.hEvent) return false;

    BOOL ok = ConnectNamedPipe(hPipe, &ov);
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
                success = GetOverlappedResult(hPipe, &ov, &transferred, FALSE) != 0;
            } else {
                // Timeout/abandoned: cancel and wait for the cancellation to
                // settle before releasing the event.
                CancelIo(hPipe);
                DWORD transferred = 0;
                GetOverlappedResult(hPipe, &ov, &transferred, TRUE);
            }
        }
        // Any other error: leave success == false.
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool PipeServer::overlappedRead(void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesRead) {
    bytesRead = 0;
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = ReadFile(hPipe, buf, size, &bytesRead, &ov);
    bool success = false;
    if (ok) {
        // Completed synchronously; bytesRead already filled.
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                success = GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE) != 0;
            } else {
                CancelIo(hPipe);
                DWORD transferred = 0;
                GetOverlappedResult(hPipe, &ov, &transferred, TRUE);
            }
        }
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool PipeServer::overlappedWrite(const void* buf, DWORD size, DWORD timeoutMs, DWORD& bytesWritten) {
    bytesWritten = 0;
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = WriteFile(hPipe, buf, size, &bytesWritten, &ov);
    bool success = false;
    if (ok) {
        success = true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (wait == WAIT_OBJECT_0) {
                success = GetOverlappedResult(hPipe, &ov, &bytesWritten, FALSE) != 0;
            } else {
                CancelIo(hPipe);
                DWORD transferred = 0;
                GetOverlappedResult(hPipe, &ov, &transferred, TRUE);
            }
        }
    }

    CloseHandle(ov.hEvent);
    return success;
}

bool PipeServer::receive(ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    if (!connected) {
        if (!overlappedConnect(INFINITE)) {
            connected = false;
            return false;
        }
        connected = true;
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&msg, sizeof(ProxyMessage), INFINITE, bytesRead)) {
        connected = false;
        return false;
    }
    return bytesRead >= sizeof(ProxyMessage) - sizeof(msg.data);
}

bool PipeServer::send(const ProxyResponse& resp) {
    if (hPipe == INVALID_HANDLE_VALUE || !connected) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&resp, sizeof(ProxyResponse), INFINITE, bytesWritten);
}

bool PipeServer::sendMsg(const ProxyMessage& msg) {
    if (hPipe == INVALID_HANDLE_VALUE || !connected) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), INFINITE, bytesWritten);
}

bool PipeServer::sendMsgBounded(const ProxyMessage& msg, DWORD timeoutMs) {
    if (hPipe == INVALID_HANDLE_VALUE || !connected) return false;
    DWORD bytesWritten = 0;
    return overlappedWrite(&msg, sizeof(ProxyMessage), timeoutMs, bytesWritten);
}

bool PipeServer::receiveResp(ProxyResponse& resp) {
    // Bounded READY wait: a hung child must not hang the engine forever.
    // Connect is normally near-instant (child connects right after spawn); the
    // dominant cost is the child's plugin init before it writes READY, so the
    // read gets the full kReadyTimeoutMs budget.
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    if (!connected) {
        if (!overlappedConnect(kReadyTimeoutMs)) {
            connected = false;
            return false;
        }
        connected = true;
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), kReadyTimeoutMs, bytesRead)) {
        connected = false;
        return false;
    }
    return bytesRead >= sizeof(ProxyResponse) - sizeof(resp.data);
}

bool PipeServer::receiveRespBounded(ProxyResponse& resp, DWORD timeoutMs) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    if (!connected) {
        if (!overlappedConnect(timeoutMs)) {
            connected = false;
            return false;
        }
        connected = true;
    }
    DWORD bytesRead = 0;
    if (!overlappedRead(&resp, sizeof(ProxyResponse), timeoutMs, bytesRead)) {
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
