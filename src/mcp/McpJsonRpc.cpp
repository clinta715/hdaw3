#include "McpJsonRpc.h"
#include <QJsonDocument>

namespace mcp {

McpResponse McpResponse::success(const QJsonValue& id, const QJsonValue& result) {
    McpResponse r; r.id = id; r.result = result; r.isError = false; return r;
}
McpResponse McpResponse::failure(const QJsonValue& id, int code, const QString& message,
                                  const QJsonValue& data) {
    McpResponse r; r.id = id; r.isError = true;
    QJsonObject err; err["code"] = code; err["message"] = message;
    if (!data.isUndefined() && !data.isNull()) err["data"] = data;
    r.error = err;
    return r;
}

static QJsonObject baseEnvelope() {
    QJsonObject o; o["jsonrpc"] = "2.0"; return o;
}

QString serializeResponse(const McpResponse& r) {
    QJsonObject o = baseEnvelope();
    o["id"] = r.id;
    if (r.isError) o["error"] = r.error; else o["result"] = r.result;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString serializeNotification(const McpNotification& n) {
    QJsonObject o = baseEnvelope();
    o["method"] = n.method; o["params"] = n.params;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonValue parseLine(const QByteArray& line, bool* ok) {
    *ok = false;
    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) return {};
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return {};
    *ok = true;
    return doc.object();
}

std::variant<McpRequest, McpResponse> validateRequest(const QJsonValue& v) {
    if (!v.isObject()) return McpResponse::failure({}, err::InvalidRequest, "expected JSON object");
    auto o = v.toObject();
    if (o.value("jsonrpc").toString() != "2.0")
        return McpResponse::failure({}, err::InvalidRequest, "jsonrpc must be \"2.0\"");
    if (!o.contains("method") || !o.value("method").isString())
        return McpResponse::failure({}, err::InvalidRequest, "method must be a string");
    McpRequest r;
    r.method = o.value("method").toString();
    r.params = o.value("params");
    if (o.contains("id")) r.id = o.value("id");
    return r;
}

} // namespace mcp
