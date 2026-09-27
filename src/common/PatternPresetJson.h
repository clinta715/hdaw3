#pragma once
// B1: the ONE pattern-preset payload shaper for load_pattern (MCP) and
// composition.loadPattern (JSON-RPC) — identical object by construction
// (AGENTS.md parity rule; the two hand-rolled copies had already drifted:
// the RPC route dropped category/author/createdAt).
//
// Managed fields come from PatternPreset; preserved extras (notes/role/
// descriptor/... — the PatternLibrary B1 passthrough) merge at TOP level so
// an imported document reads back in its original shape.
#include "../engine/PatternLibrary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace HDAW {

inline QJsonObject patternPresetToJson(const PatternPreset& preset)
{
    QJsonObject obj;
    obj["name"] = QString::fromStdString(preset.name.toStdString());
    obj["style"] = QString::fromStdString(preset.style.toStdString());
    obj["category"] = QString::fromStdString(preset.category.toStdString());
    obj["description"] = QString::fromStdString(preset.description.toStdString());
    obj["author"] = QString::fromStdString(preset.author.toStdString());
    obj["createdAt"] = QString::fromStdString(preset.createdAt.toStdString());
    if (preset.paramsJson.isNotEmpty())
    {
        auto doc = QJsonDocument::fromJson(preset.paramsJson.toRawUTF8());
        if (doc.isObject()) obj["params"] = doc.object();
    }
    if (preset.styleParamsJson.isNotEmpty())
    {
        auto doc = QJsonDocument::fromJson(preset.styleParamsJson.toRawUTF8());
        if (doc.isObject()) obj["styleParams"] = doc.object();
    }
    QJsonArray tags;
    for (const auto& t : preset.tags)
        tags.append(QString::fromStdString(t.toStdString()));
    obj["tags"] = tags;

    // Preserved extras merge at top level; managed keys above always win.
    if (preset.extraJson.isNotEmpty())
    {
        const auto extras = QJsonDocument::fromJson(preset.extraJson.toRawUTF8()).object();
        for (auto it = extras.begin(); it != extras.end(); ++it)
            if (! obj.contains(it.key()))
                obj[it.key()] = it.value();
    }
    return obj;
}

} // namespace HDAW
