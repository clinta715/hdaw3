#include <gtest/gtest.h>
#include "proxy/ProxyCommon.h"

using namespace proxy;

TEST(RingBuffer, SizeSanity) {
    EXPECT_GE(sizeof(ProxyMessage), 256u);
    EXPECT_GE(sizeof(ProxyResponse), 256u);
}

TEST(RingBuffer, ComputeShmSize) {
    uint32_t size2ch = computeShmSize(2, 512);
    uint32_t size1ch = computeShmSize(1, 256);
    EXPECT_GT(size2ch, sizeof(ShmHeader));
    EXPECT_GT(size1ch, sizeof(ShmHeader));
    EXPECT_GT(size2ch, size1ch);
}
