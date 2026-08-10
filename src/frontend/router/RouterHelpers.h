#pragma once

// Param-extraction helpers for the JSON-RPC router.
// Every helper returns true on success and writes the out-param. On failure
// they return false and set *err to a JSON-RPC InvalidParams payload.

#include "../FrontendRpc.h"
#include "../../engine/EnvelopeGenerator.h"

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
