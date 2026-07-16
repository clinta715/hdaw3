#include <gtest/gtest.h>
#include "engine/RegionClipboard.h"

TEST(RegionClipboard, StoreAndRetrieve) {
    HDAW::RegionClipboard::store({"test.wav", 1.5, 4.0});
    EXPECT_TRUE(HDAW::RegionClipboard::hasContent());
    auto& e = HDAW::RegionClipboard::get();
    EXPECT_EQ(e.sourceFile, "test.wav");
    EXPECT_DOUBLE_EQ(e.offset, 1.5);
    EXPECT_DOUBLE_EQ(e.duration, 4.0);
}

TEST(RegionClipboard, Clear) {
    HDAW::RegionClipboard::store({"test.wav", 0.0, 2.0});
    HDAW::RegionClipboard::clear();
    EXPECT_FALSE(HDAW::RegionClipboard::hasContent());
}

TEST(RegionClipboard, Overwrite) {
    HDAW::RegionClipboard::store({"a.wav", 0.0, 1.0});
    HDAW::RegionClipboard::store({"b.wav", 2.0, 3.0});
    EXPECT_EQ(HDAW::RegionClipboard::get().sourceFile, "b.wav");
}
