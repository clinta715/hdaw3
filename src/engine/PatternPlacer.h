#pragma once

// PatternPlacer — pure, header-only pattern-placement math (std only; no JUCE,
// no audio). Tiles caller-supplied bar-aligned MIDI patterns (e.g. the
// patterns[] array returned by the analyze_midi_file MCP tool) across a beat
// range with per-placement transforms: octave shift, velocity scale, and
// time-order reversal (retrograde).
//
// Patterns are NOT stored server-side: the payload is the caller's own pattern
// notes. PatternLibrary (src/engine/PatternLibrary.h) stores generation
// PRESETS (name/style/category/params) — never raw notes — so placed patterns
// never persist there.
//
// Units: startBeat/durationBeats are CLIP-LOCAL beats (the same convention the
// clip ValueTree and the add_notes MCP tool use). The result is deterministic
// by construction: no RNG and at most one multiply + round + clamp per note.
//
// Transform contract (documented, exercised by pattern_placer_test.cpp and the
// place_patterns MCP tool):
//   1. pitch   += octaveShift * 12          (caller clamps octaveShift to -6..+6)
//   2. velocity = round(velocity * velocityScale), clamped to 1..127
//                                            (caller clamps velocityScale to 0.05..2.0)
//   3. reverse = RETROGRADE: the time order of the notes is reversed while each
//      note keeps its own duration. The pattern's occupied span
//      [0, max(startBeat + durationBeats)) is preserved exactly: a note
//      starting at s with duration d maps to start = span - s - d. So the
//      first note (by time) becomes the last, and the last becomes the first.
//   4. finally every startBeat += placement.start (clip-local beat offset).
//
// Output order: input pattern order (kept stable for determinism). For a
// reversed placement the emitted startBeats are descending; sorting the
// result by startBeat yields the retrograde.

#include <algorithm>
#include <cmath>
#include <vector>

struct PatternPlacer
{
    struct PatternNote
    {
        int pitch = 60;
        double startBeat = 0.0;
        double durationBeats = 1.0;
        int velocity = 100; // 0..127 in the payload; /127 when stored in the tree
    };

    struct Placement
    {
        double start = 0.0;        // clip-local beat offset for this placement
        int octaveShift = 0;       // -6..+6 (clamped by the caller)
        double velocityScale = 1.0; // 0.05..2.0 (clamped by the caller)
        bool reverse = false;      // retrograde the pattern within its own span
    };

    static constexpr int kMaxOctaveShift = 6;
    static constexpr double kMinVelocityScale = 0.05;
    static constexpr double kMaxVelocityScale = 2.0;

    // Apply one placement to one pattern (see contract above). Deterministic.
    static std::vector<PatternNote> place(const std::vector<PatternNote>& pattern,
                                          const Placement& p)
    {
        if (pattern.empty())
            return {};

        // Occupied span of the pattern in beats (used as the retrograde mirror
        // extent so a reversed placement occupies exactly the same span).
        double span = 0.0;
        for (const auto& n : pattern)
            span = (std::max)(span, n.startBeat + n.durationBeats);

        std::vector<PatternNote> out;
        out.reserve(pattern.size());
        for (const auto& n : pattern)
        {
            PatternNote placed = n;
            placed.pitch = n.pitch + p.octaveShift * 12;
            placed.velocity = static_cast<int>(std::lround(n.velocity * p.velocityScale));
            placed.velocity = (std::max)(1, (std::min)(127, placed.velocity));
            if (p.reverse)
                placed.startBeat = span - n.startBeat - n.durationBeats;
            placed.startBeat += p.start;
            out.push_back(placed);
        }
        return out;
    }
};

