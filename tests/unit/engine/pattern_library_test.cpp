#include <gtest/gtest.h>
#include "engine/PatternLibrary.h"
#include <juce_core/juce_core.h>

class PatternLibraryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("hdaw_pattern_lib_test_" + juce::String(juce::Time::getMillisecondCounter()));
        tempDir.createDirectory();
        lib = std::make_unique<HDAW::PatternLibrary>(tempDir);
    }

    void TearDown() override
    {
        lib.reset();
        tempDir.deleteRecursively();
    }

    HDAW::PatternPreset makeTestPreset(const juce::String& name, const juce::String& style = "Standard")
    {
        HDAW::PatternPreset p;
        p.name = name;
        p.style = style;
        p.category = "test";
        p.tags = {"test", "unit"};
        p.paramsJson = R"({"scaleRoot":0,"scaleMode":0,"lowNote":48,"highNote":84,"minVelocity":60,"maxVelocity":110,"seed":42,"lengthBeats":4.0,"density":8,"noteDuration":0.5})";
        p.styleParamsJson = "{}";
        return p;
    }

    std::unique_ptr<HDAW::PatternLibrary> lib;
    juce::File tempDir;
};

TEST_F(PatternLibraryTest, SaveAndLoadRoundtrip)
{
    auto preset = makeTestPreset("My Test Pattern", "Arpeggio");
    juce::String error;
    ASSERT_TRUE(lib->savePattern(preset, error)) << error.toStdString();

    juce::String id = "user/test/My Test Pattern";
    HDAW::PatternPreset loaded;
    ASSERT_TRUE(lib->loadPattern(id, loaded, error)) << error.toStdString();
    EXPECT_EQ(loaded.name, "My Test Pattern");
    EXPECT_EQ(loaded.style, "Arpeggio");
    EXPECT_EQ(loaded.category, "test");
}

TEST_F(PatternLibraryTest, ListPatterns)
{
    juce::String errA, errB;
    ASSERT_TRUE(lib->savePattern(makeTestPreset("Pattern A", "Standard"), errA));
    ASSERT_TRUE(lib->savePattern(makeTestPreset("Pattern B", "Arpeggio"), errB));

    auto all = lib->listPatterns();
    EXPECT_EQ(all.size(), 2u);

    auto filtered = lib->listPatterns({}, "Arpeggio");
    EXPECT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].style, "Arpeggio");
}

TEST_F(PatternLibraryTest, DeletePattern)
{
    juce::String errSave;
    ASSERT_TRUE(lib->savePattern(makeTestPreset("To Delete"), errSave));
    juce::String id = "user/test/To Delete";

    juce::String error;
    ASSERT_TRUE(lib->deletePattern(id, error)) << error.toStdString();

    HDAW::PatternPreset loaded;
    EXPECT_FALSE(lib->deletePattern(id, error));
}

TEST_F(PatternLibraryTest, FactoryPatternCannotBeDeleted)
{
    juce::File factoryDir = tempDir.getChildFile("_factory/test");
    factoryDir.createDirectory();
    juce::File factoryFile = factoryDir.getChildFile("factory-pattern.json");
    factoryFile.replaceWithText(R"({"version":1,"name":"Factory One","style":"Standard","category":"test","tags":["factory"]})");

    lib->rebuildIndex();

    juce::String error;
    EXPECT_FALSE(lib->deletePattern("factory/test/factory-pattern", error));
    EXPECT_TRUE(error.startsWith("Cannot delete factory pattern"));
}

TEST_F(PatternLibraryTest, ImportJsonString)
{
    juce::String json = R"({
        "version": 1,
        "name": "Imported Pattern",
        "style": "BassLine",
        "category": "user",
        "tags": ["import"],
        "params": {"scaleRoot":0,"scaleMode":1},
        "styleParams": {}
    })";

    juce::String id, error;
    ASSERT_TRUE(lib->importPattern(json, id, error)) << error.toStdString();
    EXPECT_TRUE(id.startsWith("user/"));

    HDAW::PatternPreset loaded;
    ASSERT_TRUE(lib->loadPattern(id, loaded, error));
    EXPECT_EQ(loaded.name, "Imported Pattern");
}

TEST_F(PatternLibraryTest, ImportInvalidJsonFails)
{
    juce::String id, error;
    EXPECT_FALSE(lib->importPattern("not json", id, error));
    EXPECT_FALSE(error.isEmpty());
}

TEST_F(PatternLibraryTest, ExportReturnsValidJson)
{
    juce::String errSave;
    ASSERT_TRUE(lib->savePattern(makeTestPreset("Export Me"), errSave));
    juce::String id = "user/test/Export Me";

    juce::String json, error;
    ASSERT_TRUE(lib->exportPattern(id, json, error)) << error.toStdString();

    EXPECT_TRUE(json.contains("Export Me"));
}

TEST_F(PatternLibraryTest, IndexRebuildsOnSave)
{
    juce::String errFirst, errSecond;
    lib->savePattern(makeTestPreset("First"), errFirst);
    EXPECT_EQ(lib->listPatterns().size(), 1u);

    lib->savePattern(makeTestPreset("Second"), errSecond);
    EXPECT_EQ(lib->listPatterns().size(), 2u);
}

TEST_F(PatternLibraryTest, SanitizedNameTruncates)
{
    juce::String longName = juce::String::repeatedString("A", 100);
    auto preset = makeTestPreset(longName);
    juce::String error;
    ASSERT_TRUE(lib->savePattern(preset, error)) << error.toStdString();

    auto patterns = lib->listPatterns();
    ASSERT_EQ(patterns.size(), 1u);

    juce::File userDir = tempDir.getChildFile("user").getChildFile("test");
    auto files = userDir.findChildFiles(juce::File::findFiles, false, "*.json");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_LE(files[0].getFileNameWithoutExtension().length(), 64);
}

// ── B1 regression: import→export must round-trip the FULL document ──
// The import contract (docs/skills/psy-song-session/roles/pattern-researcher.md)
// carries notes/role/descriptor at top level; import used to store only the
// preset envelope, so export_pattern came back hollow (vector-bloom session:
// 10/10 hollow on 2026-09-26).
TEST_F(PatternLibraryTest, ImportExportRoundTripsPayload)
{
    const juce::String json = R"({
        "version": 1,
        "name": "VB Kick",
        "style": "FourFloorKick",
        "role": "kick",
        "descriptor": {"bpm": 140, "key": "G minor", "bars": 1, "noteCount": 4},
        "notes": [
            {"pitch": 36, "startBeat": 0.0, "durationBeats": 0.5, "velocity": 112},
            {"pitch": 36, "startBeat": 1.0, "durationBeats": 0.5, "velocity": 96}
        ]
    })";

    juce::String id, error;
    ASSERT_TRUE(lib->importPattern(json, id, error)) << error.toStdString();

    juce::String exported, exportErr;
    ASSERT_TRUE(lib->exportPattern(id, exported, exportErr)) << exportErr.toStdString();

    // Named local — the var owns the parsed object; a temporary would free it
    // at the end of the statement and leave `obj` dangling.
    const auto parsed = juce::JSON::parse(exported);
    auto* obj = parsed.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    // Managed envelope fields survive...
    EXPECT_EQ(obj->getProperty("name").toString(), juce::String("VB Kick"));
    EXPECT_EQ(obj->getProperty("style").toString(), juce::String("FourFloorKick"));
    EXPECT_EQ(static_cast<int>(obj->getProperty("version")), 1);
    // ...and the unmanaged payload round-trips.
    EXPECT_EQ(obj->getProperty("role").toString(), juce::String("kick"));

    auto* notes = obj->getProperty("notes").getArray();
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->size(), 2);
    auto* firstNote = (*notes)[0].getDynamicObject();
    ASSERT_NE(firstNote, nullptr);
    EXPECT_EQ(static_cast<int>(firstNote->getProperty("pitch")), 36);
    EXPECT_NEAR(static_cast<double>(firstNote->getProperty("startBeat")), 0.0, 1e-9);
    EXPECT_EQ(static_cast<int>(firstNote->getProperty("velocity")), 112);

    auto* descriptor = obj->getProperty("descriptor").getDynamicObject();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(static_cast<int>(descriptor->getProperty("bpm")), 140);
    EXPECT_EQ(descriptor->getProperty("key").toString(), juce::String("G minor"));
}

TEST_F(PatternLibraryTest, LoadPatternPreservesExtras)
{
    const juce::String json = R"({
        "version": 1,
        "name": "VB Bass",
        "style": "RollingBass",
        "role": "bass",
        "notes": [{"pitch": 33, "startBeat": 0.0, "durationBeats": 0.25, "velocity": 100}]
    })";

    juce::String id, error;
    ASSERT_TRUE(lib->importPattern(json, id, error)) << error.toStdString();

    HDAW::PatternPreset loaded;
    ASSERT_TRUE(lib->loadPattern(id, loaded, error)) << error.toStdString();
    EXPECT_EQ(loaded.name, "VB Bass");
    EXPECT_FALSE(loaded.extraJson.isEmpty());

    const auto parsedExtras = juce::JSON::parse(loaded.extraJson);
    auto* extras = parsedExtras.getDynamicObject();
    ASSERT_NE(extras, nullptr);
    EXPECT_EQ(extras->getProperty("role").toString(), juce::String("bass"));
    auto* notes = extras->getProperty("notes").getArray();
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->size(), 1);
}

TEST_F(PatternLibraryTest, ImportNameCollisionKeepsPayload)
{
    const juce::String json = R"({
        "version": 1, "name": "Same Name", "style": "S", "role": "perc",
        "notes": [{"pitch": 40, "startBeat": 0.0, "durationBeats": 0.5, "velocity": 90}]
    })";

    juce::String idA, idB, error;
    ASSERT_TRUE(lib->importPattern(json, idA, error)) << error.toStdString();
    ASSERT_TRUE(lib->importPattern(json, idB, error)) << error.toStdString();
    EXPECT_NE(idA, idB) << "the collision rename must still register the second id";

    juce::String exported, exportErr;
    ASSERT_TRUE(lib->exportPattern(idB, exported, exportErr)) << exportErr.toStdString();
    const auto parsedB = juce::JSON::parse(exported);
    auto* obj = parsedB.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->getProperty("role").toString(), juce::String("perc"));
    auto* notes = obj->getProperty("notes").getArray();
    ASSERT_NE(notes, nullptr);
    EXPECT_EQ(notes->size(), 1);
}

TEST_F(PatternLibraryTest, SaveWithoutExtrasOmitsThem)
{
    juce::String error;
    ASSERT_TRUE(lib->savePattern(makeTestPreset("No Extras"), error)) << error.toStdString();

    juce::String exported, exportErr;
    ASSERT_TRUE(lib->exportPattern("user/test/No Extras", exported, exportErr))
        << exportErr.toStdString();
    // A save-built preset has no extras: no phantom payload keys appear.
    EXPECT_FALSE(exported.contains("\"notes\""));
}
