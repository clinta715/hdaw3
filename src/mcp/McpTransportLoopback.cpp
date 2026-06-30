#include "McpTransportLoopback.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QJsonDocument>

namespace mcp {

TransportLoopback::TransportLoopback() = default;

bool TransportLoopback::start(McpServer* s) { server_ = s; stopped_ = false; return true; }
void TransportLoopback::stop() {
    QMutexLocker lk(&mtx_); stopped_ = true; cv_.wakeAll();
}
void TransportLoopback::send(const QByteArray& line) {
    QMutexLocker lk(&mtx_); outgoing_ += line; outgoing_ += '\n'; cv_.wakeAll();
}
void TransportLoopback::notify(const QByteArray& line) { send(line); }

void TransportLoopback::pumpIncoming(const QByteArray& line) {
    if (!server_) return;
    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (server_ && server_->transport()) {
            server_->transport()->send(serializeResponse(
                McpResponse::failure({}, err::ParseError,
                    QString::fromUtf8(pe.errorString().toUtf8()))).toUtf8());
        }
        return;
    }
    auto v = validateRequest(doc.object());
    if (std::holds_alternative<McpResponse>(v)) {
        // Forward the parse/validation error to the transport so the test
        // (or future client) can assert on it instead of hanging.
        if (server_ && server_->transport()) {
            server_->transport()->send(serializeResponse(std::get<McpResponse>(v)).toUtf8());
        }
        return;
    }
    auto& req = std::get<McpRequest>(v);
    QMetaObject::invokeMethod(server_, "handleRequest",
        Qt::DirectConnection,
        Q_ARG(QJsonValue, req.id),
        Q_ARG(QString, req.method),
        Q_ARG(QJsonValue, req.params));
}

QByteArray TransportLoopback::drainOutgoing() {
    QMutexLocker lk(&mtx_); QByteArray r = outgoing_; outgoing_.clear(); return r;
}
bool TransportLoopback::waitForOutgoing(int msec, QByteArray* out) {
    QMutexLocker lk(&mtx_);
    if (!outgoing_.isEmpty()) { if (out) *out = outgoing_; return true; }
    if (stopped_) return false;
    cv_.wait(&mtx_, msec);
    if (out) *out = outgoing_;
    return !outgoing_.isEmpty();
}

} // namespace mcp
