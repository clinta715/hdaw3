#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <memory>

class ChainLibrary : public ::testing::Test {
protected:
    void SetUp() override
    {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("hdaw_chain_lib_test_"
                        + juce::String(juce::Time::getMillisecondCounter())
                        + "_" + juce::String(++sCounter));
        tempDir.createDirectory();
        lib = std::make_unique<HDAW::ChainLibrary>(tempDir);
    }

    void TearDown() override
    {
        lib.reset();
        tempDir.deleteRecursively();
    }

    HDAW::ChainPreset makeFullPreset(const juce::String& name = "Full Stack")
    {
        HDAW::ChainPreset p;
        p.name = name;

        HDAW::ChainPreset::Slot comp;
        comp.fxType = "compressor";
        comp.params = { { "param_0", -12.0 }, { "param_1", 4.0 } };
        p.slots.push_back(comp);

        HDAW::ChainPreset::Slot filt;
        filt.fxType = "filter";
        filt.bypassed = true;
        filt.name = "My Filter";
        p.slots.push_back(filt);

        HDAW::ChainPreset::Slot plug;
        plug.fxType = "plugin";
        plug.plugin.id = "com.test.synth";
        plug.plugin.format = "VST3";
        plug.plugin.path = "C:/plugins/test.vst3";
        plug.plugin.stateBase64 = "QUJDRA==";
        p.slots.push_back(plug);

        HDAW::ChainPreset::Slot samp;
        samp.fxType = "sampler";
        samp.sampler = { { "sampleFile", "C:/samples/kick.wav" },
                         { "mode", "one-shot" },
                         { "rootNote", "60" } };
        samp.slicePoints = "0.1 0.5 0.9";
        p.slots.push_back(samp);

        HDAW::ChainPreset::Slot fm;
        fm.fxType = "psy_fm";
        fm.psyFmMatrix = "op1>op2:0.75";
        fm.psyFmSweepRate = 2.5;
        p.slots.push_back(fm);

        return p;
    }

    std::unique_ptr<HDAW::ChainLibrary> lib;
    juce::File tempDir;
    static std::atomic<int> sCounter;
};

std::atomic<int> ChainLibrary::sCounter { 0 };

TEST_F(ChainLibrary, FullRoundTripPreservesAllFields)
{
    auto id = lib->savePreset(makeFullPreset());
    ASSERT_FALSE(id.isEmpty());

    // Id stability: list + load echo the same id.
    auto list = lib->listPresets();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, id);

    auto loaded = lib->loadPreset(id);
    EXPECT_EQ(loaded.id, id);
    EXPECT_EQ(loaded.name, "Full Stack");
    EXPECT_EQ(loaded.version, 1);
    ASSERT_EQ(loaded.slots.size(), 5u);

    // Slot 0: compressor with 2 params.
    EXPECT_EQ(loaded.slots[0].fxType, "compressor");
    EXPECT_FALSE(loaded.slots[0].bypassed);
    EXPECT_DOUBLE_EQ(loaded.slots[0].params.at("param_0"), -12.0);
    EXPECT_DOUBLE_EQ(loaded.slots[0].params.at("param_1"), 4.0);

    // Slot 1: bypassed filter with name.
    EXPECT_EQ(loaded.slots[1].fxType, "filter");
    EXPECT_TRUE(loaded.slots[1].bypassed);
    EXPECT_EQ(loaded.slots[1].name, "My Filter");

    // Slot 2: full plugin ref.
    EXPECT_EQ(loaded.slots[2].plugin.id, "com.test.synth");
    EXPECT_EQ(loaded.slots[2].plugin.format, "VST3");
    EXPECT_EQ(loaded.slots[2].plugin.path, "C:/plugins/test.vst3");
    EXPECT_EQ(loaded.slots[2].plugin.stateBase64, "QUJDRA==");

    // Slot 3: sampler map + slice points.
    EXPECT_EQ(loaded.slots[3].sampler.at("sampleFile"), "C:/samples/kick.wav");
    EXPECT_EQ(loaded.slots[3].sampler.at("mode"), "one-shot");
    EXPECT_EQ(loaded.slots[3].sampler.at("rootNote"), "60");
    EXPECT_EQ(loaded.slots[3].slicePoints, "0.1 0.5 0.9");

    // Slot 4: psy-FM matrix + sweep rate.
    EXPECT_EQ(loaded.slots[4].psyFmMatrix, "op1>op2:0.75");
    EXPECT_DOUBLE_EQ(loaded.slots[4].psyFmSweepRate, 2.5);

    // Version is persisted on disk.
    juce::File file = tempDir.getChildFile("user").getChildFile("Full_Stack.json");
    ASSERT_TRUE(file.existsAsFile());
    EXPECT_TRUE(file.loadFileAsString().contains("version"));

    EXPECT_TRUE(lib->deletePreset(id));
    EXPECT_TRUE(lib->listPresets().empty());
}

TEST_F(ChainLibrary, SanitizesNameToFileName)
{
    auto id = lib->savePreset(makeFullPreset("Driven Bass"));
    EXPECT_EQ(id, "user/Driven_Bass.json");
}

TEST_F(ChainLibrary, UniquifiesDuplicateNames)
{
    auto first = lib->savePreset(makeFullPreset("Driven Bass"));
    auto second = lib->savePreset(makeFullPreset("Driven Bass"));
    ASSERT_FALSE(first.isEmpty());
    ASSERT_FALSE(second.isEmpty());
    EXPECT_NE(first, second);
    EXPECT_EQ(second, "user/Driven_Bass-1.json");

    auto list = lib->listPresets();
    EXPECT_EQ(list.size(), 2u);
}

TEST_F(ChainLibrary, EmptyNameSaveReturnsEmptyId)
{
    HDAW::ChainPreset p;
    p.name = "";
    EXPECT_TRUE(lib->savePreset(p).isEmpty());

    HDAW::ChainPreset blank;
    blank.name = "   ";
    EXPECT_TRUE(lib->savePreset(blank).isEmpty());
    EXPECT_TRUE(lib->listPresets().empty());
}

TEST_F(ChainLibrary, CorruptFileLoadReturnsEmptyWithoutCrashing)
{
    juce::File dir = tempDir.getChildFile("user");
    dir.createDirectory();
    ASSERT_TRUE(dir.getChildFile("x.json").replaceWithText("not json {{{ [[[\x01\x02"));

    auto loaded = lib->loadPreset("user/x.json");
    EXPECT_TRUE(loaded.id.isEmpty());
    EXPECT_TRUE(loaded.name.isEmpty());

    // The corrupt entry is skipped by the scan, not listed.
    EXPECT_TRUE(lib->listPresets().empty());
}

TEST_F(ChainLibrary, TraversalAndMissingIdsRejected)
{
    EXPECT_TRUE(lib->loadPreset("../../x").id.isEmpty());
    EXPECT_TRUE(lib->loadPreset("").id.isEmpty());
    EXPECT_FALSE(lib->deletePreset("user/does-not-exist.json"));
    EXPECT_FALSE(lib->deletePreset("_factory/evil.json"));
    EXPECT_FALSE(lib->deletePreset(""));
}
