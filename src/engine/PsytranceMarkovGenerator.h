#pragma once
// PsytranceMarkovGenerator — incremental 2-bar Markov psytrance score
// generation. A pool of elements (roles) grows the track 2 bars at a time;
// each 2-bar window a seeded std::mt19937 (seed_seq from the full uint64)
// picks the next variation action in a FIXED draw order, so the same seed +
// params reproduce a byte-identical score. Global min/max active tracks plus
// a percussive sublimit ({kick,hat,clap} within [minPercTracks,maxPercTracks])
// create natural build-ups/breakdowns. Track age (running bars, reset on
// re-add) drives age-weighted replacement: RemoveLayer / SwapPattern /
// evict-on-max prefer the longest-running layer. Pads are handled as thick
// gated chord beds: triads or 7ths, stepped on 8th/16th grids, while key
// discipline keeps every pitched note in-scale before AND after a change.
// Pure deterministic function — no engine/model dependency. Reuses
// PsytranceNote/PsytranceClip (PsytranceGenerator.h) and
// PhraseGenerator::scaleDegreeToPitch for key discipline. TWO TIERS: a slow
// section-energy state (sparse/build/peak/breakdown, re-rolled every
// sectionCycleBars) biases the fast per-window Markov, whose actions are
// cadence-gated (melodic layer add/remove on 4-bar boundaries; bass and kick
// hold >= 8 bars once active) so most elements hold steady while 1-2 move.
// The legacy PsytranceGenerator is untouched.

#include "engine/PsytranceGenerator.h"
#include <string>
#include <vector>
#include <cstdint>

namespace HDAW {

struct PsytranceMarkovParams {
    int keyRoot = 0;       // 0..11 — scale root pitch class
    int scaleMode = 1;     // PhraseGenerator scale index (0..12)
    double density = 0.7;  // 0..1 — scales the variation-action probability
    uint64_t seed = 0;     // deterministic (full 64 bits seed the mt19937)
    int totalBars = 32;    // even (odd input rounds up), 1..256
    int minTracks = 2;     // global active-layer floor
    int maxTracks = 6;     // global active-layer ceiling (1..7)
    int minPercTracks = 1; // percussive sublimit floor ({kick,hat,clap})
    int maxPercTracks = 3; // percussive sublimit ceiling (0..3)
    int everyBars = 32;    // periodic KeyChange boundary (0 = off, else >= 8)
    int keyShiftDegrees = 0; // KeyChange size in scale degrees (0 = seeded +1/+2)
    int sectionCycleBars = 32; // slow-tier section-energy clock (0 = off, else >= 8)
    // Role → track index. -1 = unmapped → skipped
    int kick = -1, bass = -1, hat = -1, arp = -1, stab = -1, pad = -1,
        riser = -1, down = -1, clap = -1; // NoteLengthVariant may target bass/arp/stab/pad
    std::vector<int> progressionA; // optional degree overrides (default i-VII-VI-VII)
    std::vector<int> progressionB; // optional degree overrides (default VI-VII-i-i)
};

enum class MarkovAction {
    Keep,
    AddLayer,
    RemoveLayer,
    SwapPattern,
    FxHit,
    Breakbeat,
    FilterSweep,
    RhythmVariant,
    ArpVariant,
    NoteLengthVariant,
    KeyChange
};

struct MarkovStep {
    int barStart = 0;                     // bar index where the window starts (0,2,4...)
    MarkovAction action = MarkovAction::Keep;
    std::string targetRole;               // role affected ("" for Keep)
    std::vector<std::string> activeRoles; // sorted snapshot after the action
    std::vector<int> ages;                // parallel to activeRoles: running bars
    int keyRoot = -1;                     // key in effect for this window
    std::string section;                  // slow tier: sparse|build|peak|breakdown
};

struct PsytranceAutomationPoint {
    std::string role;      // e.g. "arp"
    std::string param;     // e.g. "filterCutoff"
    double startBeat = 0.0;
    double value = 0.0;    // 0..1 normalized
    double durationBeats = 0.0;
};

struct PsytranceMarkovScore {
    std::vector<PsytranceClip> clips; // one per role that produced notes
    std::vector<std::string> skipped; // unmapped roles and roles with no notes
    std::vector<MarkovStep> steps;
    std::vector<PsytranceAutomationPoint> automations;
    double totalBeats = 0.0;
    int notesTotal = 0;
    std::string error; // non-empty => generation failed
};

class PsytranceMarkovGenerator {
public:
    static PsytranceMarkovScore generate(const PsytranceMarkovParams& params);
    static const char* actionName(MarkovAction a);
};

} // namespace HDAW
