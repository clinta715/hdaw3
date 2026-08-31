#include <gtest/gtest.h>
#include "engine/BreakPatternGenerator.h"

// Break chopper/composer (docs/plans/2026-08-29-jungle-dnb-feature-gaps.md
// P2-1): pure, seeded, deterministic slice-trigger pattern generation.
// Gates G1 (determinism, range, amen template sanity, bounded counts,
// velocity humanization) — the generator is exercised directly (no JUCE,
// no audio).

namespace {

using Style = BreakPatternGenerator::Style;
using Step  = BreakPatternGenerator::Step;
using Note  = BreakPatternGenerator::Note;

std::vector<Note> notes(const BreakPatternGenerator::Params& p, int baseNote = 60)
{
    return BreakPatternGenerator::asNotes(p, BreakPatternGenerator::generate(p), baseNote);
}

} // namespace

// G1: determinism — identical params + seed -> identical note lists.
TEST(BreakPatternGenerator, DeterministicSameSeedSameNotes)
{
    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
    {
        BreakPatternGenerator::Params p;
        p.style = s;
        p.bars = 8;
        p.ghostFills = 2;
        p.seed = 7;
        p.dropFirst = true;
        auto a = notes(p, 60);
        auto b = notes(p, 60);
        ASSERT_EQ(a.size(), b.size()) << BreakPatternGenerator::styleName(s);
        for (size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(a[i].startBeat, b[i].startBeat) << "style=" << BreakPatternGenerator::styleName(s) << " i=" << i;
            EXPECT_EQ(a[i].pitch, b[i].pitch) << "style=" << BreakPatternGenerator::styleName(s) << " i=" << i;
            EXPECT_EQ(a[i].velocity, b[i].velocity) << "style=" << BreakPatternGenerator::styleName(s) << " i=" << i;
        }
    }
}

// G1: seed == 0 resolves to the stable default (12345), never to a time/pid
// dependent value — repeated runs with seed 0 must be identical.
TEST(BreakPatternGenerator, SeedZeroUsesStableDefault)
{
    BreakPatternGenerator::Params p0;
    p0.seed = 0;
    p0.style = Style::JungleEdit;
    p0.bars = 4;
    BreakPatternGenerator::Params p1 = p0;
    p1.seed = BreakPatternGenerator::kDefaultSeed;
    EXPECT_EQ(notes(p0, 60), notes(p1, 60));
}

// G1: range — every pitch within [baseNote, baseNote+sliceCount-1] and every
// startBeat within [0, bars*4) across all styles/grids.
TEST(BreakPatternGenerator, RangePitchesAndBeats)
{
    const int sliceCount = 23;
    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
        for (int grid : { 1, 2, 4, 8 })
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = sliceCount;
            p.bars = 5;
            p.grid = grid;
            p.style = s;
            p.ghostFills = 2;
            p.seed = 99;
            const auto ns = notes(p, 60);
            for (const auto& n : ns)
            {
                EXPECT_GE(n.pitch, 60);
                EXPECT_LE(n.pitch, 60 + sliceCount - 1);
                EXPECT_GE(n.startBeat, 0.0);
                EXPECT_LT(n.startBeat, 5.0 * 4.0);
            }
        }
}

// G1: amen template sanity — bar 0 has a kick at step 0 (when !dropFirst)
// and snares at 16ths 4 & 12 (beats 1.0 and 3.0).
TEST(BreakPatternGenerator, AmenTemplateSkeleton)
{
    BreakPatternGenerator::Params p;
    p.sliceCount = 8;
    p.bars = 1;
    p.grid = 4;
    p.style = Style::Amen;
    p.ghostFills = 0;
    p.seed = 12345;

    auto ns = notes(p, 60);
    ASSERT_EQ(ns.size(), 6u);
    // beats: 0 (kick, slice 0), 1 (snare, slice 2), 2 (kick, slice 4),
    //       3 (snare, slice 2), 3.5 (fill, slice 4), 3.75 (fill, slice 5).
    const double beats[]   = { 0.0, 1.0, 2.0, 3.0, 3.5, 3.75 };
    const int    pitches[] = { 60,  62,  64,  62,  64,  65  };
    for (size_t i = 0; i < ns.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(ns[i].startBeat, beats[i]) << "i=" << i;
        EXPECT_EQ(ns[i].pitch, pitches[i]) << "i=" << i;
    }
    // kick/snare/fill velocities are humanized within the velocity range.
    for (const auto& n : ns)
    {
        EXPECT_GE(n.velocity, p.velocityMin);
        EXPECT_LE(n.velocity, p.velocityMax);
    }
}

// G1: dropFirst makes step 0 of bar 0 a REST (no note at startBeat 0).
TEST(BreakPatternGenerator, AmenDropFirstRestsStepZero)
{
    BreakPatternGenerator::Params p;
    p.sliceCount = 8;
    p.bars = 1;
    p.grid = 4;
    p.style = Style::Amen;
    p.ghostFills = 0;
    p.dropFirst = true;
    p.seed = 12345;

    auto ns = notes(p, 60);
    ASSERT_EQ(ns.size(), 5u);
    EXPECT_GT(ns.front().startBeat, 0.0);
    EXPECT_DOUBLE_EQ(ns.front().startBeat, 1.0); // opens on the snare
    for (const auto& n : ns)
        EXPECT_NE(n.startBeat, 0.0);
}

// G1: note counts match the documented template table (grid=4) and the
// hard bound notes <= bars*16 + 2*bars holds for every style.
TEST(BreakPatternGenerator, NoteCountMatchesDocsTableAndIsBounded)
{
    const int bars = 8;
    for (int g : { 0, 1, 2 })
    {
        // amen: 6+g per bar
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = 16; p.bars = bars; p.style = Style::Amen; p.ghostFills = g; p.seed = 5;
            EXPECT_EQ(notes(p, 60).size(), static_cast<size_t>(bars * (6 + g)));
        }
        // twoStep: 4+g per bar
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = 16; p.bars = bars; p.style = Style::TwoStep; p.ghostFills = g; p.seed = 5;
            EXPECT_EQ(notes(p, 60).size(), static_cast<size_t>(bars * (4 + g)));
        }
        // halftime: 3+g per bar
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = 16; p.bars = bars; p.style = Style::Halftime; p.ghostFills = g; p.seed = 5;
            EXPECT_EQ(notes(p, 60).size(), static_cast<size_t>(bars * (3 + g)));
        }
        // jungleEdit: amen skeleton (6+g)/bar minus the always-dropped bar-0 kick
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = 16; p.bars = bars; p.style = Style::JungleEdit; p.ghostFills = g; p.seed = 5;
            EXPECT_EQ(notes(p, 60).size(), static_cast<size_t>(bars * (6 + g) - 1));
        }
    }

    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
        for (uint64_t seed : { 1ull, 7ull, 12345ull })
        {
            BreakPatternGenerator::Params p;
            p.sliceCount = 24; p.bars = 8; p.style = s; p.ghostFills = 2;
            p.dropFirst = true; p.seed = seed;
            const auto ns = notes(p, 60);
            EXPECT_LE(ns.size(), static_cast<size_t>(bars * 16 + 2 * bars))
                << "style=" << BreakPatternGenerator::styleName(s) << " seed=" << seed;
        }
}

// G1: velocity humanization — every velocity within [velocityMin, velocityMax].
TEST(BreakPatternGenerator, VelocitiesWithinRange)
{
    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
    {
        BreakPatternGenerator::Params p;
        p.sliceCount = 12; p.bars = 4; p.style = s; p.ghostFills = 2;
        p.velocityMin = 30; p.velocityMax = 110; p.seed = 42;
        for (const auto& n : notes(p, 60))
        {
            EXPECT_GE(n.velocity, 30);
            EXPECT_LE(n.velocity, 110);
        }
    }
}

// jungleEdit: the first kick is ALWAYS dropped (its signature edit), even
// without the dropFirst flag.
TEST(BreakPatternGenerator, JungleEditForcesDroppedFirstKick)
{
    BreakPatternGenerator::Params p;
    p.sliceCount = 8; p.bars = 1; p.grid = 4; p.style = Style::JungleEdit;
    p.ghostFills = 0; p.dropFirst = false; p.seed = 12345;
    auto ns = notes(p, 60);
    ASSERT_EQ(ns.size(), 5u);
    for (const auto& n : ns)
        EXPECT_NE(n.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(ns.front().startBeat, 1.0);
}

// Random: seeded walk — two runs identical; bounds hold; per-bar slice
// indices span the detected set.
TEST(BreakPatternGenerator, RandomWalkDeterministicAndBounded)
{
    BreakPatternGenerator::Params p;
    p.sliceCount = 16; p.bars = 4; p.style = Style::Random; p.seed = 77;
    auto a = notes(p, 60);
    auto b = notes(p, 60);
    ASSERT_EQ(a, b);
    EXPECT_GE(a.size(), static_cast<size_t>(4 * 6));     // skeleton always hits
    EXPECT_LE(a.size(), static_cast<size_t>(4 * 16 + 2 * 4));
    for (const auto& n : a)
    {
        EXPECT_GE(n.pitch, 60);
        EXPECT_LE(n.pitch, 75);
    }
}

// Grid resolutions keep every beat inside [0, bars*4) and stay deterministic.
TEST(BreakPatternGenerator, CoarseAndFineGridsStayInBounds)
{
    for (int grid : { 1, 2, 4, 8 })
    {
        BreakPatternGenerator::Params p;
        p.sliceCount = 10; p.bars = 3; p.grid = grid; p.style = Style::Amen;
        p.ghostFills = 2; p.seed = 3;
        auto a = notes(p, 60);
        auto b = notes(p, 60);
        EXPECT_EQ(a, b) << "grid=" << grid;
        EXPECT_FALSE(a.empty()) << "grid=" << grid;
        for (const auto& n : a)
        {
            EXPECT_GE(n.startBeat, 0.0);
            EXPECT_LT(n.startBeat, 12.0);
            EXPECT_GE(n.pitch, 60);
            EXPECT_LE(n.pitch, 69);
        }
    }
}

// Empty output when there are no slices (sliceCount <= 0).
TEST(BreakPatternGenerator, EmptyForNoSlices)
{
    BreakPatternGenerator::Params p;
    p.sliceCount = 0;
    EXPECT_TRUE(BreakPatternGenerator::generate(p).empty());
}

// Style name round-trip (used by the MCP tool).
TEST(BreakPatternGenerator, StyleNameRoundTrip)
{
    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
    {
        Style out;
        ASSERT_TRUE(BreakPatternGenerator::styleFromName(
            BreakPatternGenerator::styleName(s), out));
        EXPECT_EQ(out, s);
    }
    Style out;
    EXPECT_FALSE(BreakPatternGenerator::styleFromName("bogus", out));
}

// Regression (2026-09-02 session): with more detected slices than 7-bit MIDI
// can address from baseNote (e.g. 108 slices at baseNote 60 -> pitch 167),
// the generator emitted pitches > 127, which crashed the render thread
// (MidiClipProcessor's 128-wide pitch tables, MSVC "array subscript out of
// range"). The effective slice pool must cap at 128 - baseNote, and asNotes
// must additionally clamp defensively.
TEST(BreakPatternGenerator, PitchesStayMidiAddressableWhenSlicesExceedRange)
{
    for (Style s : { Style::Amen, Style::TwoStep, Style::Halftime,
                     Style::JungleEdit, Style::Random })
    {
        BreakPatternGenerator::Params p;
        p.style = s;
        p.sliceCount = 108;   // grid-sliced amen: 0.25-beat grid over 27 beats
        p.baseNote = 60;
        p.bars = 8;
        p.ghostFills = 2;
        p.seed = 4242;
        const auto ns = notes(p, 60);
        ASSERT_FALSE(ns.empty()) << BreakPatternGenerator::styleName(s);
        for (const auto& n : ns)
        {
            EXPECT_GE(n.pitch, 0) << "style=" << BreakPatternGenerator::styleName(s);
            EXPECT_LE(n.pitch, 127) << "style=" << BreakPatternGenerator::styleName(s);
        }
    }
}

// baseNote near the top of the range leaves few addressable slices; the
// generator must stay bounded (and non-empty while at least one slice fits).
TEST(BreakPatternGenerator, HighBaseNoteCapsSlicePool)
{
    BreakPatternGenerator::Params p;
    p.style = Style::Random;
    p.sliceCount = 40;
    p.baseNote = 120;         // only 8 addressable pitches (120..127)
    p.bars = 4;
    p.seed = 9;
    const auto ns = notes(p, 120);
    ASSERT_FALSE(ns.empty());
    for (const auto& n : ns)
        EXPECT_LE(n.pitch, 127);
}
