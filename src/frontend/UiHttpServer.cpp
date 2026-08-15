#include "UiHttpServer.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QHostAddress>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

#include <random>
#include <iomanip>
#include <optional>
#include <sstream>

namespace frontend {

static std::optional<QHttpServerResponse> checkAuth(
    const QString& authToken, const QHttpServerRequest& req)
{
    if (authToken.isEmpty())
        return std::nullopt;
    const QByteArray authHeader = req.value("Authorization");
    if (authHeader == QByteArray("Bearer ") + authToken.toUtf8())
        return std::nullopt;
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
}

UiHttpServer::UiHttpServer(quint16 wsPort)
    : wsPort_(wsPort)
{
    // Generate a random 32-char hex token for HTTP auth.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 15);
    std::ostringstream oss;
    for (int i = 0; i < 32; ++i)
        oss << std::hex << std::setw(1) << dist(gen);
    authToken_ = QString::fromStdString(oss.str());

    // HDAW_AUTH_TOKEN env override: set to use a specific token, set to
    // empty to disable auth entirely.
    if (qEnvironmentVariableIsSet("HDAW_AUTH_TOKEN"))
        authToken_ = qEnvironmentVariable("HDAW_AUTH_TOKEN");
}

UiHttpServer::~UiHttpServer() { stop(); }

bool UiHttpServer::start(quint16 port, const QHostAddress& bindAddress) {
    if (tcp_ && tcp_->isListening())
        return true; // already listening

    // Create a fresh QHttpServer so no dangling pointer survives from a prior stop().
    server_ = std::make_unique<QHttpServer>();

    // GET / — serve index.html with WS port and auth token injection.
    server_->route("/", QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) -> QHttpServerResponse {
            if (auto deny = checkAuth(authToken_, req)) return std::move(*deny);

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

            html.replace("</head>",
                QString("<script>window.__HDAW_WS_PORT__ = %1; window.__HDAW_AUTH_TOKEN__ = '%2';</script></head>")
                    .arg(wsPort_)
                    .arg(authToken_));

            return QHttpServerResponse("text/html", html.toUtf8());
        });

    // GET /assets/<filename> — serve static assets from Qt resources.
    server_->route("/assets/<arg>", QHttpServerRequest::Method::Get,
        [this](const QUrl& url,
               const QHttpServerRequest& req) -> QHttpServerResponse {
            if (auto deny = checkAuth(authToken_, req)) return std::move(*deny);

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
    if (!tcp_->listen(bindAddress, port)) {
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

QString UiHttpServer::authToken() const { return authToken_; }

} // namespace frontend
