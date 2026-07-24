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
#include <QFileSystemWatcher>
#include <QCoreApplication>
#include <cmath>
#include <cstdlib>

namespace frontend {

namespace {
constexpr quint16 kDefaultPort = 8766;   // MCP HTTP is 8765; keep them distinct.
constexpr int kPushIntervalMs = 33;      // ~30 Hz, matches the Qt GUI cadence.
} // namespace

FrontendServer::FrontendServer(AudioEngine& engine, QObject* parent)
    : QObject(parent), engine_(engine)
{
    instance_ = this;
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

    // Plugin directory watcher — auto-rescan when VST3/CLAP dirs change.
    pluginDirWatcher_ = new QFileSystemWatcher(this);
    connect(pluginDirWatcher_, &QFileSystemWatcher::directoryChanged,
            this, &FrontendServer::onPluginDirChanged);

    pluginDirDebounceTimer_ = new QTimer(this);
    pluginDirDebounceTimer_->setSingleShot(true);
    pluginDirDebounceTimer_->setInterval(800);
    connect(pluginDirDebounceTimer_, &QTimer::timeout,
            this, &FrontendServer::onPluginDirDebounceExpired);
}

FrontendServer::~FrontendServer()
{
    stop();
    if (instance_ == this) instance_ = nullptr;
}

bool FrontendServer::start(quint16 port) {
    if (port == 0) port = kDefaultPort;
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        // listen() fails if already listening or port in use.
        if (!server_->isListening())
            return false;
    }
    meterTimer_->start();
    transportTimer_->start();

    // Watch VST3/CLAP plugin directories for changes — auto-rescan when a
    // plugin is added or removed. The watcher fires on any touch to the
    // directory (Windows Defender scans, Search Indexer, plugin host file
    // locks), so onPluginDirDebounceExpired() compares the current plugin-
    // file count against lastPluginFileCount_ and only rescans when they
    // differ. In-place plugin updates don't change the count; users should
    // hit "Rescan" in the Plugin Manager for that case. Set
    // HDAW_WATCH_PLUGINS=0 to disable entirely.
    const bool watchPlugins = []() {
        const auto* v = std::getenv("HDAW_WATCH_PLUGINS");
        if (v == nullptr) return true;              // default: on
        return v[0] != '0' && v[0] != 'f' && v[0] != 'F';
    }();
    if (watchPlugins)
    {
        juce::StringArray watchedDirs;
        for (const auto& dir : engine_.getPluginManager().getVst3Dirs())
            watchedDirs.add(dir);
        for (const auto& dir : engine_.getPluginManager().getClapDirs())
            watchedDirs.add(dir);

        auto addDirWatcher = [&](const juce::String& dirPath) {
            auto dir = juce::File(dirPath);
            if (dir.isDirectory())
                pluginDirWatcher_->addPath(QString::fromUtf8(dirPath.toRawUTF8()));
        };
        for (const auto& dir : watchedDirs)
            addDirWatcher(dir);

        // Seed the baseline count so the first directory event after startup
        // (which is almost always a Defender/indexer touch, not a real plugin
        // change) doesn't trigger an immediate rescan.
        lastPluginFileCount_ = engine_.getPluginManager()
            .findPluginFiles(watchedDirs).size();
    }
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

    auto result = frontend::dispatch(engine_, req.method, req.params, this);

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

void FrontendServer::broadcastNotificationFromAnyThread(const QString& method, const QJsonValue& params) {
    // Marshal to the main thread before touching clients_. This is the
    // documented entry point for non-main-thread callers (e.g. export
    // worker progress callbacks); broadcastNotification above stays
    // main-thread-only so the hot 30 Hz timer paths stay lock-free.
    QMetaObject::invokeMethod(this, [this, method, params]() {
        broadcastNotification(method, params);
    }, Qt::QueuedConnection);
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
    QJsonObject payload = toJson(engine_.getReadModel().getTransport());
    // Skip the broadcast if nothing meaningful has changed since the last
    // push. The 30 Hz timer keeps firing in the idle case (paused, not
    // recording, no loop toggle); without this guard every connected client
    // receives ~30 identical TransportSnapshots per second forever.
    //
    // currentTimeSeconds is the only field that advances during playback;
    // quantize it to the centisecond so sub-frame jitter doesn't defeat
    // the comparison. The other fields are booleans/doubles that only
    // change on explicit user/engine actions.
    auto quantized = [](const QJsonObject& o) -> QJsonObject {
        QJsonObject q = o;
        if (q.contains("currentTimeSeconds")) {
            double t = q.value("currentTimeSeconds").toDouble();
            q["currentTimeSeconds"] = std::round(t * 100.0) / 100.0;
        }
        return q;
    };
    if (quantized(payload) == quantized(lastTransportPayload_))
        return;
    lastTransportPayload_ = payload;
    broadcastNotification(notify::Transport, payload);
}

void FrontendServer::onPluginDirChanged() {
    // Debounce rapid directory events (e.g. a bulk copy of plugin files).
    // Always restart the timer; the actual scan only runs when it fires.
    pluginDirDebounceTimer_->start();
}

void FrontendServer::onPluginDirDebounceExpired() {
    if (pluginScanInProgress_)
        return;

    // The directory watcher fires on every touch to the watched dirs
    // (Defender, indexer, plugin hosts). Only rescan when the VST3/CLAP
    // file count actually changed since the last baseline — this skips
    // the frequent false-positive events without missing real installs.
    juce::StringArray watchedDirs;
    for (const auto& dir : engine_.getPluginManager().getVst3Dirs())
        watchedDirs.add(dir);
    for (const auto& dir : engine_.getPluginManager().getClapDirs())
        watchedDirs.add(dir);
    const int currentCount = engine_.getPluginManager()
        .findPluginFiles(watchedDirs).size();
    if (currentCount == lastPluginFileCount_)
        return;
    lastPluginFileCount_ = currentCount;

    pluginScanInProgress_ = true;

    // Run the scan on a background thread so the main thread stays responsive.
    // Broadcast progress for each file, then broadcast completion.
    std::thread scanThread([this]() {
        engine_.getPluginService().scanAll([this](const std::string& fileName, int completed, int total) {
            QJsonObject payload{
                { "fileName", QString::fromStdString(fileName) },
                { "completed", completed },
                { "total", total },
            };
            broadcastNotificationFromAnyThread(notify::ScanProgress, payload);
        });
        broadcastNotificationFromAnyThread(notify::ScanProgress,
            QJsonObject{ { "fileName", "" }, { "completed", -1 }, { "total", -1 },
                         { "done", true } });
        pluginScanInProgress_ = false;
    });
    scanThread.detach();
}

} // namespace frontend
