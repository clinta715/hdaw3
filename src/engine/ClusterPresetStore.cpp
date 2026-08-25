// src/engine/ClusterPresetStore.cpp
#include "ClusterPresetStore.h"
#include "common/DebugLog.h"
#include <juce_core/juce_core.h>

namespace HDAW {

namespace {

juce::String limitName(const juce::String& name) {
    // Gate 9: name is stored as-is, length-capped at 200 chars.
    return name.substring(0, (int)ClusterPresetStore::kMaxNameLength);
}

// Library ids <-> JSON array.
juce::Array<juce::var> libraryIdsToVar(const juce::StringArray& ids) {
    juce::Array<juce::var> arr;
    for (const auto& id : ids) arr.add(id);
    return arr;
}

void libraryIdsFromVar(const juce::var& v, juce::StringArray& out) {
    out.clear();
    if (auto* arr = v.getArray()) {
        for (const auto& e : *arr)
            out.add(e.toString());
    }
}

juce::var memberToVar(const ClusterPresetMember& m) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("name", m.name);
    o->setProperty("path", m.path);
    o->setProperty("tags", m.tags);
    o->setProperty("description", m.description);
    o->setProperty("similarity", m.similarity);
    return juce::var(o.get());
}

bool memberFromVar(const juce::var& v, ClusterPresetMember& out) {
    auto* o = v.getDynamicObject();
    if (!o) return false;
    out.name = o->getProperty("name").toString();
    out.path = o->getProperty("path").toString();
    out.tags = o->getProperty("tags").toString();
    out.description = o->getProperty("description").toString();
    out.similarity = (double)o->getProperty("similarity");
    return true;
}

juce::var clusterToVar(const ClusterPresetCluster& c) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    juce::Array<juce::var> members;
    for (const auto& m : c.members) members.add(memberToVar(m));
    o->setProperty("id", c.id);
    o->setProperty("label", c.label);
    o->setProperty("size", (int)c.members.size());
    o->setProperty("members", members);
    return juce::var(o.get());
}

bool clusterFromVar(const juce::var& v, ClusterPresetCluster& out) {
    auto* o = v.getDynamicObject();
    if (!o) return false;
    out.id = o->getProperty("id").toString();
    out.label = o->getProperty("label").toString();
    out.members.clear();
    if (auto* arr = o->getProperty("members").getArray()) {
        for (const auto& e : *arr) {
            ClusterPresetMember m;
            if (memberFromVar(e, m)) out.members.push_back(std::move(m));
        }
    }
    return true;
}

} // namespace

ClusterPresetStore::ClusterPresetStore(const juce::File& librariesDir)
    : presetFile(librariesDir.getChildFile("cluster_presets.json"))
{
    load();
}

void ClusterPresetStore::load() {
    presets.clear();
    if (!presetFile.existsAsFile()) return;

    auto content = presetFile.loadFileAsString();
    if (content.isEmpty()) return;

    juce::var json;
    try {
        json = juce::JSON::parse(content);
    } catch (...) {
        HDAW_LOG("LibraryPresets", "ClusterPresetStore: unparseable JSON in "
                 + presetFile.getFullPathName() + " — starting with an empty store");
        return;
    }
    auto* obj = json.getDynamicObject();
    auto* arr = obj != nullptr ? obj->getProperty("presets").getArray() : nullptr;
    if (arr == nullptr) {
        HDAW_LOG("LibraryPresets", "ClusterPresetStore: no valid 'presets' array in "
                 + presetFile.getFullPathName() + " — starting with an empty store");
        return;
    }
    for (int i = 0; i < arr->size(); ++i) {
        ClusterPreset p;
        if (varToRecord((*arr)[i], p)) presets.push_back(std::move(p));
    }
}

void ClusterPresetStore::save() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& p : presets) arr.add(recordToVar(p));
    root->setProperty("presets", arr);

    presetFile.getParentDirectory().createDirectory();
    const juce::String json = juce::JSON::toString(juce::var(root.get()));

    // Atomic persistence: write the temp file, then move it over the real one.
    auto tmp = presetFile.getSiblingFile("cluster_presets.json.tmp");
    tmp.replaceWithText(json);
    bool moved = tmp.moveFileTo(presetFile);
    if (!moved && presetFile.existsAsFile()) {
        presetFile.deleteFile(); // stale target may block the move
        moved = tmp.moveFileTo(presetFile);
    }
    if (!moved) {
        HDAW_LOG("LibraryPresets", "ClusterPresetStore: atomic save failed for "
                 + presetFile.getFullPathName() + " (in-memory state kept)");
    }
}

std::vector<ClusterPresetSummary> ClusterPresetStore::list() const {
    std::vector<ClusterPresetSummary> out;
    out.reserve(presets.size());
    for (const auto& p : presets) {
        ClusterPresetSummary s;
        s.id = p.id;
        s.name = p.name;
        s.createdAt = p.createdAt;
        s.libraryIds = p.libraryIds;
        s.method = p.method;
        s.k = p.k;
        s.clusterId = p.clusterId;
        s.clusterCount = (int)p.clusters.size();
        s.entryCount = p.entryCount;
        out.push_back(std::move(s));
    }
    return out;
}

bool ClusterPresetStore::get(const juce::String& id, ClusterPreset& out) const {
    for (const auto& p : presets) {
        if (p.id == id) {
            out = p;
            return true;
        }
    }
    (void)out;
    return false;
}

juce::String ClusterPresetStore::upsert(ClusterPreset record) {
    record.name = limitName(record.name);

    // entryCount is always truthful: recomputed from the snapshot.
    int count = 0;
    for (const auto& c : record.clusters) count += (int)c.members.size();
    count += (int)record.unassigned.size();
    record.entryCount = count;

    if (record.id.isEmpty()) {
        record.id = generateId();
    } else {
        // Existing id -> replace in place.
        for (auto& p : presets) {
            if (p.id == record.id) {
                p = std::move(record);
                save();
                return p.id;
            }
        }
        // Unknown explicit id: keep it (push below).
    }

    presets.push_back(std::move(record));
    const juce::String id = presets.back().id;
    save();
    return id;
}

bool ClusterPresetStore::remove(const juce::String& id, juce::String& error) {
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        if (it->id == id) {
            presets.erase(it);
            save();
            return true;
        }
    }
    error = "preset not found: " + id;
    return false;
}

juce::String ClusterPresetStore::recordToJson(const ClusterPreset& p) {
    return juce::JSON::toString(recordToVar(p));
}

juce::var ClusterPresetStore::recordToVar(const ClusterPreset& p) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("id", p.id);
    o->setProperty("name", p.name);
    o->setProperty("createdAt", p.createdAt);
    o->setProperty("libraryIds", libraryIdsToVar(p.libraryIds));
    o->setProperty("method", p.method);
    o->setProperty("k", p.k);
    // clusterId null = whole result was saved (plan §Storage).
    o->setProperty("clusterId", p.clusterId.isEmpty() ? juce::var() : juce::var(p.clusterId));
    juce::Array<juce::var> clusters;
    for (const auto& c : p.clusters) clusters.add(clusterToVar(c));
    o->setProperty("clusters", clusters);
    // unassigned null when single-cluster save; array otherwise (may be empty).
    if (!p.clusterId.isEmpty()) {
        o->setProperty("unassigned", juce::var());
    } else {
        juce::Array<juce::var> unassigned;
        for (const auto& u : p.unassigned) {
            juce::DynamicObject::Ptr uo = new juce::DynamicObject();
            uo->setProperty("name", u.name);
            uo->setProperty("path", u.path);
            unassigned.add(juce::var(uo.get()));
        }
        o->setProperty("unassigned", unassigned);
    }
    o->setProperty("entryCount", p.entryCount);
    return juce::var(o.get());
}

bool ClusterPresetStore::varToRecord(const juce::var& v, ClusterPreset& out) {
    auto* o = v.getDynamicObject();
    if (!o) return false;
    out.id = o->getProperty("id").toString();
    out.name = limitName(o->getProperty("name").toString());
    out.createdAt = o->getProperty("createdAt").toString();
    libraryIdsFromVar(o->getProperty("libraryIds"), out.libraryIds);
    out.method = o->getProperty("method").toString();
    out.k = (int)(double)o->getProperty("k");
    const auto clusterIdVar = o->getProperty("clusterId");
    out.clusterId = (clusterIdVar.isVoid() || clusterIdVar.isUndefined())
                        ? juce::String()
                        : clusterIdVar.toString();
    out.clusters.clear();
    if (auto* arr = o->getProperty("clusters").getArray()) {
        for (const auto& e : *arr) {
            ClusterPresetCluster c;
            if (clusterFromVar(e, c)) out.clusters.push_back(std::move(c));
        }
    }
    out.unassigned.clear();
    const auto unassignedVar = o->getProperty("unassigned");
    if (!unassignedVar.isVoid() && !unassignedVar.isUndefined()) {
        if (auto* arr = unassignedVar.getArray()) {
            for (const auto& e : *arr) {
                ClusterPresetMember m;
                if (memberFromVar(e, m)) out.unassigned.push_back(std::move(m));
            }
        }
    }
    out.entryCount = (int)(double)o->getProperty("entryCount");
    return true;
}

juce::String ClusterPresetStore::generateId() const {
    // "cp_" + 8 lowercase hex, collision-checked against the live set.
    for (int attempt = 0; attempt < 200; ++attempt) {
        auto uuid = juce::Uuid().toString().removeCharacters("-{}").toLowerCase();
        const juce::String id = "cp_" + uuid.substring(0, 8);
        bool collision = false;
        for (const auto& p : presets) {
            if (p.id == id) { collision = true; break; }
        }
        if (!collision) return id;
    }
    // Practically unreachable fallback: timestamp-derived, still unique-shaped.
    return "cp_" + juce::String::toHexString(juce::Time::currentTimeMillis()).toLowerCase().substring(6, 14);
}

} // namespace HDAW
