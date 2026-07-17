#include "FrontendServer.h"
#include "FrontendRouter.h"
#include "FrontendTreeWatcher.h"

#include "../engine/AudioEngine.h"
#include "../common/ReadModel.h"
#include "../mcp/McpJsonRpc.h"

#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace frontend {

namespace {
constexpr quint16 kDefaultPort = 8766;   // MCP HTTP is 8765; keep them distinct.
constexpr int kPushIntervalMs = 33;      // ~30 Hz, matches the Qt GUI cadence.
} // namespace

FrontendServer::FrontendServer(AudioEngine& engine, QObject* parent)
    : QObject(parent), engine_(engine)
{
    server_ = new QWebSocketServer(QStringLiteral("HDAW Frontend"),
                                   QWebSocketServer::NonSecureMode, this);
    connect(server_, &QWebSocketServer::newConnection,
            this, &FrontendServer::onNewConnection);

    meterTimer_ = new QTimer(this);
    meterTimer_->setInterval(kPushIntervalMs);
    connect(meterTimer_, &QTimer::timeout, this, &FrontendServer::onMeterTimer);

    transportTimer_ = new QTimer(this);
    transportTimer_->setInterval(kPushIntervalMs);
    connect(transportTimer_, &QTimer::timeout, this, &FrontendServer::onTransportTimer);

    // The tree watcher attaches itself to the project root tree in its ctor
    // and re-broadcasts notify.treeChanged on any change.
    treeWatcher_ = std::make_unique<FrontendTreeWatcher>(engine_, *this, this);
}

FrontendServer::~FrontendServer() { stop(); }

bool FrontendServer::start(quint16 port) {
    if (port == 0) port = kDefaultPort;
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        // listen() fails if already listening or port in use.
        if (!server_->isListening())
            return false;
    }
    meterTimer_->start();
    transportTimer_->start();
    return true;
}

void FrontendServer::stop() {
    if (meterTimer_)     meterTimer_->stop();
    if (transportTimer_) transportTimer_->stop();
    if (server_ && server_->isListening()) server_->close();
    for (auto* c : qAsConst(clients_)) {
        c->disconnect(this);
        c->close();
    }
    clients_.clear();
}

quint16 FrontendServer::port() const {
    return server_ ? server_->serverPort() : 0;
}

void FrontendServer::onNewConnection() {
    while (auto* socket = server_->nextPendingConnection()) {
        clients_.insert(socket);
        connect(socket, &QWebSocket::textMessageReceived,
                this, [this, socket](const QString& msg) {
            onTextMessageReceived(msg);
            // keep `socket` referenced through the captured pointer for symmetry
            // with the binary path (Qt queues the signal; the pointer stays live
            // because the socket is in clients_ until disconnected).
            (void)socket;
        });
        connect(socket, &QWebSocket::binaryMessageReceived,
                this, [this, socket](const QByteArray& data) {
            onBinaryMessageReceived(data);
            (void)socket;
        });
        connect(socket, &QWebSocket::disconnected,
                this, [this, socket]() {
            // Tag the sender via the captured pointer; the QSet removes it.
            clients_.remove(socket);
            socket->deleteLater();
        });
    }
}

void FrontendServer::onTextMessageReceived(const QString& text) {
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (socket != nullptr)
        handleOneMessage(socket, text.toUtf8());
}

void FrontendServer::onBinaryMessageReceived(const QByteArray& data) {
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (socket != nullptr)
        handleOneMessage(socket, data);
}

void FrontendServer::handleOneMessage(QWebSocket* socket, const QByteArray& bytes) {
    // Reuse the MCP framing helpers: JSON-RPC 2.0 over WebSocket is the same
    // line protocol as over stdio. parseLine accepts a single JSON value.
    auto parsed = mcp::parseLine(bytes);
    if (!parsed.has_value()) {
        socket->sendTextMessage(mcp::serializeResponse(
            mcp::McpResponse::failure({}, mcp::err::ParseError,
                                      "invalid JSON")));
        return;
    }
    auto reqOrResp = mcp::validateRequest(parsed.value());
    if (!std::holds_alternative<mcp::McpRequest>(reqOrResp)) {
        socket->sendTextMessage(mcp::serializeResponse(
            mcp::McpResponse::failure({}, mcp::err::InvalidRequest,
                                      "expected a JSON-RPC 2.0 request")));
        return;
    }
    const auto req = std::get<mcp::McpRequest>(reqOrResp);

    // Notifications (no id) execute side effects but get no response.
    const bool isNotification = req.isNotification();

    auto result = frontend::dispatch(engine_, req.method, req.params);

    if (isNotification)
        return;  // fire-and-forget; no id to echo back

    if (result.isError) {
        const auto errObj = result.payload.toObject();
        socket->sendTextMessage(mcp::serializeResponse(mcp::McpResponse::failure(
            req.id, errObj.value("code").toInt(mcp::err::InternalError),
            errObj.value("message").toString("internal error"))));
    } else {
        socket->sendTextMessage(mcp::serializeResponse(
            mcp::McpResponse::success(req.id, result.payload)));
    }
}

void FrontendServer::broadcastNotification(const QString& method, const QJsonValue& params) {
    if (clients_.isEmpty()) return;
    const QByteArray bytes = mcp::serializeNotification(
        mcp::McpNotification{ method, params }).toUtf8();
    for (auto* c : qAsConst(clients_))
        c->sendTextMessage(QString::fromUtf8(bytes));
}

void FrontendServer::onMeterTimer() {
    if (clients_.isEmpty()) return;
    // Read-only snapshot of meter levels. The reads hit std::atomic<float>
    // values written by the audio thread; no locks, no audio-thread contact.
    auto& readModel = engine_.getReadModel();
    QJsonArray tracks;
    const int n = readModel.getTrackCount();
    for (int i = 0; i < n; ++i)
        tracks.append(toJson(readModel.getTrackMeter(i)));
    QJsonObject payload{
        { "master", toJson(readModel.getMasterMeter()) },
        { "tracks", tracks },
    };
    broadcastNotification(notify::Meters, payload);
}

void FrontendServer::onTransportTimer() {
    if (clients_.isEmpty()) return;
    broadcastNotification(notify::Transport, toJson(engine_.getReadModel().getTransport()));
}

} // namespace frontend
