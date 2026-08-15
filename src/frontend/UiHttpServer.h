#pragma once

#include <QtGlobal>
#include <QHostAddress>
#include <QString>
#include <memory>

class QTcpServer;
class QHttpServer;

namespace frontend {

// Static file server for the bundled HTML frontend.
// Serves index.html at GET / with the WebSocket port injected,
// and static assets from Qt resources at GET /assets/<filename>.
class UiHttpServer {
public:
    explicit UiHttpServer(quint16 wsPort);
    ~UiHttpServer();

    // Bind to the given HTTP port. Defaults to loopback. Returns false if the
    // port is already in use. Idempotent: a second start() after stop() works.
    bool start(quint16 port,
               const QHostAddress& bindAddress = QHostAddress::LocalHost);

    // Stop listening and tear down the server.
    void stop();

    // The actual bound port (handy if start(0) was used to pick a free port).
    quint16 port() const;

    // The auth token. Empty when auth is disabled.
    QString authToken() const;

private:
    quint16 wsPort_;
    QString authToken_;
    std::unique_ptr<QTcpServer> tcp_;
    std::unique_ptr<QHttpServer> server_;
};

} // namespace frontend
