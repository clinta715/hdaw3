#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <variant>
#include <optional>

namespace mcp {

struct McpRequest {
    QJsonValue id;
    QString     method;
    QJsonValue  params;
    bool isNotification() const { return id.isNull() && !method.isEmpty(); }
};

struct McpResponse {
    QJsonValue id;
    QJsonValue result;
    QJsonValue error;
    bool isError = false;
    static McpResponse success(const QJsonValue& id, const QJsonValue& result);
    static McpResponse failure(const QJsonValue& id, int code, const QString& message,
                               const QJsonValue& data = {});
};

struct McpNotification { QString method; QJsonValue params; };

namespace err {
    constexpr int ParseError     = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams  = -32602;
    constexpr int InternalError  = -32603;
}

QString serializeResponse(const McpResponse& r);
QString serializeNotification(const McpNotification& n);
std::optional<QJsonValue> parseLine(const QByteArray& line);
std::variant<McpRequest, McpResponse> validateRequest(const QJsonValue& v);

} // namespace mcp
