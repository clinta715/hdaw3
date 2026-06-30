#pragma once
#include "McpTransport.h"
#include <QByteArray>
#include <QMutex>
#include <QWaitCondition>

namespace mcp {
class TransportLoopback : public Transport {
public:
    TransportLoopback();
    bool start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
    void pumpIncoming(const QByteArray& line);
    QByteArray drainOutgoing();
    bool waitForOutgoing(int msec, QByteArray* out);
private:
    McpServer* server_ = nullptr;
    QMutex mtx_;
    QByteArray outgoing_;
    QWaitCondition cv_;
    bool stopped_ = false;
};
}
