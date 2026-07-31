#include <gtest/gtest.h>
#include "engine/PhraseGenerator.h"
#include "engine/Generative.h"
#include <vector>

namespace
{

void expectSameNotes(const std::vector<PhraseGenerator::GeneratedNote>& a,
                     const std::vector<PhraseGenerator::GeneratedNote>& b)
{
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(a[i].startBeat, b[i].startBeat) << "note " << i;
        EXPECT_EQ(a[i].noteNumber, b[i].noteNumber) << "note " << i;
        EXPECT_EQ(a[i].velocity, b[i].velocity) << "note " << i;
        EXPECT_DOUBLE_EQ(a[i].durationBeats, b[i].durationBeats) << "note " << i;
    }
}

} // namespace

TEST(PhraseGeneratorDeterminism, EveryStyleSameSeedSameOutput)
{
    for (int style = 0; style <= static_cast<int>(PhraseGenerator::Euclidean); ++style)
    {
        PhraseGenerator::PhraseParams p;
        p.style = static_cast<PhraseGenerator::Style>(style);
        p.seed = 1337;
        p.lengthBeats = 4.0;
        p.density = 8;

        auto a = PhraseGenerator::generatePhrase(p);
        auto b = PhraseGenerator::generatePhrase(p);
        ASSERT_FALSE(a.empty()) << "style index " << style;
        expectSameNotes(a, b);
    }
}

TEST(PhraseGeneratorDeterminism, DifferentSeedDiffers)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Lead;
    p.lengthBeats = 4.0;
    p.density = 16;

    p.seed = 1;
    auto a = PhraseGenerator::generatePhrase(p);
    p.seed = 2;
    auto b = PhraseGenerator::generatePhrase(p);

    bool differs = a.size() != b.size();
    for (size_t i = 0; i < a.size() && i < b.size() && !differs; ++i)
        differs = (a[i].noteNumber != b[i].noteNumber) || (a[i].velocity != b[i].velocity);
    EXPECT_TRUE(differs);
}

TEST(PhraseGeneratorDeterminism, ChordAndProgressionDeterministic)
{
    PhraseGenerator::ChordParams cp;
    cp.chordType = 0;
    cp.seed = 42;
    auto ca = PhraseGenerator::generateChord(60, cp);
    auto cb = PhraseGenerator::generateChord(60, cp);
    ASSERT_FALSE(ca.empty());
    expectSameNotes(ca, cb);

    PhraseGenerator::ProgressionParams pp;
    pp.patternIndex = 0;
    pp.beatsPerChord = 4.0;
    pp.seed = 42;
    auto pa = PhraseGenerator::generateProgression(pp);
    auto pb = PhraseGenerator::generateProgression(pp);
    ASSERT_FALSE(pa.empty());
    expectSameNotes(pa, pb);
}

TEST(PhraseGeneratorDeterminism, ProgressionChordsDifferFromEachOther)
{
    // The per-chord derived seed must make consecutive chords distinct, not clones.
    PhraseGenerator::ProgressionParams pp;
    pp.patternIndex = 1; // I-V-vi-IV
    pp.beatsPerChord = 4.0;
    pp.seed = 1234;
    auto notes = PhraseGenerator::generateProgression(pp);
    ASSERT_GE(notes.size(), 6u);
    // First chord root vs third chord root should not be identical pitch sets.
    EXPECT_NE(notes.front().noteNumber, notes[notes.size() / 2].noteNumber);
}

TEST(PhraseGeneratorDeterminism, ZeroSeedStillGenerates)
{
    PhraseGenerator::PhraseParams p; // seed defaults to 0 (legacy non-deterministic)
    p.style = PhraseGenerator::Standard;
    p.density = 8;
    auto r = PhraseGenerator::generatePhrase(p);
    EXPECT_FALSE(r.empty());
}

TEST(PhraseGeneratorEuclidean, OnsetCountTracksDensity)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Euclidean;
    p.lengthBeats = 4.0; // 16-step grid
    p.seed = 7;

    p.density = 5;
    EXPECT_EQ(PhraseGenerator::generatePhrase(p).size(), 5u);

    p.density = 16;
    EXPECT_EQ(PhraseGenerator::generatePhrase(p).size(), 16u);

    p.density = 99; // clamps to the 16-step grid
    EXPECT_EQ(PhraseGenerator::generatePhrase(p).size(), 16u);
}

TEST(PhraseGeneratorEuclidean, OnsetsMatchEuclideanGrid)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Euclidean;
    p.lengthBeats = 4.0;
    p.density = 5;
    p.seed = 7;
    auto notes = PhraseGenerator::generatePhrase(p);

    auto expected = HDAW::euclideanSteps(5, 16);
    ASSERT_EQ(notes.size(), expected.size());
    for (size_t i = 0; i < notes.size(); ++i)
        EXPECT_NEAR(notes[i].startBeat, expected[i] * 0.25, 1e-9);

    for (const auto& n : notes)
    {
        EXPECT_GE(n.noteNumber, p.lowNote);
        EXPECT_LE(n.noteNumber, p.highNote);
    }
}

TEST(PhraseGeneratorEuclidean, Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Euclidean;
    p.lengthBeats = 4.0;
    p.density = 7;
    p.seed = 999;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    expectSameNotes(a, b);
}
