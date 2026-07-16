#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <atomic>
#include "McpToolDef.h"

class AudioEngine;

namespace mcp {
class Transport;
}

namespace mcp {
class McpServer : public QObject {
    Q_OBJECT
public:
    // Centralized dispatch result. One call to dispatchRequest covers all
    // entry points: the stdio/HTTP transports, the test thread helper, and
    // the GUI's queued request handler.
    struct DispatchResult {
        bool isNotification = false;   // true → no response, caller returns nothing
        bool isError = false;          // true → JSON-RPC error
        QJsonValue payload;            // on !isError: bare tool/initialize/ping result
                                       // on isError: {code, message} object
    };

    explicit McpServer(QObject* parent = nullptr);
    ~McpServer() override;

    void registerTool(McpToolDef def);
    const QHash<QString, McpToolDef>& tools() const { return tools_; }

    void setTransport(Transport* t);
    Transport* transport() const { return transport_; }
    void start();
    void stop();

    void setEngine(AudioEngine* e) { engine_ = e; }
    AudioEngine* engine() const { return engine_; }

    bool isCancelRequested() const { return cancelFlag_.load(std::memory_order_relaxed); }
    void resetCancelFlag() { cancelFlag_.store(false, std::memory_order_relaxed); }

    QString serverName()    const { return "hdaw"; }
    QString serverVersion() const { return "0.9.0"; }
    QString protocolVersion() const { return "2024-11-05"; }

    // Pure dispatch: routes (id, method, params) to the right handler and
    // returns a normalized result. Public so the HTTP transport can call
    // it directly (the QHttpServer handler runs on the main thread, which
    // is the same thread McpServer lives on — no queueing needed).
    DispatchResult dispatchRequest(const QJsonValue& id, const QString& method, const QJsonValue& params);

public slots:
    void handleRequest(QJsonValue id, QString method, QJsonValue params);
    QJsonValue handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params);
    void setCancelFlag(bool cancel);
    // Send a server-initiated JSON-RPC notification (e.g. notifications/progress)
    // to the transport. Safe to call from any thread: when invoked via
    // QMetaObject::invokeMethod with Qt::QueuedConnection, the actual transport
    // write happens on the main thread where the transport is safe to use.
    void notifyFromBackground(const QString& jsonLine);

private:
    QJsonValue handleInitialize(const QJsonValue& params);
    QJsonValue handleToolsList();
    QJsonValue handleToolsCall(const QJsonValue& params);
    QJsonValue handlePing();
    QJsonValue handleMethod(const QString& method, const QJsonValue& params, QJsonValue* outError);

    void sendResponse(const QJsonValue& id, const QJsonValue& result);
    void sendError(const QJsonValue& id, int code, const QString& message,
                   const QJsonValue& data = {});

    QHash<QString, McpToolDef> tools_;
    Transport* transport_ = nullptr;
    AudioEngine* engine_ = nullptr;
    std::atomic<bool> cancelFlag_{false};
};
}
