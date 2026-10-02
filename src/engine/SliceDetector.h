#pragma once

// ============================================================================
//  SliceDetector - slice-point analysis for the internal sampler.
//
//  NOTE FOR FUTURE READERS: the previous implementation of transient() was a
//  single BROADBAND envelope follower
//      env = 0.999*env + 0.001*|x[i]|
//      thresh = (1-sensitivity) * globalPeak * 0.5
//      minGap = len/64, env reset to 0 after every hit,
//      trigger on |x[i]| > |x[i-1]|
//  It was measured against real library loops (ground truth = smoothed-envelope
//  peaks > 25 % of max, 90 ms refractory) and scored ~0 % recall:
//      madoxy loop (full) 140bpm.wav       : 51 hits ->  0
//      madoxy loop (hats only) 140bpm.wav  : 26 hits ->  0
//      madoxy glitch loop 135bpm.wav       : 43 hits -> 20 (23 %)
//      ANTINOMY_04_Closed_Hihat_Pluck.wav  :  1 hit  ->  0
//  Two independent defects caused that: (a) the threshold was derived from the
//  GLOBAL peak, so one loud hit masked every quiet hit after it; (b) resetting
//  the envelope to zero after each hit manufactured spurious extra onsets
//  (two per kick, ~14 ms and ~46 ms late).  Do NOT restore it.  The band-aware
//  engine below is the replacement.
//
//  Header-only and dependency-free on purpose: SamplerEngine.h includes it, so
//  neither JUCE nor Qt may leak in here.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace HDAW {

struct SliceOnset
{
    int64_t  frame    = 0;    // frame index into the analysed buffer
    uint32_t bandMask = 0;    // bit0 low, bit1 low-mid, bit2 mid, bit3 high
    float    strength = 0.0f; // 0..1, the band's flux at this onset / that band's max flux
};

struct SliceDetectOptions
{
    // 0..1, higher = more onsets.  This is the ONLY decision knob: it drives the
    // histogram percentile, the per-band refractory, the global refractory and
    // the strength floor through the monotone mapping documented on onsets().
    // Leaving the other decision variables fixed is what made the old version's
    // `sensitivity` inert (a 4.7x knob sweep moved the onset count by 17 %).
    double sensitivity = 0.5;
};

class SliceDetector
{
public:
    static constexpr int kNumBands = 4;

    // Band edges (Hz).  The four bands are formed by a CASCADE of two one-pole
    // low-pass stages per corner (12 dB/oct):
    //     band 0  low      : LP150
    //     band 1  low-mid  : LP800  - LP150
    //     band 2  mid      : LP2500 - LP800
    //     band 3  high     : HP2500  (== x - LP2500)
    // The cascade is what keeps the leakage of a low tone into the high band
    // small (a single pole leaks ~2 % of an 8 kHz tone into the 150 Hz band and
    // ~2.4 % of a 60 Hz tone into the high band; two poles leak ~0.04 % and
    // ~0.05 % respectively - the remaining ~4.8 % high-band residue of a 60 Hz
    // tone is the *phase* lag of the low-pass, not magnitude, and is what the
    // participation gate in onsets() is there to reject).
    static constexpr double kBandEdges[5] = { 20.0, 150.0, 800.0, 2500.0, 16000.0 };

    // transient() has no sample-rate parameter (pinned API), so it analyses at
    // this nominal rate.  The band edges above are still correct for 44.1/48 kHz
    // material; call onsets() directly when the true rate matters.
    static constexpr double kNominalSampleRate = 48000.0;

    // Grid: divide region [0, len) into slices of `gridBeats` at given bpm/sampleRate.
    // Returns sorted frame indices including 0 and len.
    static std::vector<int64_t> grid (int64_t len, double sr, double bpm, double gridBeats)
    {
        std::vector<int64_t> pts;
        if (bpm <= 0.0 || gridBeats <= 0.0 || sr <= 0.0 || len <= 0)
            return pts;

        const double beatsTotal = (static_cast<double> (len) / sr) * (bpm / 60.0);
        const int count = std::max (1, static_cast<int> (std::round (beatsTotal / gridBeats)));

        for (int i = 0; i <= count; ++i)
            pts.push_back (static_cast<int64_t> (static_cast<double> (i) * len / count));

        if (! pts.empty())
            pts.back() = len;

        return pts;
    }

    // Band-aware onset engine.
    //
    //  1. band split (see kBandEdges) with per-band filter state;
    //  2. per band: rectify -> one-pole smoothing (~5 ms) -> DIFFERENCE OVER A
    //     ~6 ms LAG -> half-wave rectify => "flux";
    //  3. ADAPTIVE THRESHOLD PER BAND (the masking fix):
    //         global = max(histogram p(s) percentile of this band's POSITIVE flux,
    //                      mean + kStdMult * stddev of that same positive flux)
    //                  * c(s)
    //         local  = kLocalRatio * (trailing ~40 ms mean of this band's flux;
    //                                 the pre-buffer counts as silence)
    //         thr    = max(global, local, f(s) * (this band's max flux), 1e-3 * mx)
    //     No global peak is ever consulted - that is what used to mask quiet
    //     hits behind loud ones;
    //  4. peak-pick per band: local maximum within +-8 samples, above threshold,
    //     respecting m(s) (a later stronger peak inside the gap replaces the
    //     weaker one instead of adding a duplicate);
    //  5. merge across bands: onsets within kMergeMs collapse into one, bandMask
    //     OR'd and max strength kept;
    //  6. global refractory g(s): walk the merged list and DROP any onset that
    //     lands within g(s) of the previously kept one.  This is the primary
    //     onset-count control - the old 40 ms per-band refractory was far too
    //     short for material whose hits are 90-310 ms apart, so a single acoustic
    //     event emitted one onset per band (its band peaks sit up to ~25 ms
    //     apart) plus one per decay-tail flux bump;
    //  7. strength = flux at the onset / that band's max flux, clamped 0..1.
    //
    //  SENSITIVITY MAPPING (s = clamp(sensitivity, 0, 1); every term is monotone
    //  in s, conservative at 0 and permissive at 1).  Calibrated against four
    //  real library loops with a smoothed-envelope ground truth:
    //
    //      quantity                    at s=0    at s=1     meaning
    //      --------------------------  --------  ---------  ---------------------
    //      histogram percentile p(s)    0.990     0.924     of the band's positive flux
    //      threshold scale c(s)         1.00      0.23      multiplies the global term
    //      strength floor f(s)          0.30      0.00      fraction of the band's max flux
    //      per-band refractory m(s)     60 ms     0 ms      inside one band
    //      global refractory g(s)      140 ms     10 ms     over the merged list
    //
    //      p(s) = 0.990 - 0.066 * s
    //      c(s) = 1.00  - 0.77  * s
    //      f(s) = max(0, 0.30 - 0.44 * s)
    //      m(s) = 60.0  - 60.0  * s   [ms]
    //      g(s) = 140.0 - 130.0 * s   [ms]
    //
    //  The three load-bearing terms are the ones the old code left fixed, which
    //  is why its `sensitivity` did nothing: the refractory and the strength
    //  floor dominate the count, and the percentile decides how much of the
    //  flux distribution is even a candidate.  At the default 0.5 the mapping
    //  yields p=0.957, c=0.615, f=0.08, m=30 ms, g=75 ms.
    //
    //  Why the lagged difference: a one-sample difference of the smoothed
    //  envelope has the same magnitude for a hit's attack as for the smoothed
    //  noise of its own decay tail (both are ~mu/tau), so a bare flux picks a
    //  decaying noise hit apart.  Differencing over ~6 ms keeps the whole
    //  envelope RISE (~mu) while the tail's fluctuation stays far smaller - the
    //  2 ms lag the first band-aware version used still resolved decay-tail
    //  bumps into separate peaks, which is where most of its false positives
    //  came from.
    //
    //  Why the local term: a sustained tone's rectification ripple has a
    //  *steady* flux, so it looks like an endless run of onsets.  The trailing
    //  mean is high there (peak/mean ~2.5) and near zero in front of a real hit
    //  (peak/mean > 10), which is what separates the two.  The ratio is 5.0, not
    //  6.0: real glitch/percussion material puts quiet-but-real hits at
    //  peak/mean ~4.5, and a 6.0 gate silently dropped them.
    //
    //  A band whose RMS is below kParticipation (7 % of the loudest band's RMS)
    //  is skipped: it is filter leakage, not signal (the phase lag of the
    //  one-pole cascade leaks ~4.8 % of a 60 Hz tone into the 2500 Hz-high
    //  band).  Nothing is allocated per sample; scratch is sized once per call.
    static std::vector<SliceOnset> onsets (const float* x, int64_t len, double sr,
                                           const SliceDetectOptions& opt)
    {
        std::vector<SliceOnset> out;
        if (x == nullptr || len <= 0 || sr <= 0.0)
            return out;

        BandSplitter split;
        split.prepare (sr);

        // ---- pass 1: per-band RMS (participation gate) --------------------
        double sumSq[kNumBands] = { 0.0, 0.0, 0.0, 0.0 };
        double bnd[kNumBands];
        for (int64_t i = 0; i < len; ++i)
        {
            split.next (static_cast<double> (x[i]), bnd);
            for (int b = 0; b < kNumBands; ++b)
                sumSq[b] += bnd[b] * bnd[b];
        }

        double rms[kNumBands];
        double rmsMax = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            rms[b] = std::sqrt (sumSq[b] / static_cast<double> (len));
            rmsMax = std::max (rmsMax, rms[b]);
        }
        if (rmsMax <= 1e-12)
            return out;

        bool active[kNumBands];
        for (int b = 0; b < kNumBands; ++b)
            active[b] = rms[b] >= kParticipation * rmsMax;

        // ---- sensitivity -> decision variables (monotone; see the mapping on
        //      the doc comment above) ------------------------------------------
        const double sens      = std::min (1.0, std::max (0.0, opt.sensitivity));
        const double percentile = kPercentileHi - kPercentileSpan * sens;
        const double scale      = kScaleHi - kScaleSpan * sens;
        const double floorFrac  = std::max (0.0, kFloorHi - kFloorSpan * sens);
        const double minGapMs   = kMinGapHi - kMinGapSpan * sens;
        const double globalGapMs= kGlobalGapHi - kGlobalGapSpan * sens;

        const double smoothA = 1.0 - std::exp (-1.0 / (kSmoothMs * 0.001 * sr));  // ~5 ms
        const int64_t minGap = std::max<int64_t> (1, static_cast<int64_t> (
                                   std::llround (minGapMs * 0.001 * sr)));
        const int64_t lag    = std::max<int64_t> (1, static_cast<int64_t> (
                                   std::llround (kLagMs * 0.001 * sr)));
        const int64_t win    = std::max<int64_t> (1, static_cast<int64_t> (
                                   std::llround (kLocalWinMs * 0.001 * sr)));

        std::vector<double>  flux    (static_cast<size_t> (len), 0.0); // sized once, per call
        std::vector<double>  envRing (static_cast<size_t> (lag), 0.0); // env[i - lag] lookback
        std::vector<int32_t> hist    (kHistBins, 0);                   // sized once, per call

        for (int b = 0; b < kNumBands; ++b)
        {
            if (! active[b])
                continue;

            split.reset();
            std::fill (envRing.begin(), envRing.end(), 0.0);

            double  sm = 0.0, mx = 0.0;
            int64_t ringPos = 0;
            for (int64_t i = 0; i < len; ++i)
            {
                split.next (static_cast<double> (x[i]), bnd);
                sm += smoothA * (std::fabs (bnd[b]) - sm);

                const double prevEnv = envRing[static_cast<size_t> (ringPos)];
                envRing[static_cast<size_t> (ringPos)] = sm;
                if (++ringPos == lag)
                    ringPos = 0;

                double pf = sm - prevEnv;         // half-wave rectified lagged difference
                if (pf < 0.0)
                    pf = 0.0;

                flux[static_cast<size_t> (i)] = pf;
                if (pf > mx)
                    mx = pf;
            }

            if (mx <= 0.0)
                continue;

            // statistics over this band's POSITIVE flux (single extra pass, no
            // scratch vector): histogram for the percentile + sum/sumSq for the
            // mean+k*stddev floor.
            std::fill (hist.begin(), hist.end(), 0);
            double  sum = 0.0, sumSq = 0.0;
            int64_t nPos = 0;
            for (int64_t i = 0; i < len; ++i)
            {
                const double v = flux[static_cast<size_t> (i)];
                if (v <= 0.0)
                    continue;
                ++nPos;
                sum   += v;
                sumSq += v * v;
                int bin = static_cast<int> (v / mx * (kHistBins - 1));
                bin = std::min (kHistBins - 1, std::max (0, bin));
                ++hist[static_cast<size_t> (bin)];
            }
            if (nPos <= 0)
                continue;

            // histogram percentile of the positive flux
            const double want = percentile * static_cast<double> (nPos);
            double cum = 0.0;
            int    pctBin = kHistBins - 1;
            for (int k = 0; k < kHistBins; ++k)
            {
                cum += hist[static_cast<size_t> (k)];
                if (cum >= want) { pctBin = k; break; }
            }
            const double pct = (static_cast<double> (pctBin) + 1.0) / kHistBins * mx;

            // mean + k * stddev floor
            const double mean  = sum / static_cast<double> (nPos);
            const double var   = std::max (0.0, sumSq / static_cast<double> (nPos) - mean * mean);
            const double floorStat = mean + kStdMult * std::sqrt (var);

            // A weak flux blip is not a hit: gate on an absolute fraction of this
            // band's max flux as well (f(s); the band-relative terms above cannot
            // see it, and real material's decay tails sit at 0.10-0.25 of mx).
            const double globalThr = std::max ({ std::max (pct, floorStat) * scale,
                                                 floorFrac * mx, 1e-3 * mx });

            // peak-pick: local max within +-8, above max(globalThr, kLocalRatio *
            // trailing-mean flux), minGapMs refractory.
            int64_t lastFrame    = -minGap - 1;
            float   lastStrength = 0.0f;
            double  runSum       = 0.0;
            for (int64_t i = 0; i < len; ++i)
            {
                const double v = flux[static_cast<size_t> (i)];

                runSum += v;
                if (i >= win)
                    runSum -= flux[static_cast<size_t> (i - win)];
                // Divide by the full window: everything before frame 0 is silence.
                const double localMean = runSum / static_cast<double> (win);

                if (v <= globalThr || v < kLocalRatio * localMean)
                    continue;

                const int64_t lo = std::max<int64_t> (0, i - kPeakWindow);
                const int64_t hi = std::min<int64_t> (len - 1, i + kPeakWindow);
                bool isMax = true;
                for (int64_t j = lo; j <= hi; ++j)
                    if (flux[static_cast<size_t> (j)] > v) { isMax = false; break; }
                if (! isMax)
                    continue;

                const float s = static_cast<float> (std::min (1.0, v / mx));
                if (i - lastFrame >= minGap)
                {
                    out.push_back ({ i, static_cast<uint32_t> (1u << b), s });
                    lastFrame = i;
                    lastStrength = s;
                }
                else if (s > lastStrength && ! out.empty())
                {
                    out.back() = { i, static_cast<uint32_t> (1u << b), s };
                    lastFrame = i;
                    lastStrength = s;
                }
            }
        }

        // ---- merge across bands -------------------------------------------
        if (out.empty())
            return out;

        std::sort (out.begin(), out.end(),
                   [] (const SliceOnset& a, const SliceOnset& b) { return a.frame < b.frame; });

        const int64_t mergeFrames = std::max<int64_t> (1, static_cast<int64_t> (
                                        std::llround (kMergeMs * 0.001 * sr)));
        std::vector<SliceOnset> merged;
        merged.reserve (out.size());
        for (const auto& o : out)
        {
            if (! merged.empty() && o.frame - merged.back().frame <= mergeFrames)
            {
                merged.back().bandMask |= o.bandMask;
                merged.back().strength  = std::max (merged.back().strength, o.strength);
            }
            else
            {
                merged.push_back (o);
            }
        }

        // ---- global refractory: the primary onset-count control ------------
        // One acoustic event places its per-band flux peaks up to ~25 ms apart
        // (the merge above handles those) but its decay tail can still produce a
        // second peak 30-90 ms later; the per-band refractory cannot see that
        // because the tail peak usually lands in a *different* band.  Keeping the
        // EARLIEST onset of each g(s) window preserves the attack time, which a
        // replace-if-stronger rule does not (it walks the onset onto the tail).
        const int64_t globalGap = std::max<int64_t> (1, static_cast<int64_t> (
                                      std::llround (globalGapMs * 0.001 * sr)));
        std::vector<SliceOnset> kept;
        kept.reserve (merged.size());
        for (const auto& o : merged)
        {
            if (kept.empty() || o.frame - kept.back().frame >= globalGap)
                kept.push_back (o);
        }
        return kept;
    }

    // Transient: delegates to the band-aware onsets() engine.
    // Always returns sorted, deduped { 0, <onset frames>, len }.
    static std::vector<int64_t> transient (const std::vector<float>& x, double sensitivity)
    {
        const int64_t len = static_cast<int64_t> (x.size());

        SliceDetectOptions opt;
        opt.sensitivity = sensitivity;

        const std::vector<SliceOnset> ons = onsets (x.empty() ? nullptr : x.data(),
                                                    len, kNominalSampleRate, opt);

        std::vector<int64_t> pts;
        pts.reserve (ons.size() + 2);
        pts.push_back (0);
        for (const auto& o : ons)
            pts.push_back (o.frame);
        pts.push_back (len);

        std::sort (pts.begin(), pts.end());
        pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
        return pts;
    }

    // Onset-tracked grid for drifting/wobbling sources.
    //
    //  1. run onsets(); fewer than 4 -> return grid() unchanged;
    //  2. give every onset its nearest NOMINAL step index and least-squares fit
    //     frame = a + b*step; one outlier-rejection pass drops the worst residual
    //     when |r| > 2 sigma and refits;
    //  3. REJECT the fit (-> grid()) when b deviates from the nominal
    //     samples-per-step by more than +-25 %, or when a/b are non-finite;
    //  4. emit a + i*b for i = 0..N clamped to [0, len], first forced to 0 and
    //     last to len, sorted and strictly increasing.
    //
    //  The point: on a source that plays 4 % slow the emitted slices follow the
    //  performance instead of the nominal grid (which drifts ~4 % of the loop
    //  length away by the end).
    static std::vector<int64_t> driftAlignedGrid (const float* x, int64_t len, double sr,
                                                  double bpm, double gridBeats,
                                                  double sensitivity = 0.5)
    {
        std::vector<int64_t> fallback = grid (len, sr, bpm, gridBeats);
        if (x == nullptr || len <= 0 || sr <= 0.0 || bpm <= 0.0 || gridBeats <= 0.0)
            return fallback;

        SliceDetectOptions opt;
        opt.sensitivity = sensitivity;
        const std::vector<SliceOnset> ons = onsets (x, len, sr, opt);
        if (ons.size() < 4)
            return fallback;

        const double samplesPerStep = (60.0 / bpm) * gridBeats * sr;
        if (! (samplesPerStep > 1.0))
            return fallback;

        std::vector<double> step, frame;
        step.reserve (ons.size());
        frame.reserve (ons.size());
        for (const auto& o : ons)
        {
            step.push_back (std::round (static_cast<double> (o.frame) / samplesPerStep));
            frame.push_back (static_cast<double> (o.frame));
        }

        double a = 0.0, b = 0.0;
        if (! leastSquares (step, frame, a, b))
            return fallback;

        // one outlier-rejection pass
        {
            std::vector<double> res (frame.size(), 0.0);
            double mean = 0.0;
            for (size_t i = 0; i < frame.size(); ++i)
            {
                res[i] = frame[i] - (a + b * step[i]);
                mean += res[i];
            }
            mean /= static_cast<double> (frame.size());
            double var = 0.0;
            for (size_t i = 0; i < res.size(); ++i)
                var += (res[i] - mean) * (res[i] - mean);
            var /= static_cast<double> (res.size());
            const double sd = std::sqrt (var);

            size_t worst = 0;
            double worstAbs = -1.0;
            for (size_t i = 0; i < res.size(); ++i)
            {
                const double r = std::fabs (res[i]);
                if (r > worstAbs) { worstAbs = r; worst = i; }
            }

            if (sd > 0.0 && worstAbs > 2.0 * sd && res.size() > 2)
            {
                step.erase (step.begin() + static_cast<std::ptrdiff_t> (worst));
                frame.erase (frame.begin() + static_cast<std::ptrdiff_t> (worst));
                if (! leastSquares (step, frame, a, b))
                    return fallback;
            }
        }

        if (! std::isfinite (a) || ! std::isfinite (b))
            return fallback;
        if (std::fabs (b - samplesPerStep) > 0.25 * samplesPerStep)
            return fallback;

        const int64_t nSteps = std::max<int64_t> (1, static_cast<int64_t> (
                                    std::llround (static_cast<double> (len) / samplesPerStep)));

        std::vector<int64_t> pts;
        pts.reserve (static_cast<size_t> (nSteps) + 1);
        for (int64_t i = 0; i <= nSteps; ++i)
        {
            int64_t v = static_cast<int64_t> (std::llround (a + static_cast<double> (i) * b));
            v = std::min<int64_t> (len, std::max<int64_t> (0, v));
            pts.push_back (v);
        }
        if (pts.empty())
            return fallback;
        pts.front() = 0;
        pts.back()  = len;

        std::sort (pts.begin(), pts.end());
        pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
        return pts;
    }

private:
    static constexpr int     kHistBins      = 256;
    static constexpr int64_t kPeakWindow    = 8;     // samples, +-window for the local-max test
    static constexpr double  kParticipation = 0.07;  // band RMS / loudest band RMS
    static constexpr double  kSmoothMs      = 5.0;   // envelope smoothing
    static constexpr double  kLagMs         = 6.0;   // flux difference lag
    static constexpr double  kStdMult       = 1.4;   // mean + k*stddev floor
    static constexpr double  kLocalRatio    = 5.0;   // peak / trailing-mean flux
    static constexpr double  kLocalWinMs    = 40.0;  // trailing-mean window
    static constexpr double  kMergeMs       = 38.0;  // cross-band merge window

    // Sensitivity mapping (see the table on onsets()): value at s=0, minus
    // span * s.  All five terms are monotone decreasing in sensitivity.
    static constexpr double  kPercentileHi   = 0.990, kPercentileSpan = 0.066;
    static constexpr double  kScaleHi        = 1.00,  kScaleSpan      = 0.77;
    static constexpr double  kFloorHi        = 0.30,  kFloorSpan      = 0.44;
    static constexpr double  kMinGapHi       = 60.0,  kMinGapSpan     = 60.0;
    static constexpr double  kGlobalGapHi    = 140.0, kGlobalGapSpan  = 130.0;

    // Cascade of two one-pole low-pass stages per corner frequency (12 dB/oct).
    // The second stage is what cuts the magnitude leakage between bands: a
    // single pole leaks ~2 % of an 8 kHz tone into the 150 Hz band, two poles
    // leak ~0.04 %.  (The residual ~4.8 % of a 60 Hz tone in the high band is
    // the low-pass *phase* lag, which no cascade removes - that is what the
    // participation gate in onsets() is for.)
    struct BandSplitter
    {
        double a150 = 0.0, a800 = 0.0, a2500 = 0.0;
        double s150a = 0.0, s150b = 0.0;
        double s800a = 0.0, s800b = 0.0;
        double s2500a = 0.0, s2500b = 0.0;

        void prepare (double sr)
        {
            const double twoPi = 2.0 * 3.14159265358979323846;
            a150  = 1.0 - std::exp (-twoPi * 150.0  / sr);
            a800  = 1.0 - std::exp (-twoPi * 800.0  / sr);
            a2500 = 1.0 - std::exp (-twoPi * 2500.0 / sr);
            reset();
        }

        void reset()
        {
            s150a = s150b = s800a = s800b = s2500a = s2500b = 0.0;
        }

        void next (double x, double out[kNumBands])
        {
            s150a  += a150  * (x - s150a);   s150b  += a150  * (s150a  - s150b);
            s800a  += a800  * (x - s800a);   s800b  += a800  * (s800a  - s800b);
            s2500a += a2500 * (x - s2500a);  s2500b += a2500 * (s2500a - s2500b);

            out[0] = s150b;             // low     : LP150
            out[1] = s800b  - s150b;    // low-mid : LP800  - LP150
            out[2] = s2500b - s800b;    // mid     : LP2500 - LP800
            out[3] = x - s2500b;        // high    : HP2500
        }
    };

    static bool leastSquares (const std::vector<double>& s, const std::vector<double>& f,
                              double& a, double& b)
    {
        const size_t n = s.size();
        if (n < 2 || f.size() != n)
            return false;

        double sumS = 0.0, sumF = 0.0, sumSS = 0.0, sumSF = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            sumS  += s[i];
            sumF  += f[i];
            sumSS += s[i] * s[i];
            sumSF += s[i] * f[i];
        }

        const double denom = static_cast<double> (n) * sumSS - sumS * sumS;
        if (std::fabs (denom) < 1e-9)
            return false;

        b = (static_cast<double> (n) * sumSF - sumS * sumF) / denom;
        a = (sumF - b * sumS) / static_cast<double> (n);
        return std::isfinite (a) && std::isfinite (b);
    }
};

} // namespace HDAW
