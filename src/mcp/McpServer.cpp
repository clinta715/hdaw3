#include "McpServer.h"
#include "McpTransport.h"
#include "McpJsonRpc.h"
#include "McpSchema.h"
#include "common/ToolUnits.h"
#include "common/WindowUnitArgs.h"
#include "../engine/AudioEngine.h"
#include <QJsonArray>

namespace mcp {

McpServer::McpServer(QObject* parent) : QObject(parent) {}
McpServer::~McpServer() { stop(); }

void McpServer::registerTool(McpToolDef def) {
    // Central unit annotation (docs/plans/2026-09-28-agent-mechanization.md, S1):
    // the SAME enriched schema is stored, so tools/list AND tools/call
    // validation (validateSchema reads t.inputSchema) agree by construction.
    // S6: the unit-explicit window spellings (startSec/startBeat/unit/…) are
    // declared here from the ONE spec table (common/WindowUnitArgs.h), so the
    // validator accepts exactly the keys the shared resolver understands.
    def.inputSchema = HDAW::injectWindowUnitProperties(def.name, def.inputSchema);
    def.inputSchema = toolunits::enrichSchemaUnits(def.name, def.inputSchema);
    // ITEM B: the per-tool shape example is the STANDARD JSON Schema `examples`
    // annotation (an ARRAY) inside inputSchema — not an ad-hoc top-level field a
    // strict MCP client may strip. ONE place: tools/list and tool_help both read
    // this stored schema. The convention for a ZERO-ARG tool (no properties) is
    // [{}] — always exactly one example object, `{}` when it takes no arguments.
    def.inputSchema.insert("examples",
                           QJsonArray{ toolunits::buildToolExample(def.inputSchema) });
    tools_.insert(def.name, std::move(def));
}
void McpServer::setTransport(Transport* t) { transport_ = t; }
void McpServer::start() { if (transport_) transport_->start(this); }
void McpServer::stop()  { if (transport_) { transport_->stop(); transport_ = nullptr; } }
void McpServer::setCancelFlag(bool c) { cancelFlag_.store(c, std::memory_order_relaxed); }

void McpServer::notifyFromBackground(const QString& jsonLine) {
    if (transport_ == nullptr || jsonLine.isEmpty()) return;
    transport_->notify(jsonLine.toUtf8());
}

void McpServer::handleRequest(QJsonValue id, QString method, QJsonValue params) {
    auto r = dispatchRequest(id, method, params);
    if (r.isNotification) return;
    if (r.isError) {
        int code = r.payload.toObject().value("code").toInt(err::InternalError);
        sendError(id, code, r.payload.toObject().value("message").toString());
    } else {
        sendResponse(id, r.payload);
    }
}

QJsonValue McpServer::handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params) {
    auto r = dispatchRequest(id, method, params);
    if (r.isNotification) return {};
    if (r.isError) {
        return QJsonObject{{"isError", true},
                           {"error", r.payload.toObject()}};
    }
    // Preserve the bare-result contract that the existing tool-registry
    // and dry-run tests depend on (they read .toObject().value("isError")
    // and .value("content") directly off the return value).
    return r.payload;
}

McpServer::DispatchResult McpServer::dispatchRequest(const QJsonValue& id, const QString& method, const QJsonValue& params) {
    DispatchResult r;
    bool isNotification = id.isNull();
    r.isNotification = isNotification;
    if (isNotification) {
        // For notifications, still dispatch side-effects (e.g.
        // notifications/cancelled sets the cancel flag) but produce no
        // response. The error out param is intentionally ignored.
        QJsonValue ignoredErr;
        handleMethod(method, params, &ignoredErr);
        return r;
    }
    QJsonValue err;
    auto result = handleMethod(method, params, &err);
    if (err.isObject()) {
        r.isError = true;
        r.payload = err;
        return r;
    }
    // result is either a {isError, content} object (from handleToolsCall
    // tool failures/successes) or a plain object (initialize/tools.list/ping).
    r.payload = result;
    return r;
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
        {"name", t.name},{"description", t.description},{"inputSchema", t.inputSchema},{"category", t.category}});
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

    // S6 (docs/plans/2026-09-28-agent-mechanization.md §4): unit-explicit time
    // windows. ONE shared resolver (common/WindowUnitArgs.h) — the same function
    // the JSON-RPC dispatch runs, so a conversion and a refusal are
    // byte-identical on both surfaces. It runs BEFORE schema validation so an
    // accepted ALIAS spelling (`startBeat`/`startSec`) also satisfies a schema
    // that still REQUIRES the canonical key (the resolver writes the resolved
    // value back into the key the handler reads). `args` is a local copy.
    QString unitUsed, windowErr;
    const bool speaksWindow = HDAW::toolSpeaksWindow(name);
    if (speaksWindow)
    {
        const double bpm = engine_ ? engine_->getTransportManager().getBPM() : 120.0;
        if (!HDAW::normalizeWindowArgsForTool(name, args, bpm, unitUsed, windowErr))
            return QJsonObject{{"isError", true},
                {"content", QJsonArray{QJsonObject{{"type","text"},{"text", windowErr}}}}};
    }

    auto verr = validateSchema(args, t.inputSchema);
    if (verr) return QJsonObject{{"isError", true},
        {"content", QJsonArray{QJsonObject{{"type","text"},
            {"text","invalid params: " + verr->toString()}}}}};

    McpToolResult r;
    try { r = t.handler(args); }
    catch (const std::exception& ex) { r = McpToolResult::text(QString("handler exception: ") + ex.what(), true); }
    catch (...) { r = McpToolResult::text("handler threw unknown exception", true); }

    // The response echoes the unit actually used.
    if (!r.isError && speaksWindow && !unitUsed.isEmpty() && HDAW::toolEchoesUnit(name))
    {
        const bool allowText = HDAW::toolEchoesUnitText(name);
        QJsonArray content = r.content;
        for (int i = 0; i < content.size(); ++i)
        {
            QJsonObject c = content[i].toObject();
            if (c.value("type").toString() == QLatin1String("text"))
            {
                c["text"] = HDAW::withUnitEchoText(c.value("text").toString(), unitUsed, allowText);
                content[i] = c;
            }
        }
        r.content = content;
    }
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
