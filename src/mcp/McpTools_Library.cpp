#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"
#include "../engine/FileLibraryManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

void registerLibraryDomain(McpServer& s, AudioEngine* e)
{
    auto* lib = &e->getFileLibraryManager();

    s.registerTool({"list_libraries", "List all configured file libraries with metadata.",
        objSchema({}),
        "library",
        [lib](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            for (const auto& id : lib->getLibraryIds()) {
                auto info = lib->getLibraryInfo(id);
                arr.append(QJsonObject{
                    {"id", jstr(info.id)},
                    {"name", jstr(info.name)},
                    {"path", jstr(info.path)},
                    {"type", jstr(info.type)},
                    {"lastScan", jstr(info.lastScan)},
                    {"fileCount", info.fileCount},
                    {"autoScan", info.autoScan}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"add_library", "Add a new file library (midi or audio).",
        objSchema({{"name", QJsonObject{{"type","string"}}},
                   {"path", QJsonObject{{"type","string"}}},
                   {"type", QJsonObject{{"type","string"}, {"enum", QJsonArray{"midi","audio"}}}}},
                  {"name","path","type"}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            QString name = a.value("name").toString();
            QString path = a.value("path").toString();
            QString type = a.value("type").toString();
            if (name.isEmpty() || path.isEmpty() || type.isEmpty())
                return McpToolResult::text("name, path, and type are required", true);
            auto id = lib->addLibrary(
                juce::String(name.toUtf8().constData()),
                juce::String(path.toUtf8().constData()),
                juce::String(type.toUtf8().constData()));
            QJsonObject obj{{"id", jstr(id)}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_library", "Remove a file library by id (entries are deleted, source files are untouched).",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            QString id = a.value("id").toString();
            if (id.isEmpty())
                return McpToolResult::text("id is required", true);
            lib->removeLibrary(juce::String(id.toUtf8().constData()));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"scan_library", "Scan a library to (re)index its files. Omit id to scan all libraries.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            QString id = a.value("id").toString();
            if (id.isEmpty())
                lib->scanAll();
            else
                lib->scanLibrary(juce::String(id.toUtf8().constData()));
            return McpToolResult::text("scan started");
        }});

    s.registerTool({"search_library", "Search indexed library entries by text and metadata filters.",
        objSchema({{"query", QJsonObject{{"type","string"}}},
                   {"type", QJsonObject{{"type","string"}}},
                   {"libraryId", QJsonObject{{"type","string"}}},
                   {"durationMin", QJsonObject{{"type","number"}}},
                   {"durationMax", QJsonObject{{"type","number"}}},
                   {"bpmMin", QJsonObject{{"type","number"}}},
                   {"bpmMax", QJsonObject{{"type","number"}}},
                   {"key", QJsonObject{{"type","string"}}},
                   {"offset", QJsonObject{{"type","integer"}}},
                   {"limit", QJsonObject{{"type","integer"}}}}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            auto results = lib->search(
                juce::String(a.value("query").toString().toUtf8().constData()),
                juce::String(a.value("type").toString().toUtf8().constData()),
                juce::String(a.value("libraryId").toString().toUtf8().constData()),
                a.contains("durationMin") ? a.value("durationMin").toDouble() : -1.0,
                a.contains("durationMax") ? a.value("durationMax").toDouble() : -1.0,
                a.contains("bpmMin") ? a.value("bpmMin").toDouble() : -1.0,
                a.contains("bpmMax") ? a.value("bpmMax").toDouble() : -1.0,
                juce::String(a.value("key").toString().toUtf8().constData()),
                a.value("offset").toInt(0),
                a.value("limit").toInt(50));
            QJsonArray arr;
            for (const auto& e : results) {
                QJsonObject obj{
                    {"name", jstr(e.name)},
                    {"path", jstr(e.path)},
                    {"size", static_cast<qint64>(e.size)},
                    {"durationSeconds", e.durationSeconds},
                    {"key", jstr(e.key)}
                };
                if (e.tracks > 0) obj["tracks"] = e.tracks;
                if (e.notes > 0) obj["notes"] = e.notes;
                if (e.sampleRate > 0) obj["sampleRate"] = e.sampleRate;
                if (e.channels > 0) obj["channels"] = e.channels;
                double bpm = e.bpm > 0 ? e.bpm : e.tempo;
                if (bpm > 0) obj["bpm"] = bpm;
                if (e.format.isNotEmpty()) obj["format"] = jstr(e.format);
                if (e.timeSignature.isNotEmpty()) obj["timeSignature"] = jstr(e.timeSignature);
                arr.append(obj);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_library_entry", "Get full metadata for a single indexed entry.",
        objSchema({{"libraryId", QJsonObject{{"type","string"}}},
                   {"path", QJsonObject{{"type","string"}}}},
                  {"libraryId","path"}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            auto entry = lib->getEntry(
                juce::String(a.value("libraryId").toString().toUtf8().constData()),
                juce::String(a.value("path").toString().toUtf8().constData()));
            if (entry.name.isEmpty())
                return McpToolResult::text("entry not found", true);
            QJsonObject obj{
                {"name", jstr(entry.name)},
                {"path", jstr(entry.path)},
                {"size", static_cast<qint64>(entry.size)},
                {"durationSeconds", entry.durationSeconds},
                {"key", jstr(entry.key)},
                {"tracks", entry.tracks},
                {"notes", entry.notes},
                {"sampleRate", entry.sampleRate},
                {"channels", entry.channels},
                {"bpm", entry.bpm > 0 ? entry.bpm : entry.tempo},
                {"format", jstr(entry.format)},
                {"timeSignature", jstr(entry.timeSignature)}
            };
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_library_autoscan", "Enable or disable automatic rescanning for a library.",
        objSchema({{"id", QJsonObject{{"type","string"}}},
                   {"enabled", QJsonObject{{"type","boolean"}}}},
                  {"id","enabled"}),
        "library",
        [lib](const QJsonObject& a) -> McpToolResult {
            QString id = a.value("id").toString();
            if (id.isEmpty())
                return McpToolResult::text("id is required", true);
            bool enabled = a.value("enabled").toBool();
            lib->setAutoScan(juce::String(id.toUtf8().constData()), enabled);
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
