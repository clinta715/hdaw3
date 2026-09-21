#pragma once

// Shared loader for the core-synth Device Parameter Map
// (timbre-lib/device_map/*.params.json, schema hdaw.device.param.map.v1).
//
// Lives in src/common so BOTH the MCP tool (mcp/McpTools_Device.cpp) and the
// frontend RPC surface (frontend/router/Router_Device.cpp) read the exact same
// artifact with the exact same filtering — RPC/MCP parity by construction.
//
// Engine surface ONLY: bounded file reads + JSON shaping. No DSP, no audio
// thread, no ValueTree, no plugin instantiation.
//
// Plan: docs/plans/2026-09-21-device-param-map.md

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace HDAW {

inline constexpr const char* kDeviceMapSchema   = "hdaw.device.param.map.v1";
inline constexpr const char* kDeviceIndexSchema = "hdaw.device.index.v1";

struct DeviceMapDir
{
    QString env;    // env value the resolution was cached for
    QString dir;    // resolved device_map directory (empty on failure)
    QString error;  // human-readable failure reason (set iff dir is empty)
};

// Resolve the device_map directory. Precedence:
//   1. env HDAW_DEVICE_MAP_DIR (authoritative — never silently falls back)
//   2. <cwd>/timbre-lib/device_map
//   3. <exeDir>/../timbre-lib/device_map
//   4. <exeDir>/timbre-lib/device_map
// Cached, keyed on the env value (so a test that changes the env re-resolves).
const DeviceMapDir& resolveDeviceMapDir();

// Size-bounded JSON object read. On failure returns {} and sets `err`.
QJsonObject readDeviceMapFile(const QString& path, QString& err);

// Engines with a <engine>.params.json in `dir` (sorted, deduped).
QStringList deviceMapEngines(const QString& dir);

// Engine-id guard shared by the MCP tool and the RPC surface.
bool validEngineId(const QString& engine);

// Filter a map's `params` array by the optional filters (empty = no filter).
// `matched` receives the FULL hit count before truncation; the returned array
// holds at most `limit` entries, each projected to the compact wire shape
// (name, category, tier, intents, stages, plus index/offset/trapReason/note
// only when non-null).
QJsonArray filterDeviceParams(const QJsonObject& map,
                              const QString& category, const QString& intent,
                              const QString& stage, const QString& tier,
                              int limit, int& matched);

} // namespace HDAW
