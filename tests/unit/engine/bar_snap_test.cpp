#include <gtest/gtest.h>
#include <cmath>

namespace
{

const double bpm = 120.0;
const int beatsPerBar = 4;
const double secPerBar = (60.0 / bpm) * beatsPerBar;

double snapPure(double t)
{
    double barPos = t / secPerBar;
    return std::round(barPos) * secPerBar;
}

TEST(BarSnapTest, SnapsToNearestBar)
{
    EXPECT_DOUBLE_EQ(snapPure(0.0), 0.0);
    EXPECT_DOUBLE_EQ(snapPure(0.5), 0.0);
    EXPECT_DOUBLE_EQ(snapPure(1.0), 2.0);  // halfway between bars, round away from zero
    EXPECT_DOUBLE_EQ(snapPure(1.5), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.0), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.3), 2.0);
    EXPECT_DOUBLE_EQ(snapPure(2.7), 2.0);
}

TEST(BarSnapTest, DoesNotShiftMicroAmount)
{
    double sampleDuration = 1.0 / 44100.0;
    double t = secPerBar;
    double barPos = t / secPerBar;
    double snapped = std::round(barPos) * secPerBar;
    EXPECT_TRUE(std::abs(snapped - t) < sampleDuration);
}

TEST(BarSnapTest, ZeroTimeReturnsZero)
{
    EXPECT_DOUBLE_EQ(snapPure(0.0), 0.0);
}

}
