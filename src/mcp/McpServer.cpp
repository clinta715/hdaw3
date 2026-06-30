#include "McpServer.h"
#include "McpTransport.h"
#include "McpJsonRpc.h"
#include "McpSchema.h"
#include <QJsonArray>

namespace mcp {

McpServer::McpServer(QObject* parent) : QObject(parent) {}
McpServer::~McpServer() { stop(); }

void McpServer::registerTool(McpToolDef def) { tools_.insert(def.name, std::move(def)); }
void McpServer::setTransport(Transport* t) { transport_ = t; }
void McpServer::start() { if (transport_) transport_->start(this); }
void McpServer::stop()  { if (transport_) transport_->stop(); }
void McpServer::setCancelFlag(bool c) { cancelFlag_.store(c, std::memory_order_relaxed); }

void McpServer::handleRequest(QJsonValue id, QString method, QJsonValue params) {
    // Notifications (id is null) do not get a response per JSON-RPC 2.0.
    bool isNotification = id.isNull();
    QJsonValue err;
    QJsonValue result = handleMethod(method, params, &err);
    if (isNotification) return;
    if (err.isObject()) {
        int code = err.toObject().value("code").toInt(err::InternalError);
        sendError(id, code, err.toObject().value("message").toString());
    } else {
        sendResponse(id, result);
    }
}

QJsonValue McpServer::handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params) {
    // Notifications have no response — just dispatch and return an empty value.
    bool isNotification = id.isNull();
    QJsonValue err;
    QJsonValue result = handleMethod(method, params, &err);
    if (isNotification) return {};
    if (err.isObject()) {
        return QJsonObject{{"isError", true},
                           {"error", err.toObject()}};
    }
    return result;
}

QJsonValue McpServer::handleMethod(const QString& m, const QJsonValue& p, QJsonValue* out) {
    if (m == "initialize") return handleInitialize(p);
    if (m == "tools/list") return handleToolsList();
    if (m == "tools/call") return handleToolsCall(p);
    if (m == "ping")      return handlePing();
    if (m == "notifications/cancelled") {
        cancelFlag_.store(true, std::memory_order_relaxed);
        return {};
    }
    *out = QJsonObject{{"code", err::MethodNotFound},{"message","unknown method: " + m}};
    return {};
}

QJsonValue McpServer::handleInitialize(const QJsonValue&) {
    return QJsonObject{
        {"protocolVersion", protocolVersion()},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
        {"serverInfo", QJsonObject{{"name", serverName()},{"version", serverVersion()}}}
    };
}

QJsonValue McpServer::handleToolsList() {
    QJsonArray arr;
    for (const auto& t : tools_) arr.append(QJsonObject{
        {"name", t.name},{"description", t.description},{"inputSchema", t.inputSchema}});
    return QJsonObject{{"tools", arr}};
}

QJsonValue McpServer::handleToolsCall(const QJsonValue& params) {
    if (!params.isObject() || !params.toObject().contains("name"))
        return QJsonObject{{"isError", true},
            {"content", QJsonArray{QJsonObject{{"type","text"},
                {"text","tools/call requires {name, arguments?}"}}}}};
    auto p = params.toObject();
    QString name = p.value("name").toString();
    QJsonObject args = p.value("arguments").toObject();
    if (!tools_.contains(name))
        return QJsonObject{{"isError", true},
            {"content", QJsonArray{QJsonObject{{"type","text"},
                {"text","unknown tool: " + name}}}}};
    const auto& t = tools_.value(name);
    auto verr = validateSchema(args, t.inputSchema);
    if (verr) return QJsonObject{{"isError", true},
        {"content", QJsonArray{QJsonObject{{"type","text"},
            {"text","invalid params: " + verr->toString()}}}}};
    McpToolResult r;
    try { r = t.handler(args); }
    catch (const std::exception& ex) { r = McpToolResult::text(QString("handler exception: ") + ex.what(), true); }
    catch (...) { r = McpToolResult::text("handler threw unknown exception", true); }
    return QJsonObject{{"isError", r.isError},{"content", r.content}};
}

QJsonValue McpServer::handlePing() { return QJsonObject{}; }

void McpServer::sendResponse(const QJsonValue& id, const QJsonValue& result) {
    if (!transport_) return;
    transport_->send(serializeResponse(McpResponse::success(id, result)).toUtf8());
}

void McpServer::sendError(const QJsonValue& id, int code, const QString& message, const QJsonValue& data) {
    if (!transport_) return;
    transport_->send(serializeResponse(McpResponse::failure(id, code, message, data)).toUtf8());
}

} // namespace mcp
