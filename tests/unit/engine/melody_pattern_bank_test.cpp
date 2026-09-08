#include <gtest/gtest.h>
#include <string>

#include "engine/MelodyPatternBank.h"

// ── Corpus melodic phrase bank (MelodyPatternBank.h) ──
// Scoped to the NEW bank only (per lesson 9: never assert an absolute count
// on a bank you don't own). RhythmPatternBank.* is untouched.
namespace {

TEST(MelodyPatternBank, EnumeratesAllPhrases)
{
    const int count = HDAW::melodyPhraseCount();
    EXPECT_EQ(count, 87); // starting curated bank (Phase 1)
    EXPECT_GE(count, 50);

    int total = 0;
    for (int i = 0; i < count; ++i)
    {
        const HDAW::MelodicPhrase& p = HDAW::melodyPhrases()[i];
        EXPECT_NE(p.id, nullptr) << i;
        EXPECT_NE(p.role, nullptr) << i;
        EXPECT_GE(p.bars, 2) << p.id;          // multi-bar only
        EXPECT_GT(p.nNotes, 0) << p.id;
        EXPECT_GE(p.noteOffset, 0) << p.id;
        EXPECT_GE(p.grid, 8) << p.id;
        EXPECT_GE(p.scaleMode, 0) << p.id;
        EXPECT_LT(p.scaleMode, HDAW::melodyScaleCount()) << p.id;
        total += p.nNotes;
    }
    EXPECT_EQ(total, HDAW::melodyNoteCount());
}

TEST(MelodyPatternBank, HasLeadBassAndChordRoles)
{
    bool lead = false, bass = false, chord = false;
    for (int i = 0; i < HDAW::melodyPhraseCount(); ++i)
    {
        const std::string r = HDAW::melodyPhrases()[i].role;
        if (r == "lead") lead = true;
        else if (r == "bass") bass = true;
        else if (r == "chord") chord = true;
    }
    EXPECT_TRUE(lead);
    EXPECT_TRUE(bass);
    EXPECT_TRUE(chord);
}

TEST(MelodyPatternBank, EveryPhraseHasEnoughContour)
{
    for (int i = 0; i < HDAW::melodyPhraseCount(); ++i)
    {
        const HDAW::MelodicPhrase& p = HDAW::melodyPhrases()[i];
        const HDAW::MelodyNote* notes = HDAW::melodyPhraseNotes(p);
        int distinct = 0;
        int last = -999;
        for (int n = 0; n < p.nNotes; ++n)
            if (notes[n].degree != last) { distinct++; last = notes[n].degree; }
        EXPECT_GE(distinct, 2) << p.id; // a real contour, not a drone
        EXPECT_GE(p.nNotes, 5) << p.id;
    }
}

// G1.2 — property test: every phrase transposes to ALL 12 roots and every
// note stays in-scale and in-range (the core key-relative contract).
TEST(MelodyPatternBank, TransposesToAnyRootStaysInScale)
{
    for (int i = 0; i < HDAW::melodyPhraseCount(); ++i)
    {
        const HDAW::MelodicPhrase& p = HDAW::melodyPhrases()[i];
        const HDAW::MelodyNote* notes = HDAW::melodyPhraseNotes(p);
        for (int n = 0; n < p.nNotes; ++n)
            for (int root = 0; root < 12; ++root)
            {
                const int pitch = HDAW::melodyNotePitch(p, notes[n], root);
                EXPECT_GE(pitch, 0) << p.id << " n" << n << " root" << root;
                EXPECT_LE(pitch, 127) << p.id << " n" << n << " root" << root;
                EXPECT_TRUE(HDAW::melodyNoteInScale(pitch, root, p.scaleMode))
                    << p.id << " n" << n << " root" << root;
            }
    }
}

// Source-key reconstruction is in-scale too.
TEST(MelodyPatternBank, SourceKeyReconstructionInScale)
{
    for (int i = 0; i < HDAW::melodyPhraseCount(); ++i)
    {
        const HDAW::MelodicPhrase& p = HDAW::melodyPhrases()[i];
        const HDAW::MelodyNote* notes = HDAW::melodyPhraseNotes(p);
        for (int n = 0; n < p.nNotes; ++n)
        {
            const int pitch = HDAW::melodyNotePitch(p, notes[n], p.rootPc);
            EXPECT_GE(pitch, 0) << p.id;
            EXPECT_TRUE(HDAW::melodyNoteInScale(pitch, p.rootPc, p.scaleMode)) << p.id;
        }
    }
}

// Transpose preserves the register (degree + octave held, root shifts by k).
TEST(MelodyPatternBank, TransposePreservesRegister)
{
    for (int i = 0; i < HDAW::melodyPhraseCount(); ++i)
    {
        const HDAW::MelodicPhrase& p = HDAW::melodyPhrases()[i];
        const HDAW::MelodyNote* notes = HDAW::melodyPhraseNotes(p);
        const int k = 5; // transpose up a perfect 4th (safe within 25..100 range)
        for (int n = 0; n < p.nNotes; ++n)
        {
            const int src = HDAW::melodyNotePitch(p, notes[n], p.rootPc);
            const int t = HDAW::melodyNotePitch(p, notes[n], (p.rootPc + k) % 12);
            EXPECT_EQ(t - src, k) << p.id << " n" << n;
        }
    }
}

} // namespace
