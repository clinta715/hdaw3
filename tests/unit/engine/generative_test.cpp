#include <gtest/gtest.h>
#include "engine/Generative.h"
#include <algorithm>
#include <set>
#include <vector>

using namespace HDAW;

// ── SplitMix64 ──

TEST(GenerativePrng, SameSeedSameStream)
{
    SplitMix64 a(12345), b(12345);
    for (int i = 0; i < 1000; ++i)
        EXPECT_EQ(a.nextU64(), b.nextU64());
}

TEST(GenerativePrng, DifferentSeedDiffers)
{
    SplitMix64 a(12345), b(12346);
    EXPECT_NE(a.nextU64(), b.nextU64());
}

TEST(GenerativePrng, NextIntInRange)
{
    SplitMix64 rng(999);
    for (int i = 0; i < 5000; ++i)
    {
        int v = rng.nextInt(5, 10);
        EXPECT_GE(v, 5);
        EXPECT_LE(v, 10);
    }
}

TEST(GenerativePrng, NextIntDegenerate)
{
    SplitMix64 rng(1);
    EXPECT_EQ(rng.nextInt(7, 7), 7);
    EXPECT_EQ(rng.nextInt(9, 3), 9); // lo > hi collapses to lo
}

TEST(GenerativePrng, NextBoolEdges)
{
    SplitMix64 rng(2);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_FALSE(rng.nextBool(0.0));
        EXPECT_TRUE(rng.nextBool(1.0));
    }
}

TEST(GenerativePrng, NextFloatUnitInterval)
{
    SplitMix64 rng(3);
    for (int i = 0; i < 5000; ++i)
    {
        double f = rng.nextFloat();
        EXPECT_GE(f, 0.0);
        EXPECT_LT(f, 1.0);
    }
}

// ── Seed derivation / ReproState ──

TEST(GenerativeSeed, DeriveIsDeterministicAndSensitive)
{
    EXPECT_EQ(deriveSeed(42, "track:bass"), deriveSeed(42, "track:bass"));
    EXPECT_NE(deriveSeed(42, "track:bass"), deriveSeed(42, "track:lead"));
    EXPECT_NE(deriveSeed(42, "track:bass"), deriveSeed(43, "track:bass"));
}

TEST(GenerativeSeed, ReproStateBranchesAreStableAndIndependent)
{
    ReproState r(777);
    auto a1 = r.rng("rhythm", "kick");
    auto a2 = r.rng("rhythm", "kick");
    auto b  = r.rng("rhythm", "hats");
    auto c  = r.rng("kick", "rhythm"); // order matters

    for (int i = 0; i < 100; ++i)
    {
        uint64_t x = a1.nextU64();
        EXPECT_EQ(x, a2.nextU64());
    }
    SplitMix64 a3 = r.rng("rhythm", "kick");
    SplitMix64 bb = r.rng("rhythm", "hats");
    SplitMix64 cc = r.rng("kick", "rhythm");
    EXPECT_NE(a3.nextU64(), bb.nextU64());
    EXPECT_NE(a3.nextU64(), cc.nextU64());
}

// ── Euclidean ──

TEST(GenerativeEuclidean, KnownVector3Over8)
{
    EXPECT_EQ(euclideanSteps(3, 8), (std::vector<int>{ 2, 5, 7 }));
}

TEST(GenerativeEuclidean, Rotation)
{
    // base [2,5,7] rotated by 1 -> [3,6,0] -> sorted [0,3,6]
    EXPECT_EQ(euclideanSteps(3, 8, 1), (std::vector<int>{ 0, 3, 6 }));
}

TEST(GenerativeEuclidean, EdgeCases)
{
    EXPECT_TRUE(euclideanSteps(0, 8).empty());
    EXPECT_TRUE(euclideanSteps(3, 0).empty());
    EXPECT_TRUE(euclideanSteps(-1, 8).empty());
    EXPECT_EQ(euclideanSteps(8, 8), (std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }));
    EXPECT_EQ(euclideanSteps(9, 8), (std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }));
}

TEST(GenerativeEuclidean, HitCountMatchesK)
{
    for (int k = 1; k < 16; ++k)
        EXPECT_EQ(static_cast<int>(euclideanSteps(k, 16).size()), k);
}

// ── mapStepsToDivision ──

TEST(GenerativeMap, IdentityExpansionAndResample)
{
    EXPECT_EQ(mapStepsToDivision({ 0, 2 }, 4, 4), (std::vector<int>{ 0, 2 }));
    EXPECT_EQ(mapStepsToDivision({ 0 }, 4, 16), (std::vector<int>{ 0 }));
    EXPECT_EQ(mapStepsToDivision({ 0, 1 }, 2, 8), (std::vector<int>{ 0, 4 }));
    // non-integer: 3 source steps -> 16 division, rounded + deduped
    auto r = mapStepsToDivision({ 0, 1, 2 }, 3, 16);
    EXPECT_FALSE(r.empty());
    for (int x : r) { EXPECT_GE(x, 0); EXPECT_LT(x, 16); }
    EXPECT_TRUE(std::is_sorted(r.begin(), r.end()));
}

// ── Rhythm DSL ──

TEST(GenerativeDsl, BasicSequences)
{
    EXPECT_EQ(expandToDivision("xxxx", 4), (std::vector<int>{ 0, 1, 2, 3 }));
    EXPECT_EQ(expandToDivision("x-x-", 4), (std::vector<int>{ 0, 2 }));
    EXPECT_EQ(expandToDivision("x---", 16), (std::vector<int>{ 0 }));
}

TEST(GenerativeDsl, IgnoresWhitespaceAndUnderscore)
{
    EXPECT_EQ(expandToDivision("x - x -", 4), (std::vector<int>{ 0, 2 }));
    EXPECT_EQ(expandToDivision("x_x_", 4), (std::vector<int>{ 0, 2 }));
}

TEST(GenerativeDsl, GroupRepeat)
{
    EXPECT_EQ(expandToDivision("[xx]x2", 4), (std::vector<int>{ 0, 1, 2, 3 }));
    EXPECT_EQ(expandToDivision("[x-]x2", 4), (std::vector<int>{ 0, 2 }));
}

TEST(GenerativeDsl, EuclideanToken)
{
    EXPECT_EQ(expandToDivision("E(3,8)", 8), (std::vector<int>{ 2, 5, 7 }));
    EXPECT_EQ(expandToDivision("E(3,8)", 16), (std::vector<int>{ 4, 10, 14 }));
    EXPECT_EQ(expandToDivision("E(3,8,1)", 8), (std::vector<int>{ 0, 3, 6 }));
}

TEST(GenerativeDsl, CombinedPattern)
{
    // [x-]x2 -> steps 0,2 ; E(2,4) -> hits 1,3 mapped after 4 source steps
    EXPECT_EQ(expandToDivision("[x-]x2 E(2,4)", 8), (std::vector<int>{ 0, 2, 5, 7 }));
}

TEST(GenerativeDsl, EmptyAndZeroDivision)
{
    EXPECT_TRUE(expandToDivision("", 16).empty());
    EXPECT_TRUE(expandToDivision("xxxx", 0).empty());
    EXPECT_TRUE(expandToDivision("----", 16).empty()); // all rests
}

TEST(GenerativeDsl, MalformedThrows)
{
    EXPECT_THROW(expandToDivision("E(3,8", 16), std::invalid_argument);   // unterminated E
    EXPECT_THROW(expandToDivision("[xx", 16), std::invalid_argument);     // unterminated group
    EXPECT_THROW(expandToDivision("[]", 16), std::invalid_argument);      // empty group
    EXPECT_THROW(expandToDivision("E(-1,8)", 16), std::invalid_argument); // k <= 0
    EXPECT_THROW(expandToDivision("E(3,0)", 16), std::invalid_argument);  // n <= 0
    EXPECT_THROW(expandToDivision("E(3,8,-1)", 16), std::invalid_argument); // negative rotation
    EXPECT_THROW(expandToDivision("z", 16), std::invalid_argument);       // bad token
    EXPECT_THROW(expandToDivision("xx]", 16), std::invalid_argument);     // trailing garbage
}

// ── Micro-timing helpers ──

TEST(GenerativeHumanize, StaysWithinWindowAndDeterministic)
{
    SplitMix64 a(55), b(55);
    for (int i = 0; i < 500; ++i)
    {
        int va = humanizeInt(a, 100, -5, 5);
        int vb = humanizeInt(b, 100, -5, 5);
        EXPECT_EQ(va, vb);
        EXPECT_GE(va, 95);
        EXPECT_LE(va, 105);
    }
}

TEST(GenerativeSwing, Behaviour)
{
    const int s16 = 240;
    EXPECT_EQ(applySwing(100, 2, 70, s16), 100);        // even step: unchanged
    EXPECT_EQ(applySwing(100, 1, 50, s16), 100);        // 50% = straight
    EXPECT_EQ(applySwing(240, 1, 75, s16), 300);        // +25% -> +60 ticks
    EXPECT_EQ(applySwing(240, 1, 25, s16), 180);        // -25% -> -60 ticks
    EXPECT_EQ(applySwing(0, 1, 0, s16), 0);             // clamped at zero
}

// ── Pitch-motion helpers ──

TEST(GenerativeMarkov, WeightedChoiceDeterministicAndBounded)
{
    SplitMix64 a(11), b(11);
    std::vector<std::pair<int, double>> cands{ { 0, 1.0 }, { 1, 2.0 }, { 2, 0.0 } };
    for (int i = 0; i < 200; ++i)
    {
        int va = weightedChoice(a, cands);
        int vb = weightedChoice(b, cands);
        EXPECT_EQ(va, vb);
        EXPECT_GE(va, 0);
        EXPECT_LE(va, 2);
    }
    SplitMix64 c(12);
    EXPECT_EQ(weightedChoice(c, { { 42, 0.0 } }), 42); // only candidate
}

TEST(GenerativeMarkov, NextDegreeStaysInRangeAndDeterministic)
{
    SplitMix64 a(21), b(21);
    for (int i = 0; i < 1000; ++i)
    {
        int va = nextMarkovDegree(a, 3, 7);
        int vb = nextMarkovDegree(b, 3, 7);
        EXPECT_EQ(va, vb);
        EXPECT_GE(va, 0);
        EXPECT_LT(va, 7);
    }
}
