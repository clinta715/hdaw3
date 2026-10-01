#pragma once
// Argument parsing for the S4 render tools — verified_window / render_and_verify
// (docs/plans/2026-09-28-agent-mechanization.md §5). The MCP tools AND their RPC
// twins parse through these functions, so argument names, defaults and refusal
// BYTES are identical by construction (AGENTS.md "Feature parity: MCP + RPC").
//
// The `invalid params: <key>: missing required property '<key>'` / `expected
// <type>` wording mirrors src/mcp/McpSchema.cpp exactly — that validator
// pre-empts these same refusals on the TOOL surface, so the route has to speak
// its language to stay byte-identical (the BatchEditJson.h precedent). The two
// passes are mirrored too: property-TYPE errors first, in schema-property
// order, then missing-REQUIRED, in `required` order.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace HDAW {

inline QString s4MissingRequired(const char* key)
{
    return QStringLiteral("invalid params: %1: missing required property '%1'")
        .arg(QString::fromUtf8(key));
}

inline QString s4ExpectedType(const char* key, const char* type)
{
    return QStringLiteral("invalid params: %1: expected %2")
        .arg(QString::fromUtf8(key), QString::fromUtf8(type));
}

// Mirrors McpSchema::typeMatches. An explicit null/undefined value is SKIPPED by
// the validator's property pass (only `contains` decides required), so it is
// skipped here as well.
inline bool s4TypeMatches(const QJsonObject& o, const char* key, const char* type)
{
    if (!o.contains(key))
        return true;
    const QJsonValue v = o.value(key);
    if (v.isNull() || v.isUndefined())
        return true;
    const QString t = QString::fromUtf8(type);
    if (t == QLatin1String("string"))  return v.isString();
    if (t == QLatin1String("number"))  return v.isDouble();
    if (t == QLatin1String("object"))  return v.isObject();
    if (t == QLatin1String("integer"))
        return v.isDouble()
               && v.toDouble() == static_cast<double>(static_cast<qint64>(v.toDouble()));
    return true;
}

inline bool s4Requires(const QJsonObject& o, const char* key, QString& error)
{
    if (o.contains(key))
        return true;
    error = s4MissingRequired(key);
    return false;
}

struct VerifyWindowArgs
{
    double startBeat = 0.0;
    double endBeat = 0.0;
    QJsonObject targets;          // `expect` is the accepted alias
    std::string outputPath;       // empty => the command picks a temp file
    uint32_t timeoutMs = 600000;
};

// verify_window {startBeat, endBeat, targets|expect?, outputPath?, timeoutMs?}
// The expectation object is STRICT: its keys must be one of rmsMin, masterRms,
// ceilingHitPctMax, kickProminenceMin, targetDurationSeconds — an unknown key is
// refused ("unknown expectation key <key> (valid: <the accepted keys>)") BEFORE
// the render, on both surfaces.
// `rmsMin` is a LINEAR RMS floor, in the SAME units as the report's root `rms`
// and as `masterRms` (a silent window reports rms 0 and fails any positive floor).
inline bool parseVerifyWindowArgs(const QJsonObject& o, VerifyWindowArgs& out, QString& error)
{
    error.clear();
    if (!s4TypeMatches(o, "startBeat", "number"))  { error = s4ExpectedType("startBeat", "number"); return false; }
    if (!s4TypeMatches(o, "endBeat", "number"))    { error = s4ExpectedType("endBeat", "number"); return false; }
    if (!s4TypeMatches(o, "targets", "object"))    { error = s4ExpectedType("targets", "object"); return false; }
    if (!s4TypeMatches(o, "expect", "object"))     { error = s4ExpectedType("expect", "object"); return false; }
    if (!s4TypeMatches(o, "outputPath", "string")) { error = s4ExpectedType("outputPath", "string"); return false; }
    if (!s4TypeMatches(o, "timeoutMs", "integer")) { error = s4ExpectedType("timeoutMs", "integer"); return false; }
    if (!s4Requires(o, "startBeat", error)) return false;
    if (!s4Requires(o, "endBeat", error))   return false;

    // STRICT expectation keys (verify_window's OWN argument contract — do NOT push
    // this into applyTargetGates, whose permissiveness for mix_report/mix_verdict's
    // brief-supplied targets is deliberate). An unknown key used to be silently
    // ignored, so a typo'd gate returned a green `ok:true` for a check that never
    // ran (the lesson-34 class). Validated BEFORE the render, so a bad call costs
    // nothing. Both `targets` and `expect` are checked when both are present.
    static const char* const kAccepted[] = {
        "rmsMin", "masterRms", "ceilingHitPctMax",
        "kickProminenceMin", "targetDurationSeconds" };
    QString acceptedList;
    for (const char* a : kAccepted)
    {
        if (!acceptedList.isEmpty()) acceptedList += ", ";
        acceptedList += QLatin1String(a);
    }
    for (const char* key : { "targets", "expect" })
    {
        if (!o.contains(key) || !o.value(key).isObject())
            continue;
        const QJsonObject t = o.value(key).toObject();
        for (auto it = t.begin(); it != t.end(); ++it)
        {
            bool known = false;
            for (const char* a : kAccepted)
                if (it.key() == QLatin1String(a)) { known = true; break; }
            if (!known)
            {
                error = QStringLiteral("unknown expectation key %1 (valid: %2)")
                            .arg(it.key(), acceptedList);
                return false;
            }
        }
    }

    out.startBeat = o.value("startBeat").toDouble();
    out.endBeat = o.value("endBeat").toDouble();
    out.targets = o.contains("targets") ? o.value("targets").toObject()
                                        : o.value("expect").toObject();
    out.outputPath = o.value("outputPath").toString().toStdString();
    out.timeoutMs = static_cast<uint32_t>(
        std::max(0.0, o.value("timeoutMs").toDouble(600000.0)));
    return true;
}

struct RenderAndVerifyArgs
{
    QString outputPath;
    QJsonObject targets;
    uint32_t timeoutMs = 600000;
    bool fromPlan = false;         // MIRRORS mix_verdict's default (whole-file verdict)
    double dropBuildRatio = 0.9;
    double introSeconds = 2.0;
};

// render_and_verify {outputPath, targets?, timeoutMs?, fromPlan?, dropBuildRatio?,
// introSeconds?}
inline bool parseRenderAndVerifyArgs(const QJsonObject& o, RenderAndVerifyArgs& out, QString& error)
{
    error.clear();
    if (!s4TypeMatches(o, "outputPath", "string")) { error = s4ExpectedType("outputPath", "string"); return false; }
    if (!s4TypeMatches(o, "targets", "object"))    { error = s4ExpectedType("targets", "object"); return false; }
    if (!s4TypeMatches(o, "timeoutMs", "integer")) { error = s4ExpectedType("timeoutMs", "integer"); return false; }
    if (!s4TypeMatches(o, "fromPlan", "boolean"))  { error = s4ExpectedType("fromPlan", "boolean"); return false; }
    if (!s4TypeMatches(o, "dropBuildRatio", "number")) { error = s4ExpectedType("dropBuildRatio", "number"); return false; }
    if (!s4TypeMatches(o, "introSeconds", "number"))   { error = s4ExpectedType("introSeconds", "number"); return false; }
    if (!s4Requires(o, "outputPath", error)) return false;

    out.outputPath = o.value("outputPath").toString();
    out.targets = o.value("targets").toObject();
    out.timeoutMs = static_cast<uint32_t>(
        std::max(0.0, o.value("timeoutMs").toDouble(600000.0)));
    out.fromPlan = o.value("fromPlan").toBool(false);
    out.dropBuildRatio = o.value("dropBuildRatio").toDouble(0.9);
    out.introSeconds = o.value("introSeconds").toDouble(2.0);
    return true;
}

} // namespace HDAW
