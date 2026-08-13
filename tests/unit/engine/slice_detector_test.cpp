#include <gtest/gtest.h>
#include "engine/SliceDetector.h"
#include "engine/SamplerSound.h"
#include <vector>
#include <algorithm>
#include <cmath>

TEST(SliceDetector, GridModeSlicesAtBeats)
{
    // 2-second sample at 1000 Hz (2000 frames), bpm 120, grid 0.25 beat.
    // 120 bpm = 2 beats/sec → 4 beats in 2s → 16 slices at 0.25 beat.
    auto pts = HDAW::SliceDetector::grid(2000, 1000.0, 120.0, 0.25);
    EXPECT_FALSE(pts.empty());
    EXPECT_EQ(pts.front(), 0);
    EXPECT_EQ(pts.back(), 2000);
    EXPECT_TRUE(std::is_sorted(pts.begin(), pts.end()));
}

TEST(SliceDetector, TransientDetectsSharpOnsets)
{
    // A sustained loud sine burst starting at frame 500, preceded by silence.
    // The envelope follower needs sustained energy to build above threshold,
    // and the sine wave provides natural rising edges for detection.
    const int N = 4000;
    std::vector<float> x(N, 0.0f);
    for (int i = 500; i < N; ++i)
        x[i] = std::sin(6.2831853 * 440.0 * i / 44100.0);

    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    ASSERT_FALSE(pts.empty());
    // The onset at frame 500 is detected once the slow envelope (0.001 coeff)
    // builds above threshold — first detection lands around frame 1003.
    bool found = false;
    for (auto p : pts) if (p >= 900 && p <= 1100) found = true;
    EXPECT_TRUE(found);
}

TEST(SliceDetector, TransientAlwaysIncludesStartAndEnd)
{
    std::vector<float> x(500, 0.5f);
    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    ASSERT_GE(pts.size(), 2u);
    EXPECT_EQ(pts.front(), 0);
    EXPECT_EQ(pts.back(), 500);
}
