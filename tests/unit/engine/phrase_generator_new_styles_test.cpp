#include <gtest/gtest.h>
#include "engine/PhraseGenerator.h"
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

// ── Individual determinism tests (indices 10–24) ──

TEST(PhraseGeneratorNewStyles, TrapHiHat_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::TrapHiHat;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, DrillBass_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::DrillBass;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, Counterpoint_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Counterpoint;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, WalkingBass_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::WalkingBass;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, SwingComping_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::SwingComping;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, MarkovMelody_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MarkovMelody;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, EvolvingTexture_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::EvolvingTexture;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, Aleatoric_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Aleatoric;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, ScalarRun_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::ScalarRun;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, ChordToneSeq_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::ChordToneSeq;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, CallResponse_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::CallResponse;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, PhaseShift_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::PhaseShift;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, AdditiveRhythm_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::AdditiveRhythm;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, MinimalistLoop_Deterministic)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MinimalistLoop;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

// ── Aggregate test: loop through all 15 new styles (10–24) ──

TEST(PhraseGeneratorNewStyles, AllNewStyles_Deterministic)
{
    for (int style = static_cast<int>(PhraseGenerator::TrapHiHat);
         style <= static_cast<int>(PhraseGenerator::Layered); ++style)
    {
        PhraseGenerator::PhraseParams p;
        p.style = static_cast<PhraseGenerator::Style>(style);
        p.seed = 42;
        p.lengthBeats = 4.0;
        p.density = 8;

        auto a = PhraseGenerator::generatePhrase(p);
        auto b = PhraseGenerator::generatePhrase(p);
        if (style == static_cast<int>(PhraseGenerator::Layered))
        {
            // Layered is deferred (stub) — expect empty
            EXPECT_TRUE(a.empty()) << "style index " << style;
            EXPECT_TRUE(b.empty()) << "style index " << style;
        }
        else
        {
            ASSERT_FALSE(a.empty()) << "style index " << style
                << " (" << PhraseGenerator::styleName(p.style) << ")";
            expectSameNotes(a, b);
        }
    }
}

// ── Style-specific behavioral tests ──

TEST(PhraseGeneratorNewStyles, PhaseShift_TwoVoices)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::PhaseShift;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    p.phaseShift.voice1Grid = 8;
    p.phaseShift.voice2Grid = 6;
    auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());

    // Verify notes are within range
    for (const auto& n : notes)
    {
        EXPECT_GE(n.noteNumber, p.lowNote);
        EXPECT_LE(n.noteNumber, p.highNote);
        EXPECT_GE(n.startBeat, 0.0);
        EXPECT_LT(n.startBeat, p.lengthBeats);
    }
}

TEST(PhraseGeneratorNewStyles, AdditiveRhythm_DefaultGrouping)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::AdditiveRhythm;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    p.additiveRhythm.grouping = "3+3+2";
    p.additiveRhythm.subdivision = 8;
    auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());

    // Default 3+3+2 over 8 should produce 8 notes
    EXPECT_EQ(notes.size(), 8u);
}

TEST(PhraseGeneratorNewStyles, MinimalistLoop_CellRepeat)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MinimalistLoop;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    p.minimalistLoop.cellLength = 6;
    p.minimalistLoop.mutationRate = 0.0; // no mutation — all repetitions identical
    auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());

    // With mutationRate=0, every 6th note should repeat identically
    // Check a few positions
    if (notes.size() >= 12)
    {
        EXPECT_EQ(notes[0].noteNumber, notes[6].noteNumber);
        EXPECT_EQ(notes[1].noteNumber, notes[7].noteNumber);
    }
}

TEST(PhraseGeneratorNewStyles, MinimalistLoop_WithMutation)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MinimalistLoop;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    p.minimalistLoop.cellLength = 6;
    p.minimalistLoop.mutationRate = 1.0; // always mutate
    auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());

    // With mutationRate=1.0, some notes should differ from their cell counterparts
    // (unless all shifts happen to land on the same pitch — unlikely with seed=42)
    bool anyDifferent = false;
    for (size_t i = 6; i < notes.size() && i < 12; ++i)
    {
        if (notes[i].noteNumber != notes[i - 6].noteNumber)
        {
            anyDifferent = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifferent);
}

TEST(PhraseGeneratorNewStyles, Layered_ReturnsEmpty)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Layered;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto notes = PhraseGenerator::generatePhrase(p);
    EXPECT_TRUE(notes.empty());
}

TEST(PhraseGeneratorNewStyles, AdditiveRhythm_AlternativeGrouping)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::AdditiveRhythm;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    p.additiveRhythm.grouping = "2+2+2+2";
    p.additiveRhythm.subdivision = 8;
    auto notes = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(notes.empty());
    EXPECT_EQ(notes.size(), 8u);
}
