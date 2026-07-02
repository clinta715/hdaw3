#pragma once
#include "McpTransport.h"
#include <QHttpServer>
#include <memory>

namespace mcp {
class TransportHttp : public Transport {
public:
    TransportHttp(quint16 port, const QString& host = QString());
    ~TransportHttp() override;
    bool start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
    quint16 port() const { return port_; }
    QString lastError() const { return lastError_; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    quint16 port_;
    QString host_;
    McpServer* server_ = nullptr;
    QString lastError_;
};
}
