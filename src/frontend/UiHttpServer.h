#pragma once

#include <QtGlobal>
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

    // Bind to the given HTTP port on loopback. Returns false if the port is
    // already in use. Idempotent: a second start() after stop() works.
    bool start(quint16 port);

    // Stop listening and tear down the server.
    void stop();

    // The actual bound port (handy if start(0) was used to pick a free port).
    quint16 port() const;

private:
    quint16 wsPort_;
    std::unique_ptr<QTcpServer> tcp_;
    std::unique_ptr<QHttpServer> server_;
};

} // namespace frontend
