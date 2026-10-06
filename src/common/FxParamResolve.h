#pragma once
// THE ONE parameter-address resolver for an FX slot — slice 3 of
// docs/plans/2026-10-05-param-batch-and-bugfixes.md.
//
// The engine's ONE write path (AudioEngineCommands::writeFxParam, slice 1) used
// to resolve `paramIndex` > `paramName` > `intent` inline, while the single MCP
// tools (set_fx_param / set_internal_fx_param) resolved it a second time in
// their handlers. This header lifts that resolution out so there is exactly ONE
// implementation: writeFxParam AND the single tools call it. The resolver is
// PURE — it reads no engine, touches no ValueTree and writes nothing; it maps a
// slot's address space (its internal param defs, or the live plugin param cache)
// plus the write's addressing fields to a CODE + the resolved param index.
//
// Precedence is kept LITERAL (the single tools' documented order): an explicit
// `paramName` beats `paramIndex`, and `intent` is consulted only when neither is
// given. An internal slot resolves `intent` through the SAME shared
// HDAW::resolveInternalFxIntent the RPC route uses; a plugin slot never consults
// an intent (there is no Device Parameter Map to resolve it against — the caller
// reports HDAW::pluginIntentRefusalText() before reaching here).
//
// CODES map 1:1 to the existing message families, so each surface renders its
// own text and NOTHING in this header decides wording:
//   ok               -> the address resolved (paramIndex set)
//   paramRequired    -> "paramIndex, paramName or intent required"
//   unknownParamName -> "unknown paramName: <name>"   (caller supplies the name)
//   indexOutOfRange  -> "param index out of range"
//   intentError      -> `error` carries HDAW::resolveInternalFxIntent's text
// Header-only (all inline) — no CMake source registration, same as
// ParamVerity.h / IntentResolve.h / FxParamBatchJson.h.

#include "IntentResolve.h"          // resolveInternalFxIntent (the ONE intent resolver)
#include "ParamVerity.h"            // paramIndexByName (the ONE internal name match)
#include "PluginParamService.h"     // PluginParamSnapshot (the live plugin param row)
#include "../engine/TrackFXSlot.h"  // TrackFXSlot::InternalParamDef

#include <QString>

#include <string>
#include <vector>

namespace HDAW {

// How one write addresses its parameter. `hasParamIndex` records whether the
// `paramIndex` key was SUPPLIED at all — a presence test, never a value test
// (an explicit paramIndex 0 is a real address; the B2 rule at the param layer).
struct FxParamAddress
{
    bool hasParamIndex = false;
    int paramIndex = -1;
    std::string paramName;   // may be empty
    std::string intent;      // may be empty; internal slots only (see above)
};

enum class FxParamResolveCode
{
    ok,
    paramRequired,
    unknownParamName,
    indexOutOfRange,
    intentError,
};

struct FxParamResolution
{
    FxParamResolveCode code = FxParamResolveCode::ok;
    int paramIndex = -1;   // set iff code == ok
    std::string error;     // set iff code == intentError
};

// No addressing argument at all -> the caller's "required" refusal (checked by
// both writeFxParam and the single tools BEFORE they reach the slot-kind gates).
inline bool fxParamAddressEmpty(const FxParamAddress& a)
{
    return !a.hasParamIndex && a.paramName.empty() && a.intent.empty();
}

// Resolve one write against an INTERNAL slot's param defs (`defs` is the
// fxType's table from TrackFXSlot::getParamDefsForType; it MAY be empty for an
// fxType with no table — the caller decides what an empty table means).
inline FxParamResolution resolveInternalFxParam(const std::string& fxType,
                                                const std::vector<TrackFXSlot::InternalParamDef>& defs,
                                                const FxParamAddress& a)
{
    FxParamResolution r;
    if (fxParamAddressEmpty(a))
    {
        r.code = FxParamResolveCode::paramRequired;
        return r;
    }
    int pi = a.paramIndex;
    if (!a.paramName.empty())
    {
        // paramName beats paramIndex (the single tools' documented order).
        pi = paramIndexByName(defs, QString::fromStdString(a.paramName));
        if (pi < 0)
        {
            r.code = FxParamResolveCode::unknownParamName;
            return r;
        }
    }
    else if (!a.hasParamIndex && !a.intent.empty())
    {
        // intent is consulted only when neither paramIndex nor paramName is
        // given — the shared resolver, the same text the single tools report.
        const IntentResolution res = resolveInternalFxIntent(fxType, a.intent);
        if (!res.ok)
        {
            r.code = FxParamResolveCode::intentError;
            r.error = res.error;
            return r;
        }
        pi = res.paramIndex;
    }
    if (pi < 0 || pi >= static_cast<int>(defs.size()))
    {
        r.code = FxParamResolveCode::indexOutOfRange;
        return r;
    }
    r.code = FxParamResolveCode::ok;
    r.paramIndex = pi;
    return r;
}

// Resolve one write against a PLUGIN slot's live host-param cache (`params` is
// PluginParamService::getParams; an EMPTY list means no live instance — the
// index is then accepted without a range check, the single tools' contract).
// `intent` is ignored here on purpose: a plugin intent refusal is the caller's
// up-front gate.
inline FxParamResolution resolvePluginFxParam(const std::vector<PluginParamSnapshot>& params,
                                              const FxParamAddress& a)
{
    FxParamResolution r;
    if (fxParamAddressEmpty(a))
    {
        r.code = FxParamResolveCode::paramRequired;
        return r;
    }
    int pi = a.paramIndex;
    if (!a.paramName.empty())
    {
        pi = -1;
        const QString want = QString::fromStdString(a.paramName);
        for (const auto& p : params)
            if (QString::fromStdString(p.name).compare(want, Qt::CaseInsensitive) == 0)
            {
                pi = p.index;
                break;
            }
        if (pi < 0)
        {
            r.code = FxParamResolveCode::unknownParamName;
            return r;
        }
    }
    // Bounds: the live param list is authoritative when the instance resolves.
    // An EMPTY list means no live instance (deviceless, or not settled yet): the
    // write is then persisted for the offline replay without a range check.
    if (pi < 0 || (!params.empty() && pi >= static_cast<int>(params.size())))
    {
        r.code = FxParamResolveCode::indexOutOfRange;
        return r;
    }
    r.code = FxParamResolveCode::ok;
    r.paramIndex = pi;
    return r;
}

// Render one resolution's message-family text. `paramName` supplies the
// data-dependent unknown-name row; an intent refusal carries its own text in
// `r.error`. Returns "" for `ok` (a resolved address is not an error).
inline std::string fxParamResolveErrorText(FxParamResolveCode code,
                                           const FxParamResolution& r,
                                           const std::string& paramName)
{
    switch (code)
    {
        case FxParamResolveCode::paramRequired:    return "paramIndex, paramName or intent required";
        case FxParamResolveCode::unknownParamName: return "unknown paramName: " + paramName;
        case FxParamResolveCode::intentError:      return r.error;
        case FxParamResolveCode::indexOutOfRange:  return "param index out of range";
        case FxParamResolveCode::ok:               break;
    }
    return {};
}

} // namespace HDAW
