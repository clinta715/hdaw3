#pragma once
// PsytranceGenerator — pure, deterministic psytrance SCORE generation per the
// verified recipe grammar in docs/psytrance-composition-guide.md §4 (+ the
// DarkForestV5 test patterns): key-disciplined scales, 4-on-floor kick,
// offbeat rolling bass, offbeat hats + 16th rolls, chord-tone arp with the
// +12 glint, beat-2 stabs, long pads, breakdown melody, riser/downlifter
// schedule into drops. Generates NOTES ONLY (one clip per mapped role is
// materialized by the AudioEngineCommands layer); the kit (sample loading)
// and the production stack (FX/LFO/automation, guide §5) stay deliberate
// MCP-side steps. No engine/model dependency — fully unit-testable.
//
// Plan: docs/plans/2026-08-30-psytrance-mcp-generator.md (W1).

#include <string>
#include <vector>
#include <cstdint>

namespace HDAW {

// Section kind drives the per-role activity schedule (guide §4 table).
enum class PsytranceSectionKind {
    Intro,     // pads only + sparse hat quarters from bar 4
    Build,     // + kick 4-on-floor, hat quarters, claps on 2/4
    MainA,     // full stack: kick + bass + hats + arp + stabs + pads
    Mini,      // kick/bass OUT; pads only (drop-out, not a drop)
    MainB,     // mainA + bass an octave up, extra hat 16ths
    Breakdown, // kick AND bass out; slow reverbed melody on the arp track
    Finale,    // densest: mainA + bass octave + hat 16ths throughout
    Other      // unrecognized name → behaves like MainA (full stack)
};

struct PsytranceSection {
    std::string name;  // case-insensitive; see PsytranceGenerator::kindFromName
    double start = 0.0; // beats (absolute)
    double end = 0.0;   // beats (absolute)
};

struct PsytranceParams {
    int keyRoot = 0;      // scale root MIDI pitch class 0..11
    int scaleMode = 1;    // PhraseGenerator scale index (1 = Minor/Aeolian)
    double density = 0.7; // 0..1 — gates extra 16th rolls / extra stabs
    uint64_t seed = 0;    // deterministic; 0 = fixed default sequence

    // Role → track index. -1 = role unmapped → skipped. `clap` defaults to
    // the hat track inside the generator when left unmapped; the breakdown
    // melody rides inside the `arp` clip.
    int kick = -1, bass = -1, hat = -1, arp = -1, stab = -1, pad = -1,
        riser = -1, down = -1, clap = -1;

    std::vector<PsytranceSection> sections; // chronological in practice; totalBeats = max end

    // Optional 8-degree progression overrides (0..6 scale degrees).
    // Defaults (DarkForestV5): A = i–VII–VI–VII, B = VI–VII–i–i.
    std::vector<int> progressionA;
    std::vector<int> progressionB;
};

struct PsytranceNote {
    double startBeat = 0.0; // absolute beats (clip-local when the clip starts at 0)
    int pitch = 0;          // 0..127
    int velocity = 0;       // 1..127
    double durationBeats = 0.0;
};

struct PsytranceClip {
    std::string role;                 // "kick", "bass", "hat", "arp", "stab", "pad", "clap", "riser", "down"
    int trackIndex = -1;
    std::vector<PsytranceNote> notes; // ascending by startBeat
};

struct PsytranceScore {
    std::vector<PsytranceClip> clips; // one per role that produced notes
    std::vector<std::string> skipped; // mapped-out roles and roles with no notes
    double totalBeats = 0.0;
    int notesTotal = 0;
    std::string error;                // non-empty → generation failed
};

class PsytranceGenerator
{
public:
    static PsytranceSectionKind kindFromName(const std::string& name);

    // Pure function: params → per-role note vectors. Deterministic for a
    // given seed; rng is consumed in a fixed section/bar/beat order so the
    // same seed + params reproduce the exact same score. On invalid params it
    // returns a score with `error` set (no exceptions).
    static PsytranceScore generate(const PsytranceParams& params);
};

} // namespace HDAW