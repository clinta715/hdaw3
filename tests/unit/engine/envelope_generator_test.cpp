#include <gtest/gtest.h>
#include "engine/EnvelopeGenerator.h"
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace
{

using Point = std::pair<double, double>;
using HDAW::EnvelopeGenerator;

} // namespace

TEST(EnvelopeGenerator, Determinism)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Noise;
    p.endTime = 4.0;
    p.densityPerSec = 64;
    p.seed = 42;

    auto a = EnvelopeGenerator::generate(p);
    auto b = EnvelopeGenerator::generate(p);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].first, b[i].first) << "point " << i;
        EXPECT_EQ(a[i].second, b[i].second) << "point " << i;
    }

    p.seed = 43;
    auto c = EnvelopeGenerator::generate(p);
    bool differs = a.size() != c.size();
    for (size_t i = 0; i < a.size() && i < c.size() && !differs; ++i)
        differs = (a[i].first != c[i].first) || (a[i].second != c[i].second);
    EXPECT_TRUE(differs);
}

TEST(EnvelopeGenerator, RampEndpointsExact)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Ramp;
    p.startTime = 1.0;
    p.endTime = 5.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.densityPerSec = 16;

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 2u);
    EXPECT_DOUBLE_EQ(out.front().first, 1.0);
    EXPECT_DOUBLE_EQ(out.front().second, 0.0);
    EXPECT_DOUBLE_EQ(out.back().first, 5.0);
    EXPECT_DOUBLE_EQ(out.back().second, 1.0);

    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_GE(out[i].second, out[i - 1].second - 1e-12) << "monotone at " << i;
}

TEST(EnvelopeGenerator, AdsrStagesMonotoneAndInRange)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::ADSR;
    p.startTime = 0.0;
    p.endTime = 1.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.densityPerSec = 101.0; // 101 points -> grid step 0.01, boundaries land on grid
    p.attack = 0.2;
    p.decay = 0.15;
    p.sustainLevel = 0.5;
    p.release = 0.25;

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 2u);
    ASSERT_EQ(out.size(), 101u);

    // First = startValue, peak = endValue (end of attack), last = startValue.
    EXPECT_DOUBLE_EQ(out.front().second, 0.0);
    EXPECT_DOUBLE_EQ(out[20].second, 1.0);
    EXPECT_NEAR(out.back().second, 0.0, 1e-12);

    // All in [0, 1].
    for (const auto& pt : out)
    {
        EXPECT_GE(pt.second, 0.0);
        EXPECT_LE(pt.second, 1.0);
    }

    // Attack strictly increasing (samples 0..20).
    for (size_t i = 1; i <= 20; ++i)
        EXPECT_GT(out[i].second, out[i - 1].second) << "attack at " << i;
    // Decay non-increasing (samples 20..35).
    for (size_t i = 21; i <= 35; ++i)
        EXPECT_LE(out[i].second, out[i - 1].second + 1e-12) << "decay at " << i;
    // Release non-increasing (samples 75..100).
    for (size_t i = 76; i < out.size(); ++i)
        EXPECT_LE(out[i].second, out[i - 1].second + 1e-12) << "release at " << i;

    // Sustain held at sustainLevel for a substantial run.
    const double sustainValue = p.startValue + p.sustainLevel * (p.endValue - p.startValue);
    size_t held = 0;
    for (const auto& pt : out)
        if (std::fabs(pt.second - sustainValue) < 1e-12)
            ++held;
    EXPECT_GE(held, 20u);
}

TEST(EnvelopeGenerator, WaveCycleCounts)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Sine;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.cycles = 3.0;
    p.phase = 0.0;
    p.densityPerSec = 257.0; // grid 1027: no sample lands exactly on a zero crossing

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 4u);

    // Midline zero crossings: 3 cycles -> ~6 crossings.
    const double mid = (p.startValue + p.endValue) * 0.5;
    int crossings = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        const double prev = out[i - 1].second - mid;
        const double cur = out[i].second - mid;
        if ((prev > 0.0 && cur < 0.0) || (prev < 0.0 && cur > 0.0))
            ++crossings;
    }
    EXPECT_NEAR(crossings, 6, 2);

    // Phase respected: phase 0.25 -> first sample sits at the peak.
    p.phase = 0.25;
    auto ph = EnvelopeGenerator::generate(p);
    ASSERT_GE(ph.size(), 2u);
    EXPECT_NEAR(ph.front().second, p.endValue, 1e-9);
}

TEST(EnvelopeGenerator, StaircaseSteps)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Staircase;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.steps = 5;
    p.densityPerSec = 64;

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 2u);
    EXPECT_DOUBLE_EQ(out.front().second, 0.0);
    EXPECT_DOUBLE_EQ(out.back().second, 1.0);

    std::set<double> unique;
    for (const auto& pt : out)
        unique.insert(std::round(pt.second * 1e6) / 1e6);
    ASSERT_EQ(unique.size(), 5u);

    // Plateau values are the steps' lerp positions: k/(steps-1).
    for (double v : unique)
    {
        EXPECT_NEAR(v, std::round(v * 4.0) / 4.0, 1e-9);
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 1.0);
    }
}

TEST(EnvelopeGenerator, SCurveEndpointsAndShape)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::SCurve;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.densityPerSec = 256.25; // 1025 points -> sample 512 lands exactly on t=0.5

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 2u);
    EXPECT_DOUBLE_EQ(out.front().second, 0.0);
    EXPECT_DOUBLE_EQ(out.back().second, 1.0);

    EXPECT_NEAR(out[512].second, 0.5, 1e-12);

    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_GE(out[i].second, out[i - 1].second - 1e-12) << "monotone at " << i;
}

TEST(EnvelopeGenerator, RandomWalkAndNoiseBounded)
{
    EnvelopeGenerator::Params p;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.startValue = 0.0;
    p.endValue = 1.0;
    p.densityPerSec = 64;
    p.seed = 7;

    p.shape = EnvelopeGenerator::Shape::RandomWalk;
    auto walk = EnvelopeGenerator::generate(p);
    ASSERT_GE(walk.size(), 2u);
    const double range = p.endValue - p.startValue;
    for (const auto& pt : walk)
    {
        EXPECT_GE(pt.second, 0.0);
        EXPECT_LE(pt.second, 1.0);
    }
    for (size_t i = 1; i < walk.size(); ++i)
        EXPECT_LE(std::fabs(walk[i].second - walk[i - 1].second), 0.15 * range + 1e-9) << "step " << i;

    p.shape = EnvelopeGenerator::Shape::Noise;
    auto noise = EnvelopeGenerator::generate(p);
    ASSERT_GE(noise.size(), 64u);
    double mn = 1.0;
    double mx = 0.0;
    for (const auto& pt : noise)
    {
        EXPECT_GE(pt.second, 0.0);
        EXPECT_LE(pt.second, 1.0);
        mn = std::min(mn, pt.second);
        mx = std::max(mx, pt.second);
    }
    EXPECT_LT(mn, 0.3);
    EXPECT_GT(mx, 0.7);
}

TEST(EnvelopeGenerator, SortedAndDeduped)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Sine;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.densityPerSec = 100;

    auto out = EnvelopeGenerator::generate(p);
    ASSERT_GE(out.size(), 2u);
    for (size_t i = 1; i < out.size(); ++i)
    {
        EXPECT_GT(out[i].first, out[i - 1].first) << "sorted at " << i;
        EXPECT_GE(out[i].first - out[i - 1].first, 1e-9) << "spacing at " << i;
    }
    EXPECT_DOUBLE_EQ(out.back().first, 4.0);

    // endTime < startTime is swapped.
    p.startTime = 4.0;
    p.endTime = 0.0;
    auto swapped = EnvelopeGenerator::generate(p);
    ASSERT_GE(swapped.size(), 2u);
    EXPECT_DOUBLE_EQ(swapped.front().first, 0.0);
    EXPECT_DOUBLE_EQ(swapped.back().first, 4.0);

    // NaN input -> empty.
    p.startTime = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(EnvelopeGenerator::generate(p).empty());

    // Range < 1e-9 -> single point.
    p.startTime = 1.0;
    p.endTime = 1.0;
    auto single = EnvelopeGenerator::generate(p);
    ASSERT_EQ(single.size(), 1u);
}

TEST(EnvelopeGenerator, DensityCap4096)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Ramp;
    p.startTime = 0.0;
    p.endTime = 100.0;
    p.densityPerSec = 1000.0;

    auto out = EnvelopeGenerator::generate(p);
    EXPECT_LE(out.size(), 4096u);
    ASSERT_GE(out.size(), 2u);
    EXPECT_DOUBLE_EQ(out.back().first, 100.0);
}

TEST(EnvelopeGenerator, SmoothReducesDeltas)
{
    EnvelopeGenerator::Params p;
    p.shape = EnvelopeGenerator::Shape::Noise;
    p.startTime = 0.0;
    p.endTime = 4.0;
    p.densityPerSec = 128;
    p.seed = 99;

    auto raw = EnvelopeGenerator::generate(p);
    ASSERT_GE(raw.size(), 2u);
    auto sm = EnvelopeGenerator::smooth(raw, 0.9);
    ASSERT_EQ(sm.size(), raw.size());
    EXPECT_DOUBLE_EQ(sm.front().second, raw.front().second);
    EXPECT_DOUBLE_EQ(sm.back().second, raw.back().second);

    double meanIn = 0.0;
    double meanOut = 0.0;
    for (size_t i = 1; i < raw.size(); ++i)
    {
        meanIn += std::fabs(raw[i].second - raw[i - 1].second);
        meanOut += std::fabs(sm[i].second - sm[i - 1].second);
    }
    meanIn /= static_cast<double>(raw.size() - 1);
    meanOut /= static_cast<double>(raw.size() - 1);
    EXPECT_GT(meanIn, 0.0);
    EXPECT_LE(meanOut, 0.25 * meanIn);
}

TEST(EnvelopeGenerator, ClampDensityDecimates)
{
    std::vector<Point> pts;
    pts.reserve(100);
    for (int i = 0; i < 100; ++i)
        pts.emplace_back(i * 0.1, static_cast<double>(i) / 99.0);

    auto out = EnvelopeGenerator::clampDensity(pts, 1e9, 10);
    ASSERT_EQ(out.size(), 10u);
    EXPECT_DOUBLE_EQ(out.front().first, 0.0);
    EXPECT_DOUBLE_EQ(out.front().second, 0.0);
    EXPECT_DOUBLE_EQ(out.back().first, 9.9);
    EXPECT_DOUBLE_EQ(out.back().second, 1.0);
    for (size_t i = 1; i < out.size(); ++i)
        EXPECT_GT(out[i].first, out[i - 1].first) << "sorted at " << i;

    // Dense input below the cap passes through unchanged.
    auto same = EnvelopeGenerator::clampDensity(pts, 1e9, 500);
    ASSERT_EQ(same.size(), 100u);
    EXPECT_DOUBLE_EQ(same.front().second, pts.front().second);
    EXPECT_DOUBLE_EQ(same.back().second, pts.back().second);

    // Empty input stays empty.
    EXPECT_TRUE(EnvelopeGenerator::clampDensity({}, 1e9, 10).empty());
}

TEST(EnvelopeGenerator, ShapeNameStrings)
{
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Ramp), "ramp");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::ADSR), "adsr");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Sine), "sine");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Triangle), "triangle");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Saw), "saw");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Square), "square");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Pulse), "pulse");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Staircase), "staircase");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::SCurve), "sCurve");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::RandomWalk), "randomWalk");
    EXPECT_STREQ(EnvelopeGenerator::shapeName(EnvelopeGenerator::Shape::Noise), "noise");
}
