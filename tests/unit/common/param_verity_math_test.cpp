// ParamVerity verdict math — pure unit tests (no engine, no renders).
// Contract: docs/plans/2026-09-22-param-verity-pipeline.md + ParamVerity.h.
// Encodes the lesson-27 discipline: only multi-x separations count, a silent
// baseline proves nothing, and the Xenia case (spread > separation) must NOT
// claim an effect.
#include <gtest/gtest.h>

#include "common/ParamVerity.h"

namespace {

double mean(const std::vector<double>& v)
{
    double s = 0.0;
    for (const double x : v) s += x;
    return s / static_cast<double>(v.size());
}

} // namespace

TEST(ParamVerityMath, SpreadIsMaxDeviationFromMean)
{
    // Tight, deterministic renders: spread = max |run - mean|.
    const std::vector<double> runs{ 0.100, 0.104, 0.098 };
    const double m = mean(runs);                       // 0.100667
    EXPECT_NEAR(HDAW::veritySpread(runs, m), 0.003334, 1e-6);
}

TEST(ParamVerityMath, ThresholdFloorAppliesWhenSpreadIsZero)
{
    // One run (no variance evidence) or identical runs -> floor 1e-4.
    EXPECT_NEAR(HDAW::verityThreshold(0.0), HDAW::kVerityMinSeparation, 1e-12);
    const std::vector<double> identical{ 0.2, 0.2 };
    EXPECT_NEAR(HDAW::verityThreshold(HDAW::veritySpread(identical, 0.2)),
                HDAW::kVerityMinSeparation, 1e-12);
}

TEST(ParamVerityMath, StrongSeparationIsAudible)
{
    // Baseline ~0.10 with tiny spread; a sweep step at 0.16 separates by
    // 0.06 — far above 3x spread. Audible.
    const std::vector<double> base{ 0.100, 0.102, 0.099 };
    const double m = mean(base);
    const double spread = HDAW::veritySpread(base, m);
    const double threshold = HDAW::verityThreshold(spread);
    EXPECT_TRUE(HDAW::verityAudible(true, 0.16, m, threshold));
    // Direction does not matter: a quieter render separates too.
    EXPECT_TRUE(HDAW::verityAudible(true, 0.04, m, threshold));
}

TEST(ParamVerityMath, XeniaStyleSpreadAboveSeparationIsNotAudible)
{
    // The measured Xenia case (hardware-va-suite §9): same-input spread 0.0056
    // on ~0.05 RMS; a candidate delta of 0.0023 does NOT clear 3x spread —
    // no effect may be claimed.
    const std::vector<double> base{ 0.0450, 0.0560, 0.0500 };
    const double m = mean(base);
    const double threshold = HDAW::verityThreshold(HDAW::veritySpread(base, m));
    EXPECT_FALSE(HDAW::verityAudible(true, m + 0.0023, m, threshold));
}

TEST(ParamVerityMath, SilentBaselineIsInconclusiveNeverAudible)
{
    // Lesson 25: silence masks every delta. verityAudible must be false no
    // matter how large the numeric delta looks against a silent baseline.
    EXPECT_FALSE(HDAW::verityAudible(/*baselineAudible=*/false, 0.5, 0.0, 1e-4));
}

TEST(ParamVerityMath, SeparationBelowFloorIsNotAudible)
{
    // Numerically dead renders (e.g. identical silence-adjacent dust) must
    // still respect the absolute floor.
    EXPECT_FALSE(HDAW::verityAudible(true, 0.10000, 0.100005, 1e-4));
    EXPECT_TRUE(HDAW::verityAudible(true, 0.1002, 0.100005, 1e-4));
}
