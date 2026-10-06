#pragma once
// Batched bus / send CREATION request shaping — the STRICT parser for the
// `buses` / `sends` arrays of the batch creator tools, shared by the MCP tools
// (add_buses / add_sends) and their JSON-RPC twins (project.addBuses /
// project.addSends) so both surfaces refuse a malformed item with the SAME bytes
// BY CONSTRUCTION (AGENTS.md parity contract; the deferred follow-up of
// docs/plans/2026-10-05-param-batch-and-bugfixes.md §"Not done"). Header-only;
// JUCE + Qt only — no CMake source registration.
//
// Mirrors src/common/FxParamBatchJson.h (and, under it, BatchEditJson.h): the
// MCP tool schema declares each item with additionalProperties:false + a
// required list, so the MCP validator rejects a typo'd key / missing required
// field BEFORE the handler runs, with text "invalid params: buses[i].<key>:
// unknown property" (and the MCP-server-wide "invalid params: " prefix). The RPC
// route has no such validator, so this parser reproduces those exact strings —
// the twin tests compare the two failure messages byte-for-byte. "An unknown
// key, a missing required field, or a non-integral index is REFUSED, never
// silently dropped" (lesson 38).
//
// STRUCTURE here, SEMANTICS at the command layer: this parser validates keys,
// types, integrality and required fields (all-or-nothing, like
// parseBusFxParamWrites), but does NOT check that a bus the item names EXISTS or
// that a track index is in range. Those are per-item errors the batch COMMAND
// reports (PARTIAL-APPLY) — the split FxParamBatchJson makes for an unknown
// param/track at write time.
//
// The send item addresses its track through the ONE shared rule
// (common/StableRefResolve.h): `trackID` (stable) wins over `trackId`
// (positional) and a disagreement is an error naming both — reusing
// FxParamBatchJson.h's resolveWriteTrackRef so a track ref resolves (and fails)
// identically to every other batch writer.

#include "ProjectCommands.h"   // ProjectCommands::BatchResult / BusCreateSpec / SendCreateSpec
#include "FxParamBatchJson.h"  // resolveWriteTrackRef (the ONE track-ref rule for a batch item)

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
// The arrays are named for their entity, so each refusal names what is empty.
inline constexpr const char* kEmptyBusesError = "buses must not be empty";
inline constexpr const char* kEmptySendsError = "sends must not be empty";

// The declared item property names (each schema's additionalProperties set).
// Anything else is a typo and is rejected.
inline const QSet<QString>& busCreateKeys()
{
    static const QSet<QString> t{
        QStringLiteral("busType"), QStringLiteral("name"),
        QStringLiteral("fxType"), QStringLiteral("busTarget") };
    return t;
}

inline const QSet<QString>& sendCreateKeys()
{
    static const QSet<QString> t{
        QStringLiteral("trackId"), QStringLiteral("trackID"),
        QStringLiteral("busTarget"), QStringLiteral("level"), QStringLiteral("isPreFader") };
    return t;
}

// Parse a `buses` array into BusCreateSpec values. Returns false and sets
// `error` to the shared refusal text on: a non-array/absent value; an empty
// array; a non-object element; an unknown key; a wrongly-typed field; a missing
// required field. Required per item: busType (name/fxType/busTarget optional —
// the command layer owns whether an fx bus needs an fxType).
inline bool parseBusCreates(const QJsonValue& busesValue,
                            std::vector<ProjectCommands::BusCreateSpec>& out,
                            QString& error)
{
    out.clear();
    error.clear();

    if (!busesValue.isArray())
    {
        error = QStringLiteral("invalid params: buses: expected array");
        return false;
    }
    const QJsonArray arr = busesValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptyBusesError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("buses[%1]").arg(i);
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
            if (!busCreateKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::BusCreateSpec spec;
        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("busType"))
            {
                if (!v.isString()) return reject(key, "expected string");
                spec.busType = v.toString().toStdString();
            }
            else if (key == QLatin1String("name"))
            {
                if (!v.isString()) return reject(key, "expected string");
                spec.name = v.toString().toStdString();
            }
            else if (key == QLatin1String("fxType"))
            {
                if (!v.isString()) return reject(key, "expected string");
                spec.fxType = v.toString().toStdString();
            }
            else if (key == QLatin1String("busTarget"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                spec.busTarget = static_cast<int>(v.toDouble());
            }
        }

        // required:["busType"] — checked AFTER the property loop, like the validator.
        if (!e.contains(QStringLiteral("busType")))
            return reject(QStringLiteral("busType"), "missing required property 'busType'");

        out.push_back(spec);
    }
    return true;
}

// Parse a `sends` array into SendCreateSpec values. Required per item:
// busTarget + a resolvable track argument (trackId|trackID — absence answers
// "trackId required", the single add_send tool's exact text). `busTarget` is a
// busID (already an identity, not a position).
inline bool parseSendCreates(const juce::ValueTree& trackList, const QJsonValue& sendsValue,
                             std::vector<ProjectCommands::SendCreateSpec>& out, QString& error)
{
    out.clear();
    error.clear();

    if (!sendsValue.isArray())
    {
        error = QStringLiteral("invalid params: sends: expected array");
        return false;
    }
    const QJsonArray arr = sendsValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptySendsError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("sends[%1]").arg(i);
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
            if (!sendCreateKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::SendCreateSpec spec;
        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            // The track keys are TYPE-checked here (the validator's per-property
            // pass) so a non-integral trackId fails with its path-prefixed text
            // BEFORE the required/ref passes below — the resolver runs last.
            if (key == QLatin1String("trackId") || key == QLatin1String("trackID"))
            {
                if (!v.isDouble())
                    return reject(key, "expected integer");
                if (!isJsonInteger(v))
                    return reject(key, "expected integer");
            }
            else if (key == QLatin1String("busTarget"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                spec.busTarget = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("level"))
            {
                if (!v.isDouble()) return reject(key, "expected number");
                spec.level = static_cast<float>(v.toDouble());
            }
            else if (key == QLatin1String("isPreFader"))
            {
                if (!v.isBool()) return reject(key, "expected boolean");
                spec.isPreFader = v.toBool();
            }
        }

        // required:["busTarget"] — checked AFTER the property loop, like the
        // validator (and BEFORE the track-ref resolution, which is a HANDLER
        // step the validator knows nothing about: a {} item must answer the
        // missing-busTarget text, not the resolver's "trackId required").
        if (!e.contains(QStringLiteral("busTarget")))
            return reject(QStringLiteral("busTarget"), "missing required property 'busTarget'");

        // The track ref resolves HERE (all-or-nothing, like parseFxParamWrites):
        // `trackID` wins over `trackId`, a disagreement / an unknown id / no
        // track argument at all is a shared refusal, never a per-item error.
        if (!resolveWriteTrackRef(trackList, e, spec.trackIndex, error))
            return false;

        out.push_back(spec);
    }
    return true;
}

// The compact-JSON payload BOTH surfaces return for a batch creation — the SAME
// bytes by construction. The MCP tool emits this string as its text; the RPC
// route parses it back into the reply object (the apply_movement_plan
// precedent, Router_Project.cpp), so the two can never drift.
//   {"ok":<bool>,"created":N,"failed":M,"<idKey>":[...],"errors":[{"index":i,"error":"..."}]}
// `<idKey>` is "busIDs" / "sendIndexes" and is sized to the request with -1 for
// each FAILED item; `errors` is the failures ONLY, numbered by ORIGINAL index,
// so a caller can map a row back to its item.
inline QString busSendBatchPayloadJson(const ProjectCommands::BatchResult& r,
                                       const std::vector<int>& ids,
                                       const std::vector<std::string>& errors,
                                       const char* idKey)
{
    QJsonArray idArr, rows;
    int failed = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
        idArr.append(ids[i]);
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
        { QStringLiteral("created"), r.applied },
        { QStringLiteral("failed"), failed },
        { QString::fromUtf8(idKey), idArr },
        { QStringLiteral("errors"), rows } };
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

} // namespace HDAW
