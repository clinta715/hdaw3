#include "Router_Plugin.h"
#include "RouterHelpers.h"

#include "../FrontendServer.h"

#include "../../common/PluginService.h"
#include "../../common/PluginParamService.h"
#include "../../common/SettingsKeys.h"
#include "../../common/NordBankLoader.h"
#include "../../common/PresetApply.h"   // loadPresetFile: the shared setStateInformation loader
#include "../../engine/AudioEngine.h"
#include "../../model/ProjectModel.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QString>

#include <string>
#include <thread>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchPlugin(PluginService& s, AudioEngine& engine, const QString& m,
                              const QJsonValue& params,
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
    if (m == "loadNordBank") {
        // Clavia Nord bank into a plugin slot — the RPC twin of the MCP `load_nord_bank` tool,
        // sharing HDAW::loadNordBankFile (src/common/NordBankLoader.cpp) with it so what gets
        // queued cannot drift (retrofit backlog item 7: it was the last preset-loading tool with
        // no RPC route). Validation happens before anything is queued, and the error CLASS comes
        // from the loader: -32603 environment/artifact, -32602 invalid params.
        // Parameter names mirror the MCP tool EXACTLY (trackId, NOT trackIndex): a surface that
        // renames an argument is not a parity twin. The first draft of this route read
        // `trackIndex`, and the twin test caught it (the MCP rejected the arg as an unknown
        // property while the RPC reported a loader error) — keep the schema and the check in sync.
        int ti = 0, si = 0;
        std::string filePath;
        if (!requireInt(o, "trackId", ti, nullptr))
            return makeError(-32602, "trackId required");
        if (!requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "slotIndex required");
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");
        const int program = optInt(o, "program", -1, nullptr);
        if (o.contains("program") && program < 0)
            return makeError(-32602, "program must be 0..127");
        const bool capture = optBool(o, "captureToTree", true, nullptr);
        const auto r = HDAW::loadNordBankFile(engine, ti, si, QString::fromStdString(filePath),
                                              program, capture);
        if (!r.ok)
            return makeError(r.environmentFailure ? -32603 : -32602, r.error);
        return { false, QJsonObject{ { "queued", r.queued }, { "bytes", r.totalBytes },
                                     { "program", r.program },
                                     { "capturedToTree", r.capturedToTree } } };
    }
    if (m == "getIsolationEnabled") { QSettings qs; return { false, qs.value(SettingsKeys::kKeyPluginIsolation, true).toBool() }; }
    if (m == "setIsolationEnabled") { bool v; if (!requireBool(o, "value", v, nullptr)) return makeError(-32602, "value required"); QSettings qs; qs.setValue(SettingsKeys::kKeyPluginIsolation, v); return { false, QJsonValue::Null }; }
    if (m == "getWatchPlugins") { QSettings qs; return { false, qs.value(SettingsKeys::kKeyWatchPlugins, true).toBool() }; }
    if (m == "setWatchPlugins") { bool v; if (!requireBool(o, "value", v, nullptr)) return makeError(-32602, "value required"); QSettings qs; qs.setValue(SettingsKeys::kKeyWatchPlugins, v); return { false, QJsonValue::Null }; }
    if (m == "getCustomScanDirs") {
        QJsonArray arr;
        for (const auto& d : s.getCustomScanDirs())
            arr.append(QString::fromStdString(d));
        return { false, arr };
    }
    if (m == "addCustomScanDir") {
        std::string dir;
        if (!requireString(o, "dir", dir, nullptr))
            return makeError(-32602, "dir required");
        s.addCustomScanDir(dir);
        return { false, QJsonValue::Null };
    }
    if (m == "removeCustomScanDir") {
        std::string dir;
        if (!requireString(o, "dir", dir, nullptr))
            return makeError(-32602, "dir required");
        s.removeCustomScanDir(dir);
        return { false, QJsonValue::Null };
    }
    if (m == "searchPresets") {
        std::string query;
        if (!requireString(o, "query", query, nullptr))
            return makeError(-32602, "query required");
        int limit = 50;
        requireInt(o, "limit", limit, nullptr);
        auto results = s.searchPresets(query, limit);
        QJsonArray arr;
        for (const auto& r : results) {
            arr.append(QJsonObject{
                {"pluginId", QString::fromStdString(r.pluginId)},
                {"pluginName", QString::fromStdString(r.pluginName)},
                {"presetIndex", r.presetIndex},
                {"presetName", QString::fromStdString(r.presetName)}
            });
        }
        return {false, arr};
    }
    if (m == "loadPresetFile") {
        // MCP twin of load_plugin_preset_file (McpTools_FxPreset.cpp): load a
        // preset file into a plugin FX slot via setStateInformation
        // (.SerumPreset / .fxp / .syx). Runs the SAME shared loader the tool
        // runs (HDAW::loadPluginPresetFileToolText, src/common/PresetApply.h)
        // -> parsePresetFile + setStateInformation + the pluginState tree
        // capture, so the surfaces cannot drift. Same key names as the tool.
        //
        // Thread note (Gate 16): the frontend router executes on the host
        // message thread (the same thread the MCP server serves requests on),
        // which is the lifecycle call's sanctioned context for in-process
        // instances — identical to the pre-existing audio.swapFxSnapshot /
        // audio.captureFxSnapshot setStateInformation routes.
        int ti, si;
        if (!requireInt(o, "trackId", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr))
            return makeError(-32602, "trackId and slotIndex required");
        std::string filePath;
        if (!requireString(o, "filePath", filePath, nullptr))
            return makeError(-32602, "filePath required");

        bool ok = false;
        const QString text = HDAW::loadPluginPresetFileToolText(
            engine, ti, si, QString::fromStdString(filePath), &ok);
        if (!ok)
            return makeError(-32602, text);
        return { false, text };
    }
    return makeError(-32601, "unknown plugin method: " + m);
}

namespace {
// Live slot index for `pluginID` on `trackIndex` (the FX chain's plugin slots
// are addressed by id on the pluginParam surface). -1 when the slot is not in
// the ReadModel projection.
int findPluginSlotIndex(AudioEngine& engine, int trackIndex, const std::string& pluginId) {
    const auto fxSlots = engine.getReadModel().getFxSlots(trackIndex);
    for (int si = 0; si < static_cast<int>(fxSlots.size()); ++si)
        if (fxSlots[si].pluginId == pluginId) return si;
    return -1;
}
} // namespace

DispatchResult dispatchPluginParam(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    auto& s = engine.getPluginParamService();
    const auto o = paramsObject(params);
    if (m == "getParams") {
        int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required");
        // Persisted offline-replay overrides for this slot: an index present in
        // the ledger will also be replayed into every tree-copy render
        // (export_audio / audition_plugin / verify_part). Lets a client — and an
        // agent — tell live-only state from state that survives a render.
        const int slotIndex = findPluginSlotIndex(engine, i, id);
        std::vector<std::pair<int, float>> overrides;
        if (slotIndex >= 0)
            overrides = engine.getProjectCommands().getPluginParamOverrides(i, slotIndex);
        QJsonArray arr;
        for (const auto& p : s.getParams(i, id)) {
            bool overridden = false;
            for (const auto& ov : overrides)
                if (ov.first == p.index) { overridden = true; break; }
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
                { "overridden",  overridden },
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
        // Route through the shared command layer (same path as MCP
        // set_fx_param) so the write is ALSO persisted into the slot's
        // offline-replay ledger: a bare PluginParamService::setParam reaches
        // the live child only, and no tree-copy render (export_audio /
        // audition_plugin / verify_part) or save/load would see it.
        // See docs/plans/2026-09-21-vavra-live-param-delivery.md.
        const int slotIndex = findPluginSlotIndex(engine, i, id);
        if (slotIndex >= 0) {
            const int overrides = engine.getProjectCommands().setPluginParam(i, slotIndex, pi, v);
            return { false, QJsonObject{ { "overrides", overrides } } };
        }
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
