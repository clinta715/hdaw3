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

// Snap a MIDI pitch to the nearest note in the given scale.
// scaleIntervals is the set of pitch classes (0..11) that belong to the scale.
// Returns the original pitch if already in-scale, or the nearest in-scale pitch.
// Ties go upward (sharp-side). Pure, no heap allocation.
inline int snapToScale(int pitch, int rootPc, const int* intervals, int numIntervals)
{
    if (pitch < 0 || pitch > 127 || numIntervals <= 0) return pitch;
    const int pc = ((pitch - rootPc) % 12 + 12) % 12;
    for (int i = 0; i < numIntervals; ++i)
        if (intervals[i] == pc) return pitch; // already in scale
    // Find nearest: try +1, -1, +2, -2, ... up to ±6 semitones
    for (int d = 1; d <= 6; ++d)
    {
        const int up = (pc + d) % 12;
        const int down = ((pc - d) % 12 + 12) % 12;
        for (int i = 0; i < numIntervals; ++i)
        {
            if (intervals[i] == up)   return pitch + d;
            if (intervals[i] == down) return pitch - d;
        }
    }
    return pitch; // shouldn't happen for standard 7-note scales
}

// Natural minor intervals (Aeolian) — the default psytrance scale.
inline const int* minorIntervals() { static const int v[] = {0,2,3,5,7,8,10}; return v; }
inline constexpr int kMinorIntervalCount = 7;

// Per-role note accumulator: one clip's notes plus its palette track index.
struct RoleCtx {
    int track = -1;
    int scaleRoot = 0;     // scale root pitch class (0=A, 1=Bb, ... 11=G#)
    int scaleMode = 1;     // 1=natural minor (default)
    PsytranceClip clip;
    void add(double startBeat, int pitch, int velocity, double durationBeats, int maxNotes)
    {
        if ((int) clip.notes.size() >= maxNotes) return;
        if (pitch < 0 || pitch > 127) return;
        if (velocity < 1) velocity = 1;
        if (velocity > 127) velocity = 127;
        // Snap pitched notes (non-percussion) to scale — percussion roles
        // use fixed MIDI pitches (kick=36, snare=38, etc.) and must not be
        // altered. The key-filter gate prevents the "473 off-key notes" bug
        // (2026-09-08 session: Markov generators produced F#/G#/A# in A minor).
        if (!isPercRole(clip.role))
        {
            // Default to natural minor if no custom intervals are provided.
            // For the standard 7-note minor scale, snapToScale is O(7).
            pitch = snapToScale(pitch, scaleRoot, minorIntervals(), kMinorIntervalCount);
        }
        clip.notes.push_back({ startBeat, pitch, velocity, durationBeats });
    }
};

} // namespace HDAW
