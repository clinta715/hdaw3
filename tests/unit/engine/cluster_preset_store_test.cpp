// tests/unit/engine/cluster_preset_store_test.cpp
// ClusterPresetStoreTest — save/list/get/delete round-trip, persistence across
// re-instantiation, corruption tolerance. docs/plans/2026-08-25-cluster-presets.md, G2.

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/ClusterPresetStore.h"

#include <algorithm>

namespace {

juce::File makeLibrariesDir(juce::File& tempDir) {
    auto libs = tempDir.getChildFile("libraries");
    libs.createDirectory();
    return libs;
}

HDAW::ClusterPreset makePreset(const juce::String& id, const juce::String& name) {
    HDAW::ClusterPreset p;
    p.id = id;
    p.name = name;
    p.createdAt = "2026-08-25T12:00:00Z";
    p.libraryIds = juce::StringArray{"libA", "libB"};
    p.method = "hybrid";
    p.k = 3;
    // clusterId empty -> whole-result save (unassigned serialized as array).

    HDAW::ClusterPresetCluster c1;
    c1.id = "c1";
    c1.label = "dark";
    {
        HDAW::ClusterPresetMember m;
        m.name = "dark1.wav"; m.path = "C:/sounds/dark1.wav";
        m.tags = "dark, pad"; m.description = "A dark pad.";
        m.similarity = 0.937654321; // non-trivial double — round-trip must survive
        c1.members.push_back(m);
        m.name = "dark2.wav"; m.path = "C:/sounds/dark2.wav";
        m.tags = "dark, drone"; m.description = "A dark drone.";
        m.similarity = 0.9123456789;
        c1.members.push_back(m);
    }
    HDAW::ClusterPresetCluster c2;
    c2.id = "c2";
    c2.label = "bright";
    {
        HDAW::ClusterPresetMember m;
        m.name = "bright1.wav"; m.path = "C:/sounds/bright1.wav";
        m.tags = "bright, chime"; m.description = "A bright chime.";
        m.similarity = 0.8812345678;
        c2.members.push_back(m);
    }
    p.clusters = {c1, c2};

    HDAW::ClusterPresetMember u;
    u.name = "nosignal.wav"; u.path = "C:/sounds/nosignal.wav";
    p.unassigned = {u};

    p.entryCount = 4; // 3 clustered + 1 unassigned
    return p;
}

// Single-cluster save shape: clusterId set, unassigned null on disk.
HDAW::ClusterPreset makeSingleClusterPreset(const juce::String& id, const juce::String& name) {
    HDAW::ClusterPreset p;
    p.id = id;
    p.name = name;
    p.createdAt = "2026-08-25T13:00:00Z";
    p.libraryIds = juce::StringArray{};
    p.method = "dsp";
    p.k = 0;
    p.clusterId = "c2";
    HDAW::ClusterPresetCluster c2;
    c2.id = "c2";
    c2.label = "bright";
    HDAW::ClusterPresetMember m;
    m.name = "bright1.wav"; m.path = "C:/sounds/bright1.wav";
    m.tags = "bright"; m.description = "A bright chime.";
    m.similarity = 0.75;
    c2.members.push_back(m);
    p.clusters = {c2};
    // unassigned intentionally empty (serialized as null)
    p.entryCount = 1;
    return p;
}

} // namespace

class ClusterPresetStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_cluster_preset_store_test");
        tempDir.deleteRecursively();
        tempDir.createDirectory();
    }
    void TearDown() override {
        tempDir.deleteRecursively();
    }
    juce::File tempDir;
};

// G2-1: save -> list -> get round-trips ALL fields including clusters+members
// and unassigned; the on-disk text is the canonical JSON wrapper and a
// re-serialized record is byte-identical to the original.
TEST_F(ClusterPresetStoreTest, RoundTripAllFieldsByteFaithful) {
    auto libs = makeLibrariesDir(tempDir);
    auto record = makePreset({}, "My Preset"); // id empty -> store generates
    EXPECT_TRUE(record.id.isEmpty());

    HDAW::ClusterPresetStore store(libs);
    const juce::String id = store.upsert(record);

    EXPECT_TRUE(id.startsWith("cp_"));
    EXPECT_EQ(id.length(), 11) << "cp_ + 8 hex";
    EXPECT_FALSE(store.list().empty());

    HDAW::ClusterPreset loaded;
    ASSERT_TRUE(store.get(id, loaded));

    // Field-by-field assertions (byte-faithful semantic round-trip).
    EXPECT_EQ(loaded.id, id);
    EXPECT_EQ(loaded.name, "My Preset");
    EXPECT_EQ(loaded.createdAt, "2026-08-25T12:00:00Z");
    EXPECT_EQ(loaded.libraryIds.size(), 2);
    EXPECT_EQ(loaded.libraryIds[0], "libA");
    EXPECT_EQ(loaded.libraryIds[1], "libB");
    EXPECT_EQ(loaded.method, "hybrid");
    EXPECT_EQ(loaded.k, 3);
    EXPECT_TRUE(loaded.clusterId.isEmpty());
    ASSERT_EQ(loaded.clusters.size(), 2u);
    EXPECT_EQ(loaded.clusters[0].id, "c1");
    EXPECT_EQ(loaded.clusters[0].label, "dark");
    ASSERT_EQ(loaded.clusters[0].members.size(), 2u);
    EXPECT_EQ(loaded.clusters[0].members[0].name, "dark1.wav");
    EXPECT_EQ(loaded.clusters[0].members[0].path, "C:/sounds/dark1.wav");
    EXPECT_EQ(loaded.clusters[0].members[0].tags, "dark, pad");
    EXPECT_EQ(loaded.clusters[0].members[0].description, "A dark pad.");
    EXPECT_DOUBLE_EQ(loaded.clusters[0].members[0].similarity, 0.937654321);
    EXPECT_DOUBLE_EQ(loaded.clusters[0].members[1].similarity, 0.9123456789);
    ASSERT_EQ(loaded.clusters[1].members.size(), 1u);
    EXPECT_EQ(loaded.clusters[1].label, "bright");
    ASSERT_EQ(loaded.unassigned.size(), 1u);
    EXPECT_EQ(loaded.unassigned[0].name, "nosignal.wav");
    EXPECT_EQ(loaded.unassigned[0].path, "C:/sounds/nosignal.wav");
    EXPECT_EQ(loaded.entryCount, 4);

    // Byte-faithful: re-serializing the loaded record yields the identical text.
    // (record.id was generated by the store — mirror it before comparing.)
    record.id = id;
    EXPECT_EQ(HDAW::ClusterPresetStore::recordToJson(loaded),
              HDAW::ClusterPresetStore::recordToJson(record))
        << "load -> re-serialize must reproduce the original record byte for byte";

    // The file itself is exactly the canonical wrapper (stored record text
    // equals the writer's serialization of the very same record).
    auto file = libs.getChildFile("cluster_presets.json");
    ASSERT_TRUE(file.existsAsFile());
    auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* rootObj = parsed.getDynamicObject();
    ASSERT_NE(rootObj, nullptr);
    auto* arr = rootObj->getProperty("presets").getArray();
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->size(), 1);
    EXPECT_EQ(juce::JSON::toString((*arr)[0]),
              HDAW::ClusterPresetStore::recordToJson(loaded))
        << "the file must carry exactly this record, byte for byte";

    // Summary list carries the count fields.
    auto summaries = store.list();
    ASSERT_EQ(summaries.size(), 1);
    EXPECT_EQ(summaries[0].id, id);
    EXPECT_EQ(summaries[0].clusterCount, 2);
    EXPECT_EQ(summaries[0].entryCount, 4);
}

// G2-2: single-cluster save keeps only that cluster; unassigned is null on
// disk and rounds trip as empty; entryCount matches the narrow snapshot.
TEST_F(ClusterPresetStoreTest, SingleClusterSaveRoundTrip) {
    auto libs = makeLibrariesDir(tempDir);
    auto record = makeSingleClusterPreset({}, "Dark Only");
    HDAW::ClusterPresetStore store(libs);
    const juce::String id = store.upsert(record);

    HDAW::ClusterPreset loaded;
    ASSERT_TRUE(store.get(id, loaded));
    EXPECT_EQ(loaded.clusterId, "c2");
    ASSERT_EQ(loaded.clusters.size(), 1u);
    EXPECT_EQ(loaded.clusters[0].members.size(), 1u);
    EXPECT_TRUE(loaded.unassigned.empty());
    EXPECT_EQ(loaded.entryCount, 1);

    // unassigned serialized as a JSON null (not []).
    auto raw = tempDir.getChildFile("libraries/cluster_presets.json").loadFileAsString();
    auto parsed = juce::JSON::parse(raw);
    auto* arr = parsed.getDynamicObject()->getProperty("presets").getArray();
    ASSERT_EQ(arr->size(), 1);
    auto* o = (*arr)[0].getDynamicObject();
    ASSERT_NE(o, nullptr);
    const auto unassignedVar = o->getProperty("unassigned");
    EXPECT_TRUE(unassignedVar.isVoid() || unassignedVar.isUndefined())
        << "single-cluster save must write null unassigned, got: "
        << unassignedVar.toString().toStdString();
    record.id = id;
    EXPECT_EQ(HDAW::ClusterPresetStore::recordToJson(loaded),
              HDAW::ClusterPresetStore::recordToJson(record));
}

// G2-3: unknown id -> get returns false, remove returns false + error text.
TEST_F(ClusterPresetStoreTest, UnknownIdError) {
    auto libs = makeLibrariesDir(tempDir);
    HDAW::ClusterPresetStore store(libs);

    HDAW::ClusterPreset out;
    EXPECT_FALSE(store.get("cp_deadbeef", out));

    juce::String error;
    EXPECT_FALSE(store.remove("cp_deadbeef", error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(error.contains("cp_deadbeef"));
}

// G2-4: duplicate names are allowed; ids stay unique.
TEST_F(ClusterPresetStoreTest, DuplicateNamesOkUniqueIds) {
    auto libs = makeLibrariesDir(tempDir);
    HDAW::ClusterPresetStore store(libs);

    auto a = makePreset({}, "Same Name");
    auto b = makePreset({}, "Same Name");
    const juce::String idA = store.upsert(a);
    const juce::String idB = store.upsert(b);

    EXPECT_NE(idA, idB);
    auto summaries = store.list();
    ASSERT_EQ(summaries.size(), 2);
    EXPECT_EQ(summaries[0].name, "Same Name");
    EXPECT_EQ(summaries[1].name, "Same Name");
}

// G2-5: delete removes only the target preset.
TEST_F(ClusterPresetStoreTest, DeleteRemovesOnlyTarget) {
    auto libs = makeLibrariesDir(tempDir);
    HDAW::ClusterPresetStore store(libs);

    const juce::String idA = store.upsert(makePreset({}, "Keep Me"));
    const juce::String idB = store.upsert(makePreset({}, "Delete Me"));

    juce::String error;
    ASSERT_TRUE(store.remove(idB, error));
    EXPECT_TRUE(error.isEmpty());

    HDAW::ClusterPreset out;
    EXPECT_FALSE(store.get(idB, out)) << "deleted preset must be gone";
    EXPECT_TRUE(store.get(idA, out)) << "other preset must survive";
    ASSERT_EQ(store.list().size(), 1);
    EXPECT_EQ(store.list()[0].id, idA);

    // A fresh store instance re-reads the file: deletion persisted.
    HDAW::ClusterPresetStore fresh(libs);
    EXPECT_TRUE(fresh.get(idA, out));
    EXPECT_FALSE(fresh.get(idB, out));
}

// G2-6: re-instantiation from disk preserves the presets.
TEST_F(ClusterPresetStoreTest, ReinstantiationPreservesPresets) {
    auto libs = makeLibrariesDir(tempDir);
    {
        HDAW::ClusterPresetStore store(libs);
        store.upsert(makePreset({}, "Persisted"));
        store.upsert(makeSingleClusterPreset({}, "Single"));
    }
    HDAW::ClusterPresetStore fresh(libs);
    auto summaries = fresh.list();
    ASSERT_EQ(summaries.size(), 2);

    HDAW::ClusterPreset out;
    ASSERT_TRUE(fresh.get(summaries[0].id, out));
    EXPECT_EQ(out.name, "Persisted");
    EXPECT_EQ(out.entryCount, 4);
    EXPECT_EQ(out.libraryIds.size(), 2);
}

// G2-7: corrupted JSON -> empty store + no crash (warning is logged, never thrown).
TEST_F(ClusterPresetStoreTest, CorruptedJsonTolerated) {
    auto libs = makeLibrariesDir(tempDir);
    libs.getChildFile("cluster_presets.json").replaceWithText("{ this is not valid json !!!");

    HDAW::ClusterPresetStore store(libs); // must not crash
    EXPECT_TRUE(store.list().empty());

    HDAW::ClusterPreset out;
    EXPECT_FALSE(store.get("cp_00000000", out));

    // A structurally-wrong file (parses, but no presets array) is tolerated too.
    libs.getChildFile("cluster_presets.json").replaceWithText("{\"nope\":42}");
    HDAW::ClusterPresetStore store2(libs);
    EXPECT_TRUE(store2.list().empty());

    // After corruption the store still works: upsert writes a fresh valid file.
    const juce::String id = store.upsert(makePreset({}, "Recovered"));
    HDAW::ClusterPresetStore store3(libs);
    HDAW::ClusterPreset out2;
    EXPECT_TRUE(store3.get(id, out2));
    EXPECT_EQ(out2.name, "Recovered");
}

// G2-8 (Gate 9): name length-capped at 200 chars, stored as-is otherwise.
TEST_F(ClusterPresetStoreTest, NameCappedAtTwoHundred) {
    auto libs = makeLibrariesDir(tempDir);
    HDAW::ClusterPresetStore store(libs);

    juce::String longName;
    for (int i = 0; i < 250; ++i) longName += "x";
    auto record = makePreset({}, longName);
    const juce::String id = store.upsert(record);

    HDAW::ClusterPreset out;
    ASSERT_TRUE(store.get(id, out));
    EXPECT_EQ(out.name.length(), 200) << "name must be length-capped at 200";
    for (int i = 0; i < 200; ++i)
        EXPECT_EQ(out.name[i], 'x');
}
