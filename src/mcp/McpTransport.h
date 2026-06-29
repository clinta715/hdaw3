#pragma once
#include <QByteArray>
class McpServer;
namespace mcp {
class Transport {
public:
    virtual ~Transport() = default;
    virtual void start(McpServer* server) = 0;
    virtual void stop() = 0;
    virtual void send(const QByteArray& jsonLine) = 0;
    virtual void notify(const QByteArray& jsonLine) = 0;
};
}
