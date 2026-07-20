#include "UiHttpServer.h"

#include <QHttpServer>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace frontend {

UiHttpServer::UiHttpServer(quint16 wsPort)
    : wsPort_(wsPort)
{
}

UiHttpServer::~UiHttpServer() { stop(); }

bool UiHttpServer::start(quint16 port) {
    if (tcp_ && tcp_->isListening())
        return true; // already listening

    // Create a fresh QHttpServer so no dangling pointer survives from a prior stop().
    server_ = std::make_unique<QHttpServer>();

    // GET / — serve index.html with WS port injection.
    server_->route("/", QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest&) -> QHttpServerResponse {
            QFile f(":/ui/index.html");
            if (!f.open(QIODevice::ReadOnly)) {
                QByteArray errorHtml = R"(
<!DOCTYPE html>
<html><head><title>HDAW</title></head>
<body style="background:#141416;color:#ccc;font-family:sans-serif;padding:40px">
<h1>HDAW Frontend Not Built</h1>
<p>The HTML frontend has not been compiled into this executable.</p>
<p>Run <code>cd frontend &amp;&amp; npm run build</code>, then rebuild HDAW.</p>
</body></html>
                )";
                return QHttpServerResponse("text/html", errorHtml);
            }

            QString html = QString::fromUtf8(f.readAll());

            // Inject the WebSocket port before </head>.
            html.replace("</head>",
                QString("<script>window.__HDAW_WS_PORT__ = %1;</script></head>")
                    .arg(wsPort_));

            return QHttpServerResponse("text/html", html.toUtf8());
        });

    // GET /assets/<filename> — serve static assets from Qt resources.
    server_->route("/assets/<arg>", QHttpServerRequest::Method::Get,
        [](const QUrl& url) -> QHttpServerResponse {
            const QString filename = QFileInfo(url.path()).fileName();
            const QString resourcePath = ":/ui/assets/" + filename;

            QFile f(resourcePath);
            if (!f.open(QIODevice::ReadOnly))
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);

            QMimeDatabase db;
            QMimeType mime = db.mimeTypeForFile(filename);
            return QHttpServerResponse(mime.name().toLatin1(), f.readAll());
        });

    tcp_ = std::make_unique<QTcpServer>();
    if (!tcp_->listen(QHostAddress::LocalHost, port)) {
        tcp_.reset();
        server_.reset();
        return false;
    }
    if (!server_->bind(tcp_.get())) {
        tcp_->close();
        tcp_.reset();
        server_.reset();
        return false;
    }
    return true;
}

void UiHttpServer::stop() {
    server_.reset();   // drop QHttpServer first — it holds a raw ptr to tcp_
    if (tcp_) {
        tcp_->close();
        tcp_.reset();
    }
}

quint16 UiHttpServer::port() const {
    return tcp_ ? tcp_->serverPort() : 0;
}

} // namespace frontend
