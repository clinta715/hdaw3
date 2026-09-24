#include "McpTransportHttp.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHttpServerConfiguration>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <chrono>

namespace mcp {

class TransportHttp::Impl {
public:
    QHttpServer server;
    std::unique_ptr<QTcpServer> tcp;
};

TransportHttp::TransportHttp(quint16 p, const QString& h) : port_(p), host_(h), impl_(std::make_unique<Impl>()) {}
TransportHttp::~TransportHttp() { stop(); }

bool TransportHttp::start(McpServer* s) {
    lastError_.clear();
    server_ = s;
    // rebuild-commands-drop-response: Qt's default 15 s keep-alive abort()s
    // the connection on a long synchronous rebuild because lastActiveTimer
    // only restarts on request read — the buffered response is discarded.
    // 900 s matches the MCP client timeout convention (900000 ms in
    // .mcp.json), exceeds any realistic rebuild (bake budget tops at 120 s),
    // and makes the heartbeat (interval = timeout/2) unable to fire between
    // a long request's completion and its response flush — which is exactly
    // the drop mechanism. Local control server: a 900 s idle keep-alive is
    // harmless (clients time out first). Applied to impl_->server — the same
    // instance that serves /mcp via route() and bind() below.
    QHttpServerConfiguration keepAliveCfg = impl_->server.configuration();
    keepAliveCfg.setKeepAliveTimeout(std::chrono::seconds(900));
    impl_->server.setConfiguration(keepAliveCfg);
    impl_->server.route("/mcp", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) -> QHttpServerResponse {
            auto body = req.body();
            QJsonParseError pe;
            auto doc = QJsonDocument::fromJson(body, &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                return QHttpServerResponse("application/json",
                    serializeResponse(
                        McpResponse::failure({}, err::ParseError, "invalid JSON")).toUtf8());
            }
            auto v = validateRequest(doc.object());
            if (std::holds_alternative<McpResponse>(v)) {
                return QHttpServerResponse("application/json",
                    serializeResponse(std::get<McpResponse>(v)).toUtf8());
            }
            auto& req2 = std::get<McpRequest>(v);
            if (!server_) {
                return QHttpServerResponse("application/json",
                    serializeResponse(
                        McpResponse::failure({}, err::InternalError, "server not set")).toUtf8());
            }
            // dispatchRequest is main-thread-safe; the QHttpServer handler
            // IS the main thread (or a thread we've chosen to live on for
            // tests), so a direct call is correct.
            if (req2.id.isNull()) {
                // Notification: dispatch side-effects (e.g. notifications/cancelled
                // sets the cancel flag) and return an empty 200 — per JSON-RPC 2.0
                // the server must not produce a response for a notification.
                server_->dispatchRequest(req2.id, req2.method, req2.params);
                return QHttpServerResponse("application/json", QByteArray(""));
            }
            auto dr = server_->dispatchRequest(req2.id, req2.method, req2.params);
            if (dr.isError) {
                auto errObj = dr.payload.toObject();
                return QHttpServerResponse("application/json",
                    serializeResponse(McpResponse::failure(
                        req2.id,
                        errObj.value("code").toInt(err::InternalError),
                        errObj.value("message").toString())).toUtf8());
            }
            // dr.payload is the tool result (either {isError, content} for
            // tools/call, or the bare result object for
            // initialize/tools.list/ping). For a JSON-RPC success response
            // we always wrap in {result: ...}.
            return QHttpServerResponse("application/json",
                QJsonDocument(QJsonObject{
                    {"jsonrpc","2.0"},{"id", req2.id},{"result", dr.payload}
                }).toJson());
        });
    impl_->tcp = std::make_unique<QTcpServer>();
    QHostAddress addr = host_.isEmpty() ? QHostAddress::LocalHost
                                        : QHostAddress(host_);
    if (!impl_->tcp->listen(addr, port_)) {
        lastError_ = QString("failed to listen on %1:%2 (%3)")
                       .arg(addr.toString()).arg(port_).arg(impl_->tcp->errorString());
        impl_->tcp.reset();
        return false;
    }
    if (!impl_->server.bind(impl_->tcp.get())) {
        lastError_ = QString("failed to bind QHttpServer on %1:%2")
                       .arg(addr.toString()).arg(port_);
        impl_->tcp->close();
        impl_->tcp.reset();
        return false;
    }
    // Refresh after a successful listen: with port 0 the OS assigned an
    // ephemeral port, so port() must expose the bound port. No-op for an
    // explicit nonzero port (serverPort() equals the requested port); on a
    // listen/bind failure above we never reach here, so the constructor
    // value is preserved.
    port_ = impl_->tcp->serverPort();
    return true;
}

void TransportHttp::stop() {
    if (impl_) {
        if (impl_->tcp) {
            impl_->tcp->close();
            impl_->tcp.reset();
        }
        impl_->server.disconnect();
    }
    server_ = nullptr;
}
void TransportHttp::send(const QByteArray&) { /* async response path; v1 no-op */ }
void TransportHttp::notify(const QByteArray& l) { send(l); }

} // namespace mcp
