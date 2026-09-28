#pragma once
// set_clips_edit request shaping, the strict INTEGER-argument parsers
// (parseIntArg / parseIntArray) and the batch-edit refusal texts, shared by the
// MCP tools (src/mcp/McpTools_Note.cpp / McpTools_Clip.cpp) and the
// JSON-RPC routes (project.setNotesGain / project.setNoteGain /
// project.setNotePan / project.setClipsEdit), so both
// surfaces emit the SAME error bytes BY CONSTRUCTION (AGENTS.md parity
// contract; S3 of docs/plans/2026-09-28-agent-mechanization.md). Header-only;
// JUCE + Qt only.
//
// WHY the parser re-words mcp::validateSchema: the MCP tool schema declares the
// `edits` item with additionalProperties:false + required:["clipId"], so the
// MCP validator rejects a typo'd key or a missing clipId BEFORE the handler
// runs, with text "invalid params: edits[i].<key>: unknown property" (and
// MCP-server-wide "invalid params: " prefix). The RPC route has no such
// validator, so this parser reproduces those exact strings — the twin test
// compares the two failure messages byte-for-byte ("an edit with an unknown key
// is REJECTED, never silently dropped").
#include "ProjectCommands.h"   // ProjectCommands::ClipEdit / BatchResult
// The ONE integrality predicate (mcp::validateSchema's integer test), shared
// with the router's argument helpers — see common/JsonInteger.h.
#include "JsonInteger.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <vector>

namespace HDAW {

// The ONE empty-batch refusal text per command — emitted verbatim by both
// surfaces (the MCP tool renders it as an in-band error, the RPC as -32602).
inline constexpr const char* kEmptyNoteIdsError = "noteIds must not be empty";
inline constexpr const char* kEmptyEditsError   = "edits must not be empty";

// The declared `edits` item property names (the schema's additionalProperties
// set). Anything else is a typo and is rejected.
inline const QSet<QString>& clipEditKeys()
{
    static const QSet<QString> t{
        QStringLiteral("clipId"), QStringLiteral("start"), QStringLiteral("duration"),
        QStringLiteral("gain"), QStringLiteral("fadeIn"), QStringLiteral("fadeOut"),
        QStringLiteral("name"), QStringLiteral("looping"),
    };
    return t;
}

// The integer-array half of the same mirror (mcp::validateSchema's
// `{"type":"array","items":{"type":"integer"}}`): every element must be an
// integral JSON number. A non-array is refused with the validator's
// `expected array`, a failing element with the validator's `expected integer`
// (McpSchema.cpp's typeMatches answers `expected integer` for BOTH a
// non-number and a non-integral number, because the item's declared type is
// `integer` — so the two surfaces cannot drift by construction). The
// "invalid params: " prefix is part of the text because the MCP handler emits
// its refusal verbatim while McpServer prepends that prefix to the validator's
// own, and the RPC route passes this string straight into makeError.
// An EMPTY array is NOT an error here: "must not be empty" belongs to the
// command layer's shared refusal text, which both surfaces already carry.
inline bool parseIntArray(const QJsonValue& v, const char* key, std::vector<int>& out,
                          QString& error)
{
    out.clear();
    error.clear();

    if (!v.isArray())
    {
        error = QStringLiteral("invalid params: ") + QLatin1String(key)
                + QStringLiteral(": expected array");
        return false;
    }
    const QJsonArray a = v.toArray();
    for (int i = 0; i < a.size(); ++i)
    {
        if (!isJsonInteger(a.at(i)))
        {
            error = QStringLiteral("invalid params: ") + QLatin1String(key)
                    + QStringLiteral("[%1]: expected integer").arg(i);
            return false;
        }
        out.push_back(static_cast<int>(a.at(i).toDouble()));
    }
    return true;
}

// The SCALAR twin of the above. Deliberately keeps the ROUTER's wording for a
// missing/non-numeric value ("missing or non-numeric param: <key>", the exact
// bytes RouterHelpers::requireInt has always emitted for the RPC surface, which
// the MCP handler mirrors in its own pre-checks) so the established twin tests
// stay green, and answers a numeric-but-non-integral double with the MCP
// validator's "invalid params: <key>: expected integer" — the text the MCP
// surface produces for an `{"type":"integer"}` argument, which used to be
// silently TRUNCATED on the RPC surface.
inline bool parseIntArg(const QJsonObject& o, const char* key, int& out, QString& error)
{
    error.clear();
    if (!o.contains(key) || !o.value(key).isDouble())
    {
        error = QStringLiteral("missing or non-numeric param: ") + QLatin1String(key);
        return false;
    }
    const QJsonValue v = o.value(key);
    if (!isJsonInteger(v))
    {
        error = QStringLiteral("invalid params: ") + QLatin1String(key)
                + QStringLiteral(": expected integer");
        return false;
    }
    out = static_cast<int>(v.toDouble());
    return true;
}

// Parse the `edits` value into ClipEdit values. Returns false and sets `error`
// to the shared refusal text on: a non-array/absent `edits`; an empty array; a
// non-object element; an unknown key; a wrongly-typed field; a missing clipId.
// Every failure text mirrors mcp::validateSchema's, so the RPC route and the
// MCP validator answer identically.
inline bool parseClipEdits(const QJsonValue& editsValue,
                           std::vector<ProjectCommands::ClipEdit>& edits,
                           QString& error)
{
    edits.clear();
    error.clear();

    if (!editsValue.isArray())
    {
        error = QStringLiteral("invalid params: edits: expected array");
        return false;
    }
    const QJsonArray arr = editsValue.toArray();
    if (arr.isEmpty())
    {
        error = QString::fromUtf8(kEmptyEditsError);
        return false;
    }

    for (int i = 0; i < arr.size(); ++i)
    {
        const QString base = QStringLiteral("edits[%1]").arg(i);
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
            if (!clipEditKeys().contains(it.key()))
                return reject(it.key(), "unknown property");

        ProjectCommands::ClipEdit ce;
        for (auto it = e.begin(); it != e.end(); ++it)
        {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("clipId"))
            {
                if (!isJsonInteger(v)) return reject(key, "expected integer");
                ce.clipId = static_cast<int>(v.toDouble());
            }
            else if (key == QLatin1String("start") || key == QLatin1String("duration")
                     || key == QLatin1String("gain")
                     || key == QLatin1String("fadeIn") || key == QLatin1String("fadeOut"))
            {
                if (!v.isDouble()) return reject(key, "expected number");
                const double d = v.toDouble();
                if (key == QLatin1String("start"))         ce.start = d;
                else if (key == QLatin1String("duration")) ce.duration = d;
                else if (key == QLatin1String("gain"))     ce.gain = d;
                else if (key == QLatin1String("fadeIn"))   ce.fadeIn = d;
                else                                       ce.fadeOut = d;
            }
            else if (key == QLatin1String("name"))
            {
                if (!v.isString()) return reject(key, "expected string");
                ce.name = v.toString().toStdString();
            }
            else if (key == QLatin1String("looping"))
            {
                if (!v.isBool()) return reject(key, "expected boolean");
                ce.looping = v.toBool();
            }
        }

        // required:["clipId"] — checked AFTER the property loop, like the validator.
        if (!e.contains(QStringLiteral("clipId")))
            return reject(QStringLiteral("clipId"), "missing required property 'clipId'");

        edits.push_back(ce);
    }
    return true;
}

} // namespace HDAW
