// Unit tests for the automation preset bank (P2-3, docs/plans/
// 2026-08-29-jungle-dnb-feature-gaps.md): src/engine/AutomationPreset.h.
//
// Contract under test: plan() emits EnvelopeGenerator::Params in BEATS with a
// per-beat density of 4.0 (a 0.25-beat grid); the command layer converts
// beats→seconds and scales density to per-second at write time (120 BPM →
// ×2). EnvelopeGenerator is unit-agnostic, so generating directly on the
// plan's params yields exactly the point counts the command produces at
// 120 BPM — an 8-beat window at density 4 produces ~32 points either way.
#include <gtest/gtest.h>
#include "engine/AutomationPreset.h"
#include "engine/EnvelopeGenerator.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using HDAW::AutomationPreset;
using HDAW::EnvelopeGenerator;

// Generate all plan segments (unit-agnostic, beats domain) and concatenate.
std::vector<std::pair<double, double>> generatePlan(
    const AutomationPreset::PresetWindow& w, uint64_t seed)
{
    std::vector<std::pair<double, double>> pts;
    const auto plan = AutomationPreset::plan(w, seed);
    for (const auto& seg : plan.segments)
    {
        const auto g = EnvelopeGenerator::generate(seg);
        pts.insert(pts.end(), g.begin(), g.end());
    }
    return pts;
}

AutomationPreset::PresetWindow window(AutomationPreset::Preset p, double start, double end)
{
    AutomationPreset::PresetWindow w;
    w.start = start;
    w.end = end;
    w.preset = p;
    return w;
}

} // namespace

// Bottom of the deterministic contract: same window + seed -> identical plan
// and identical generated point lists (seed 0 is random by design, so the
// test pins a nonzero seed).
TEST(AutomationPreset, DeterministicSameParamsSamePlan)
{
    const auto w = window(AutomationPreset::Preset::Pump, 0.0, 8.0);
    const auto a = AutomationPreset::plan(w, 42);
    const auto b = AutomationPreset::plan(w, 42);
    ASSERT_EQ(a.segments.size(), b.segments.size());
    ASSERT_FALSE(a.segments.empty());

    const auto& pa = a.segments[0];
    const auto& pb = b.segments[0];
    EXPECT_EQ(pa.shape, pb.shape);
    EXPECT_DOUBLE_EQ(pa.startTime, pb.startTime);
    EXPECT_DOUBLE_EQ(pa.endTime, pb.endTime);
    EXPECT_DOUBLE_EQ(pa.startValue, pb.startValue);
    EXPECT_DOUBLE_EQ(pa.endValue, pb.endValue);
    EXPECT_DOUBLE_EQ(pa.cycles, pb.cycles);
    EXPECT_DOUBLE_EQ(pa.densityPerSec, pb.densityPerSec);
    EXPECT_EQ(pa.seed, pb.seed);

    const auto ga = generatePlan(w, 42);
    const auto gb = generatePlan(w, 42);
    ASSERT_EQ(ga.size(), gb.size());
    for (size_t i = 0; i < ga.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(ga[i].first, gb[i].first);
        EXPECT_DOUBLE_EQ(ga[i].second, gb[i].second);
    }
}

// Pump = one triangle cycle per beat between default 0.70 and 1.00 on a
// 0.25-beat grid: an 8-beat window at density 4 => 32 points, all inside
// [0.70, 1.00], starting at the trough (first value ≈ 0.70).
TEST(AutomationPreset, PumpProducesPerBeatTriangle)
{
    const auto w = window(AutomationPreset::Preset::Pump, 0.0, 8.0);
    const auto plan = AutomationPreset::plan(w, 7);
    ASSERT_EQ(plan.segments.size(), 1u);
    const auto& seg = plan.segments[0];
    EXPECT_EQ(seg.shape, EnvelopeGenerator::Shape::Triangle);
    EXPECT_DOUBLE_EQ(seg.cycles, 8.0);        // one cycle per beat
    EXPECT_DOUBLE_EQ(seg.densityPerSec, 4.0); // 0.25-beat grid
    EXPECT_DOUBLE_EQ(seg.startValue, 0.70);
    EXPECT_DOUBLE_EQ(seg.endValue, 1.00);

    const auto pts = generatePlan(w, 7);
    EXPECT_EQ(pts.size(), 32u); // 8 beats * 4 points/beat
    EXPECT_NEAR(pts.front().second, 0.70, 1e-9); // triangle starts at the trough
    for (const auto& [t, v] : pts)
    {
        EXPECT_GE(v, 0.70 - 1e-9);
        EXPECT_LE(v, 1.00 + 1e-9);
    }
    // Peak halfway through each period must be reached (bounce, not a flat band).
    double maxV = 0.0;
    for (const auto& [t, v] : pts) maxV = std::max(maxV, v);
    EXPECT_GT(maxV, 0.95);
}

// Macro = linear ramp from default 0.15 to 0.60; values never decrease.
TEST(AutomationPreset, MacroMonotonicRamp)
{
    const auto w = window(AutomationPreset::Preset::Macro, 0.0, 8.0);
    const auto plan = AutomationPreset::plan(w, 3);
    ASSERT_EQ(plan.segments.size(), 1u);
    EXPECT_EQ(plan.segments[0].shape, EnvelopeGenerator::Shape::Ramp);
    EXPECT_DOUBLE_EQ(plan.segments[0].startValue, 0.15);
    EXPECT_DOUBLE_EQ(plan.segments[0].endValue, 0.60);

    const auto pts = generatePlan(w, 3);
    ASSERT_FALSE(pts.empty());
    EXPECT_NEAR(pts.front().second, 0.15, 1e-9);
    EXPECT_NEAR(pts.back().second, 0.60, 1e-9);
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_GE(pts[i].second + 1e-9, pts[i - 1].second); // non-decreasing
}

// OpenClose = S-curve down leg (0.60 -> 0.05) then ramp up leg (0.05 -> 0.90):
// there exist t1 < t2 < t3 with v(t1) > v(t2) (down) and v(t2) < v(t3) (up),
// and the valley sits near the window centre at ≈ 0.05.
TEST(AutomationPreset, OpenCloseDownThenUp)
{
    const auto w = window(AutomationPreset::Preset::OpenClose, 0.0, 8.0);
    const auto plan = AutomationPreset::plan(w, 5);
    ASSERT_EQ(plan.segments.size(), 2u);
    EXPECT_EQ(plan.segments[0].shape, EnvelopeGenerator::Shape::SCurve);
    EXPECT_EQ(plan.segments[1].shape, EnvelopeGenerator::Shape::Ramp);
    EXPECT_DOUBLE_EQ(plan.segments[0].startValue, 0.60);
    EXPECT_DOUBLE_EQ(plan.segments[0].endValue, 0.05); // closed at the midpoint
    EXPECT_DOUBLE_EQ(plan.segments[1].startValue, 0.05);
    EXPECT_DOUBLE_EQ(plan.segments[1].endValue, 0.90);

    const auto pts = generatePlan(w, 5);
    ASSERT_GE(pts.size(), 30u); // two density-4 legs over 8 beats (~64 pts)
    EXPECT_NEAR(pts.front().second, 0.60, 1e-9);
    EXPECT_NEAR(pts.back().second, 0.90, 1e-9);

    // Valley: the minimum value ≈ 0.05, located inside [2, 6] (near centre 4).
    double minV = 1.0, minT = -1.0;
    for (const auto& [t, v] : pts)
        if (v < minV) { minV = v; minT = t; }
    EXPECT_NEAR(minV, 0.05, 1e-6);
    EXPECT_GE(minT, 2.0);
    EXPECT_LE(minT, 6.0);

    // t1 (window open) > valley and valley < t3 (window reopen): down then up.
    EXPECT_GT(pts.front().second, minV);
    EXPECT_GT(pts.back().second, minV);
    EXPECT_NEAR(pts.front().second, 0.60, 1e-6);
    EXPECT_NEAR(pts.back().second, 0.90, 1e-6);
}

// Riser = monotonic S-curve from default 0.10 to 0.90.
TEST(AutomationPreset, RiserMonotonicUp)
{
    const auto w = window(AutomationPreset::Preset::Riser, 0.0, 8.0);
    const auto plan = AutomationPreset::plan(w, 1);
    ASSERT_EQ(plan.segments.size(), 1u);
    EXPECT_EQ(plan.segments[0].shape, EnvelopeGenerator::Shape::SCurve);

    const auto pts = generatePlan(w, 1);
    ASSERT_FALSE(pts.empty());
    EXPECT_NEAR(pts.front().second, 0.10, 1e-9);
    EXPECT_NEAR(pts.back().second, 0.90, 1e-9);
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_GE(pts[i].second + 1e-9, pts[i - 1].second); // non-decreasing
}

// Gate 9: inverted or zero-length windows must produce an empty plan, not a
// crash or garbage; a valid window still plans.
TEST(AutomationPreset, WindowValidationRejectsInverted)
{
    EXPECT_TRUE(AutomationPreset::plan(window(AutomationPreset::Preset::Pump, 8.0, 4.0), 1)
                    .segments.empty());
    EXPECT_TRUE(AutomationPreset::plan(window(AutomationPreset::Preset::Pump, 4.0, 4.0), 1)
                    .segments.empty());
    EXPECT_TRUE(AutomationPreset::plan(window(AutomationPreset::Preset::OpenClose, 16.0, 2.0), 1)
                    .segments.empty());
    EXPECT_FALSE(AutomationPreset::plan(window(AutomationPreset::Preset::Pump, 0.0, 4.0), 1)
                     .segments.empty());
}

// Name <-> enum round trip for all six presets; unknown names reject.
TEST(AutomationPreset, PresetNameRoundTrip)
{
    const AutomationPreset::Preset all[] = {
        AutomationPreset::Preset::Pump,
        AutomationPreset::Preset::Macro,
        AutomationPreset::Preset::OpenClose,
        AutomationPreset::Preset::Riser,
        AutomationPreset::Preset::Sine,
        AutomationPreset::Preset::Square
    };
    for (const auto p : all)
    {
        const std::string name = AutomationPreset::presetName(p);
        EXPECT_FALSE(name.empty());
        const auto back = AutomationPreset::presetFromName(name);
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, p);
    }
    EXPECT_FALSE(AutomationPreset::presetFromName("bogus").has_value());
    EXPECT_FALSE(AutomationPreset::presetFromName("").has_value());
    EXPECT_FALSE(AutomationPreset::presetFromName("Pump").has_value()); // exact names only
    // Documentation table covers every preset with a non-empty line.
    EXPECT_EQ(AutomationPreset::kPresetDocumentationCount, 6u);
    for (std::size_t i = 0; i < AutomationPreset::kPresetDocumentationCount; ++i)
    {
        EXPECT_TRUE(AutomationPreset::kPresetDocumentation[i].name != nullptr);
        EXPECT_TRUE(AutomationPreset::kPresetDocumentation[i].line != nullptr);
        EXPECT_EQ(AutomationPreset::presetFromName(
                      AutomationPreset::kPresetDocumentation[i].name).has_value(), true);
    }
}

// A full 48-beat (12-bar) drop-length pump stays on the 0.25-beat grid (192
// points) well under the generator's 4096-point cap, values inside [0.70,1.00].
TEST(AutomationPreset, FortyEightBeatPumpCoarseGridOk)
{
    const auto w = window(AutomationPreset::Preset::Pump, 0.0, 48.0);
    const auto plan = AutomationPreset::plan(w, 11);
    ASSERT_EQ(plan.segments.size(), 1u);
    EXPECT_DOUBLE_EQ(plan.segments[0].cycles, 48.0);

    const auto pts = generatePlan(w, 11);
    EXPECT_EQ(pts.size(), 192u); // 48 beats * 4 points/beat
    for (const auto& [t, v] : pts)
    {
        EXPECT_GE(v, 0.70 - 1e-9);
        EXPECT_LE(v, 1.00 + 1e-9);
    }
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_GT(pts[i].first, pts[i - 1].first); // strictly increasing time axis
}
