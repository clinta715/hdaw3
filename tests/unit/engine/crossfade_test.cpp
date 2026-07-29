#include <gtest/gtest.h>
#include "engine/CrossfadeEngine.h"

using namespace HDAW;

// Two adjacent clips (touching at t=2.0): should get a short crossfade at the boundary.
TEST(CrossfadeEngine, AdjacentClipsGetShortCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { /*id=*/1, /*start=*/0.0, /*dur=*/2.0, /*fadeIn=*/0.0, /*fadeOut=*/0.0 },
        { /*id=*/2, /*start=*/2.0, /*dur=*/2.0, /*fadeIn=*/0.0, /*fadeOut=*/0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01); // 10ms default

    ASSERT_EQ(result.size(), 2u);
    // Clip 1 should get fade-out points near t=2.0 (clip-local: 1.99..2.0)
    EXPECT_GE(result[0].points.size(), 2u);
    EXPECT_EQ(result[0].clipId, 1);
    // Clip 2 should get fade-in points near t=0.0 (clip-local: 0.0..0.01)
    EXPECT_GE(result[1].points.size(), 2u);
    EXPECT_EQ(result[1].clipId, 2);
}

// Two overlapping clips (A [0,4), B [2,6)): crossfade in [2,4).
TEST(CrossfadeEngine, OverlappingClipsGetFullCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 4.0, 0.0, 0.0 },
        { 2, 2.0, 4.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 2u);
    // Clip 1 (fading out): crossfade region is [2,4) clip-local = [2,4).
    // Should have points at 2.0 (gain 1.0) and 4.0 (gain 0.0).
    auto& a = result[0];
    ASSERT_GE(a.points.size(), 2u);
    EXPECT_DOUBLE_EQ(a.points.front().time, 2.0);
    EXPECT_FLOAT_EQ(a.points.front().gain, 1.0f);
    EXPECT_DOUBLE_EQ(a.points.back().time, 4.0);
    EXPECT_FLOAT_EQ(a.points.back().gain, 0.0f);

    // Clip 2 (fading in): crossfade region is clip-local [0,2).
    auto& b = result[1];
    ASSERT_GE(b.points.size(), 2u);
    EXPECT_DOUBLE_EQ(b.points.front().time, 0.0);
    EXPECT_FLOAT_EQ(b.points.front().gain, 0.0f);
    EXPECT_DOUBLE_EQ(b.points.back().time, 2.0);
    EXPECT_FLOAT_EQ(b.points.back().gain, 1.0f);
}

// Non-overlapping, non-adjacent clips: no crossfade points.
TEST(CrossfadeEngine, DistantClipsGetNoCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 5.0, 2.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_TRUE(result[0].points.empty());
    EXPECT_TRUE(result[1].points.empty());
}

// Clip with existing fadeIn should not be overridden — crossfade is skipped.
TEST(CrossfadeEngine, ExistingFadeInIsRespected)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 2.0, 2.0, 0.5, 0.0 },  // 0.5s fadeIn already set
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    // Clip 2 already has a 0.5s fadeIn; the crossfade (0.01s) is shorter,
    // so it should NOT add points that would shorten the existing fade.
    ASSERT_EQ(result.size(), 2u);
    // Clip 1 still gets fade-out at the boundary.
    EXPECT_FALSE(result[0].points.empty());
    // Clip 2 should NOT get crossfade points (fadeIn already handles it).
    EXPECT_TRUE(result[1].points.empty());
}

// Three clips in a row: each adjacent pair gets a crossfade.
TEST(CrossfadeEngine, ThreeAdjacentClips)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 2.0, 2.0, 0.0, 0.0 },
        { 3, 4.0, 2.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 3u);
    // Each clip should get crossfade points (clip 1 at end, clip 2 at both
    // ends, clip 3 at start). Clip 2 has two crossfade regions.
    EXPECT_FALSE(result[0].points.empty());
    EXPECT_FALSE(result[1].points.empty());
    EXPECT_FALSE(result[2].points.empty());
}
