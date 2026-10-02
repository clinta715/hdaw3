// Tests for the band-aware SliceDetector.
//
// The previous broadband detector (threshold from the GLOBAL peak, envelope
// reset after every hit) is gone: it scored ~0 % recall on real library loops.
// These tests are synthetic + deterministic and run at a realistic sample rate
// (48 kHz) so the band edges (150 / 800 / 2500 Hz) actually mean something -
// the old test used 1000 Hz, which hid the failure completely.

#include <gtest/gtest.h>
#include "engine/SliceDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

inline int64_t msToFrames (double ms) { return static_cast<int64_t> (std::llround (ms * 0.001 * kSr)); }

inline int64_t absDiff (int64_t a, int64_t b) { return a > b ? a - b : b - a; }

// Nearest distance from `frame` to any of `truth`.
int64_t nearestError (int64_t frame, const std::vector<int64_t>& truth)
{
    int64_t best = INT64_MAX;
    for (auto t : truth)
        best = std::min (best, absDiff (frame, t));
    return best;
}

// Short broadband burst (noise with an exponential decay) - a snare/hat hit.
// Step onset, so the envelope rise (and therefore the flux) lands on the first
// frames of the hit.
void addNoiseBurst (std::vector<float>& x, int64_t at, double amp, double decayMs, std::mt19937& rng)
{
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    const double inv = 1.0 / (decayMs * 0.001 * kSr);
    for (int64_t k = 0; at + k < static_cast<int64_t> (x.size()); ++k)
    {
        const double env = std::exp (-static_cast<double> (k) * inv);
        if (env < 1e-4)
            break;
        x[static_cast<size_t> (at + k)] += static_cast<float> (amp * env) * dist (rng);
    }
}

// Decaying tone: sine with a raised-cosine attack and an exponential decay.
// The attack is kept short but not clicky so the hit stays in its own band
// instead of leaking a broadband edge into every other band.
void addDecayingTone (std::vector<float>& x, int64_t at, double amp,
                      double freq, double attackMs, double decayMs)
{
    const int64_t atk = std::max<int64_t> (1, msToFrames (attackMs));
    const double decayRate = 1.0 / (decayMs * 0.001 * kSr);
    for (int64_t k = 0; at + k < static_cast<int64_t> (x.size()); ++k)
    {
        const double env = (k < atk)
            ? 0.5 * (1.0 - std::cos (kPi * static_cast<double> (k) / static_cast<double> (atk)))
            : std::exp (-static_cast<double> (k - atk) * decayRate);

        if (env < 1e-4 && k > atk)
            break;

        x[static_cast<size_t> (at + k)] +=
            static_cast<float> (amp * env * std::sin (2.0 * kPi * freq * static_cast<double> (k) / kSr));
    }
}

// transient() always brackets the buffer with 0 and len; strip those so the
// tests only look at real onsets.
std::vector<int64_t> interiorOnsets (const std::vector<int64_t>& pts, int64_t len)
{
    std::vector<int64_t> out;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const int64_t p = pts[i];
        if (p == 0 && i == 0)
            continue;
        if (p == len && i + 1 == pts.size())
            continue;
        out.push_back (p);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Grid (unchanged behaviour)
// ---------------------------------------------------------------------------

TEST(SliceDetector, GridModeSlicesAtBeats)
{
    // 2-second sample at 1000 Hz (2000 frames), bpm 120, grid 0.25 beat.
    // 120 bpm = 2 beats/sec -> 4 beats in 2s -> 16 slices at 0.25 beat.
    auto pts = HDAW::SliceDetector::grid(2000, 1000.0, 120.0, 0.25);
    EXPECT_FALSE(pts.empty());
    EXPECT_EQ(pts.front(), 0);
    EXPECT_EQ(pts.back(), 2000);
    EXPECT_TRUE(std::is_sorted(pts.begin(), pts.end()));
}

// ---------------------------------------------------------------------------
// transient() - onset finding
// ---------------------------------------------------------------------------

TEST(SliceDetector, TransientFindsOnsetsWithinTolerance)
{
    // A lone snare-like burst at a known frame.  Proof: exactly one onset is
    // reported, within +-10 ms of that frame.  The old broadband detector found
    // 0 interior onsets here (its envelope never built up fast enough).
    const int64_t len   = msToFrames(1000.0);
    const int64_t onset = msToFrames(416.0);

    std::mt19937 rng(20241001u);
    std::vector<float> x(static_cast<size_t>(len), 0.0f);
    addNoiseBurst(x, onset, 0.9, 25.0, rng);

    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    const auto found = interiorOnsets(pts, len);

    ASSERT_EQ(found.size(), 1u) << "expected exactly one onset for a lone hit";
    EXPECT_LE(absDiff(found.front(), onset), msToFrames(10.0))
        << "onset " << found.front() << " not within 10 ms of " << onset;
}

TEST(SliceDetector, TransientFindsQuietHatsUnderALoudKick)
{
    // Loud kick (1.0, 55 Hz) on the beats, quiet hats (0.15, noise) on the
    // offbeats.  Proof: all 8 hits are found within +-10 ms AND exactly one
    // onset is emitted per hit.  The old code found 0 hats and 2 points per kick
    // (one from the envelope reset) because its threshold came from the global
    // peak, i.e. from the kick.
    const int64_t len    = msToFrames(2000.0);   // 120 bpm
    const int64_t base   = msToFrames(50.0);
    const int64_t eighth = msToFrames(250.0);

    std::mt19937 rng(7u);
    std::vector<float> x(static_cast<size_t>(len), 0.0f);

    std::vector<int64_t> hits;
    for (int k = 0; k < 4; ++k)
    {
        const int64_t at = base + eighth * (2 * k);
        hits.push_back(at);
        addDecayingTone(x, at, 1.0, 55.0, 3.0, 25.0);   // kick
    }
    for (int k = 0; k < 4; ++k)
    {
        const int64_t at = base + eighth * (2 * k + 1);
        hits.push_back(at);
        addNoiseBurst(x, at, 0.15, 20.0, rng);           // hat
    }

    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    const auto found = interiorOnsets(pts, len);
    const int64_t tol = msToFrames(10.0);

    EXPECT_EQ(found.size(), 8u) << "expected exactly one onset per hit";

    for (auto t : hits)
    {
        int n = 0;
        for (auto p : found)
            if (absDiff(p, t) <= tol)
                ++n;
        EXPECT_EQ(n, 1) << "hit at frame " << t << " matched " << n << " onsets";
    }
}

TEST(SliceDetector, TransientIsNotMaskedByAGlobalPeak)
{
    // One loud snare (1.0, 180 Hz body) then 3 quiet hats (0.25 noise).
    // Proof: all 4 hits are found, once each.  The old code's threshold was a
    // fraction of the GLOBAL peak, so the 4x quieter hats were below it and
    // vanished; here each band carries its own threshold.
    const int64_t len = msToFrames(1500.0);

    std::mt19937 rng(11u);
    std::vector<float> x(static_cast<size_t>(len), 0.0f);

    std::vector<int64_t> hits;
    hits.push_back(msToFrames(125.0));
    addDecayingTone(x, hits.back(), 1.0, 180.0, 5.0, 40.0);      // loud snare

    for (int k = 1; k <= 3; ++k)
    {
        hits.push_back(msToFrames(125.0) + msToFrames(250.0) * k);
        addNoiseBurst(x, hits.back(), 0.25, 15.0, rng);           // quiet hat
    }

    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    const auto found = interiorOnsets(pts, len);
    const int64_t tol = msToFrames(10.0);

    EXPECT_EQ(found.size(), 4u);
    for (auto t : hits)
    {
        int n = 0;
        for (auto p : found)
            if (absDiff(p, t) <= tol)
                ++n;
        EXPECT_EQ(n, 1) << "hit at frame " << t << " matched " << n << " onsets";
    }
}

TEST(SliceDetector, OnsetsCarryBandMasks)
{
    HDAW::SliceDetectOptions opt;

    {   // low-only hit: a 60 Hz thump -> low band (bit0), never the high band (bit3)
        std::vector<float> x(static_cast<size_t>(msToFrames(600.0)), 0.0f);
        addDecayingTone(x, msToFrames(50.0), 0.8, 60.0, 5.0, 200.0);

        auto ons = HDAW::SliceDetector::onsets(x.data(), static_cast<int64_t>(x.size()), kSr, opt);
        ASSERT_EQ(ons.size(), 1u);
        EXPECT_TRUE((ons.front().bandMask & 1u) != 0u) << "low band missing for a 60 Hz hit";
        EXPECT_TRUE((ons.front().bandMask & 8u) == 0u) << "high band fired for a 60 Hz hit";
    }

    {   // high-only hit: an 8 kHz thump -> high band (bit3), never the low band (bit0)
        std::vector<float> x(static_cast<size_t>(msToFrames(600.0)), 0.0f);
        addDecayingTone(x, msToFrames(50.0), 0.8, 8000.0, 5.0, 200.0);

        auto ons = HDAW::SliceDetector::onsets(x.data(), static_cast<int64_t>(x.size()), kSr, opt);
        ASSERT_EQ(ons.size(), 1u);
        EXPECT_TRUE((ons.front().bandMask & 8u) != 0u) << "high band missing for an 8 kHz hit";
        EXPECT_TRUE((ons.front().bandMask & 1u) == 0u) << "low band fired for an 8 kHz hit";
    }
}

TEST(SliceDetector, TransientStillIncludesStartAndEnd)
{
    std::vector<float> x(500, 0.5f);
    auto pts = HDAW::SliceDetector::transient(x, 0.5);
    ASSERT_GE(pts.size(), 2u);
    EXPECT_EQ(pts.front(), 0);
    EXPECT_EQ(pts.back(), 500);
}

// ---------------------------------------------------------------------------
// driftAlignedGrid()
// ---------------------------------------------------------------------------

TEST(SliceDetector, DriftAlignedGridFollowsAWobblingSource)
{
    // A source that plays 4 % slow: nominal step 24000 frames, real step 24960.
    // Proof: every emitted point (bar the final one, which the contract forces
    // to len) lands within +-10 ms of a true onset, and the mean absolute error
    // is at least 3x smaller than the plain grid()'s - the plain grid is a
    // nominal one and drifts ~4 % of the loop length away by the end.
    const double bpm = 120.0, gridBeats = 1.0;
    const int64_t nominalStep = static_cast<int64_t> (std::llround((60.0 / bpm) * gridBeats * kSr));
    const int64_t trueStep    = static_cast<int64_t> (std::llround(nominalStep * 1.04));
    const int     nHits       = 9;
    const int64_t len         = nominalStep * nHits;

    std::mt19937 rng(99u);
    std::vector<float> x(static_cast<size_t>(len), 0.0f);
    std::vector<int64_t> truth;
    for (int i = 0; i < nHits; ++i)
    {
        const int64_t at = trueStep * i;
        truth.push_back(at);
        addNoiseBurst(x, at, 0.8, 20.0, rng);
    }
    ASSERT_LT(truth.back(), len);

    auto drift = HDAW::SliceDetector::driftAlignedGrid(x.data(), len, kSr, bpm, gridBeats, 0.5);
    auto plain = HDAW::SliceDetector::grid(len, kSr, bpm, gridBeats);

    ASSERT_GE(drift.size(), 4u);
    ASSERT_GE(plain.size(), 4u);
    EXPECT_EQ(drift.front(), 0);
    EXPECT_EQ(drift.back(), len);
    EXPECT_TRUE(std::is_sorted(drift.begin(), drift.end()));

    const int64_t tol = msToFrames(10.0);
    for (size_t i = 0; i + 1 < drift.size(); ++i)
        EXPECT_LE(nearestError(drift[i], truth), tol)
            << "drift point " << i << " at " << drift[i] << " is off the performance";

    // Mean absolute error, ignoring each list's final (forced) point.
    const size_t cmp = std::min(drift.size(), plain.size()) - 1;
    double errDrift = 0.0, errPlain = 0.0;
    for (size_t i = 0; i < cmp; ++i)
    {
        errDrift += static_cast<double>(nearestError(drift[i], truth));
        errPlain += static_cast<double>(nearestError(plain[i], truth));
    }
    errDrift /= static_cast<double>(cmp);
    errPlain /= static_cast<double>(cmp);

    EXPECT_GT(errPlain, errDrift * 3.0)
        << "drift mean error " << errDrift << " vs plain grid mean error " << errPlain;
}

TEST(SliceDetector, DriftAlignedGridFallsBackOnSparseInput)
{
    // Fewer than 4 onsets: driftAlignedGrid must be exactly the nominal grid.
    const int64_t len = msToFrames(4500.0);

    std::mt19937 rng(5u);
    std::vector<float> x(static_cast<size_t>(len), 0.0f);
    addNoiseBurst(x, msToFrames(500.0), 0.8, 20.0, rng);
    addNoiseBurst(x, msToFrames(2500.0), 0.8, 20.0, rng);

    auto drift = HDAW::SliceDetector::driftAlignedGrid(x.data(), len, kSr, 120.0, 1.0, 0.5);
    auto plain = HDAW::SliceDetector::grid(len, kSr, 120.0, 1.0);

    EXPECT_EQ(drift, plain);
}
