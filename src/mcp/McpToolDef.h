#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <functional>

namespace mcp {

struct McpToolResult {
    QJsonArray content;
    bool isError = false;
    static McpToolResult text(const QString& t, bool isError = false) {
        McpToolResult r;
        r.isError = isError;
        r.content.append(QJsonObject{{"type", "text"}, {"text", t}});
        return r;
    }
};

struct McpToolDef {
    QString name;
    QString description;
    QJsonObject inputSchema;
    std::function<McpToolResult(const QJsonObject&)> handler;
};

} // namespace mcp
