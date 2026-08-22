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
