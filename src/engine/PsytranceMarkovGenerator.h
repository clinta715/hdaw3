#pragma once
// PsytranceMarkovGenerator — incremental 2-bar Markov psytrance score
// generation.
//
// COMPONENT ARCHITECTURE (0.29.0): generate() is a thin delegate — the
// orchestrator is MarkovArranger (src/engine/MarkovArranger.{h,cpp}), which
// drives PercussionEngine (themes, 16-step grids, rotation), HarmonyEngine
// (key, progressions, chord tones, pitched emission) and TextureEngine
// (riser/downlifter + filter sweeps). Each component owns a style-parameter
// struct (PercussionStyle/HarmonyStyle/TextureStyle) — the seam where future
// genre style packs (JSON) land. Shared role predicates and draw primitives
// live in MarkovRoles.h.
//
// Same-seed determinism is the only sequencing contract (same seed + params
// → byte-identical score, run to run). The 0.29.0 component refactor
// preserved the seeded draw order, so per-seed scores carry over from 0.28.x;
// per-seed continuity across future versions is NOT contractual — a reorder
// is acceptable (tests are self-comparisons and property sweeps).
//
// A pool of elements (roles) grows the track 2 bars at a time;
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
// Floor canon: kick+bass are only removed inside breakdown sections (the
// tension device); breakdown removals prefer the floor, and a transition
// into Build (the drop) prefers the floor's return. With the section tier
// off (sectionCycleBars=0) the floor is never removed.
//
// THREE-GROUP ELEMENT ONTOLOGY (P0): roles partition into PERC (kick/hat/
// clap/snare/rim — themed pattern sets held >= 32 bars between flips), FX
// (riser/down — composed texture), and CORE (arp/stab/pad/bass — persistent
// tonal identity varied by synth tweaks, not constant re-rolls). The floor
// (kick/bass) is the protected CORE subset: it enters/leaves hard-edged
// (NO volume fades), while other non-floor layers get engine-written
// volume fade-in/out automation points (core 4 bars in, perc 2 bars in,
// 2 bars out) that the command layer writes to real Volume lanes; FX
// emission (filterCutoff) stays advisory data.
//
// PERCUSSIVE THEMES (P1): the percussive groove is a THEME — one coordinated
// set of 16-step velocity grids for hat/snare/rim plus a kick broken/straight
// flag — rotated as a UNIT. Theme 0 is the canonical opener (offbeat-8th hats
// with the beat-1 offbeat accented, silent snare/rim, straight kick); themes
// 1..T-1 (T seeded 2..3, drawn once at generation start) derive each voice's
// grid from the euclidean RhythmPatternGenerator (seed its PARAMS from the
// mt19937 — the generator itself stays pure/no-RNG). RhythmVariant rotates to
// the next theme (targetRole "theme") only after the current theme held
// kPercPatternHoldBars = 32 bars; Breakbeat toggles the CURRENT theme's kick
// flag and waits >= 32 bars since the last kick-pattern event (Breakbeat OR
// rotation — a rotation may swap the kick flag too). themeAge resets on
// rotation only. Snare (pitch 38) and rim (37) are fixed-pitch perc voices
// that enter via AddLayer like the other perc roles (perc pool is now 5, so
// maxPercTracks <= 5 and maxTracks <= 9); a theme that leaves a voice silent
// (theme 0 has no snare/rim) falls back to that voice's canonical filler
// grid while it is ACTIVE, so an added layer is never a silent fade target.
// Clap stays the canonical theme-independent 2/4 backbeat.
// The legacy PsytranceGenerator is untouched.

#include "engine/PsytranceGenerator.h"
#include <string>
#include <vector>
#include <cstdint>

namespace HDAW {

// Explicit section-script entry (optional). When PsytranceMarkovParams::
// sections is non-empty it replaces the seeded slow-tier schedule: the
// arrangement is a run of {type, bars} blocks, each resolving its own layer
// bounds from the per-section overrides or the section-type budget
// (MarkovArranger::sectionDefaults) and gently biasing the active count
// toward the section's target layer count. When sections is empty the
// seeded jittered schedule + global min/max/perc bounds drive everything
// (byte-identical to pre-script behavior).
struct MarkovSectionSpec {
    std::string type;        // "sparse"|"build"|"peak"|"breakdown"
                             // (aliases: intro/outro -> sparse, drop/climax -> peak)
    int bars = 8;            // section length in bars (even, >= 2, <= 256)
    int minTracks = -1;      // -1 = use the type default from sectionDefaults
    int maxTracks = -1;
    int minPercTracks = -1;
    int maxPercTracks = -1;
};

struct PsytranceMarkovParams {
    int keyRoot = 0;       // 0..11 — scale root pitch class
    int scaleMode = 1;     // PhraseGenerator scale index (0..12)
    double density = 0.7;  // 0..1 — scales the variation-action probability
    uint64_t seed = 0;     // deterministic (full 64 bits seed the mt19937)
    int totalBars = 32;    // even (odd input rounds up), 1..256
    int minTracks = 2;     // global active-layer floor
    int maxTracks = 6;     // global active-layer ceiling (1..9)
    int minPercTracks = 1; // percussive sublimit floor ({kick,hat,clap,snare,rim})
    int maxPercTracks = 3; // percussive sublimit ceiling (0..5)
    int everyBars = 32;    // periodic KeyChange boundary (0 = off, else >= 8)
    int keyShiftDegrees = 0; // KeyChange size in scale degrees (0 = seeded +1/+2)
    int sectionCycleBars = 32; // slow-tier section-energy clock (0 = off, else >= 8)
    // Role → track index. -1 = unmapped → skipped
    int kick = -1, bass = -1, hat = -1, arp = -1, stab = -1, pad = -1,
        riser = -1, down = -1, clap = -1, snare = -1, rim = -1; // NoteLengthVariant may target bass/arp/stab/pad
    std::vector<int> progressionA; // optional degree overrides (default i-VII-VI-VII)
    std::vector<int> progressionB; // optional degree overrides (default VI-VII-i-i)
    std::vector<MarkovSectionSpec> sections; // empty = current seeded behavior
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
