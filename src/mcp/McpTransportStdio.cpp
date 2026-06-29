#include "McpTransportStdio.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

namespace mcp {

class TransportStdio::Reader {
public:
    Reader(TransportStdio* p) : parent_(p) {}
    void run() {
        QFile in;
        in.open(STDIN_FILENO, QIODevice::ReadOnly | QIODevice::Text);
        QTextStream ts(&in);
        while (!parent_->stopped_.load(std::memory_order_relaxed)) {
            QString line = ts.readLine();
            if (line.isNull()) break;
            auto trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            QJsonParseError pe;
            auto doc = QJsonDocument::fromJson(trimmed.toUtf8(), &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                parent_->send(serializeResponse(
                    McpResponse::failure({}, err::ParseError, "invalid JSON")).toUtf8());
                continue;
            }
            auto v = validateRequest(doc.object());
            if (std::holds_alternative<McpResponse>(v)) {
                parent_->send(serializeResponse(std::get<McpResponse>(v)).toUtf8());
                continue;
            }
            auto& req = std::get<McpRequest>(v);
            QMetaObject::invokeMethod(parent_->server_, "handleRequest",
                Qt::QueuedConnection,
                Q_ARG(QJsonValue, req.id),
                Q_ARG(QString, req.method),
                Q_ARG(QJsonValue, req.params));
        }
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), "quit",
                                      Qt::QueuedConnection);
        }
    }
private:
    TransportStdio* parent_;
};

void TransportStdio::ReaderThread::run() {
    if (reader_) reader_->run();
}

TransportStdio::TransportStdio() = default;
TransportStdio::~TransportStdio() { stop(); }

void TransportStdio::start(McpServer* s) {
    server_ = s; stopped_ = false;
    reader_ = new Reader(this);
    readerThread_.setReader(reader_);
    readerThread_.start();
}

void TransportStdio::stop() {
    stopped_ = true;
    readerThread_.quit();
    readerThread_.wait(200);
    delete reader_; reader_ = nullptr;
}

void TransportStdio::send(const QByteArray& line) {
    QMutexLocker lk(&stdoutMtx_);
    QFile out;
    out.open(STDOUT_FILENO, QIODevice::WriteOnly);
    out.write(line);
    out.putChar('\n');
    out.flush();
}

void TransportStdio::notify(const QByteArray& line) { send(line); }

} // namespace mcp
