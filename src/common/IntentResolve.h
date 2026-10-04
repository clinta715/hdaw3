#pragma once
// INTENT-BASED PARAM ADDRESSING for internal FX slots (slice C) — the ONE
// resolver BOTH surfaces call, so the MCP tools (set_internal_fx_param /
// set_fx_param) and the JSON-RPC route (project.setFxSlotParam) resolve, succeed
// and REFUSE with byte-identical text by construction (AGENTS.md parity rule:
// "where both surfaces shape the same artifact, put the logic in src/common/").
//
// The vocabulary is the EXISTING core-synth Device Parameter Map
// (timbre-lib/device_map/<engine>.params.json, schema hdaw.device.param.map.v1,
// engine id == the internal FX fxType) — the same artifact list_device_params /
// device.listParams serves. This header READS it; no second vocabulary is
// invented and the corpus is never rewritten. Dir resolution goes through the
// shared loader (common/DeviceParamMap.h), so HDAW_DEVICE_MAP_DIR and the
// cwd/exe-relative precedence behave exactly like the device tool.
//
// Resolver contract (deliberately conservative — never guess):
//   * exactly ONE param declares the intent -> ok, with its `index` + `name`;
//   * NO param declares it                  -> error naming the engine and the
//     intent plus the engine's available intent ids (bounded, first 20);
//   * MORE THAN ONE param declares it       -> error `ambiguous intent ...` with
//     the (index, name) candidate pairs — REFUSE, never pick one;
//   * a single match with no `index` (an offset-only map entry) -> error: there
//     is nothing to address, so the caller must pass paramIndex;
//   * dir unresolvable / engine has no map (or a foreign schema) -> error
//     carrying the loader's own text.
//
// Matching is the loader's own convention (DeviceParamMap.cpp matchesAny):
// case-insensitive on the WHOLE intent id — never a substring — so `intent:
// "filter-sweep"` matches exactly that vocabulary entry.
//
// Header-only (all inline) — no CMake source registration, same as
// ParamVerity.h / MasterFxAccess.h / FxSidechain.h. Engine surface ONLY: bounded
// file reads + JSON shaping on the calling (message) thread. No DSP, no audio
// thread, no ValueTree, no plugin instantiation, no mutation — a refusal can
// never have written anything.

#include "DeviceParamMap.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace HDAW {

// How many available intent ids an "unknown intent" error lists before it
// truncates (the caller needs enough to correct the spelling, not the catalogue).
inline constexpr int kIntentHintLimit = 20;

// The ONE refusal for `intent` on a PLUGIN slot. A plugin's parameters live in
// the live plugin cache, not in a Device Parameter Map, so there is nothing to
// resolve against; both surfaces report this exact string.
inline const std::string& pluginIntentRefusalText()
{
    static const std::string text =
        "intent is not supported for a plugin FX slot: pass paramIndex or paramName";
    return text;
}

struct IntentResolution
{
    bool ok = false;
    int paramIndex = -1;
    std::string name;
    std::string error;                                   // set iff !ok
    std::vector<std::pair<int, std::string>> candidates; // (index, name); -1 = no index
};

// Resolve one intent id against the device map of `fxType`. See the contract
// above; `error` is empty iff `ok`.
inline IntentResolution resolveInternalFxIntent(const std::string& fxType,
                                               const std::string& intent)
{
    IntentResolution r;
    const QString engine = QString::fromStdString(fxType);
    const QString want   = QString::fromStdString(intent);

    // Gate 9: the engine id becomes a path component, so it must pass the same
    // [a-z0-9_] guard the device surface uses — never a traversal.
    if (!validEngineId(engine))
    {
        r.error = "engine '" + fxType + "' has no device map (engine ids match [a-z0-9_])";
        return r;
    }

    const auto& dd = resolveDeviceMapDir();
    if (dd.dir.isEmpty())
    {
        r.error = "device map directory unavailable: " + dd.error.toStdString();
        return r;
    }

    QString err;
    const QJsonObject root = readDeviceMapFile(dd.dir + "/" + engine + ".params.json", err);
    if (!err.isEmpty())
    {
        r.error = "no device map for engine '" + fxType + "': " + err.toStdString();
        return r;
    }
    const QString schema = root.value("schema").toString();
    if (schema != QLatin1String(kDeviceMapSchema))
    {
        r.error = (fxType + ".params.json: unsupported schema '" + schema.toStdString()
                   + "' (expected " + kDeviceMapSchema + ")");
        return r;
    }

    QStringList available;                         // every intent id in the map
    std::vector<std::pair<int, std::string>> matches;
    for (const auto& pv : root.value("params").toArray())
    {
        const QJsonObject p = pv.toObject();
        bool hit = false;
        for (const auto& iv : p.value("intents").toArray())
        {
            const QString id = iv.toString();
            if (id.isEmpty())
                continue;
            if (!available.contains(id, Qt::CaseInsensitive))
                available << id;
            if (id.compare(want, Qt::CaseInsensitive) == 0)
                hit = true;
        }
        if (!hit)
            continue;
        const QJsonValue idx = p.value("index");
        matches.emplace_back(idx.isDouble() ? static_cast<int>(idx.toDouble()) : -1,
                             p.value("name").toString().toStdString());
    }

    if (matches.empty())
    {
        r.error = "unknown intent '" + intent + "' on " + fxType + ": no param declares it";
        available.sort(Qt::CaseInsensitive);
        if (!available.isEmpty())
        {
            const QStringList shown = available.mid(0, kIntentHintLimit);
            r.error += " (available intents: " + shown.join(", ").toStdString();
            if (available.size() > shown.size())
                r.error += ", ...";
            r.error += ")";
        }
        return r;
    }

    if (matches.size() > 1)
    {
        // Ambiguity is REFUSED: a device map can carry one intent on several
        // params (a real eq declares filter-sweep on both Frequency and Gain),
        // and silently picking one would be a guess (Gate 2 — no silent
        // behaviour). The candidates travel back so the caller can re-issue
        // with paramIndex.
        r.candidates = matches;
        r.error = "ambiguous intent '" + intent + "' on " + fxType + ": "
                  + std::to_string(matches.size()) + " params declare it (";
        const std::size_t shown = matches.size() < static_cast<std::size_t>(kIntentHintLimit)
                                      ? matches.size()
                                      : static_cast<std::size_t>(kIntentHintLimit);
        for (std::size_t i = 0; i < shown; ++i)
        {
            if (i != 0)
                r.error += ", ";
            r.error += (matches[i].first >= 0 ? std::to_string(matches[i].first) : std::string("?"));
            if (!matches[i].second.empty())
                r.error += " " + matches[i].second;
        }
        if (matches.size() > shown)
            r.error += ", ...";
        r.error += "); pass paramIndex or paramName";
        return r;
    }

    if (matches[0].first < 0)
    {
        r.error = "intent '" + intent + "' on " + fxType
                  + " names a param with no index (offset-only map entry); pass paramIndex";
        return r;
    }

    r.ok = true;
    r.paramIndex = matches[0].first;
    r.name = matches[0].second;
    return r;
}

} // namespace HDAW
