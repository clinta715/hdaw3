#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "engine/RhythmPatternGenerator.h"
#include "engine/RhythmPatternBank.h"

using Note = RhythmPatternGenerator::Note;

namespace {

int countXs(const std::string& dsl)
{
    int c = 0;
    for (const char ch : dsl)
        if (ch == 'x')
            ++c;
    return c;
}

// A DSL 'x' at string index i maps to startBeat = i * (4.0 / grid).
void expectMatchesDsl(const std::string& dsl, int grid, const std::vector<Note>& notes)
{
    const double beatsPerStep = 4.0 / static_cast<double>(grid);
    ASSERT_EQ(notes.size(), static_cast<size_t>(countXs(dsl)));
    size_t n = 0;
    for (size_t i = 0; i < dsl.size(); ++i)
    {
        if (dsl[i] != 'x')
            continue;
        ASSERT_LT(n, notes.size());
        EXPECT_DOUBLE_EQ(notes[n].startBeat, static_cast<double>(i) * beatsPerStep);
        ++n;
    }
}

} // namespace

TEST(RhythmPolyrhythm, DefaultFourOverThree)
{
    RhythmPatternGenerator::Params p; // grid=16 bars=1 pulseA=4 pulseB=3 rotA=1 rotB=1
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 6u);
    // A: {0,4,8,12} -> beats 0,1,2,3 (pitch 36, vel 112, dur 0.2)
    // B: {0,6,11}   -> step 0 collides with A -> beats 1.5, 2.75 (pitch 42, vel 96, dur 0.1)
    const double beats[]   = { 0.0, 1.0, 1.5, 2.0, 2.75, 3.0 };
    const int    pitches[] = { 36,  36,  42,  36,  42,   36  };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
    EXPECT_EQ(notes[0].velocity, 112);
    EXPECT_EQ(notes[2].velocity, 96);
    EXPECT_DOUBLE_EQ(notes[0].durationBeats, 0.2);
    EXPECT_DOUBLE_EQ(notes[2].durationBeats, 0.1);
}

TEST(RhythmPolyrhythm, ZeroRotationPullsHitsBeforeTheBeat)
{
    RhythmPatternGenerator::Params p;
    p.rotationA = 0;
    p.rotationB = 0;
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 6u);
    // A: {3,7,11,15} -> 0.75,1.75,2.75,3.75 ; B: {5,10,15} -> 15 collides -> 1.25,2.5
    const double beats[]   = { 0.75, 1.25, 1.75, 2.5, 2.75, 3.75 };
    const int    pitches[] = { 36,   42,   36,   42,  36,   36   };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
}

TEST(RhythmPolyrhythm, LoopSpansBars)
{
    RhythmPatternGenerator::Params p;
    p.bars = 2;      // loop = 32 steps
    p.pulseA = 4;    // 4 hits over 32 steps
    p.pulseB = 0;
    p.rotationA = 1; // {7,15,23,31} + 1 -> {0,8,16,24}
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 4u);
    const double beats[] = { 0.0, 2.0, 4.0, 6.0 };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, 36);
    }
}

TEST(RhythmDsl, ExpandsEuclideanToGrid)
{
    RhythmPatternGenerator::Params p;
    p.dsl = "E(3,8)"; // expands to steps {4,10,14} on the 16 grid
    p.pulseA = 0;
    p.pulseB = 0;
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 3u);
    const double beats[] = { 1.0, 2.5, 3.5 };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, 39);
        EXPECT_EQ(notes[i].velocity, 104);
        EXPECT_DOUBLE_EQ(notes[i].durationBeats, 0.1);
    }
}

TEST(RhythmDsl, CombinedPulsesAndDslDedup)
{
    RhythmPatternGenerator::Params p; // defaults: A 4/16 rot1, B 3/16 rot1
    p.dsl = "E(5,16)"; // steps {3,6,9,12,15}; 6 claimed by B, 12 by A -> {3,9,15}
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 9u);
    const double beats[]   = { 0.0, 0.75, 1.0, 1.5, 2.0, 2.25, 2.75, 3.0, 3.75 };
    const int    pitches[] = { 36,  39,   36,  42,  36,  39,   42,   36,  39   };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
}

TEST(RhythmValidation, EmptyOnBadGrid)
{
    RhythmPatternGenerator::Params p;
    p.grid = 0;
    EXPECT_TRUE(RhythmPatternGenerator::generate(p).empty());
    RhythmPatternGenerator::Params q;
    q.bars = 0;
    EXPECT_TRUE(RhythmPatternGenerator::generate(q).empty());
}

TEST(RhythmValidation, MalformedDslThrows)
{
    RhythmPatternGenerator::Params p;
    p.dsl = "E(3,8"; // unterminated E
    EXPECT_THROW(RhythmPatternGenerator::generate(p), std::invalid_argument);
    p.dsl = "z";     // bad token
    EXPECT_THROW(RhythmPatternGenerator::generate(p), std::invalid_argument);
}

// ── Corpus phrase bank (RhythmPatternBank.h) ──

TEST(RhythmPatternBank, KnownPhraseProducesExpectedHits)
{
    const HDAW::RhythmicPhrase* ph = HDAW::findRhythmicPhrase("snare_s2_4bar");
    ASSERT_NE(ph, nullptr);
    EXPECT_EQ(ph->role, std::string("snare"));
    EXPECT_EQ(ph->bars, 4);
    EXPECT_EQ(ph->grid, 16);

    const auto notes = RhythmPatternGenerator::generatePhrase("snare_s2_4bar");
    expectMatchesDsl(ph->dsl, ph->grid, notes);
    // phrase drives the DSL voice alone -> all notes at the phrase pitch
    for (const auto& n : notes)
        EXPECT_EQ(n.pitch, ph->pitch);
}

TEST(RhythmPatternBank, TwoBarHatPhraseIsMultiBar)
{
    const auto notes = RhythmPatternGenerator::generatePhrase("hats_hh12_2bar");
    ASSERT_FALSE(notes.empty());
    bool pastBar1 = false;
    for (const auto& n : notes)
        if (n.startBeat >= 4.0)
            pastBar1 = true;
    EXPECT_TRUE(pastBar1);
}

TEST(RhythmPatternBank, UnknownIdReturnsEmpty)
{
    EXPECT_TRUE(RhythmPatternGenerator::generatePhrase("does_not_exist").empty());
}

TEST(RhythmPatternBank, ApplyPhraseByRole)
{
    RhythmPatternGenerator::Params p;
    // "snare" has 5 phrases; index 0 must resolve.
    EXPECT_TRUE(RhythmPatternGenerator::applyPhraseByRole(p, "snare", 0));
    EXPECT_FALSE(p.dsl.empty());
    EXPECT_GT(p.bars, 0);
    EXPECT_EQ(p.pulseA, 0);
    EXPECT_EQ(p.pulseB, 0);

    // roles with no phrases resolve to false (bank has no tom phrases).
    EXPECT_FALSE(RhythmPatternGenerator::applyPhraseByRole(p, "tom", 0));

    // out-of-range index -> false.
    EXPECT_FALSE(RhythmPatternGenerator::applyPhraseByRole(p, "snare", 999));
}

TEST(RhythmPatternBank, BankEnumeratesAllPhrases)
{
    EXPECT_EQ(HDAW::rhythmPhraseCount(), 62);
    for (int i = 0; i < HDAW::rhythmPhraseCount(); ++i)
    {
        const HDAW::RhythmicPhrase* ph = &HDAW::rhythmPhrases()[i];
        EXPECT_NE(HDAW::findRhythmicPhrase(ph->id), nullptr) << ph->id;
        const auto notes = RhythmPatternGenerator::generatePhrase(ph->id);
        EXPECT_EQ(notes.size(), static_cast<size_t>(countXs(ph->dsl))) << ph->id;
        for (const auto& n : notes)
            EXPECT_LT(n.startBeat, static_cast<double>(ph->bars) * 4.0);
    }
}
