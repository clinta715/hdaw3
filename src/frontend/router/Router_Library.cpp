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
        return { false, obj };
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
