#pragma once
#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

// ── Break-pattern generation (sample-slice trigger patterns, P2-1) ──
// Turns a DETECTED-slice sampler sample into a written MIDI break pattern.
// The sampler maps MIDI note -> slice chromatically (SamplerEngine:
// slice index = note - baseNote), so a pattern is just a list of notes whose
// pitch = baseNote + sliceIndex. This generator is a PURE function of its
// params (mirrors RhythmPatternGenerator / PhraseGenerator): identical params
// + identical seed -> identical note list. No JUCE, no audio, no state.
//
// Tempo/raster contract: the pattern is defined on the classic 16-step bar
// skeleton (kick@0/8, snare@4/12, fills, off-beat ghosts). `grid` is the
// OUTPUT resolution in steps per beat (1=quarter .. 8=32nds; 4 = 16ths =
// the canon). A 16th note s sits at output step (s * grid) / 4, so:
//   grid=4 (default): 16 steps/bar, step == 16th, beats = step/4
//   grid=8: 32 steps/bar; fills/ghosts may swing onto the second 32nd
//   grid=2/1: 8th/quarter resolution, odd 16th positions snap down
// startBeat = stepIndex / grid, so every note lands in [0, bars*4) beats.
// The caller writes these as clip-local beats (add_notes contract).
//
// Slice mapping (documented contract, LOOPED modulo sliceCount, clamped:
//   kick (downbeat)     -> slice 0
//   kick (second, amen) -> slice 4        (the Think break's second kick)
//   snare               -> slice 2
//   fill 14 / fill 15   -> slice 4 / 5    (jungleEdit/random: seeded draw)
//   ghost               -> seeded draw
// With fewer slices the bases wrap modulo sliceCount (degenerate for
// sliceCount < 6 — still bounded, still deterministic).
//
// Templates (16-step bar skeleton; counts for grid=4, dropFirst=false):
//   amen       kicks {0,8} snares {4,12} fills {14,15} ghosts {2,6,10}->g
//   twoStep    kicks {0,9} snares {4,12} ghosts {6,14}->g, no fills
//   halftime   kicks {0,13} snare {8} ghosts {6,14}->g, minimal
//   jungleEdit amen skeleton + per-bar seeded slice-swap on fills 14/15 +
//              the first kick is ALWAYS dropped (dropFirst is forced on —
//              the classic "no first kick" jungle edit)
//   random     amen skeleton with every slice index drawn from the seeded
//              RNG (random walk over the detected slices); each of {2,6,10}
//              gets an independent seeded 50% ghost hit (ghostFills ignored)
// Counts/bar (grid=4): amen 6+g, twoStep 4+g, halftime 3+g, jungleEdit 6+g,
// random 6..9 (seed-dependent ghosts). With dropFirst the bar-0 first kick
// (1 note) is omitted. Bound: notes <= bars*16 + 2*bars for every style.
//
// Velocities: kick = velocityMax, snare = velocityMax-5, fill = mid,
// ghost = velocityMin, each skeleton/fill note velocity jittered by a seeded
// +/-6 draw clamped to [velocityMin, velocityMax]; ghosts are pinned low.
//
// seed==0 resolves to the stable default 12345 (deterministic in tests).
class BreakPatternGenerator
{
public:
    enum class Style { Amen, TwoStep, Halftime, JungleEdit, Random };

    static const char* styleName (Style s)
    {
        switch (s)
        {
            case Style::Amen:       return "amen";
            case Style::TwoStep:    return "twoStep";
            case Style::Halftime:   return "halftime";
            case Style::JungleEdit: return "jungleEdit";
            case Style::Random:     return "random";
        }
        return "amen";
    }

    static bool styleFromName (const std::string& name, Style& out)
    {
        if      (name == "amen")       { out = Style::Amen;       return true; }
        else if (name == "twoStep")    { out = Style::TwoStep;    return true; }
        else if (name == "halftime")   { out = Style::Halftime;   return true; }
        else if (name == "jungleEdit") { out = Style::JungleEdit; return true; }
        else if (name == "random")     { out = Style::Random;     return true; }
        return false;
    }

    struct Params
    {
        int sliceCount   = 8;      // detected slices (slicePoints.size()-1)
        int bars         = 8;      // 1..64
        int grid         = 4;      // output steps per beat: 1=quarter..8=32nds
        Style style      = Style::Amen;
        bool dropFirst   = false;  // omit bar-0 step-0 kick (classic open)
        int ghostFills   = 0;      // 0..2 seeded low-velocity extra 16ths
        int velocityMin  = 60;     // 1..127
        int velocityMax  = 100;    // 1..127
        uint64_t seed    = 12345;  // 0 -> stable default 12345
    };

    static constexpr int kMinBars = 1;
    static constexpr int kMaxBars = 64;
    static constexpr int kMinGrid = 1;
    static constexpr int kMaxGrid = 8;
    static constexpr int kMaxGhostFills = 2;
    static constexpr uint64_t kDefaultSeed = 12345;

    struct Step
    {
        int stepIndex  = 0;   // pattern-resolution step across the whole length
        int sliceIndex = 0;   // 0 .. sliceCount-1
        int velocity   = 100; // 1..127
        bool operator== (const Step& o) const noexcept
        {
            return stepIndex == o.stepIndex && sliceIndex == o.sliceIndex
                && velocity == o.velocity;
        }
        bool operator!= (const Step& o) const noexcept { return ! (*this == o); }
    };

    struct Note
    {
        double startBeat    = 0.0; // clip-local beats (stepIndex / grid)
        int    pitch        = 60;  // baseNote + sliceIndex
        int    velocity     = 100; // 1..127
        double durationBeats = 0.25; // one grid step = 1.0/grid beats
        bool operator== (const Note& o) const noexcept
        {
            return startBeat == o.startBeat && pitch == o.pitch
                && velocity == o.velocity && durationBeats == o.durationBeats;
        }
        bool operator!= (const Note& o) const noexcept { return ! (*this == o); }
    };

    // Pure, deterministic, seeded. Returns steps sorted by (stepIndex,
    // sliceIndex). Empty when sliceCount <= 0. Defensive clamps keep every
    // output in [0, bars*4*grid) steps / [0, sliceCount) slices.
    static std::vector<Step> generate (const Params& raw)
    {
        Params p = raw;
        p.bars         = std::clamp (p.bars, kMinBars, kMaxBars);
        p.grid         = std::clamp (p.grid, kMinGrid, kMaxGrid);
        p.velocityMin  = std::clamp (p.velocityMin, 1, 127);
        p.velocityMax  = std::clamp (p.velocityMax, 1, 127);
        if (p.velocityMin > p.velocityMax)
            std::swap (p.velocityMin, p.velocityMax);
        p.ghostFills   = std::clamp (p.ghostFills, 0, kMaxGhostFills);
        if (p.sliceCount <= 0)
            return {};

        const uint64_t seed = (p.seed == 0) ? kDefaultSeed : p.seed;
        std::seed_seq seq { static_cast<uint32_t> (seed >> 32),
                            static_cast<uint32_t> (seed & 0xffffffffu) };
        std::mt19937 rng (seq);

        auto nextInt = [&rng] (int lo, int hi) {
            if (hi <= lo) return lo;
            return lo + static_cast<int> (rng() % static_cast<uint32_t> (hi - lo + 1));
        };
        auto nextBool = [&rng] { return (rng() & 1u) != 0u; };

        const bool dropFirst = (p.style == Style::JungleEdit) || p.dropFirst;
        const int  stepsPerBar = 4 * p.grid;
        const int  totalSteps  = p.bars * stepsPerBar;
        const bool swing32     = stepsPerBar >= 32; // grid >= 8: 32nd fills/ghosts

        auto mapStep = [&] (int bar, int s16) {
            return bar * stepsPerBar + (s16 * p.grid) / 4;
        };
        auto roleSlice = [&] (int base) {
            return base % p.sliceCount;
        };

        std::vector<Step> out;
        std::vector<char> claimed (static_cast<size_t> (totalSteps), 0);

        auto addStep = [&] (int bar, int s16, int slice, int velocity, bool allowSwing) {
            int step = mapStep (bar, s16);
            if (allowSwing && swing32)
                step += nextInt (0, 1);
            if (step < 0 || step >= totalSteps)
                step = std::clamp (step, 0, totalSteps - 1);
            if (claimed[static_cast<size_t> (step)])
                return;
            claimed[static_cast<size_t> (step)] = true;
            out.push_back ({ step, std::clamp (slice, 0, p.sliceCount - 1), velocity });
        };

        // Skeleton per style (16th steps within the bar) + role slice bases.
        const std::vector<int> kickSteps  = (p.style == Style::TwoStep)  ? std::vector<int>{ 0, 9 }
                                          : (p.style == Style::Halftime) ? std::vector<int>{ 0, 13 }
                                          : std::vector<int>{ 0, 8 };
        const std::vector<int> kickBases  = (p.style == Style::TwoStep || p.style == Style::Halftime)
                                          ? std::vector<int>{ 0, 0 }
                                          : std::vector<int>{ 0, 4 };
        const std::vector<int> snareSteps = (p.style == Style::Halftime) ? std::vector<int>{ 8 }
                                          : std::vector<int>{ 4, 12 };
        const std::vector<int> fillSteps  = (p.style == Style::TwoStep || p.style == Style::Halftime)
                                          ? std::vector<int>{ }
                                          : std::vector<int>{ 14, 15 };
        const std::vector<int> ghostPool  = (p.style == Style::Amen || p.style == Style::JungleEdit
                                          || p.style == Style::Random)
                                          ? std::vector<int>{ 2, 6, 10 }
                                          : std::vector<int>{ 6, 14 };

        const int midVel = (p.velocityMin + p.velocityMax) / 2;
        const int snareVel = std::max (p.velocityMin, p.velocityMax - 5);

        auto jitter = [&] (int base) {
            return std::clamp (base + nextInt (-6, 6), p.velocityMin, p.velocityMax);
        };

        for (int bar = 0; bar < p.bars; ++bar)
        {
            for (size_t k = 0; k < kickSteps.size(); ++k)
            {
                if (bar == 0 && k == 0 && dropFirst)
                    continue; // the classic open: bar 0 starts on the snare
                const int slice = (p.style == Style::Random)
                                    ? nextInt (0, p.sliceCount - 1)
                                    : roleSlice (kickBases[k]);
                addStep (bar, kickSteps[k], slice, jitter (p.velocityMax), false);
            }
            for (int s16 : snareSteps)
                addStep (bar, s16, roleSlice (2),
                      jitter (snareVel), false);
            for (int s16 : fillSteps)
            {
                const int slice = (p.style == Style::JungleEdit || p.style == Style::Random)
                                    ? nextInt (0, p.sliceCount - 1)
                                    : roleSlice (s16 == 15 ? 5 : 4);
                addStep (bar, s16, slice, jitter (midVel), true);
            }
            if (p.style == Style::Random)
            {
                for (int s16 : ghostPool)
                    if (nextBool())
                        addStep (bar, s16, nextInt (0, p.sliceCount - 1), p.velocityMin, true);
            }
            else
            {
                std::vector<int> pool = ghostPool;
                int picks = p.ghostFills;
                while (picks-- > 0 && ! pool.empty())
                {
                    const int idx = nextInt (0, static_cast<int> (pool.size()) - 1);
                    const int s16 = pool[static_cast<size_t> (idx)];
                    pool.erase (pool.begin() + idx);
                    addStep (bar, s16, nextInt (0, p.sliceCount - 1), p.velocityMin, true);
                }
            }
        }

        std::sort (out.begin(), out.end(), [] (const Step& a, const Step& b) {
            if (a.stepIndex != b.stepIndex) return a.stepIndex < b.stepIndex;
            return a.sliceIndex < b.sliceIndex;
        });
        return out;
    }

    // Convert generated steps to MIDI notes: pitch = baseNote + sliceIndex,
    // startBeat = stepIndex / grid (clip-local), duration = one grid step.
    static std::vector<Note> asNotes (const Params& params,
                                      const std::vector<Step>& steps, int baseNote)
    {
        const double beatsPerStep = 1.0 / static_cast<double> (std::max (1, params.grid));
        std::vector<Note> out;
        out.reserve (steps.size());
        for (const auto& s : steps)
            out.push_back ({ static_cast<double> (s.stepIndex) * beatsPerStep,
                             baseNote + s.sliceIndex, s.velocity, beatsPerStep });
        return out;
    }
};
