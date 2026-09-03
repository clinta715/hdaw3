#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"

TEST(ChainLibrary, SaveListLoadDeleteRoundTrip) {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("hdaw_chain_test");
    tmp.deleteRecursively();
    ChainLibrary lib(tmp);
    ChainPreset p;
    p.name = "Driven Bass";
    ChainPreset::Slot s;
    s.fxType = "compressor";
    s.bypassed = false;
    s.params = { {"param_0", -12.0}, {"param_1", 4.0} };
    p.slots.push_back(s);
    auto id = lib.savePreset(p);
    EXPECT_FALSE(id.isEmpty());
    auto list = lib.listPresets();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].name, "Driven Bass");
    auto loaded = lib.loadPreset(id);
    EXPECT_EQ(loaded.slots.size(), 1u);
    EXPECT_EQ(loaded.slots[0].fxType, "compressor");
    EXPECT_DOUBLE_EQ(loaded.slots[0].params.at("param_0"), -12.0);
    EXPECT_TRUE(lib.deletePreset(id));
    EXPECT_TRUE(lib.listPresets().empty());
}
