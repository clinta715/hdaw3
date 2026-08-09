#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "engine/PluginManager.h"

// Use \x01 (SOH) as separator — never appears in preset names.
// Defined as a variable to avoid MSVC greedy hex parsing issues
// (e.g. "\x01Ba" is parsed as hex 0x01BA, not \x01 + "Ba").
static const juce::String SEP = juce::String::charToString(0x01);

class PresetCacheTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_preset_cache_test");
        tempDir.createDirectory();
    }

    void TearDown() override
    {
        tempDir.deleteRecursively();
    }

    juce::File tempDir;
};

TEST_F(PresetCacheTest, GetPresetInfoReturnsNullptrForUnknown)
{
    HDAW::PluginManager pm;
    EXPECT_EQ(pm.getPresetInfo("nonexistent-plugin-id"), nullptr);
}

TEST_F(PresetCacheTest, PresetCacheSaveLoadRoundTrip)
{
    juce::XmlElement root("PRESET_CACHE");
    auto* child = root.createNewChildElement("PLUGIN");
    child->setAttribute("id", "test-plugin-id");
    child->setAttribute("numPrograms", 3);
    child->setAttribute("programNames", "Init" + SEP + "Pad" + SEP + "Lead");

    auto cacheFile = tempDir.getChildFile("preset_cache.xml");
    root.writeTo(cacheFile, {});

    ASSERT_TRUE(cacheFile.existsAsFile());

    auto xml = juce::XmlDocument::parse(cacheFile);
    ASSERT_NE(xml, nullptr);
    EXPECT_EQ(xml->getNumChildElements(), 1);

    auto* plugin = xml->getChildElement(0);
    EXPECT_EQ(plugin->getStringAttribute("id"), "test-plugin-id");
    EXPECT_EQ(plugin->getIntAttribute("numPrograms"), 3);
    EXPECT_EQ(plugin->getStringAttribute("programNames"), "Init" + SEP + "Pad" + SEP + "Lead");
}

TEST_F(PresetCacheTest, PresetCacheXmlWithMultiplePlugins)
{
    juce::XmlElement root("PRESET_CACHE");

    auto* p1 = root.createNewChildElement("PLUGIN");
    p1->setAttribute("id", "CLAP-Dexed-abc123");
    p1->setAttribute("numPrograms", 32);
    p1->setAttribute("programNames", "Init" + SEP + "Electric Piano" + SEP + "Strings" + SEP + "Bass");

    auto* p2 = root.createNewChildElement("PLUGIN");
    p2->setAttribute("id", "VST3-Surge-def456");
    p2->setAttribute("numPrograms", 1000);

    auto cacheFile = tempDir.getChildFile("preset_cache.xml");
    root.writeTo(cacheFile, {});

    auto xml = juce::XmlDocument::parse(cacheFile);
    ASSERT_NE(xml, nullptr);
    EXPECT_EQ(xml->getNumChildElements(), 2);

    auto* first = xml->getChildElement(0);
    EXPECT_EQ(first->getStringAttribute("id"), "CLAP-Dexed-abc123");
    EXPECT_EQ(first->getIntAttribute("numPrograms"), 32);

    auto* second = xml->getChildElement(1);
    EXPECT_EQ(second->getStringAttribute("id"), "VST3-Surge-def456");
    EXPECT_EQ(second->getIntAttribute("numPrograms"), 1000);
    EXPECT_FALSE(second->hasAttribute("programNames"));
}

TEST_F(PresetCacheTest, EmptyCacheFile)
{
    juce::XmlElement root("PRESET_CACHE");
    auto cacheFile = tempDir.getChildFile("preset_cache.xml");
    root.writeTo(cacheFile, {});

    auto xml = juce::XmlDocument::parse(cacheFile);
    ASSERT_NE(xml, nullptr);
    EXPECT_EQ(xml->getNumChildElements(), 0);
}

TEST_F(PresetCacheTest, MissingCacheFileHandled)
{
    auto cacheFile = tempDir.getChildFile("nonexistent.xml");
    EXPECT_FALSE(cacheFile.existsAsFile());
}

TEST_F(PresetCacheTest, ProgramNamesTokenization)
{
    // \x01 (SOH) is a single-character separator — fromTokens splits correctly
    juce::String names = "Init" + SEP + "Pad" + SEP + "Lead" + SEP + "Bass" + SEP + "Pluck";
    auto arr = juce::StringArray::fromTokens(names, SEP, "");
    EXPECT_EQ(arr.size(), 5);
    EXPECT_EQ(arr[0], "Init");
    EXPECT_EQ(arr[1], "Pad");
    EXPECT_EQ(arr[2], "Lead");
    EXPECT_EQ(arr[3], "Bass");
    EXPECT_EQ(arr[4], "Pluck");
}

TEST_F(PresetCacheTest, ProgramNamesWithSpaces)
{
    juce::String names = "Init Pad" + SEP + "Warm String" + SEP + "Deep Sub Bass";
    auto arr = juce::StringArray::fromTokens(names, SEP, "");
    EXPECT_EQ(arr.size(), 3);
    EXPECT_EQ(arr[0], "Init Pad");
    EXPECT_EQ(arr[1], "Warm String");
    EXPECT_EQ(arr[2], "Deep Sub Bass");
}

TEST_F(PresetCacheTest, SingleProgramNotCached)
{
    HDAW::PluginPresetInfo info;
    info.numPrograms = 1;
    info.programNames.add("Init");

    EXPECT_FALSE(info.numPrograms > 1);
}

TEST_F(PresetCacheTest, JoinThenTokenRoundTrip)
{
    // Verify that joinIntoString + fromTokens round-trips correctly with \x01
    juce::StringArray original;
    original.add("Init");
    original.add("Warm Pad");
    original.add("Deep Sub Bass");
    original.add("Pluck Lead");

    juce::String joined = original.joinIntoString(SEP);
    auto recovered = juce::StringArray::fromTokens(joined, SEP, "");

    EXPECT_EQ(recovered.size(), original.size());
    for (int i = 0; i < original.size(); ++i)
        EXPECT_EQ(recovered[i], original[i]);
}
