#pragma once
// PercussionEngine — percussive themes, 16-step velocity grids and unit
// rotation (P1 machinery), extracted from PsytranceMarkovGenerator. A theme
// is ONE coordinated groove statement: 16-step velocity grids per voice
// (0 = silent, else velocity) for hat/snare/rim plus the kick broken flag;
// all voices rotate TOGETHER. Per-window emission covers hat/snare/rim (the
// grid loop), kick (4-on-floor or the theme's broken beat-2 with accent
// velocities) and the canonical theme-independent 2/4 clap backbeat.
// Pure deterministic component — no engine/model dependency. Style lives in
// PercussionStyle (the genre style-pack seam).

#include "engine/MarkovRoles.h"

#include <array>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace HDAW {

// ── Percussive themes (P1) ───────────────────────────────────────────────
// Theme 0 is the canonical opener (offbeat-8th hats, beat-1 offbeat
// slightly accented, silent snare/rim, straight kick); themes 1..T-1 seed
// RhythmPatternGenerator::Params (euclidean, grid=16, bars=1 — the generator
// itself stays pure/no-RNG) from the generation mt19937 and convert the
// returned notes to step-velocity arrays (dedupe by step, louder wins).
struct PercTheme {
    bool kickBroken = false;
    std::array<uint8_t, 16> hat{};   // 0 = silent, else velocity
    std::array<uint8_t, 16> snare{}; // pitch 38 (acoustic snare)
    std::array<uint8_t, 16> rim{};   // pitch 37 (side stick)
};

struct PercussionStyle {
    // Percussive THEME hold: the whole groove (hat/snare/rim grids + the kick
    // broken flag) is one unit — a rotation must leave it in place for at
    // least this many bars so the groove establishes before it mutates
    // (themeAge is tracked in bars and gates RhythmVariant viability; a
    // rotation resets it). The kick flag additionally rides its own clock
    // (Breakbeat OR rotation resets it) so the backbeat structure never
    // flips faster than the hold.
    int percPatternHoldBars = 32;

    int themeCountMin = 2, themeCountMax = 3;
    double kickBrokenProb = 0.35;

    // Per-voice 16-step grid draw ranges, exactly as drawTheme today:
    // (hits lo/hi, velocityA lo/hi, velocityB lo/hi)
    int hatHitsLo = 4,   hatHitsHi = 12,  hatVelALo = 88,   hatVelAHi = 104,  hatVelBLo = 64,  hatVelBHi = 84;
    int snareHitsLo = 2, snareHitsHi = 5, snareVelALo = 60, snareVelAHi = 80,  snareVelBLo = 45, snareVelBHi = 60;
    double snareBackbeatProb = 0.5; // optional 2/4 backbeat accent
    int snareBackbeatVel = 90;
    int rimHitsLo = 1,   rimHitsHi = 4,   rimVelALo = 70,   rimVelAHi = 90,   rimVelBLo = 50,  rimVelBHi = 64;

    int fillerVelocity = 100;       // canonical filler grids (outside every seeded range)
    int kickAccentVel = 127, kickNormalVel = 122;
    int clapVelocity = 100;
    int snarePitch = 38, rimPitch = 37, hatPitch = 44, clapPitch = 42; // all distinct
};

class PercussionEngine {
public:
    // Draw the theme set ONCE, at this fixed draw position before the window
    // loop, then rotated as a UNIT. EXACT monolith draw order: themeCount
    // draw, then per theme: kickBroken, hat grid (pulseA, pulseB, rotA,
    // rotB, velA, velB), snare grid, backbeat draw, rim grid.
    void drawThemes(std::mt19937& rng, const PercussionStyle& style);

    // Theme-hold viability (P1): a rotation may only fire once the current
    // theme has held percPatternHoldBars AND at least one theme voice
    // (hat/snare/rim/kick) is audible — rotating with nothing audible would
    // spend the hold clock on a no-op. Used by the weight gating AND the
    // execution re-check.
    bool themeRotationViable(const std::set<std::string>& active) const;

    void rotate();          // themeIndex+1 mod size; themeAge=0; kickClock=0
    void toggleBreakbeat(); // flip the CURRENT theme's kick flag; kickClock=0

    // Theme hold clocks in bars (P0 hold, now unit-wide): +windowBars per
    // window. themeAge resets on ROTATION only; kickClock resets on every
    // kick-pattern structure event (Breakbeat OR rotation — a rotation may
    // swap the kick flag too). A flip is only viable once its clock reaches
    // percPatternHoldBars — the groove establishes before it mutates.
    void advanceClocks(int windowBars);
    int kickClockValue() const;

    // Per-bar grid loop (hat/snare/rim) + kick beat loop + clap backbeat.
    // kickPitch = diaRoot(keyRoot, 2), computed by the caller.
    void writeWindowNotes(int bar, int windowBars, const std::set<std::string>& active,
                          const PercussionStyle& style, int kickPitch, RoleCtx& kick,
                          RoleCtx& hat, RoleCtx& snare, RoleCtx& rim, RoleCtx& clap,
                          int maxNotes) const;

private:
    PercTheme drawTheme(std::mt19937& rng, const PercussionStyle& style);

    std::vector<PercTheme> themes;
    int themeIndex = 0;       // current theme; RhythmVariant rotates (targetRole "theme")
    int themeAge = 0;         // bars since rotation (rotation is the ONLY reset)
    int kickClock = 0;        // bars since the last kick-pattern event
    int patternHoldBars = 32; // copied from PercussionStyle at drawThemes
};

} // namespace HDAW
