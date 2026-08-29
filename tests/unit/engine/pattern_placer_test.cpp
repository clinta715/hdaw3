#include <gtest/gtest.h>
#include <algorithm>
#include "engine/PatternPlacer.h"

// Pattern placement (docs/plans/2026-08-29-jungle-dnb-feature-gaps.md P2-2):
// pure, header-only, deterministic tiling math — no JUCE, no audio. Gates G1
// (octave shift, velocity clamping, retrograde span preservation, start
// offset, determinism). The cyclic placement rule
// (placement j uses patterns[j % patterns.size()]) lives in the command and
// is covered by McpCoverageTest.PlacePatternsCyclicPlacement.

namespace {

using Note = PatternPlacer::PatternNote;
using Placement = PatternPlacer::Placement;

Note n(int pitch, double start, double dur, int vel = 100)
{
    return Note{pitch, start, dur, vel};
}

std::vector<Note> placed(const std::vector<Note>& pattern, const Placement& p)
{
    return PatternPlacer::place(pattern, p);
}

} // namespace

// G1: octave shift moves every pitch up +12 per octave; timing + velocity stay
// byte-identical.
TEST(PatternPlacer, OctaveShiftAddsTwelveAndKeepsTiming)
{
    const std::vector<Note> pattern = { n(60, 0.0, 1.0, 100),
                                        n(64, 1.0, 0.5, 80),
                                        n(67, 2.0, 1.5, 120) };
    Placement p;
    p.octaveShift = 1;
    auto out = placed(pattern, p);

    ASSERT_EQ(out.size(), pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        EXPECT_EQ(out[i].pitch, pattern[i].pitch + 12) << "i=" << i;
        EXPECT_DOUBLE_EQ(out[i].startBeat, pattern[i].startBeat) << "i=" << i;
        EXPECT_DOUBLE_EQ(out[i].durationBeats, pattern[i].durationBeats) << "i=" << i;
        EXPECT_EQ(out[i].velocity, pattern[i].velocity) << "i=" << i;
    }

    // Negative octaves shift down.
    Placement p2;
    p2.octaveShift = -2;
    auto out2 = placed(pattern, p2);
    EXPECT_EQ(out2[0].pitch, 60 - 24);
}

// G1: velocity scaling — 0.5 halves (100 -> 50), 3.0 clamps at 127,
// 0.01 clamps at 1. Timing and pitch untouched.
TEST(PatternPlacer, VelocityScaleClamps)
{
    const std::vector<Note> pattern = { n(60, 0.0, 1.0, 100) };

    Placement half;
    half.velocityScale = 0.5;
    auto out = placed(pattern, half);
    EXPECT_EQ(out[0].velocity, 50);
    EXPECT_EQ(out[0].pitch, 60);

    Placement loud;
    loud.velocityScale = 3.0;
    EXPECT_EQ(placed(pattern, loud)[0].velocity, 127);

    Placement quiet;
    quiet.velocityScale = 0.01;
    EXPECT_EQ(placed(pattern, quiet)[0].velocity, 1);

    // Rounding: 65 * 0.5 = 32.5 -> 33 (lround half-away-from-zero).
    const std::vector<Note> odd = { n(60, 0.0, 1.0, 65) };
    EXPECT_EQ(placed(odd, half)[0].velocity, 33);
}

// G1: reverse is a time-order retrograde — the first note (by time) becomes
// the last, each note keeps its own duration, and the occupied span
// [0, max(start + duration)) is preserved exactly.
TEST(PatternPlacer, ReverseRetrogradesTimeOrder)
{
    // Equal durations: starts 0/1/2 dur 1 -> mirrored starts 2/1/0.
    const std::vector<Note> pattern = { n(60, 0.0, 1.0),
                                        n(64, 1.0, 1.0),
                                        n(67, 2.0, 1.0) };
    Placement p;
    p.reverse = true;
    auto out = placed(pattern, p);

    // Output keeps input order (60/64/67) with DESCENDING starts.
    ASSERT_EQ(out.size(), 3u);
    EXPECT_DOUBLE_EQ(out[0].startBeat, 2.0);
    EXPECT_DOUBLE_EQ(out[1].startBeat, 1.0);
    EXPECT_DOUBLE_EQ(out[2].startBeat, 0.0);
    // Every duration preserved.
    for (const auto& nn : out)
        EXPECT_DOUBLE_EQ(nn.durationBeats, 1.0);

    // Sorted by time: 67@0, 64@1, 60@2 — the retrograde of 60/64/67.
    auto sorted = out;
    std::sort(sorted.begin(), sorted.end(),
              [](const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
    EXPECT_EQ(sorted[0].pitch, 67);
    EXPECT_EQ(sorted[1].pitch, 64);
    EXPECT_EQ(sorted[2].pitch, 60);
    // Same occupied span [0, 3).
    double minStart = sorted.front().startBeat;
    double maxEnd = 0.0;
    for (const auto& nn : sorted)
        maxEnd = (std::max)(maxEnd, nn.startBeat + nn.durationBeats);
    EXPECT_DOUBLE_EQ(minStart, 0.0);
    EXPECT_DOUBLE_EQ(maxEnd, 3.0);

    // Unequal durations: mirrored start = span - start - duration keeps the
    // occupied span identical even though durations differ.
    const std::vector<Note> ragged = { n(60, 0.0, 0.5),
                                       n(64, 1.0, 2.0),
                                       n(67, 2.0, 1.0) };
    auto outR = placed(ragged, p);
    // span = max(0.5, 3.0, 3.0) = 3.0
    auto expectStart = [&](int pitch, double start) {
        for (const auto& nn : outR)
            if (nn.pitch == pitch) { EXPECT_DOUBLE_EQ(nn.startBeat, start); return; }
        FAIL() << "missing pitch " << pitch;
    };
    expectStart(60, 3.0 - 0.0 - 0.5);
    expectStart(64, 3.0 - 1.0 - 2.0);
    expectStart(67, 3.0 - 2.0 - 1.0);
    double rMin = 1e9, rMax = 0.0;
    for (const auto& nn : outR)
    {
        rMin = (std::min)(rMin, nn.startBeat);
        rMax = (std::max)(rMax, nn.startBeat + nn.durationBeats);
    }
    EXPECT_DOUBLE_EQ(rMin, 0.0);
    EXPECT_DOUBLE_EQ(rMax, 3.0);
}

// G1: placement.start offsets every start by the given beat amount; nothing
// else changes.
TEST(PatternPlacer, PlaceOffsetsByStart)
{
    const std::vector<Note> pattern = { n(60, 0.0, 1.0, 100),
                                        n(64, 1.5, 0.5, 90) };
    Placement p;
    p.start = 8.0;
    auto out = placed(pattern, p);

    ASSERT_EQ(out.size(), 2u);
    EXPECT_DOUBLE_EQ(out[0].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(out[1].startBeat, 9.5);
    EXPECT_EQ(out[0].pitch, 60);
    EXPECT_EQ(out[0].velocity, 100);
    EXPECT_DOUBLE_EQ(out[1].durationBeats, 0.5);

    // Offsets compose with reverse.
    Placement pr;
    pr.start = 16.0;
    pr.reverse = true;
    const std::vector<Note> tri = { n(60, 0.0, 1.0), n(64, 1.0, 1.0), n(67, 2.0, 1.0) };
    auto outR = placed(tri, pr);
    EXPECT_DOUBLE_EQ(outR[0].startBeat, 16.0 + 2.0);
    EXPECT_DOUBLE_EQ(outR[1].startBeat, 16.0 + 1.0);
    EXPECT_DOUBLE_EQ(outR[2].startBeat, 16.0 + 0.0);
}

// G1: determinism — identical inputs produce byte-identical outputs.
TEST(PatternPlacer, Determinism)
{
    const std::vector<Note> pattern = { n(60, 0.0, 1.0, 100),
                                        n(64, 1.0, 0.5, 80),
                                        n(67, 2.0, 1.5, 120),
                                        n(55, 3.5, 0.25, 60) };
    for (const Placement& p : { Placement{0.0, 0, 1.0, false},
                                Placement{8.0, 2, 0.5, true},
                                Placement{16.0, -1, 1.5, false} })
    {
        auto a = placed(pattern, p);
        auto b = placed(pattern, p);
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].pitch, b[i].pitch) << "i=" << i;
            EXPECT_DOUBLE_EQ(a[i].startBeat, b[i].startBeat) << "i=" << i;
            EXPECT_DOUBLE_EQ(a[i].durationBeats, b[i].durationBeats) << "i=" << i;
            EXPECT_EQ(a[i].velocity, b[i].velocity) << "i=" << i;
        }
    }
}

// G1: an empty pattern places nothing (the command skips it in the tile loop).
TEST(PatternPlacer, EmptyPatternPlacesNothing)
{
    EXPECT_TRUE(placed({}, Placement{0.0, 0, 1.0, false}).empty());
    Placement p;
    p.reverse = true;
    EXPECT_TRUE(placed({}, p).empty());
}
