#include <gtest/gtest.h>
#include "engine/SamplerInterpolator.h"

TEST(SamplerInterpolator, ExactAtIntegerPositions)
{
    float buf[] = { 0.0f, 10.0f, 20.0f, 30.0f };
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 0.0), 0.0f);
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 1.0), 10.0f);
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 4, 3.0), 30.0f);
}
TEST(SamplerInterpolator, LinearAtMidpointOfFlatRegion)
{
    float buf[] = { 5.0f, 5.0f, 5.0f, 5.0f, 5.0f };
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 5, 2.5), 5.0f);
}
TEST(SamplerInterpolator, ClampsAtBufferEnds)
{
    float buf[] = { 1.0f, 2.0f };
    EXPECT_FLOAT_EQ(HDAW::lagrange4(buf, 2, 1.5), 2.0f);
}
