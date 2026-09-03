#pragma once
// MarkovRoles — shared role ontology + deterministic-draw primitives for the
// Markov score-generation components (MarkovArranger + PercussionEngine +
// HarmonyEngine + TextureEngine). Header-only and pure: <random>/<string>
// plus PsytranceClip — no engine/model dependency.
//
// ── Three-group element ontology (P0) ────────────────────────────────────
// FLOOR  = kick, bass      — protected tonal foundation (breakdown-only
//                            removal; hard-edged enters/leaves, NO fades).
// CORE   = arp, stab, pad, bass — persistent tonal identity, varied by
//                            synth tweaks (bass is BOTH floor-protected
//                            AND core-group; predicates overlap on purpose).
// PERC   = kick, hat, clap, snare, rim — isPercRole; themed pattern sets
//                            held >= 32 bars between flips (see PercTheme).
// FX     = riser, down     — composed texture accents.

#include "engine/PsytranceGenerator.h"

#include <random>
#include <string>

namespace HDAW {

inline int diaRoot(int pc, int octave) { return 12 * (octave + 1) + pc; }

inline int wrapDegree(int d, int len) { int r = d % len; return r < 0 ? r + len : r; }

inline bool isPercRole(const std::string& r)
{
    return r == "kick" || r == "hat" || r == "clap" || r == "snare" || r == "rim";
}

inline bool isFloorRole(const std::string& r) { return r == "kick" || r == "bass"; }
inline bool isCoreRole(const std::string& r)
{
    return r == "arp" || r == "stab" || r == "pad" || r == "bass";
}
inline bool isFxRole(const std::string& r) { return r == "riser" || r == "down"; }

// Core pool (layers that count toward min/max tracks), fixed order — the
// perc voices cluster up front (kick,bass,hat,snare,rim) and the melodic
// tail keeps its P0 relative order.
inline constexpr const char* kCoreRoles[9] = { "kick", "bass", "hat", "snare", "rim",
                                               "arp", "stab", "pad", "clap" };
// Lead family = age-biased replacement candidates (spec: arp,stab,pad,bass).
inline constexpr const char* kLeadFamily[4] = { "bass", "arp", "stab", "pad" };

// MidiClipProcessor ceiling (legacy parity)
inline constexpr int kMaxNotesPerClip = 8192;

// Seeded-draw primitives shared by every component — single-sourced so a
// draw consumes the caller's mt19937 identically wherever it is used.
inline double markovRng01(std::mt19937& rng)
{
    return (double) (rng() - rng.min()) / (double) (rng.max() - rng.min() + 1.0);
}
inline int markovRngInt(std::mt19937& rng, int lo, int hi) // inclusive
{
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
}

// Per-role note accumulator: one clip's notes plus its palette track index.
struct RoleCtx {
    int track = -1;
    PsytranceClip clip;
    void add(double startBeat, int pitch, int velocity, double durationBeats, int maxNotes)
    {
        if ((int) clip.notes.size() >= maxNotes) return;
        if (pitch < 0 || pitch > 127) return;
        if (velocity < 1) velocity = 1;
        if (velocity > 127) velocity = 127;
        clip.notes.push_back({ startBeat, pitch, velocity, durationBeats });
    }
};

} // namespace HDAW
