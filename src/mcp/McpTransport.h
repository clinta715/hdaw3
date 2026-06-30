#pragma once
#include <QByteArray>
namespace mcp { class McpServer; }
namespace mcp {
class Transport {
public:
    virtual ~Transport() = default;
    // Start the transport. Returns true on success, false if binding
    // fails (e.g. the port is already in use for the HTTP transport).
    virtual bool start(McpServer* server) = 0;
    virtual void stop() = 0;
    virtual void send(const QByteArray& jsonLine) = 0;
    virtual void notify(const QByteArray& jsonLine) = 0;
};
}
