#pragma once

// Param-extraction helpers for the JSON-RPC router.
// Every helper returns true on success and writes the out-param. On failure
// they return false and set *err to a JSON-RPC InvalidParams payload.

#include "../FrontendRpc.h"
#include "../../engine/EnvelopeGenerator.h"
#include "../../common/StableRefResolve.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace frontend::router_helpers {

template <typename T>
bool requireInt(const QJsonObject& o, const char* key, T& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("missing or non-numeric param: ") + key);
        return false;
    }
    out = static_cast<T>(o.value(key).toDouble());
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

template <typename T>
T optInt(const QJsonObject& o, const char* key, T fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("non-numeric param: ") + key);
        return fallback;
    }
    return static_cast<T>(o.value(key).toDouble());
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

// The two numbers one resolution needs, or the error for a non-numeric key.
inline HDAW::StableRefResult refArgs(const QJsonObject& o, HDAW::StableRefKeys keys,
                                     int& index, int& stableID) {
    index = HDAW::kNoRef;
    stableID = 0;
    if (o.contains(keys.index)) {
        // requireInt's exact wording: the positional half of this check predates
        // B2 and its message is part of the surface.
        if (!o.value(keys.index).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.index);
        index = static_cast<int>(o.value(keys.index).toDouble());
    }
    if (o.contains(keys.stable)) {
        if (!o.value(keys.stable).isDouble())
            return HDAW::stableRefError(std::string("missing or non-numeric param: ") + keys.stable);
        stableID = static_cast<int>(o.value(keys.stable).toDouble());
    }
    return {};
}

// `trackId` (index) / `trackID` (stable id) → the TRACK_LIST position to act on.
// A folder target resolves through the same call with HDAW::kFolderRefKeys.
inline bool trackIndexArg(const QJsonObject& o, const juce::ValueTree& trackList, int& out,
                          DispatchResult* err, HDAW::StableRefKeys keys = HDAW::kTrackRefKeys) {
    int index = HDAW::kNoRef, stableID = 0;
    const auto read = refArgs(o, keys, index, stableID);
    const auto r = read.ok ? HDAW::resolveTrackRef(trackList, index, stableID, keys) : read;
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
    const auto read = refArgs(o, keys, index, stableID);
    const auto r = read.ok ? HDAW::resolveSendRef(trackList, trackIndex, index, stableID, keys) : read;
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
