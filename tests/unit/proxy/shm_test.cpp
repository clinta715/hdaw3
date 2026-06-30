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
