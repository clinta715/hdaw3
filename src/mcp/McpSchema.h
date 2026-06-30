#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>

namespace mcp {

struct SchemaError {
    QString path;
    QString message;
    QString toString() const { return path.isEmpty() ? message : (path + ": " + message); }
};

std::optional<SchemaError> validateSchema(const QJsonValue& value, const QJsonObject& schema);

} // namespace mcp
