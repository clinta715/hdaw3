#pragma once
#include "McpTransport.h"
#include <QHttpServer>
#include <memory>

namespace mcp {
class TransportHttp : public Transport {
public:
    TransportHttp(quint16 port);
    ~TransportHttp() override;
    void start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
    quint16 port() const { return port_; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    quint16 port_;
    McpServer* server_ = nullptr;
};
}
