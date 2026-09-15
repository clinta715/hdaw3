#pragma once
// Seeded cell-content defaults (plan/cell workflow, 2026-09-14).
// The A-pattern: explicit recipe params always win; these pick ONLY when the
// recipe omits the choice, drawing from the 53-bit cell seed. One rng per
// helper, fixed single-draw order, seed_seq seeding (MarkovArranger
// precedent) — deterministic per seed, varied across seeds.
#include "BreakPatternGenerator.h"
#include "PhraseGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace HDAW {
namespace SeededDefaults {

inline std::mt19937 rngFor(uint64_t seed)
{
    std::seed_seq s{ (uint32_t) (seed & 0xffffffffu),
                     (uint32_t) ((seed >> 32) & 0xffffffffu) };
    return std::mt19937(s);
}

// Break style default: uniform over all five generator styles (each is
// seeded internally, so every outcome is deterministic per cell seed).
inline BreakPatternGenerator::Style defaultBreakStyle(uint64_t seed)
{
    std::mt19937 r = rngFor(seed);
    std::uniform_int_distribution<int> d(0, 4);
    return static_cast<BreakPatternGenerator::Style>(d(r));
}

// Phrase style default per role. Function-fixed roles pin a single style
// (bass/pad keep legacy behavior); lead/arp/stab draw from 2-entry musical
// sets (a lead never draws BassLine); unknown roles keep Standard.
inline PhraseGenerator::Style defaultPhraseStyleForRole(const std::string& role,
                                                        uint64_t seed)
{
    std::string lo;
    lo.reserve(role.size());
    for (char c : role)
        lo.push_back((char) std::tolower((unsigned char) c));
    const PhraseGenerator::Style* set = nullptr;
    size_t count = 0;
    static const PhraseGenerator::Style kLead[] =
        { PhraseGenerator::Lead, PhraseGenerator::RandomWalk };
    static const PhraseGenerator::Style kArp[] =
        { PhraseGenerator::Arpeggio, PhraseGenerator::RandomWalk };
    static const PhraseGenerator::Style kStab[] =
        { PhraseGenerator::ChordStab, PhraseGenerator::Standard };
    if (lo == "bass")
        return PhraseGenerator::BassLine;
    if (lo == "pad")
        return PhraseGenerator::Pad;
    if (lo == "riser")
        return PhraseGenerator::Buildup;
    if (lo == "lead")
    {
        set = kLead; count = 2;
    }
    else if (lo == "arp")
    {
        set = kArp; count = 2;
    }
    else if (lo == "stab")
    {
        set = kStab; count = 2;
    }
    else
    {
        return PhraseGenerator::Standard;
    }
    std::mt19937 r = rngFor(seed);
    std::uniform_int_distribution<size_t> d(0, count - 1);
    return set[d(r)];
}

// Pad voicing shape: 0 = legacy root+5th+octave, 1 = rich (+19 second
// fifth), 2 = open (root+octave+fifth-above). Perfect intervals only —
// consonant in any scale/mode.
inline int padVoicingShape(uint64_t seed)
{
    std::mt19937 r = rngFor(seed);
    std::uniform_int_distribution<int> d(0, 2);
    return d(r);
}

inline std::vector<int> padVoicingIntervals(int shape)
{
    switch (shape)
    {
        case 1:  return { 7, 12, 19 };
        case 2:  return { 12, 19 };
        case 0:
        default: return { 7, 12 };
    }
}

} // namespace SeededDefaults
} // namespace HDAW
