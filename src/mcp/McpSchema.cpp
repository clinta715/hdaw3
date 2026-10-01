#include "McpSchema.h"
#include <QJsonArray>
#include <QStringList>

namespace mcp {

static bool typeMatches(const QJsonValue& v, const QString& t) {
    if (t == "string")  return v.isString();
    if (t == "number")  return v.isDouble();
    if (t == "integer") return v.isDouble() && (v.toDouble() == static_cast<double>(static_cast<qint64>(v.toDouble())));
    if (t == "boolean") return v.isBool();
    if (t == "array")   return v.isArray();
    if (t == "object")  return v.isObject();
    if (t == "null")    return v.isNull();
    return true;
}

// The offending scalar rendered for refusal text: strings quoted, the rest bare.
static QString enumValueText(const QJsonValue& v) {
    if (v.isString()) return "\"" + v.toString() + "\"";
    if (v.isBool())   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isNull())   return QStringLiteral("null");
    if (v.isArray())  return QStringLiteral("[array]");
    if (v.isObject()) return QStringLiteral("[object]");
    return QStringLiteral("undefined");
}

static std::optional<SchemaError> validateInner(const QJsonValue& v, const QJsonObject& s,
                                                const QString& path) {
    if (s.contains("type")) {
        const auto tv = s.value("type");
        if (tv.isArray()) {
            // Standard JSON-Schema union: "type": ["object", "string"].
            QStringList names, hits;
            for (const auto& t : tv.toArray()) {
                const QString name = t.toString();
                names << name;
                if (typeMatches(v, name)) hits << name;
            }
            if (hits.isEmpty())
                return SchemaError{path, "expected one of " + names.join(", ")};
        } else {
            auto t = tv.toString();
            if (!typeMatches(v, t)) return SchemaError{path, "expected " + t};
        }
    }
    if (s.contains("enum")) {
        auto e = s.value("enum").toArray();
        bool found = false;
        for (const auto& ev : e) {
            if (ev == v) { found = true; break; }
        }
        if (!found) {
            // The refusal names the offending value AND the allowed set — the
            // set comes from THIS schema (the one source of truth the caller
            // passed), never a hand-copied list.
            QString allowed;
            for (const auto& ev : e) {
                if (!allowed.isEmpty()) allowed += ", ";
                allowed += enumValueText(ev);
            }
            return SchemaError{path, "value " + enumValueText(v)
                                     + " not in enum (allowed: " + allowed + ")"};
        }
    }
    if (v.isDouble() && (s.contains("minimum") || s.contains("maximum"))) {
        double d = v.toDouble();
        if (s.contains("minimum") && d < s.value("minimum").toDouble())
            return SchemaError{path, "value below minimum"};
        if (s.contains("maximum") && d > s.value("maximum").toDouble())
            return SchemaError{path, "value above maximum"};
    }
    if (v.isObject()) {
        auto o = v.toObject();
        if (s.contains("properties")) {
            auto props = s.value("properties").toObject();
            if (s.value("additionalProperties").toBool(false) == false) {
                for (auto it = o.begin(); it != o.end(); ++it) {
                    if (!props.contains(it.key()))
                        return SchemaError{path.isEmpty() ? it.key() : path + "." + it.key(),
                                           "unknown property"};
                }
            }
            for (auto it = props.begin(); it != props.end(); ++it) {
                auto sub = o.value(it.key());
                if (sub.isUndefined() || sub.isNull()) continue;
                auto err = validateInner(sub, it.value().toObject(),
                                         path.isEmpty() ? it.key() : path + "." + it.key());
                if (err) return err;
            }
        }
        if (s.contains("required")) {
            for (const auto& r : s.value("required").toArray()) {
                if (!o.contains(r.toString())) {
                    QString childPath = path.isEmpty() ? r.toString() : path + "." + r.toString();
                    return SchemaError{childPath, "missing required property '" + r.toString() + "'"};
                }
            }
        }
    }
    if (v.isArray() && s.contains("items")) {
        auto a = v.toArray();
        for (int i = 0; i < a.size(); ++i) {
            auto err = validateInner(a[i], s.value("items").toObject(),
                                     path + "[" + QString::number(i) + "]");
            if (err) return err;
        }
    }
    return std::nullopt;
}

std::optional<SchemaError> validateSchema(const QJsonValue& value, const QJsonObject& schema) {
    return validateInner(value, schema, QString{});
}

} // namespace mcp
