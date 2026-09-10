#include <gtest/gtest.h>

#include "engine/PhraseGenerator.h"
#include "engine/MotifStitcher.h"

#include <algorithm>
#include <set>
#include <vector>

// Phase 3 — motif-stitching melodic Markov (MotifStitcher.h + the
// PhraseGenerator::MotifStitch style).

namespace {

std::set<int> scalePcs(int root, int mode)
{
    std::set<int> pcs;
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == mode)
            for (int iv : m.intervals) pcs.insert((root + iv) % 12);
    return pcs;
}

bool inScale(const std::vector<PhraseGenerator::GeneratedNote>& notes, int root, int mode)
{
    const auto pcs = scalePcs(root, mode);
    for (const auto& n : notes)
        if (!pcs.count(n.noteNumber % 12)) return false;
    return true;
}

PhraseGenerator::PhraseParams stitchParams(uint64_t seed)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MotifStitch;
    p.scaleRoot = 5;   // F
    p.scaleMode = 1;   // minor
    p.lengthBeats = 32; // 8 bars
    p.seed = seed;
    return p;
}

} // namespace

TEST(MotifStitcher, DirectLineIsMultiBarAndNonEmpty)
{
    const auto line = HDAW::MotifStitcher::stitchMotifLine(8, 42);
    ASSERT_FALSE(line.empty());
    double maxBeat = 0;
    for (const auto& n : line) maxBeat = std::max(maxBeat, n.startBeat);
    EXPECT_GE(maxBeat, 4.0);            // spans more than one bar
    EXPECT_LT(maxBeat, 8.0 * 4.0 + 0.01); // within the requested span
}

TEST(MotifStitcher, Deterministic)
{
    const auto a = HDAW::MotifStitcher::stitchMotifLine(8, 42);
    const auto b = HDAW::MotifStitcher::stitchMotifLine(8, 42);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].startBeat, b[i].startBeat);
        EXPECT_EQ(a[i].degree, b[i].degree);
        EXPECT_EQ(a[i].octave, b[i].octave);
    }
}

TEST(MotifStitcher, DifferentSeedsDiffer)
{
    const auto a = HDAW::MotifStitcher::stitchMotifLine(8, 1);
    const auto b = HDAW::MotifStitcher::stitchMotifLine(8, 2);
    bool differ = a.size() != b.size();
    if (!differ)
        for (size_t i = 0; i < a.size() && !differ; ++i)
            if (a[i].degree != b[i].degree || a[i].startBeat != b[i].startBeat) differ = true;
    EXPECT_TRUE(differ);
}

// Style wiring: generatePhrase(MotifStitch) yields an in-scale multi-bar line.
TEST(PhraseGenerator, MotifStitchStyleInScaleAndMultiBar)
{
    const auto p = stitchParams(42);
    const auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());
    EXPECT_TRUE(inScale(notes, p.scaleRoot, p.scaleMode));
    double maxBeat = 0;
    for (const auto& n : notes) maxBeat = std::max(maxBeat, n.startBeat);
    EXPECT_GE(maxBeat, 8.0); // multi-bar (>= 2 bars of 4 beats)
}

// The stitcher actually uses learned transitions (the line is not a drone).
TEST(PhraseGenerator, MotifStitchLineHasContour)
{
    const auto p = stitchParams(7);
    const auto notes = PhraseGenerator::generatePhrase(p);
    std::set<int> pitches;
    for (const auto& n : notes) pitches.insert(n.noteNumber);
    EXPECT_GE((int) pitches.size(), 3);
}

TEST(PhraseGenerator, MotifStitchLineHasContourForSeedSweep)
{
    for (uint64_t seed = 0; seed < 20; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto p = stitchParams(seed);
        const auto notes = PhraseGenerator::generatePhrase(p);
        ASSERT_FALSE(notes.empty());

        std::set<int> pitches;
        double maxBeat = 0.0;
        for (const auto& n : notes)
        {
            pitches.insert(n.noteNumber);
            maxBeat = std::max(maxBeat, n.startBeat);
        }

        EXPECT_GE((int) pitches.size(), 3); // not a drone
        EXPECT_GE(maxBeat, 4.0);            // spans at least two bars
    }
}
