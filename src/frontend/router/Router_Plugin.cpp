#include "Router_Plugin.h"
#include "RouterHelpers.h"

#include "../FrontendServer.h"

#include "../../common/PluginService.h"
#include "../../common/PluginParamService.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <thread>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchPlugin(PluginService& s, const QString& m, const QJsonValue& params,
                              FrontendServer* server) {
    const auto o = paramsObject(params);
    auto pluginInfoToJson = [](const PluginInfo& p) {
        return QJsonObject{
            { "name",            QString::fromStdString(p.name) },
            { "format",          QString::fromStdString(p.format) },
            { "manufacturer",    QString::fromStdString(p.manufacturer) },
            { "fileOrIdentifier", QString::fromStdString(p.fileOrIdentifier) },
            { "isInstrument",    p.isInstrument },
        };
    };
    if (m == "scanAll") {
        if (s.isLoading())
            return makeError(-32603, "scan already in progress");

        // Run the scan on a background thread so the RPC doesn't block the
        // engine main thread. Broadcast notify.scanProgress for each plugin
        // file, then broadcast a completion notification when done.
        // Spin a local QEventLoop so queued cross-thread invocations
        // (the broadcastNotificationFromAnyThread hops) are processed.
        QEventLoop loop;
        bool scanDone = false;
        std::thread scanThread([&]() {
            s.scanAll([&](const std::string& fileName, int completed, int total) {
                if (server == nullptr) return;
                QJsonObject payload{
                    { "fileName", QString::fromStdString(fileName) },
                    { "completed", completed },
                    { "total", total },
                };
                server->broadcastNotificationFromAnyThread(notify::ScanProgress, payload);
            });
            scanDone = true;
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
        scanThread.detach();

        loop.exec();

        // Broadcast completion
        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ScanProgress,
                QJsonObject{ { "fileName", "" }, { "completed", -1 }, { "total", -1 },
                             { "done", true } });
        }
        return { false, QJsonValue::Null };
    }
    if (m == "isLoading") { return { false, s.isLoading() }; }
    if (m == "getPlugins") {
        QJsonArray arr; for (const auto& p : s.getPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "getInstrumentPlugins") {
        QJsonArray arr; for (const auto& p : s.getInstrumentPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "getEffectPlugins") {
        QJsonArray arr; for (const auto& p : s.getEffectPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "isBlacklisted")      { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); return { false, s.isBlacklisted(id) }; }
    if (m == "blacklistPlugin")    { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); s.blacklistPlugin(id); return { false, QJsonValue::Null }; }
    if (m == "unblacklistPlugin")  { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); s.unblacklistPlugin(id); return { false, QJsonValue::Null }; }
    if (m == "getBlacklistReason") { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); return { false, QString::fromStdString(s.getBlacklistReason(id)) }; }
    return makeError(-32601, "unknown plugin method: " + m);
}

DispatchResult dispatchPluginParam(PluginParamService& s, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "getParams") {
        int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required");
        QJsonArray arr;
        for (const auto& p : s.getParams(i, id)) {
            arr.append(QJsonObject{
                // Field name is `paramIndex` (not `index`) for consistency
                // with the write side (pluginParam.setParam's paramIndex),
                // the AutomatableParamSnapshot shape, and the frontend's
                // ParamInfo/TS interface.
                { "paramIndex",  p.index },
                { "name",        QString::fromStdString(p.name) },
                { "value",       static_cast<double>(p.value) },
                { "text",        QString::fromStdString(p.text) },
                { "label",       QString::fromStdString(p.label) },
                { "automatable", p.automatable },
            });
        }
        return { false, arr };
    }
    if (m == "getParamText") {
        int i, pi; std::string id; float v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "normalizedValue", v, nullptr))
            return makeError(-32602, "trackIndex, pluginID, paramIndex, normalizedValue required");
        return { false, QString::fromStdString(s.getParamText(i, id, pi, v)) };
    }
    if (m == "setParam") {
        int i, pi; std::string id; float v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "normalizedValue", v, nullptr))
            return makeError(-32602, "trackIndex, pluginID, paramIndex, normalizedValue required");
        s.setParam(i, id, pi, v); return { false, QJsonValue::Null };
    }
    if (m == "getProgramCount")  { int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required"); return { false, s.getProgramCount(i, id) }; }
    if (m == "getCurrentProgram"){ int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required"); return { false, s.getCurrentProgram(i, id) }; }
    if (m == "getProgramName")   { int i, pi; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "programIndex", pi, nullptr)) return makeError(-32602, "trackIndex, pluginID, programIndex required"); return { false, QString::fromStdString(s.getProgramName(i, id, pi)) }; }
    if (m == "setCurrentProgram"){ int i, pi; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "programIndex", pi, nullptr)) return makeError(-32602, "trackIndex, pluginID, programIndex required"); s.setCurrentProgram(i, id, pi); return { false, QJsonValue::Null }; }
    if (m == "listPrograms") {
        int i; std::string id;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr))
            return makeError(-32602, "trackIndex and pluginID required");
        int count = s.getProgramCount(i, id);
        int current = s.getCurrentProgram(i, id);
        QJsonArray arr;
        for (int pi = 0; pi < count; ++pi)
            arr.append(QJsonObject{{"index", pi}, {"name", QString::fromStdString(s.getProgramName(i, id, pi))}, {"current", pi == current}});
        return { false, arr };
    }
    return makeError(-32601, "unknown pluginParam method: " + m);
}

} // namespace frontend
