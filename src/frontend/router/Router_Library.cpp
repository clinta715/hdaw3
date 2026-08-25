#include "Router_Library.h"
#include "RouterHelpers.h"
#include "../../engine/FileLibraryManager.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

using namespace frontend::router_helpers;

namespace frontend {

// juce::String -> QString (UTF-8). Used consistently for all library fields.
static inline QString qstr(const juce::String& s) { return QString::fromUtf8(s.toRawUTF8()); }

DispatchResult dispatchLibrary(HDAW::FileLibraryManager& lib, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    if (m == "list") {
        QJsonArray arr;
        for (const auto& id : lib.getLibraryIds()) {
            auto info = lib.getLibraryInfo(id);
            arr.append(QJsonObject{
                {"id", qstr(info.id)},
                {"name", qstr(info.name)},
                {"path", qstr(info.path)},
                {"type", qstr(info.type)},
                {"lastScan", qstr(info.lastScan)},
                {"fileCount", info.fileCount},
                {"autoScan", info.autoScan}
            });
        }
        return { false, arr };
    }

    if (m == "add") {
        std::string name, path, type;
        if (!requireString(o, "name", name, nullptr)) return makeError(-32602, "name required");
        if (!requireString(o, "path", path, nullptr)) return makeError(-32602, "path required");
        if (!requireString(o, "type", type, nullptr)) return makeError(-32602, "type required");
        if (type != "midi" && type != "audio") return makeError(-32602, "type must be 'midi' or 'audio'");
        auto id = lib.addLibrary(juce::String(name), juce::String(path), juce::String(type));
        return { false, QJsonObject{ {"id", qstr(id)} } };
    }

    if (m == "remove") {
        std::string id;
        if (!requireString(o, "id", id, nullptr) || id.empty())
            return makeError(-32602, "id required");
        lib.removeLibrary(juce::String(id));
        return { false, QJsonValue::Null };
    }

    if (m == "scan") {
        std::string id = optString(o, "id", {});
        if (id.empty()) lib.scanAll();
        else            lib.scanLibrary(juce::String(id));
        return { false, QJsonValue::Null };
    }

    if (m == "search") {
        std::string q      = optString(o, "query", {});
        std::string tFlt   = optString(o, "type", {});
        std::string libFlt = optString(o, "libraryId", {});
        std::string keyFlt = optString(o, "key", {});
        double durMin = optDouble(o, "durationMin", -1.0, nullptr);
        double durMax = optDouble(o, "durationMax", -1.0, nullptr);
        double bpmMin = optDouble(o, "bpmMin", -1.0, nullptr);
        double bpmMax = optDouble(o, "bpmMax", -1.0, nullptr);
        int offset = optInt(o, "offset", 0, nullptr);
        int limit  = optInt(o, "limit", 50, nullptr);

        auto results = lib.search(juce::String(q), juce::String(tFlt), juce::String(libFlt),
                                  durMin, durMax, bpmMin, bpmMax, juce::String(keyFlt), offset, limit);
        QJsonArray arr;
        for (const auto& e : results) {
            QJsonObject obj;
            obj["name"] = qstr(e.name);
            obj["path"] = qstr(e.path);
            obj["size"] = static_cast<qint64>(e.size);
            obj["durationSeconds"] = e.durationSeconds;
            obj["key"] = qstr(e.key);
            if (e.tracks > 0) obj["tracks"] = e.tracks;
            if (e.notes > 0) obj["notes"] = e.notes;
            if (e.sampleRate > 0) obj["sampleRate"] = e.sampleRate;
            if (e.channels > 0) obj["channels"] = e.channels;
            double bpm = e.bpm > 0 ? e.bpm : e.tempo;
            if (bpm > 0) obj["bpm"] = bpm;
            if (e.format.isNotEmpty()) obj["format"] = qstr(e.format);
            if (e.timeSignature.isNotEmpty()) obj["timeSignature"] = qstr(e.timeSignature);
            if (e.tags.isNotEmpty()) obj["tags"] = qstr(e.tags);
            if (e.description.isNotEmpty()) obj["description"] = qstr(e.description);
            arr.append(obj);
        }
        return { false, arr };
    }

    if (m == "getEntry") {
        std::string libId, path;
        if (!requireString(o, "libraryId", libId, nullptr)) return makeError(-32602, "libraryId required");
        if (!requireString(o, "path", path, nullptr))       return makeError(-32602, "path required");
        auto entry = lib.getEntry(juce::String(libId), juce::String(path));
        if (entry.name.isEmpty())
            return makeError(-32601, "entry not found");
        QJsonObject obj;
        obj["name"] = qstr(entry.name);
        obj["path"] = qstr(entry.path);
        obj["size"] = static_cast<qint64>(entry.size);
        obj["durationSeconds"] = entry.durationSeconds;
        obj["key"] = qstr(entry.key);
        obj["tracks"] = entry.tracks;
        obj["notes"] = entry.notes;
        obj["sampleRate"] = entry.sampleRate;
        obj["channels"] = entry.channels;
        obj["bpm"] = entry.bpm > 0 ? entry.bpm : entry.tempo;
        obj["format"] = qstr(entry.format);
        obj["timeSignature"] = qstr(entry.timeSignature);
        if (entry.tags.isNotEmpty()) obj["tags"] = qstr(entry.tags);
        if (entry.description.isNotEmpty()) obj["description"] = qstr(entry.description);
        return { false, obj };
    }

    if (m == "cluster") {
        // library.cluster — same params/shape as the MCP cluster_library tool.
        juce::StringArray libraryIds;
        if (o.contains("libraryIds")) {
            const auto arr = o.value("libraryIds").toArray();
            for (const auto& v : arr)
                libraryIds.add(juce::String(v.toString().toStdString()));
        }
        const int k = optInt(o, "k", 0, nullptr);
        const std::string method = optString(o, "method", "hybrid");
        juce::String error;
        auto outcome = lib.clusterLibrary(libraryIds, k, juce::String(method), error);
        if (error.isNotEmpty())
            return makeError(-32602, qstr(error));

        QJsonObject root;
        root["method"] = qstr(outcome.method);
        root["k"] = outcome.k;
        QJsonArray clustersArr;
        for (const auto& c : outcome.clusters) {
            QJsonArray members;
            for (const auto& mem : c.members) {
                members.append(QJsonObject{
                    {"name", qstr(mem.name)},
                    {"path", qstr(mem.path)},
                    {"tags", qstr(mem.tags)},
                    {"description", qstr(mem.description)},
                    {"similarity", mem.similarity}
                });
            }
            clustersArr.append(QJsonObject{
                {"id", qstr(c.id)},
                {"label", qstr(c.label)},
                {"size", static_cast<int>(c.members.size())},
                {"members", members}
            });
        }
        root["clusters"] = clustersArr;
        QJsonArray unassigned;
        for (const auto& u : outcome.unassigned)
            unassigned.append(QJsonObject{{"name", qstr(u.name)}, {"path", qstr(u.path)}});
        root["unassigned"] = unassigned;
        if (outcome.note.isNotEmpty()) root["note"] = qstr(outcome.note);
        return { false, root };
    }

    if (m == "related") {
        // library.related — same params/shape as the MCP related_samples tool.
        juce::StringArray libraryIds;
        if (o.contains("libraryIds")) {
            const auto arr = o.value("libraryIds").toArray();
            for (const auto& v : arr)
                libraryIds.add(juce::String(v.toString().toStdString()));
        }
        const std::string filePath = optString(o, "filePath", {});
        const std::string query = optString(o, "query", {});
        const int limit = optInt(o, "limit", 10, nullptr);
        const std::string method = optString(o, "method", "hybrid");
        juce::String error;
        auto r = lib.relatedSamples(libraryIds, juce::String(filePath), juce::String(query),
                                    limit, juce::String(method), error);
        if (error.isNotEmpty()) {
            const int code = error.startsWith("entry not found") ? -32601 : -32602;
            return makeError(code, qstr(error));
        }

        QJsonObject root;
        root["method"] = qstr(r.method);
        if (r.found)
            root["seed"] = QJsonObject{{"name", qstr(r.seedName)}, {"path", qstr(r.seedPath)}};
        QJsonArray results;
        for (const auto& h : r.results) {
            results.append(QJsonObject{
                {"name", qstr(h.name)},
                {"path", qstr(h.path)},
                {"tags", qstr(h.tags)},
                {"description", qstr(h.description)},
                {"similarity", h.similarity}
            });
        }
        root["results"] = results;
        return { false, root };
    }

    if (m == "setAutoScan") {
        std::string id;
        if (!requireString(o, "id", id, nullptr) || id.empty())
            return makeError(-32602, "id required");
        bool enabled = optBool(o, "enabled", false, nullptr);
        lib.setAutoScan(juce::String(id), enabled);
        return { false, QJsonValue::Null };
    }

    return makeError(-32601, "unknown library method: " + m);
}

} // namespace frontend
