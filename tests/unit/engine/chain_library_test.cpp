#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <map>
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

    // Filter to user-saved presets: since factory seeding, listPresets()
    // also returns the built-in _factory/ roster on every fresh root.
    static std::vector<HDAW::ChainPreset> userOnly(std::vector<HDAW::ChainPreset> all)
    {
        std::vector<HDAW::ChainPreset> out;
        for (auto& p : all)
            if (! p.isFactory)
                out.push_back(std::move(p));
        return out;
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

    // Id stability: list + load echo the same id (user entries only —
    // the scan also returns the built-in factory roster).
    auto list = userOnly(lib->listPresets());
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, id);
    EXPECT_FALSE(list[0].isFactory);

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
    EXPECT_TRUE(userOnly(lib->listPresets()).empty());
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
    EXPECT_EQ(userOnly(std::move(list)).size(), 2u);
}

TEST_F(ChainLibrary, EmptyNameSaveReturnsEmptyId)
{
    HDAW::ChainPreset p;
    p.name = "";
    EXPECT_TRUE(lib->savePreset(p).isEmpty());

    HDAW::ChainPreset blank;
    blank.name = "   ";
    EXPECT_TRUE(lib->savePreset(blank).isEmpty());
    EXPECT_TRUE(userOnly(lib->listPresets()).empty());
}

TEST_F(ChainLibrary, CorruptFileLoadReturnsEmptyWithoutCrashing)
{
    juce::File dir = tempDir.getChildFile("user");
    dir.createDirectory();
    ASSERT_TRUE(dir.getChildFile("x.json").replaceWithText("not json {{{ [[[\x01\x02"));

    auto loaded = lib->loadPreset("user/x.json");
    EXPECT_TRUE(loaded.id.isEmpty());
    EXPECT_TRUE(loaded.name.isEmpty());

    // The corrupt entry is skipped by the scan, not listed (user entries;
    // the factory roster still lists).
    EXPECT_TRUE(userOnly(lib->listPresets()).empty());
}

TEST_F(ChainLibrary, TraversalAndMissingIdsRejected)
{
    EXPECT_TRUE(lib->loadPreset("../../x").id.isEmpty());
    EXPECT_TRUE(lib->loadPreset("").id.isEmpty());
    EXPECT_FALSE(lib->deletePreset("user/does-not-exist.json"));
    EXPECT_FALSE(lib->deletePreset("_factory/evil.json"));
    EXPECT_FALSE(lib->deletePreset(""));
}

// --- Factory presets (built-in _factory/ roster) ---

TEST_F(ChainLibrary, FactorySeedingListsBothSources)
{
    // Fresh root: exactly the 8 built-ins, all factory-sourced, ids
    // well-formed ("_factory/<Name>.json").
    auto list = lib->listPresets();
    ASSERT_EQ(list.size(), 8u);
    static const char* kFactoryNames[] = {
        "Acid Lead", "Arp Width", "Bass Glue", "Hat Air",
        "Kick Punch", "Pad Shimmer", "Riser Sweep", "Stab Snip" };
    for (const auto* n : kFactoryNames)
    {
        bool found = false;
        for (const auto& p : list)
        {
            if (p.name != n)
                continue;
            found = true;
            EXPECT_TRUE(p.isFactory) << n;
            EXPECT_TRUE(p.id.startsWith("_factory/")) << n;
        }
        EXPECT_TRUE(found) << n;
    }

    // A user-saved preset lands in the user group, after the factory group;
    // within the factory group the order is alphabetical by id.
    HDAW::ChainPreset up;
    up.name = "My User Chain";
    ASSERT_FALSE(lib->savePreset(up).isEmpty());
    list = lib->listPresets();
    ASSERT_EQ(list.size(), 9u);
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i < 8)
            EXPECT_TRUE(list[i].isFactory) << list[i].id.toStdString();
        else
            EXPECT_FALSE(list[i].isFactory) << list[i].id.toStdString();
    }
    EXPECT_EQ(list.back().id, "user/My_User_Chain.json");
    for (size_t i = 1; i < 8; ++i)
        EXPECT_LT(list[i - 1].id, list[i].id);
}

TEST_F(ChainLibrary, FactoryPresetsLoadWithinDefRanges)
{
    // Spot-check shape: id load + expected slot order/counts.
    auto kick = lib->loadPreset("_factory/Kick_Punch.json");
    EXPECT_EQ(kick.id, "_factory/Kick_Punch.json");
    EXPECT_EQ(kick.name, "Kick Punch");
    ASSERT_EQ(kick.slots.size(), 3u);
    EXPECT_EQ(kick.slots[0].fxType, "saturator");
    EXPECT_EQ(kick.slots[1].fxType, "eq");
    EXPECT_EQ(kick.slots[2].fxType, "eq");

    auto bass = lib->loadPreset("_factory/Bass_Glue.json");
    ASSERT_EQ(bass.slots.size(), 3u);
    EXPECT_EQ(bass.slots[1].fxType, "compressor");

    auto riser = lib->loadPreset("_factory/Riser_Sweep.json");
    ASSERT_EQ(riser.slots.size(), 2u);
    EXPECT_EQ(riser.slots[0].fxType, "filter");
    EXPECT_EQ(riser.slots[1].fxType, "reverb");

    // Every param of every factory chain must sit inside its TrackFXSlot
    // def range (mirror of getParamDefsForType for the roster's fxTypes —
    // a wrong index/value is a silent wrong-knob, so ranges are the
    // contract here).
    struct Range { double lo, hi; };
    static const std::map<juce::String, std::vector<Range>> kDefs = {
        { "saturator",  { { 0.0, 40.0 }, { 0.0, 3.0 }, { -1.0, 1.0 }, { 0.0, 1.0 }, { -24.0, 24.0 }, { 2.0, 16.0 } } },
        { "eq",         { { 20.0, 20000.0 }, { 0.1, 10.0 }, { -24.0, 24.0 } } },
        { "compressor", { { -80.0, 0.0 }, { 1.0, 40.0 }, { 0.1, 100.0 }, { 1.0, 2000.0 } } },
        { "reverb",     { { 0.0, 1.0 }, { 0.0, 1.0 }, { 0.0, 1.0 }, { 0.0, 1.0 }, { 0.0, 1.0 } } },
        { "chorus",     { { 0.1, 5.0 }, { 0.0, 1.0 }, { 1.0, 50.0 }, { -1.0, 1.0 }, { 0.0, 1.0 } } },
        { "filter",     { { 20.0, 20000.0 }, { 0.0, 2.0 }, { 0.1, 10.0 } } },
        { "delay",      { { 0.01, 5.0 }, { 0.0, 0.99 }, { 0.0, 1.0 }, { 0.0, 1.0 }, { 0.0, 6.0 } } },
        { "phaser",     { { 0.1, 5.0 }, { 0.0, 1.0 }, { 20.0, 20000.0 }, { -1.0, 1.0 }, { 0.0, 1.0 } } },
    };

    auto all = lib->listPresets();
    ASSERT_EQ(all.size(), 8u);
    for (const auto& p : all)
    {
        ASSERT_TRUE(p.isFactory) << p.id.toStdString();
        for (const auto& s : p.slots)
        {
            auto it = kDefs.find(s.fxType);
            ASSERT_TRUE(it != kDefs.end()) << s.fxType.toStdString();
            for (const auto& kv : s.params)
            {
                EXPECT_TRUE(kv.first.startsWith("param_")) << kv.first.toStdString();
                int idx = kv.first.substring(6).getIntValue();
                ASSERT_GE(idx, 0);
                ASSERT_LT(idx, (int) it->second.size())
                    << p.id.toStdString() << " " << s.fxType.toStdString()
                    << " " << kv.first.toStdString();
                EXPECT_GE(kv.second, it->second[(size_t) idx].lo)
                    << p.id.toStdString() << " " << s.fxType.toStdString()
                    << " " << kv.first.toStdString();
                EXPECT_LE(kv.second, it->second[(size_t) idx].hi)
                    << p.id.toStdString() << " " << s.fxType.toStdString()
                    << " " << kv.first.toStdString();
            }
        }
    }
}

TEST_F(ChainLibrary, FactoryDeleteRefusedAndFileSurvives)
{
    juce::File file = tempDir.getChildFile("_factory").getChildFile("Kick_Punch.json");
    ASSERT_TRUE(file.existsAsFile());
    EXPECT_FALSE(lib->deletePreset("_factory/Kick_Punch.json"));
    EXPECT_TRUE(file.existsAsFile());
}

TEST_F(ChainLibrary, FactorySeedNeverOverwritesUserEdits)
{
    juce::File file = tempDir.getChildFile("_factory").getChildFile("Kick_Punch.json");
    ASSERT_TRUE(file.existsAsFile());

    // Hand-edit the on-disk factory file (as a user customizing a chain).
    auto json = juce::JSON::parse(file.loadFileAsString());
    auto* obj = json.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    obj->setProperty("name", "User Edited");
    ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(obj, true)));

    // A fresh library on the same root re-runs seeding; the edit must
    // survive (seeding is create-if-missing, never overwrite).
    lib.reset();
    lib = std::make_unique<HDAW::ChainLibrary>(tempDir);

    auto loaded = lib->loadPreset("_factory/Kick_Punch.json");
    EXPECT_EQ(loaded.name, "User Edited");
    ASSERT_EQ(loaded.slots.size(), 3u);
}
