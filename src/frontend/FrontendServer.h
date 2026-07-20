#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QJsonValue>
#include <memory>

class QWebSocketServer;
class QWebSocket;
class QTimer;
class AudioEngine;

namespace frontend {

class FrontendTreeWatcher;

// WebSocket JSON-RPC 2.0 server for the HTML/Electron UI.
//
// Lifecycle: owned by the headless main(). The engine reference is set in
// the constructor; start() binds the socket and starts the push timers;
// stop() tears everything down. Must be constructed on the main thread
// (matches the project's single-thread rule: every dispatch runs on the
// main thread, just like mcp::McpServer).
class FrontendServer : public QObject {
    Q_OBJECT
public:
    explicit FrontendServer(AudioEngine& engine, QObject* parent = nullptr);
    ~FrontendServer() override;

    // Bind to the given port on the loopback interface. Returns false if the
    // port is already in use. Idempotent: a second start() after stop() works.
    bool start(quint16 port);

    // Stop listening, close all client sockets, stop the push timers.
    void stop();

    // The actual bound port (handy if start(0) was used to pick a free port).
    quint16 port() const;

    // Broadcast a server-initiated JSON-RPC notification to all connected
    // clients. Main-thread-only: iterates clients_ without a lock, which is
    // safe because every main-thread caller is serialized by Qt's event
    // loop. Non-main-thread callers must use broadcastNotificationFromAnyThread
    // instead.
    void broadcastNotification(const QString& method, const QJsonValue& params);

    // Thread-safe variant: hops to the main thread via a QueuedConnection
    // before calling broadcastNotification. Use this from worker threads
    // (e.g. export progress callbacks).
    void broadcastNotificationFromAnyThread(const QString& method, const QJsonValue& params);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString& text);
    void onBinaryMessageReceived(const QByteArray& data);
    void onMeterTimer();
    void onTransportTimer();

private:
    void handleOneMessage(QWebSocket* socket, const QByteArray& bytes);

    AudioEngine& engine_;
    QWebSocketServer* server_ = nullptr;
    QSet<QWebSocket*> clients_;

    // Live-data push timers (mirror the VUMeter / PlayheadCursor / timecode
    // timers in the Qt GUI). 30 Hz is the same cadence the GUI uses.
    QTimer* meterTimer_ = nullptr;
    QTimer* transportTimer_ = nullptr;

    // Server-side push: any ValueTree change re-broadcasts notify.treeChanged.
    std::unique_ptr<FrontendTreeWatcher> treeWatcher_;

    // Last-sent transport payload. The transport timer fires at 30 Hz even
    // when the project is idle; comparing the new snapshot against this and
    // skipping identical broadcasts saves WebSocket writes when paused.
    QJsonObject lastTransportPayload_;
};

} // namespace frontend
