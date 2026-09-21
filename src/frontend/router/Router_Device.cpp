// Router_Device.cpp — RPC surface for the core-synth Device Parameter Map.
//
// device.listParams mirrors the MCP tool list_device_params 1:1 (same artifact,
// same filters, same payload shape) by both calling into the shared
// HDAW:: DeviceParamMap loader. This is the RPC half of the project's
// "maintain RPC parity" rule (AGENTS.md).
//
// Omit `engine` for the index (engines + intent vocabulary + stage ownership).
// Engine surface ONLY: bounded file reads + JSON shaping. No DSP, no audio
// thread, no ValueTree, no plugin instantiation.
//
// Plan: docs/plans/2026-09-21-device-param-map.md

#include "Router_Device.h"
#include "RouterHelpers.h"

#include "../../common/DeviceParamMap.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchDevice(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    (void)engine;  // the device map is file-backed; no engine state is read

    if (m != "listParams")
        return makeError(-32601, "unknown device method: " + m);

    const auto o = paramsObject(params);
    const QString engineId = QString::fromStdString(optString(o, "engine", ""));

    const auto& dd = HDAW::resolveDeviceMapDir();
    if (dd.dir.isEmpty())
        return makeError(-32603, dd.error);

    // -- index mode: engines + intent vocabulary + stage ownership -----------
    if (engineId.isEmpty())
    {
        QString err;
        const QJsonObject root = HDAW::readDeviceMapFile(dd.dir + "/index.json", err);
        if (!err.isEmpty())
            return makeError(-32603, "device map index unavailable: " + err);
        const QString schema = root.value("schema").toString();
        if (schema != HDAW::kDeviceIndexSchema)
            return makeError(-32603, QString("index.json: unsupported schema '%1' (expected %2)")
                                         .arg(schema, HDAW::kDeviceIndexSchema));
        QJsonObject out = root;
        out["dir"] = dd.dir;
        return { false, out };
    }

    // -- engine mode: filtered params ---------------------------------------
    if (!HDAW::validEngineId(engineId))
        return makeError(-32602, "engine must match [a-z0-9_]"
                                 " (e.g. je8086, virus, nodalred2x, xenia, vavra)");

    QString err;
    const QJsonObject root = HDAW::readDeviceMapFile(dd.dir + "/" + engineId + ".params.json", err);
    if (!err.isEmpty())
        return makeError(-32602, QString("%1: %2 (available: %3)")
                                     .arg(engineId, err, HDAW::deviceMapEngines(dd.dir).join(", ")));
    const QString schema = root.value("schema").toString();
    if (schema != HDAW::kDeviceMapSchema)
        return makeError(-32603, QString("%1.params.json: unsupported schema '%2' (expected %3)")
                                     .arg(engineId, schema, HDAW::kDeviceMapSchema));

    const QString fCategory = QString::fromStdString(optString(o, "category", ""));
    const QString fIntent   = QString::fromStdString(optString(o, "intent", ""));
    const QString fStage    = QString::fromStdString(optString(o, "stage", ""));
    const QString fTier     = QString::fromStdString(optString(o, "tier", ""));
    int limit = optInt(o, "limit", 100, nullptr);
    if (limit <= 0) limit = 100;
    if (limit > 1000) limit = 1000;

    int matched = 0;
    const QJsonArray list = HDAW::filterDeviceParams(
        root, fCategory, fIntent, fStage, fTier, limit, matched);

    QJsonObject out {
        { "engine", engineId },
        { "dir", dd.dir },
        { "appliesVia", root.value("appliesVia") },
        { "durability", root.value("durability") },
        { "durabilityNote", root.value("durabilityNote") },
        { "counts", root.value("counts") },
        { "matched", matched },
        { "returned", list.size() },
        { "truncated", matched > list.size() },
        { "params", list } };
    if (matched == 0)
        out["hint"] = "no params matched these filters; omit filters (or use"
                      " the index mode) to see the engine's full map";
    return { false, out };
}

} // namespace frontend
