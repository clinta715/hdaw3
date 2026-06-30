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
    QString serverVersion() const { return "0.3.0"; }
    QString protocolVersion() const { return "2024-11-05"; }

public slots:
    void handleRequest(QJsonValue id, QString method, QJsonValue params);
    QJsonValue handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params);
    void setCancelFlag(bool cancel);

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
