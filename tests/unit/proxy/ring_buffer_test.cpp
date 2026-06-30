#include <gtest/gtest.h>
#include "proxy/ProxyRingBuffer.h"
#include <thread>
#include <vector>

using namespace proxy;

TEST(RingBuffer, WriteAndReadSingleSample) {
    RingBuffer<float> rb(64);
    float val = 3.14f;
    ASSERT_TRUE(rb.write(&val, 1));
    float out = 0;
    ASSERT_TRUE(rb.read(&out, 1));
    EXPECT_FLOAT_EQ(out, 3.14f);
}

TEST(RingBuffer, WriteBeyondCapacityFails) {
    RingBuffer<float> rb(4);
    float vals[4] = {1, 2, 3, 4};
    ASSERT_TRUE(rb.write(vals, 4));
    float extra = 5;
    EXPECT_FALSE(rb.write(&extra, 1));
}

TEST(RingBuffer, ReadFromEmptyFails) {
    RingBuffer<float> rb(4);
    float out = 0;
    EXPECT_FALSE(rb.read(&out, 1));
}

TEST(RingBuffer, WrapAround) {
    RingBuffer<float> rb(4);
    float vals[4] = {1, 2, 3, 4};
    ASSERT_TRUE(rb.write(vals, 4));
    float out[4];
    ASSERT_TRUE(rb.read(out, 4));
    float vals2[4] = {5, 6, 7, 8};
    ASSERT_TRUE(rb.write(vals2, 4));
    float out2[4];
    ASSERT_TRUE(rb.read(out2, 4));
    EXPECT_FLOAT_EQ(out2[0], 5.0f);
}

TEST(RingBuffer, SPSCConcurrent) {
    RingBuffer<float> rb(1024);
    const int N = 10000;

    std::thread writer([&]() {
        for (int i = 0; i < N; ++i) {
            float val = static_cast<float>(i);
            while (!rb.write(&val, 1)) { /* spin */ }
        }
    });

    std::thread reader([&]() {
        int count = 0;
        while (count < N) {
            float val;
            if (rb.read(&val, 1)) {
                EXPECT_FLOAT_EQ(val, static_cast<float>(count));
                count++;
            }
        }
    });

    writer.join();
    reader.join();
}

TEST(RingBuffer, WriteMultipleSamples) {
    RingBuffer<float> rb(8);
    float input[3] = {1.0f, 2.0f, 3.0f};
    ASSERT_TRUE(rb.write(input, 3));
    float output[3] = {};
    ASSERT_TRUE(rb.read(output, 3));
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 2.0f);
    EXPECT_FLOAT_EQ(output[2], 3.0f);
}
