#pragma once
// SLOT-SCOPED PATCH verbs — ONE implementation shared by the MCP tools
// (src/mcp/McpTools_FxChain.cpp: save_patch / load_patch / list_patches) and
// their JSON-RPC twins (src/frontend/router/Router_Project.cpp:
// project.savePatch / project.loadPatch / project.listPatches), per the
// AGENTS.md feature-parity contract: where both surfaces shape the same
// artifact the logic lives in src/common/, so the payload and every refusal are
// byte-identical BY CONSTRUCTION.
//
// A PATCH is ONE slot's full state (a single-slot HDAW::ChainPreset, slot 0),
// persisted by ChainLibrary::patchLibrary() under
// userApplicationDataDirectory/HDAW/patches. It is the SLOT-SCOPED sibling of
// the CHAIN verbs (save_fx_chain / load_fx_chain): applyFxChain preserves the
// target's instrument slots and APPENDS the preset's instrument as a new slot,
// so loading a 1-slot psy_fm chain onto a track that already had a psy_fm slot
// produced TWO psy_fm slots with the target untouched (measured defect).
// applyPatch writes INTO the addressed slot — never appends, never removes.
//
// The engine entry points are ProjectCommands::exportPatch (snapshot one slot)
// and ProjectCommands::applyPatch (write slot 0 into the EXISTING slot,
// refusing an out-of-range index, an empty preset and an fxType/plugin-id
// mismatch). This header is the ONE reader of the argument object, the ONE
// place each refusal text is chosen, and the ONE call site of the commands.
//
// Argument names are the tool's OWN property names and they are IDENTICAL on
// both surfaces (the frozen contract): trackId / trackID / slotIndex / name for
// save_patch, trackId / trackID / slotIndex / id for load_patch, nothing for
// list_patches. The MCP schema declares them with additionalProperties:false and
// required:["slotIndex","name"] (or [...,"id"]), so mcp::validateSchema refuses
// an unknown key / a missing required key / a mistyped value BEFORE the handler
// runs. The JSON-RPC route has no validator, so patchValidateArgs below
// reproduces those refusals byte for byte (the common/FxSidechain.h precedent:
// the v1 sidechain shaper mirrors the validator because both surfaces must agree
// on the empty-argument case the parity ratchet's dispatch probe sends).
//
// Presence of the stable id is decided with QJsonObject::contains(), never by
// the value: an explicit `trackID: 0` is an UNKNOWN id (never a missing key).
// The positional rule is the ONE shared one (common/StableRefResolve.h), so an
// unknown id / a disagreeing pair fails identically on both surfaces.
//
// Header-only (all inline) — no CMake source registration, same as
// FxSidechain.h / PresetApply.h. It includes the engine because the entry points
// it must reach (ChainLibrary::patchLibrary + the exportPatch/applyPatch
// commands) live on the engine and the concrete command layer.

#include "../engine/ChainLibrary.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"
#include "JsonInteger.h"        // isJsonInteger (the ONE shared integer predicate)
#include "StableRefResolve.h"   // resolveTrackRef (the ONE shared stable-id rule)

// Qt's qobjectdefs.h does `#define slots` (an object-like macro expanding to
// nothing), so in any TU that has already pulled in Qt, `p.slots` below parses
// as `p.` — "expected unqualified-id before '.'". ChainPreset's member is
// genuinely named `slots`, so the macro has to go for these uses to compile.
//
// Deferred, not dropped: Qt's `slots` must stay defined-and-empty for any later
// `public slots:` in the same TU (McpServer.h does exactly that), so it is
// restored at the end of this header. Same pattern as ChainLibrary.h.
#ifdef slots
#  define HDAW_PATCHPRESET_DEFER_QT_SLOTS 1
#  undef slots
#endif

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstring>
#include <string>
#include <vector>

namespace HDAW {

// ─── the mirrored schema gate ────────────────────────────────────────────────
//
// The three tools' accepted keys and declared types. Kept in ONE sorted table
// per tool so the type pass below walks them exactly as mcp::validateSchema's
// `for (props.begin..end)` does (QJsonObject iterates keys SORTED, with QString
// comparison: 'I' < 'i', so "trackID" precedes "trackId").
struct PatchArgSpec
{
    const char* key;
    const char* type;   // "integer" | "string"
};

inline const std::vector<PatchArgSpec>& savePatchArgSpecs()
{
    static const std::vector<PatchArgSpec> s{
        { "name",      "string"  },
        { "slotIndex", "integer" },
        { "trackID",   "integer" },
        { "trackId",   "integer" },
    };
    return s;
}

inline const std::vector<PatchArgSpec>& loadPatchArgSpecs()
{
    static const std::vector<PatchArgSpec> s{
        { "id",        "string"  },
        { "slotIndex", "integer" },
        { "trackID",   "integer" },
        { "trackId",   "integer" },
    };
    return s;
}

// mcp::validateSchema's typeMatches for the two declared types these tools use.
// `integer` reuses the ONE shared predicate (common/JsonInteger.h via
// StableRefResolve.h), so a numeric-but-non-integral value is answered exactly
// as the validator's qint64 round-trip answers it.
inline bool patchTypeMatches(const QJsonValue& v, const char* expectedType)
{
    if (std::strcmp(expectedType, "integer") == 0) return isJsonInteger(v);
    if (std::strcmp(expectedType, "string") == 0)  return v.isString();
    return false;
}

// The ONE mirrored policy for these tools' argument objects, in the validator's
// effective order (McpSchema.cpp validateInner):
//   (1) additionalProperties:false -> the first key absent from `properties` in
//       sorted instance order answers "<key>: unknown property";
//   (2) each DECLARED property still present, walked in sorted declared order,
//       answers "<key>: expected <declared type>" (a null value is SKIPPED,
//       exactly as validateInner skips it);
//   (3) `required` LAST, in the order the schema lists it ->
//       "<key>: missing required property '<key>'".
// All rendered through SchemaError::toString()'s "path: message" and carrying
// the MCP server's "invalid params: " prefix (McpServer prepends it to the
// validator's text). `specs` empty = the zero-arg list_patches schema, whose
// `properties` is empty, so any key is an unknown property.
inline bool patchValidateArgs(const QJsonObject& args,
                              const std::vector<PatchArgSpec>& specs,
                              const QStringList& requiredKeys,
                              QString& error)
{
    error.clear();

    auto declared = [&specs](const QString& key) -> const char* {
        for (const auto& s : specs)
            if (key == QLatin1String(s.key)) return s.type;
        return nullptr;
    };

    // (1) unknown property, in sorted instance key order (QJsonObject order).
    for (auto it = args.begin(); it != args.end(); ++it)
        if (declared(it.key()) == nullptr)
        {
            error = QStringLiteral("invalid params: ") + it.key()
                    + QStringLiteral(": unknown property");
            return false;
        }

    // (2) declared property types, in sorted declared key order.
    for (const auto& s : specs)
    {
        const QString key = QLatin1String(s.key);
        if (!args.contains(key)) continue;         // absent: satisfied
        const QJsonValue v = args.value(key);
        if (v.isNull()) continue;                  // null: skipped upstream
        if (!patchTypeMatches(v, s.type))
        {
            error = QStringLiteral("invalid params: ") + key
                    + QStringLiteral(": expected ") + QLatin1String(s.type);
            return false;
        }
    }

    // (3) required, checked LAST like validateInner, in the schema's own order.
    for (const QString& r : requiredKeys)
        if (!args.contains(r))
        {
            error = QStringLiteral("invalid params: ") + r
                    + QStringLiteral(": missing required property '") + r
                    + QStringLiteral("'");
            return false;
        }

    return true;
}

// ─── outcomes ────────────────────────────────────────────────────────────────
//
// ok == true  -> `text` is the success payload the surface reports verbatim
//                (save_patch: the compact {"id":"user/<name>.json"} object;
//                 load_patch: the literal "ok";
//                 list_patches: the compact array of
//                 {id,name,fxType,slotCount,source} rows).
// ok == false -> `text` is the refusal (byte-identical on both surfaces).
struct PatchOutcome
{
    bool ok = false;
    QString text;
};

// The ONE track-ref reader both surfaces call. Mirrors mcp::trackIndexArg
// (McpArgs.h) exactly: presence via contains(), the stable id wins, an unknown
// id / a disagreeing pair names itself. Kept here (rather than reached through
// the MCP helper) because src/common is Qt+JUCE only and the RPC route shares
// this header too.
inline bool resolveTrackRefArg(const QJsonObject& args, const juce::ValueTree& trackList,
                               int& out, std::string& error)
{
    int index = HDAW::kNoRef, stableID = 0;
    bool stablePresent = false;
    if (args.contains(QStringLiteral("trackId")))
    {
        const QJsonValue v = args.value(QStringLiteral("trackId"));
        if (!v.isDouble())
        {
            error = "missing or non-numeric param: trackId";
            return false;
        }
        if (!isJsonInteger(v))
        {
            error = "invalid params: trackId: expected integer";
            return false;
        }
        index = static_cast<int>(v.toDouble());
    }
    if (args.contains(QStringLiteral("trackID")))
    {
        const QJsonValue v = args.value(QStringLiteral("trackID"));
        if (!v.isDouble())
        {
            error = "missing or non-numeric param: trackID";
            return false;
        }
        if (!isJsonInteger(v))
        {
            error = "invalid params: trackID: expected integer";
            return false;
        }
        stableID = static_cast<int>(v.toDouble());
        stablePresent = true;
    }
    const auto r = resolveTrackRef(trackList, index, stableID,
                                   HDAW::kTrackRefKeys, stablePresent);
    if (!r.ok)
    {
        error = r.error;
        return false;
    }
    out = r.index;
    return true;
}

// Resolve the (trackIndex, slotIndex) pair a patch request addresses, reporting
// the SHARED refusal text. `outError` is empty on success. Both surfaces run
// this, so a bad track / slot refuses identically.
inline bool resolvePatchTarget(AudioEngine& engine, const QJsonObject& args,
                               int& trackIndex, int& slotIndex, QString& outError)
{
    std::string refErr;
    if (!resolveTrackRefArg(args, engine.getProjectModel().getTrackListTree(),
                            trackIndex, refErr))
    {
        outError = QString::fromStdString(refErr);
        return false;
    }
    auto tl = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= tl.getNumChildren())
    {
        outError = QStringLiteral("track not found");
        return false;
    }
    slotIndex = args.value(QStringLiteral("slotIndex")).toInt();
    const auto fxSlots = engine.getReadModel().getFxSlots(trackIndex);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxSlots.size()))
    {
        outError = QStringLiteral("slot not found");
        return false;
    }
    return true;
}

// ─── the three bodies ────────────────────────────────────────────────────────

// save_patch: snapshot ONE slot, persist it under the PATCH library.
inline PatchOutcome savePatchToolText(AudioEngine& engine, const QJsonObject& args)
{
    PatchOutcome out;
    QString err;
    if (!patchValidateArgs(args, savePatchArgSpecs(),
                           QStringList{ QStringLiteral("slotIndex"),
                                        QStringLiteral("name") }, err))
    {
        out.text = err;
        return out;
    }

    int ti = 0, si = 0;
    if (!resolvePatchTarget(engine, args, ti, si, err))
    {
        out.text = err;
        return out;
    }

    const QString name = args.value(QStringLiteral("name")).toString();
    if (name.trimmed().isEmpty())
    {
        out.text = QStringLiteral("name required");
        return out;
    }

    HDAW::ChainPreset p = engine.getProjectCommands().exportPatch(ti, si);
    if (p.slots.empty())
    {
        // The slot was validated above; an empty snapshot can only mean the
        // command could not read it — fail loudly, never save a 0-slot file.
        out.text = QStringLiteral("failed to save patch");
        return out;
    }
    p.name = juce::String(name.toStdString());
    const juce::String id = HDAW::ChainLibrary::patchLibrary().savePreset(p);
    if (id.isEmpty())
    {
        out.text = QStringLiteral("failed to save patch");
        return out;
    }

    QJsonObject payload;
    payload["id"] = QString::fromStdString(id.toStdString());
    out.ok = true;
    out.text = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return out;
}

// load_patch: read the stored patch, write it INTO the addressed slot.
inline PatchOutcome loadPatchToolText(AudioEngine& engine, const QJsonObject& args)
{
    PatchOutcome out;
    QString err;
    if (!patchValidateArgs(args, loadPatchArgSpecs(),
                           QStringList{ QStringLiteral("slotIndex"),
                                        QStringLiteral("id") }, err))
    {
        out.text = err;
        return out;
    }

    int ti = 0, si = 0;
    if (!resolvePatchTarget(engine, args, ti, si, err))
    {
        out.text = err;
        return out;
    }

    const QString id = args.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
    {
        out.text = QStringLiteral("id required");
        return out;
    }

    HDAW::ChainPreset preset =
        HDAW::ChainLibrary::patchLibrary().loadPreset(juce::String(id.toStdString()));
    if (preset.id.isEmpty())
    {
        // An UNKNOWN id is a REFUSAL, never a silent no-op.
        out.text = QStringLiteral("preset not found: ") + id;
        return out;
    }

    juce::String applyError;
    if (!engine.getProjectCommands().applyPatch(ti, si, preset, &applyError))
    {
        out.text = QString::fromStdString(applyError.toStdString());
        return out;
    }

    out.ok = true;
    out.text = QStringLiteral("ok");
    return out;
}

// list_patches: the patch roster. Factory-seeded with the 12 built-in bass
// patches (source "factory", ids "_factory/<Name>.json") plus user patches
// (source "user"); the field mirrors list_fx_chains so the shape cannot drift.
inline PatchOutcome listPatchesToolText(AudioEngine& engine, const QJsonObject& args)
{
    (void) engine;
    PatchOutcome out;
    QString err;
    if (!patchValidateArgs(args, {}, {}, err))
    {
        out.text = err;
        return out;
    }

    QJsonArray arr;
    for (const auto& p : HDAW::ChainLibrary::patchLibrary().listPresets())
    {
        QJsonObject row;
        row["id"] = QString::fromStdString(p.id.toStdString());
        row["name"] = QString::fromStdString(p.name.toStdString());
        row["fxType"] = p.slots.empty()
            ? QString() : QString::fromStdString(p.slots.front().fxType.toStdString());
        row["slotCount"] = static_cast<int>(p.slots.size());
        row["source"] = p.isFactory ? QStringLiteral("factory") : QStringLiteral("user");
        arr.append(row);
    }
    out.ok = true;
    out.text = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return out;
}

} // namespace HDAW

// Restore Qt's `slots` for every TU that includes this header (see the note at
// the top): its empty definition is what keeps a later `public slots:` valid.
#ifdef HDAW_PATCHPRESET_DEFER_QT_SLOTS
#  undef HDAW_PATCHPRESET_DEFER_QT_SLOTS
#  define slots
#endif
