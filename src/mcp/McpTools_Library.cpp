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
                if (e.tags.isNotEmpty()) obj["tags"] = jstr(e.tags);
                if (e.description.isNotEmpty()) obj["description"] = jstr(e.description);
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
            if (entry.tags.isNotEmpty()) obj["tags"] = jstr(entry.tags);
            if (entry.description.isNotEmpty()) obj["description"] = jstr(entry.description);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }});

    // libraryIds helper: array of strings -> juce::StringArray (empty when
    // omitted/empty = ALL audio-type libraries).
    auto libraryIdsFrom = [](const QJsonObject& a) {
        juce::StringArray ids;
        if (a.contains("libraryIds")) {
            const auto arr = a.value("libraryIds").toArray();
            for (const auto& v : arr)
                ids.add(juce::String(v.toString().toUtf8().constData()));
        }
        return ids;
    };

    s.registerTool({"cluster_library",
        "Cluster entries from one or more audio libraries into k groups by timbre "
        "(text tags/description + numeric dsp features from TimbreLib sidecars). "
        "Omit libraryIds to cluster ALL audio libraries. k omitted (0) = auto "
        "(silhouette). method: hybrid (default) | text | dsp.",
        objSchema({{"libraryIds", QJsonObject{{"type","array"},
                    {"items", QJsonObject{{"type","string"}}}}},
                   {"k", QJsonObject{{"type","integer"}}},
                   {"method", QJsonObject{{"type","string"},
                    {"enum", QJsonArray{"hybrid","text","dsp"}}}}}),
        "library",
        [lib, idsFrom = libraryIdsFrom](const QJsonObject& a) -> McpToolResult {
            juce::String error;
            auto outcome = lib->clusterLibrary(
                idsFrom(a),
                a.value("k").toInt(0),
                juce::String(a.value("method").toString("hybrid").toUtf8().constData()),
                error);
            if (error.isNotEmpty())
                return McpToolResult::text(QString::fromUtf8(error.toRawUTF8()), true);

            QJsonObject root;
            root["method"] = jstr(outcome.method);
            root["k"] = outcome.k;
            QJsonArray clustersArr;
            for (const auto& c : outcome.clusters) {
                QJsonArray members;
                for (const auto& m : c.members) {
                    members.append(QJsonObject{
                        {"name", jstr(m.name)},
                        {"path", jstr(m.path)},
                        {"tags", jstr(m.tags)},
                        {"description", jstr(m.description)},
                        {"similarity", m.similarity}
                    });
                }
                clustersArr.append(QJsonObject{
                    {"id", jstr(c.id)},
                    {"label", jstr(c.label)},
                    {"size", static_cast<int>(c.members.size())},
                    {"members", members}
                });
            }
            root["clusters"] = clustersArr;
            QJsonArray unassigned;
            for (const auto& u : outcome.unassigned)
                unassigned.append(QJsonObject{{"name", jstr(u.name)}, {"path", jstr(u.path)}});
            root["unassigned"] = unassigned;
            if (outcome.note.isNotEmpty()) root["note"] = jstr(outcome.note);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"related_samples",
        "Find entries similar to a seed sample (filePath) or to a text query, "
        "within one or more audio libraries — nearest neighbours by timbre "
        "(tags/description + dsp features). Exactly one of filePath or query "
        "is required. limit default 10, max 100.",
        objSchema({{"libraryIds", QJsonObject{{"type","array"},
                    {"items", QJsonObject{{"type","string"}}}}},
                   {"filePath", QJsonObject{{"type","string"}}},
                   {"query", QJsonObject{{"type","string"}}},
                   {"method", QJsonObject{{"type","string"},
                    {"enum", QJsonArray{"hybrid","text","dsp"}}}},
                   {"limit", QJsonObject{{"type","integer"}, {"maximum", 100}}}}),
        "library",
        [lib, idsFrom = libraryIdsFrom](const QJsonObject& a) -> McpToolResult {
            juce::String error;
            auto r = lib->relatedSamples(
                idsFrom(a),
                juce::String(a.value("filePath").toString().toUtf8().constData()),
                juce::String(a.value("query").toString().toUtf8().constData()),
                a.value("limit").toInt(10),
                juce::String(a.value("method").toString("hybrid").toUtf8().constData()),
                error);
            if (error.isNotEmpty())
                return McpToolResult::text(QString::fromUtf8(error.toRawUTF8()), true);

            QJsonObject root;
            root["method"] = jstr(r.method);
            if (r.found)
                root["seed"] = QJsonObject{{"name", jstr(r.seedName)}, {"path", jstr(r.seedPath)}};
            QJsonArray results;
            for (const auto& h : r.results) {
                results.append(QJsonObject{
                    {"name", jstr(h.name)},
                    {"path", jstr(h.path)},
                    {"tags", jstr(h.tags)},
                    {"description", jstr(h.description)},
                    {"similarity", h.similarity}
                });
            }
            root["results"] = results;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact)));
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
