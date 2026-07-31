#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace HDAW
{

// ── Deterministic PRNG ──
// SplitMix64: fast, high-quality, fully deterministic. Same seed → same stream.
class SplitMix64
{
public:
    explicit SplitMix64 (uint64_t seed) : state (seed) {}

    uint64_t nextU64();
    double nextFloat();                 // [0, 1), 53-bit precision
    int nextInt (int lo, int hi);       // inclusive [lo, hi]
    bool nextBool (double p);           // true with probability p in [0, 1]

private:
    uint64_t state;
};

// Deterministic namespaced seed: combine a root seed with a string namespace.
uint64_t deriveSeed (uint64_t rootSeed, const std::string& ns);

// Branches independent, reproducible RNG streams from a root seed plus a
// sequence of namespace parts (mirrors teknoir's ReproState.branch(...).rng()).
class ReproState
{
public:
    explicit ReproState (uint64_t rootSeed) : root (rootSeed) {}

    SplitMix64 rng (const std::vector<std::string>& parts) const;
    SplitMix64 rng (const std::string& a) const;
    SplitMix64 rng (const std::string& a, const std::string& b) const;
    SplitMix64 rng (const std::string& a, const std::string& b, const std::string& c) const;

    uint64_t rootSeed() const { return root; }

private:
    uint64_t root;
};

// ── Euclidean rhythms ──
// Bjorklund-style even distribution of k hits over n steps, optional rotation.
// Returns sorted step indices in [0, n-1].
std::vector<int> euclideanSteps (int k, int n, int rot = 0);

// Map hit indices defined on a sourceSteps grid onto a division grid (per bar).
std::vector<int> mapStepsToDivision (const std::vector<int>& hits, int sourceSteps, int division);

// ── Rhythm DSL ──
// Strudel-subset pattern language: 'x' hit, '-' rest, '_' separator (ignored),
// whitespace ignored, "[ ... ]xN" repeat group N times, "E(k,n[,rot])" euclidean.
// Returns sorted unique hit indices in [0, division-1].
// Throws std::invalid_argument on malformed input.
std::vector<int> expandToDivision (const std::string& pattern, int division);

// ── Micro-timing helpers ──
int humanizeInt (SplitMix64& rng, int value, int lo, int hi);

// Offset odd 16th-index onsets for swing. swingPercent 50 = straight;
// >50 pushes odd steps later, <50 earlier.
int applySwing (int tickInBar, int stepIndex, double swingPercent, int sixteenthTicks);

// ── Pitch-motion helpers ──
// Weighted choice among (value, weight) candidates. Weights clamped to >= 0.
int weightedChoice (SplitMix64& rng, const std::vector<std::pair<int, double>>& candidates);

// Teknoir-style Markov degree motion: stay / step ±1 / leap to 5th or b7,
// with a favored-degree bias. current and result are indices in [0, degreeCount).
int nextMarkovDegree (SplitMix64& rng, int current, int degreeCount);

} // namespace HDAW
