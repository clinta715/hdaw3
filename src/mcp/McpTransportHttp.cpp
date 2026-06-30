#include "McpTransportHttp.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>

namespace mcp {

class TransportHttp::Impl {
public:
    QHttpServer server;
    std::unique_ptr<QTcpServer> tcp;
};

TransportHttp::TransportHttp(quint16 p) : port_(p), impl_(std::make_unique<Impl>()) {}
TransportHttp::~TransportHttp() { stop(); }

void TransportHttp::start(McpServer* s) {
    server_ = s;
    impl_->server.route("/mcp", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) -> QHttpServerResponse {
            auto doc = QJsonDocument::fromJson(req.body());
            if (!doc.isObject())
                return QHttpServerResponse("application/json",
                    serializeResponse(
                        McpResponse::failure({}, err::ParseError, "invalid JSON")).toUtf8());
            auto v = validateRequest(doc.object());
            if (std::holds_alternative<McpResponse>(v))
                return QHttpServerResponse("application/json",
                    serializeResponse(std::get<McpResponse>(v)).toUtf8());
            // v1 sync stub: return a placeholder so clients don't hang.
            // The full async round-trip is a documented follow-up.
            return QHttpServerResponse("application/json",
                QJsonDocument(QJsonObject{{"note","v1 HTTP sync stub"}}).toJson());
        });
    impl_->tcp = std::make_unique<QTcpServer>();
    impl_->tcp->listen(QHostAddress::LocalHost, port_);
    impl_->server.bind(impl_->tcp.get());
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
