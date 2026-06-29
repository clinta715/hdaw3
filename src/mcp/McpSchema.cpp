#include "McpSchema.h"
#include <QJsonArray>

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

static std::optional<SchemaError> validateInner(const QJsonValue& v, const QJsonObject& s,
                                                const QString& path) {
    if (s.contains("type")) {
        auto t = s.value("type").toString();
        if (!typeMatches(v, t)) return SchemaError{path, "expected " + t};
    }
    if (s.contains("enum")) {
        auto e = s.value("enum").toArray();
        bool found = false;
        for (const auto& ev : e) {
            if (ev == v) { found = true; break; }
        }
        if (!found) return SchemaError{path, "value not in enum"};
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
