#pragma once
// TRACK-FX-SLOT COMPRESSOR SIDECHAIN v1 — ONE request shaper for BOTH surfaces
// (AGENTS.md feature-parity contract: "Where both surfaces shape the same
// artifact, put the logic in src/common/ — identical payload by construction").
//
//   MCP  set_fx_sidechain        (src/mcp/McpTools_FxSlot.cpp)
//   RPC  project.setFxSidechain  (src/frontend/router/Router_Project.cpp)
//
// The engine entry point is AudioEngineCommands::setFxSidechain
// (src/engine/AudioEngineCommands_Fx.cpp; contract in AudioEngineCommands.h) and
// it ALREADY accepts the optional stable-id spellings for both ends, so this
// header's job is not to re-resolve anything — it is the ONE reader of the
// argument object (exactly as the surfaces must read it), the ONE place the
// refusal text is chosen, and the ONE call site of the command. Both surfaces
// therefore report byte-identical text for the same request by construction: a
// difference would need an edit here, which both surfaces share.
//
// Argument names are the CONTRACT and they are the command's own parameter
// names (the FX-slot family's `trackIndex` spelling deliberately does NOT apply
// — the payload here IS the command's JSON, so the keys must be the command's
// keys): trackId / trackID / slotIndex / sourceTrackId / sourceTrackID / level /
// enabled. The tool declares the SAME seven keys with additionalProperties:false
// and required:["slotIndex"], so mcp::validateSchema refuses an unknown key, a
// missing slotIndex and a mistyped value BEFORE the handler runs. The JSON-RPC
// route has no validator, so fxSidechainValidateArgs below reproduces those
// refusals byte for byte (the common/BatchEditJson.h precedent; the lesson-38
// unknown-key rule), and the twin test pins every class of input on both
// surfaces.
//
// Presence is decided with QJsonObject::contains(), never by the value: an
// explicit `sourceTrackId: 0` is a REAL source (track 0), and only a stable
// `sourceTrackID: 0` — or both spellings absent — clears the sidechain. That
// asymmetry is the command's documented v1 semantics (see the header contract
// at AudioEngineCommands.h) and is NOT re-interpreted here.
//
// Stable spelling wins when BOTH spellings of one end are supplied — the
// command's rule (no "disagreement" refusal, unlike common/StableRefResolve.h's
// B2 tools: the engine behavior is frozen and this shaper must not add a gate
// the command does not have). Optionals are passed through; an absent key stays
// nullopt so the command's "leave unchanged" vs "clear" distinction survives.
//
// Header-only (all inline) — no CMake source registration, same as
// MasterFxAccess.h / PresetApply.h. It includes the engine because the ONE
// entry point it must reach (AudioEngineCommands::setFxSidechain) lives on the
// concrete command layer, exactly like PresetApply.h's loaders.

#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands.h"
#include "JsonInteger.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstring>
#include <optional>
#include <string>

namespace HDAW {

// The seven argument keys the MCP schema declares — additionalProperties:false
// upstream, so the same set is the accepted set here. QJsonObject keys carry no
// order of their own (mcp::validateSchema iterates the DECLARED `properties`,
// which IS sorted by key, and BatchEditJson.h iterates the instance's, which is
// sorted too), so a sorted list indexed by the offending key order reproduces
// the validator's first-offender for both surfaces.
inline const QStringList& fxSidechainArgKeys()
{
    static const QStringList keys{ QStringLiteral("enabled"),       QStringLiteral("level"),
                                   QStringLiteral("slotIndex"),     QStringLiteral("sourceTrackID"),
                                   QStringLiteral("sourceTrackId"), QStringLiteral("trackID"),
                                   QStringLiteral("trackId") };
    return keys;
}

inline bool fxSidechainIsAcceptedKey(const QString& key)
{
    return fxSidechainArgKeys().contains(key);
}

// The DECLARED type of one accepted key (the tool's schema): `level` is the
// only number and `enabled` the only boolean — the five id/index keys are all
// declared `integer`.
inline const char* fxSidechainDeclaredType(const QString& key)
{
    if (key == QStringLiteral("enabled")) return "boolean";
    if (key == QStringLiteral("level"))   return "number";
    return "integer";
}

// mcp::validateSchema's typeMatches for one DECLARED type name, for the four
// types the schema uses. `integer` reuses the ONE shared predicate
// (common/JsonInteger.h), so a numeric-but-non-integral value is answered
// exactly as the validator's qint64 round-trip answers it.
inline bool fxSidechainTypeMatches(const QJsonObject& args, const QString& key,
                                   const char* expectedType)
{
    const QJsonValue v = args.value(key);
    if (std::strcmp(expectedType, "integer") == 0) return isJsonInteger(v);
    if (std::strcmp(expectedType, "number") == 0)  return v.isDouble();
    if (std::strcmp(expectedType, "boolean") == 0) return v.isBool();
    return false;
}

// The ONE mirrored policy for this tool's argument object: mcp::validateSchema
// over the tool's OWN schema, in the validator's effective order —
//   (1) additionalProperties:false -> first key absent from `properties` in
//       sorted instance order answers "<key>: unknown property"
//   (2) each DECLARED property still present, in sorted declared order, answers
//       "<key>: expected <declared type>" (a null value is SKIPPED, exactly as
//       validateInner skips it)
//   (3) required:["slotIndex"] LAST -> "slotIndex: missing required property
//       'slotIndex'"
// — all rendered through SchemaError::toString()'s "path: message" and carrying
// the MCP server's "invalid params: " prefix (McpServer prepends it to the
// validator's text; BatchEditJson.h bakes it into the shared string the same
// way). Byte-identical to the MCP surface by construction, and the twin test
// pins it.
inline bool fxSidechainValidateArgs(const QJsonObject& args, QString& error)
{
    error.clear();

    // (1) unknown property, in sorted instance key order.
    for (auto it = args.begin(); it != args.end(); ++it)
        if (!fxSidechainIsAcceptedKey(it.key()))
        {
            error = QStringLiteral("invalid params: ") + it.key()
                    + QStringLiteral(": unknown property");
            return false;
        }

    // (2) declared property types, in sorted declared key order.
    for (const QString& key : fxSidechainArgKeys())
    {
        if (!args.contains(key)) continue;          // absent: satisfied
        if (args.value(key).isNull()) continue;     // null: skipped upstream
        const char* expected = fxSidechainDeclaredType(key);
        if (!fxSidechainTypeMatches(args, key, expected))
        {
            error = QStringLiteral("invalid params: ") + key
                    + QStringLiteral(": expected ") + QLatin1String(expected);
            return false;
        }
    }

    // (3) required:["slotIndex"], checked LAST like validateInner.
    if (!args.contains(QStringLiteral("slotIndex")))
    {
        error = QStringLiteral("invalid params: slotIndex: missing required property 'slotIndex'");
        return false;
    }

    return true;
}

// Resolved outcome of one set_fx_sidechain request.
//   ok == true  -> `text` is the command's compact JSON payload
//                  {"ok":true,"trackId":N,"slotIndex":N,"sourceTrackId":N,
//                   "level":F,"enabled":B}
//   ok == false -> `text` is the refusal, byte-identical to the `error` the
//                  command reported (the engine's strings travel verbatim:
//                  "unknown track", "slotIndex out of range", "sidechain
//                  requires a compressor FX slot", "source track not found: N",
//                  "self-sidechain refused (acyclic graph)", "sidechain cycle
//                  refused: …", "level out of range: … (expected 0..1)")
struct FxSidechainOutcome
{
    bool ok = false;
    QString text;
};

// ONE entry point for set_fx_sidechain / project.setFxSidechain: read the
// argument object, hand the optionals to the command, report its payload or its
// refusal verbatim. Never mutates on refusal (the command validates first).
inline FxSidechainOutcome fxSidechainToolText(AudioEngine& engine, const QJsonObject& args)
{
    FxSidechainOutcome out;

    // The mirrored schema gate runs FIRST (same order as the MCP surface, where
    // mcp::validateSchema precedes the handler), so an unknown key / missing
    // slotIndex / mistyped value is refused with byte-identical text on both
    // surfaces. Only then are the now type-checked values read.
    QString err;
    if (!fxSidechainValidateArgs(args, err))
    {
        out.text = err;
        return out;
    }

    const auto optInt = [&args](const char* key) -> std::optional<int> {
        return args.contains(key) ? std::optional<int>(static_cast<int>(args.value(key).toDouble()))
                                  : std::nullopt;
    };
    const std::optional<int> trackId       = optInt("trackId");
    const std::optional<int> trackID       = optInt("trackID");
    const std::optional<int> sourceTrackId = optInt("sourceTrackId");
    const std::optional<int> sourceTrackID = optInt("sourceTrackID");
    const std::optional<float> level = args.contains("level")
        ? std::optional<float>(static_cast<float>(args.value("level").toDouble()))
        : std::nullopt;
    const std::optional<bool> enabled = args.contains("enabled")
        ? std::optional<bool>(args.value("enabled").toBool())
        : std::nullopt;

    // slotIndex is required — the gate above already refused its absence and
    // non-integrality (MCP answers those before the handler too).
    const int slotIndex = static_cast<int>(args.value("slotIndex").toDouble());

    std::string commandError;
    const std::string json = engine.getAudioEngineCommands().setFxSidechain(
        trackId, trackID, slotIndex, sourceTrackId, sourceTrackID, level, enabled,
        &commandError);

    if (!commandError.empty())
    {
        out.text = QString::fromStdString(commandError);
        return out;
    }
    out.ok = true;
    out.text = QString::fromStdString(json);
    return out;
}

} // namespace HDAW
