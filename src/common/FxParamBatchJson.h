#pragma once
// Batched param-write request shaping — the STRICT parser for the `writes`
// arrays of the batch param tools, shared by the MCP tools (set_fx_params /
// set_bus_fx_params / set_lfo_params) and their JSON-RPC twins so both surfaces
// refuse a malformed write with the SAME bytes BY CONSTRUCTION (AGENTS.md parity
// contract; slice 1 of docs/plans/2026-10-05-param-batch-and-bugfixes.md).
// Header-only; JUCE + Qt only — no CMake source registration.
//
// Mirrors src/common/BatchEditJson.h: the MCP tool schema declares each write
// item with additionalProperties:false + a required list, so the MCP validator
// rejects a typo'd key or a missing required field BEFORE the handler runs,
// with text "invalid params: writes[i].<key>: unknown property" (and the
// MCP-server-wide "invalid params: " prefix). The RPC route has no such
// validator, so this parser reproduces those exact strings — the twin tests
// compare the two failure messages byte-for-byte. "An unknown write key, a
// missing required field, or a non-integral index is REFUSED, never silently
// dropped" (lesson 38).
//
// STRUCTURE here, SEMANTICS at the command layer: this parser validates keys,
// types, integrality and required fields (all-or-nothing, like
// parseClipEdits), but does NOT check that a track/slot/param EXISTS. Those are
// per-write errors the batch command reports (partial-apply) — the same split
// BatchEditJson/parseClipEdits makes for an unknown clipId.
//
// Track arguments are resolved HERE (a write carries the resolved POSITIONAL
// trackIndex, per ProjectCommands::FxParamWrite), through the ONE shared rule
// (common/StableRefResolve.h): `trackID` (stable) wins over `trackId`
// (positional) and a disagreement is an error naming both. That is why the two
// writers that address a track take the TRACK_LIST ValueTree — the same
// argument order MovementPlanJson::parseMovementPlan uses.

#include "ProjectCommands.h"   // ProjectCommands::FxParamWrite / BusFxParamWrite / LfoParamWrite
#include "JsonInteger.h"       // isJsonInteger (the ONE integrality predicate)
#include "StableRefResolve.h"  // resolveTrackRef (the ONE shared stable-id rule)

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <string>
#include <vector>

namespace HDAW {

// The ONE empty-batch refusal text per command — emitted verbatim by both
// surfaces (the MCP tool renders it as an in-band error, the RPC as -32602).
// The bus/LFO batches share set_fx_params' text: the array is named `writes` on
// every one of them, so the refusal names the same thing.
inline constexpr const char* kEmptyFxParamWritesError = "writes must not be empty";

// The TOP-LEVEL `mode` of set_fx_params ("real" | "normalized", default
// "real"): the per-write `mode` overrides it. Parsed HERE rather than read as
// `a.value("mode").toString()=="normalized"` so the RPC route (which has no
// schema validator) refuses an unknown value with the MCP validator's EXACT
// bytes — the enum wording is reproduced from McpSchema.cpp:59-73. Absent key
// -> real, no error.
inline bool parseFxBatchMode(const QJsonObject& args, bool& normalizedOut, QString& error)
{
    normalizedOut = false;
    error.clear();
    if (!args.contains(QStringLiteral("mode")))
        return true;
    const QJsonValue v = args.value(QStringLiteral("mode"));
    if (!v.isString())
    {
        error = QStringLiteral("invalid params: mode: expected string");
        return false;
    }
    const QString mode = v.toString();
    if (mode == QLatin1String("real"))            { normalizedOut = false; return true; }
    if (mode == QLatin1String("normalized"))      { normalizedOut = true;  return true; }
    error = QStringLiteral("invalid params: mode: value \"") + mode
            + QStringLiteral("\" not in enum (allowed: \"real\", \"normalized\")");
    return false;
}

// The compact-JSON payload BOTH surfaces return for a param batch — the SAME
// bytes by construction. The MCP tool emits this string as its text; the RPC
// route parses it back into the reply object (the apply_movement_plan
// precedent, Router_Project.cpp), so the two can never drift.
//   {"ok":<bool>,"written":N,"failed":M,"errors":[{"index":i,"error":"..."}]}
// `errors` is sized to writes.size() with "" for the writes that landed (see
// BatchResult's contract): the failures-only rows are numbered by their
// ORIGINAL write index, so a caller can map a row back to its write.
inline QString fxParamBatchPayloadJson(const ProjectCommands::BatchResult& r,
                                       const std::vector<std::string>& errors)
{
    QJsonArray rows;
    int failed = 0;
    for (std::size_t i = 0; i < errors.size(); ++i)
    {
        if (errors[i].empty())
            continue;
        ++failed;
        rows.append(QJsonObject{{ QStringLiteral("index"), static_cast<int>(i) },
                                { QStringLiteral("error"), QString::fromStdString(errors[i]) }});
    }
    const QJsonObject payload{
        { QStringLiteral("ok"), r.ok },
        { QStringLiteral("written"), r.applied },
        { QStringLiteral("failed"), failed },
        { QStringLiteral("errors"), rows } };
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

// The LFO param vocabulary set_lfo_param documents (waveform/rate/rateSync/
// depth/bipolar/phaseOffset/targetParamID/enabled), in ONE place: the batched
// writer validates a name against it and reports an unknown one with the exact
// text the single tool emits — an unknown name is REFUSED, never silently
// dropped (setLfoParam's if/else chain has no else branch, so a typo used to be
// a silent no-op — lesson 38). Sorted, so the "(valid: ...)" list is stable.
inline const std::vector<std::string>& lfoParamNames()
{
    static const std::vector<std::string> names = {
        "bipolar", "depth", "enabled", "phaseOffset",
        "rate", "rateSync", "targetParamID", "waveform" };
    return names;
}

inline bool isLfoParamName(const std::string& name)
{
    for (const auto& n : lfoParamNames())
        if (n == name)
            return true;
    return false;
}

// The ONE unknown-LFO-name refusal (the single tool's exact wording), shared by
// the batched command and (from slice 2 on) the single tool.
inline std::string unknownLfoParamError(const std::string& name)
{
    std::string out = "unknown param '" + name + "' (valid: ";
    const auto& names = lfoParamNames();
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i != 0) out += ", ";
        out += names[i];
    }
    out += ")";
    return out;
}

// The declared write-item property names (each schema's additionalProperties
// set). Anything else is a typo and is rejected.
inline const QSet<QString>& fxParamWriteKeys()
{
    static const QSet<QString> t{
        QStringLiteral("trackId"), QStringLiteral("trackID"),
        QStringLiteral("slotIndex"), QStringLiteral("paramIndex"),
        QStringLiteral("paramName"), QStringLiteral("intent"),
        QStringLiteral("value"), QStringLiteral("mode") };
    return t;
}

inline const QSet<QString>& busFxParamWriteKeys()
{
    static const QSet<QString> t{
        QStringLiteral("busID"), QStringLiteral("paramIndex"), QStringLiteral("value") };
    return t;
}

inline const QSet<QString>& lfoParamWriteKeys()
{
    static const QSet<QString> t{
        QStringLiteral("trackId"), QStringLiteral("trackID"),
        QStringLiteral("lfoIndex"), QStringLiteral("paramName"), QStringLiteral("value") };
    return t;
}

// Read `trackId` (positional) / `trackID` (stable) off one write object and
// resolve through the ONE shared rule. Presence is `contains()`, never the
// value (an explicit `trackId: 0` is track 0; `trackID: 0` is an unknown id) —
// exactly mcp::refArgs / RouterHelpers::refArgs, reproduced here because
// src/common cannot include either surface's helpers. Byte-identical failures
// fall out of resolveTrackRef.
inline bool resolveWriteTrackRef(const juce::ValueTree& trackList, const QJsonObject& o,
                                 int& out, QString& error)
{
    int index = kNoRef, stableID = 0;
    bool stablePresent = false;

    if (o.contains(QStringLiteral("trackId")))
    {
        const QJsonValue v = o.value(QStringLiteral("trackId"));
        if (!v.isDouble())
        {
            error = QStringLiteral("missing or non-numeric param: trackId");
            return false;
        }
        if (!isJsonInteger(v))
        {
            error = QStringLiteral("invalid params: trackId: expected integer");
            return false;
        }
        index = static_cast<int>(v.toDouble());
    }
    if (o.contains(QStringLiteral("trackID")))
    {
        const QJsonValue v = o.value(QStringLiteral("trackID"));
        if (!v.isDouble())
        {
            error = QStringLiteral("missing or non-numeric param: trackID");
            return false;
        }
        if (!isJsonInteger(v))
        {
            error = QStringLiteral("invalid params: trackID: expected integer");
            return false;
        }
        stableID = static_cast<int>(v.toDouble());
        stablePresent = true;
    }

    const auto r = resolveTrackRef(trackList, index, stableID, kTrackRefKeys, stablePresent);
    if (!r.ok)
    {
        error = QString::fromStdString(r.error);
        return false;
    }
    out = r.index;
    return true;
}

// Parse a `writes` array into FxParamWrite values. Returns false and sets
// `error` to the shared refusal text on: a non-array/absent value; an empty
// array; a non-object element; an unknown key; a wrongly-typed field; a missing
// required field; a bad stable-id track ref. `defaultNormalized` is the
// TOP-LEVEL `mode` the caller resolved ("normalized" -> true); a per-write
// `mode` overrides it. Required per write: slotIndex, value, and one track
// argument; paramIndex/paramName/intent are the addressing alternatives the
// resolver ranks later (paramIndex > paramName > intent).
inline bool parseFxParamWrites(const juce::ValueTree& trackList, const QJsonValue& writesValue,
                               std::vector<ProjectCommands::FxParamWrite>& out, QString& error,
                               bool defaultNormalized = false)
{
    out.clear();
    error.clear();

    if (!writesValue.isArray())
    {
        error = QStringLiteral("invalid params: writes: expected array");
        return false;
    }
    const QJsonArray arr = writesValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptyFxParamWritesError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("writes[%1]").arg(i);
        if (!arr.at(i).isObject())
        {
            error = QStringLiteral("invalid params: ") + base + QStringLiteral(": expected object");
            return false;
        }
        const QJsonObject e = arr.at(i).toObject();
        const auto reject = [&error, &base](const QString& key, const char* reason) {
            error = QStringLiteral("invalid params: ") + base + QLatin1Char('.') + key
                    + QStringLiteral(": ") + QString::fromUtf8(reason);
            return false;
        };

        // additionalProperties:false — a typo'd key is REJECTED (never silently
        // dropped), in the element's own (sorted) key order like the validator.
        for (auto it = e.begin(); it != e.end(); ++it)
            if (!fxParamWriteKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::FxParamWrite w;
        if (!resolveWriteTrackRef(trackList, e, w.trackIndex, error))
            return false;

        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("slotIndex"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                w.slotIndex = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("paramIndex"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                w.paramIndex = static_cast<int>(v.toDouble());
                w.hasParamIndex = true;   // presence, never the value (explicit 0 is real)
            }
            else if (key == QLatin1String("paramName"))
            {
                if (!v.isString()) return reject(key, "expected string");
                w.paramName = v.toString().toStdString();
            }
            else if (key == QLatin1String("intent"))
            {
                if (!v.isString()) return reject(key, "expected string");
                w.intent = v.toString().toStdString();
            }
            else if (key == QLatin1String("value"))
            {
                if (!v.isDouble()) return reject(key, "expected number");
                w.value = v.toDouble();
            }
            else if (key == QLatin1String("mode"))
            {
                if (!v.isString()) return reject(key, "expected string");
                const QString mode = v.toString();
                if (mode == QLatin1String("real"))            w.normalized = false;
                else if (mode == QLatin1String("normalized")) w.normalized = true;
                else
                {
                    error = QStringLiteral("invalid params: ") + base + QStringLiteral(".mode: ")
                            + QStringLiteral("value \"") + mode
                            + QStringLiteral("\" not in enum (allowed: \"real\", \"normalized\")");
                    return false;
                }
            }
        }
        // No per-write `mode` -> the caller's top-level default.
        if (!e.contains(QStringLiteral("mode")))
            w.normalized = defaultNormalized;

        // required:["slotIndex","value"] — checked AFTER the property loop,
        // like the validator.
        if (!e.contains(QStringLiteral("slotIndex")))
            return reject(QStringLiteral("slotIndex"), "missing required property 'slotIndex'");
        if (!e.contains(QStringLiteral("value")))
            return reject(QStringLiteral("value"), "missing required property 'value'");

        out.push_back(w);
    }
    return true;
}

// Parse a `writes` array into BusFxParamWrite values (busID + the bus fxType's
// def index + a REAL value). Required per write: busID, paramIndex, value.
inline bool parseBusFxParamWrites(const QJsonValue& writesValue,
                                  std::vector<ProjectCommands::BusFxParamWrite>& out,
                                  QString& error)
{
    out.clear();
    error.clear();

    if (!writesValue.isArray())
    {
        error = QStringLiteral("invalid params: writes: expected array");
        return false;
    }
    const QJsonArray arr = writesValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptyFxParamWritesError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("writes[%1]").arg(i);
        if (!arr.at(i).isObject())
        {
            error = QStringLiteral("invalid params: ") + base + QStringLiteral(": expected object");
            return false;
        }
        const QJsonObject e = arr.at(i).toObject();
        const auto reject = [&error, &base](const QString& key, const char* reason) {
            error = QStringLiteral("invalid params: ") + base + QLatin1Char('.') + key
                    + QStringLiteral(": ") + QString::fromUtf8(reason);
            return false;
        };

        for (auto it = e.begin(); it != e.end(); ++it)
            if (!busFxParamWriteKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::BusFxParamWrite w;
        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("busID"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                w.busID = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("paramIndex"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                w.paramIndex = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("value"))
            {
                if (!v.isDouble()) return reject(key, "expected number");
                w.value = v.toDouble();
            }
        }

        if (!e.contains(QStringLiteral("busID")))
            return reject(QStringLiteral("busID"), "missing required property 'busID'");
        if (!e.contains(QStringLiteral("paramIndex")))
            return reject(QStringLiteral("paramIndex"), "missing required property 'paramIndex'");
        if (!e.contains(QStringLiteral("value")))
            return reject(QStringLiteral("value"), "missing required property 'value'");

        out.push_back(w);
    }
    return true;
}

// Parse a `writes` array into LfoParamWrite values. Required per write: one
// track argument, lfoIndex, paramName, value. The paramName VOCABULARY is a
// command-layer (semantic) check — an unknown name is a per-write failure the
// batch reports, not a structural refusal of the whole array (see
// unknownLfoParamError / isLfoParamName).
inline bool parseLfoParamWrites(const juce::ValueTree& trackList, const QJsonValue& writesValue,
                                std::vector<ProjectCommands::LfoParamWrite>& out, QString& error)
{
    out.clear();
    error.clear();

    if (!writesValue.isArray())
    {
        error = QStringLiteral("invalid params: writes: expected array");
        return false;
    }
    const QJsonArray arr = writesValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptyFxParamWritesError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("writes[%1]").arg(i);
        if (!arr.at(i).isObject())
        {
            error = QStringLiteral("invalid params: ") + base + QStringLiteral(": expected object");
            return false;
        }
        const QJsonObject e = arr.at(i).toObject();
        const auto reject = [&error, &base](const QString& key, const char* reason) {
            error = QStringLiteral("invalid params: ") + base + QLatin1Char('.') + key
                    + QStringLiteral(": ") + QString::fromUtf8(reason);
            return false;
        };

        for (auto it = e.begin(); it != e.end(); ++it)
            if (!lfoParamWriteKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::LfoParamWrite w;
        if (!resolveWriteTrackRef(trackList, e, w.trackIndex, error))
            return false;

        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("lfoIndex"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                w.lfoIndex = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("paramName"))
            {
                if (!v.isString()) return reject(key, "expected string");
                w.paramName = v.toString().toStdString();
            }
            else if (key == QLatin1String("value"))
            {
                if (!v.isDouble()) return reject(key, "expected number");
                w.value = v.toDouble();
            }
        }

        if (!e.contains(QStringLiteral("lfoIndex")))
            return reject(QStringLiteral("lfoIndex"), "missing required property 'lfoIndex'");
        if (!e.contains(QStringLiteral("paramName")))
            return reject(QStringLiteral("paramName"), "missing required property 'paramName'");
        if (!e.contains(QStringLiteral("value")))
            return reject(QStringLiteral("value"), "missing required property 'value'");

        out.push_back(w);
    }
    return true;
}

} // namespace HDAW
