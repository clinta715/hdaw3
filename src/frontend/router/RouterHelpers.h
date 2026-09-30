#pragma once

// Param-extraction helpers for the JSON-RPC router.
// Every helper returns true on success and writes the out-param. On failure
// they return false and set *err to a JSON-RPC InvalidParams payload.

#include "../FrontendRpc.h"
#include "../../engine/EnvelopeGenerator.h"
#include "../../common/StableRefResolve.h"
#include "../../common/JsonInteger.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace frontend::router_helpers {

template <typename T>
bool requireInt(const QJsonObject& o, const char* key, T& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("missing or non-numeric param: ") + key);
        return false;
    }
    const double d = o.value(key).toDouble();
    // A non-integral JSON number must NOT be silently truncated into an id:
    // mcp::validateSchema refuses it for an `{"type":"integer"}` argument with
    // this exact text (McpSchema.cpp's typeMatches), so the RPC surface refuses
    // it too — same bytes when the caller passes `err`, and (with `err` null) it
    // still returns false, so the route refuses instead of mutating id N. The
    // is_integral guard keeps the helper's fractional instantiations intact.
    // The predicate is the ONE shared one (common/JsonInteger.h), same test the
    // stable-ref readers below and the MCP validator use.
    if constexpr (std::is_integral<T>::value) {
        if (!HDAW::isJsonInteger(o.value(key))) {
            if (err) *err = makeError(-32602,
                QString("invalid params: ") + key + QString(": expected integer"));
            return false;
        }
    }
    out = static_cast<T>(d);
    return true;
}

inline bool requireDouble(const QJsonObject& o, const char* key, double& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("missing or non-numeric param: ") + key);
        return false;
    }
    out = o.value(key).toDouble();
    return true;
}

inline bool requireFloat(const QJsonObject& o, const char* key, float& out, DispatchResult* err) {
    double d = 0.0;
    if (!requireDouble(o, key, d, err)) return false;
    out = static_cast<float>(d);
    return true;
}

inline bool requireBool(const QJsonObject& o, const char* key, bool& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isBool()) {
        if (err) *err = makeError(-32602, QString("missing or non-boolean param: ") + key);
        return false;
    }
    out = o.value(key).toBool();
    return true;
}

inline bool requireString(const QJsonObject& o, const char* key, std::string& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isString()) {
        if (err) *err = makeError(-32602, QString("missing or non-string param: ") + key);
        return false;
    }
    out = o.value(key).toString().toStdString();
    return true;
}

// Optional integer argument. `out` is ALWAYS written (the fallback when the key
// is absent), and the call returns false with *err set when the key is PRESENT
// but wrong: a non-number yields "non-numeric param: <key>", a non-integral
// number yields the MCP validator's "invalid params: <key>: expected integer".
// An ABSENT key keeps the tolerant optional behaviour (fallback, no error) —
// pass a real `err` and check the return, or a present-and-wrong value silently
// becomes the fallback (the bug this signature closes).
template <typename T>
bool optInt(const QJsonObject& o, const char* key, T& out, T fallback, DispatchResult* err) {
    out = fallback;
    if (!o.contains(key)) return true;
    if (!o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("non-numeric param: ") + key);
        return false;
    }
    // Same integrality refusal as requireInt: a non-integral JSON number must
    // not be silently truncated into an integer argument. Every instantiation of
    // this helper is genuinely integer-typed (int / uint64_t); the predicate is
    // the shared one so the refusal text mirrors the MCP validator's
    // `{"type":"integer"}` answer.
    if constexpr (std::is_integral<T>::value) {
        if (!HDAW::isJsonInteger(o.value(key))) {
            if (err) *err = makeError(-32602,
                QString("invalid params: ") + key + QString(": expected integer"));
            return false;
        }
    }
    out = static_cast<T>(o.value(key).toDouble());
    return true;
}

inline double optDouble(const QJsonObject& o, const char* key, double fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("non-numeric param: ") + key);
        return fallback;
    }
    return o.value(key).toDouble();
}

inline float optFloat(const QJsonObject& o, const char* key, float fallback, DispatchResult* err) {
    return static_cast<float>(optDouble(o, key, static_cast<double>(fallback), err));
}

inline bool optBool(const QJsonObject& o, const char* key, bool fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isBool()) {
        if (err) *err = makeError(-32602, QString("non-boolean param: ") + key);
        return fallback;
    }
    return o.value(key).toBool();
}

inline std::string optString(const QJsonObject& o, const char* key, std::string fallback) {
    if (!o.contains(key) || !o.value(key).isString()) return fallback;
    return o.value(key).toString().toStdString();
}

inline QJsonObject paramsObject(const QJsonValue& params) {
    return params.isObject() ? params.toObject() : QJsonObject{};
}

// ── Stable-id arguments (design B2) ─────────────────────────────────────────
// The RPC half of common/StableRefResolve.h's rule, and deliberately nothing
// more: read the two keys with contains() (so an explicit `trackId: 0` stays a
// real positional argument — presence is never inferred from the value), hand
// the two numbers to the shared resolver, and let THAT header word the failure.
// The MCP twin (mcp/McpArgs.h) reads the same key names off the same
// QJsonObject, so one id-holding caller sees one behaviour and one error text on
// both surfaces (the twin tests compare the messages verbatim).
//
// The key names come from HDAW::k*RefKeys, so a spelling changed on one side
// alone fails the twin tests instead of silently resolving garbage.
//
// Shape and habits are requireInt's (bool + out-param + *err): a route body
// changes by one call, not by a block. On failure *err is the -32602 payload
// whose `message` is the shared resolver's text; pass a real `err` — with
// nullptr the caller's own literal would mask the id the failure has to name.

// The two numbers one resolution needs, plus whether the STABLE key was sent at
// all, or the error for a non-numeric key. `stablePresent` is what lets
// `trackID: 0` be answered "unknown trackID 0" instead of the positional half's
// "trackId required" (P1-c): presence is `contains()`, never the value.
inline HDAW::StableRefResult refArgs(const QJsonObject& o, HDAW::StableRefKeys keys,
                                     int& index, int& stableID, bool& stablePresent) {
    index = HDAW::kNoRef;
    stableID = 0;
    stablePresent = false;
    if (o.contains(keys.index)) {
        // requireInt's exact wording: the positional half of this check predates
        // B2 and its message is part of the surface. A numeric-but-NON-INTEGRAL
        // value is refused with the MCP validator's text (the shared
        // isJsonInteger predicate) instead of being truncated onto index N —
        // the same wrong-track hazard requireInt closes, one layer up.
        if (!o.value(keys.index).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.index);
        if (!HDAW::isJsonInteger(o.value(keys.index)))
            return HDAW::stableRefError(std::string("invalid params: ") + keys.index
                                        + ": expected integer");
        index = static_cast<int>(o.value(keys.index).toDouble());
    }
    if (o.contains(keys.stable)) {
        if (!o.value(keys.stable).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.stable);
        if (!HDAW::isJsonInteger(o.value(keys.stable)))
            return HDAW::stableRefError(std::string("invalid params: ") + keys.stable
                                        + ": expected integer");
        stableID = static_cast<int>(o.value(keys.stable).toDouble());
        stablePresent = true;
    }
    return {};
}

// `trackId` (index) / `trackID` (stable id) → the TRACK_LIST position to act on.
// A folder target resolves through the same call with HDAW::kFolderRefKeys.
inline bool trackIndexArg(const QJsonObject& o, const juce::ValueTree& trackList, int& out,
                          DispatchResult* err, HDAW::StableRefKeys keys = HDAW::kTrackRefKeys) {
    int index = HDAW::kNoRef, stableID = 0;
    bool stablePresent = false;
    const auto read = refArgs(o, keys, index, stableID, stablePresent);
    const auto r = read.ok ? HDAW::resolveTrackRef(trackList, index, stableID, keys, stablePresent)
                           : read;
    if (!r.ok) {
        if (err) *err = makeError(-32602, QString::fromStdString(r.error));
        return false;
    }
    out = r.index;
    return true;
}

// `sendIndex` / `sendID` → the SEND_LIST position inside the already-resolved
// track `trackIndex`.
inline bool sendIndexArg(const QJsonObject& o, const juce::ValueTree& trackList, int trackIndex,
                         int& out, DispatchResult* err,
                         HDAW::StableRefKeys keys = HDAW::kSendRefKeys) {
    int index = HDAW::kNoRef, stableID = 0;
    bool stablePresent = false;
    const auto read = refArgs(o, keys, index, stableID, stablePresent);
    const auto r = read.ok
        ? HDAW::resolveSendRef(trackList, trackIndex, index, stableID, keys, stablePresent)
        : read;
    if (!r.ok) {
        if (err) *err = makeError(-32602, QString::fromStdString(r.error));
        return false;
    }
    out = r.index;
    return true;
}

inline std::optional<HDAW::EnvelopeGenerator::Shape> parseShape(const std::string& s) {
    using S = HDAW::EnvelopeGenerator::Shape;
    if (s == "ramp") return S::Ramp;
    if (s == "adsr") return S::ADSR;
    if (s == "sine") return S::Sine;
    if (s == "triangle") return S::Triangle;
    if (s == "saw") return S::Saw;
    if (s == "square") return S::Square;
    if (s == "pulse") return S::Pulse;
    if (s == "staircase") return S::Staircase;
    if (s == "sCurve") return S::SCurve;
    if (s == "randomWalk") return S::RandomWalk;
    if (s == "noise") return S::Noise;
    return std::nullopt;
}

inline std::vector<double> toDoubleVector(const QJsonValue& v, DispatchResult* err) {
    std::vector<double> out;
    if (!v.isArray()) {
        if (err) *err = makeError(-32602, "expected an array of numbers");
        return out;
    }
    for (const auto& e : v.toArray()) {
        if (!e.isDouble()) {
            if (err) *err = makeError(-32602, "array element is not a number");
            return {};
        }
        out.push_back(e.toDouble());
    }
    return out;
}

} // namespace frontend::router_helpers
