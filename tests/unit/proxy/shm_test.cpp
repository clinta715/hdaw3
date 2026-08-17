#include <gtest/gtest.h>
#include "proxy/ProxySharedMemory.h"

using namespace proxy;

TEST(SharedMemory, CreateAndMap) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_1", 4096));
    EXPECT_NE(region.getHeader(), nullptr);
    EXPECT_EQ(region.getHeader()->magic, SHM_MAGIC);
}

TEST(SharedMemory, WriteAndReadSamples) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_2", 4096));

    auto* hdr = region.getHeader();
    hdr->numChannels = 2;
    hdr->blockSize = 4;
    hdr->capacity = 8;

    float input[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_TRUE(region.writeInput(input, 8));

    float output[8] = {};
    ASSERT_TRUE(region.readInput(output, 8));
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[7], 8.0f);
}

TEST(SharedMemory, OutputCurrentHelper) {
    // Output is current when the child consumed exactly what the parent wrote.
    EXPECT_TRUE(proxyOutputIsCurrent(100, 100));
    // ...and when it consumed further than the parent wrote (caught up).
    EXPECT_TRUE(proxyOutputIsCurrent(200, 100));
    // A lagging child (not yet consumed the parent's write) is never current.
    EXPECT_FALSE(proxyOutputIsCurrent(50, 100));
    EXPECT_FALSE(proxyOutputIsCurrent(0, 256));
}

TEST(SharedMemory, ResyncHeaderFieldsInit) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_resync", computeShmSize(2, 512)));

    auto* hdr = region.getHeader();
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->magic, SHM_MAGIC);
    EXPECT_EQ(hdr->renderMode.load(), 0u);
    EXPECT_EQ(hdr->lastConsumedInputPos.load(), 0u);
}
