#pragma once
#include "McpTransport.h"
#include <QThread>
#include <QMutex>
#include <atomic>

namespace mcp {
class TransportStdio : public Transport {
public:
    TransportStdio();
    ~TransportStdio() override;
    void start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
private:
    class Reader;
    class ReaderThread : public QThread {
    public:
        void setReader(Reader* r) { reader_ = r; }
        void run() override;
    private:
        Reader* reader_ = nullptr;
    };
    Reader* reader_ = nullptr;
    ReaderThread readerThread_;
    McpServer* server_ = nullptr;
    std::atomic<bool> stopped_{false};
    QMutex stdoutMtx_;
};
}
