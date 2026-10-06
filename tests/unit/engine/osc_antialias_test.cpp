// Slice C of docs/plans/2026-10-05-internal-synth-expansion.md — PolyBLEP
// oscillator anti-aliasing on psyarp (Saw/Square/Pulse/SuperSaw) and sub_synth
// (Saw/Square, plus the internal sub, which uses Square).
//
// This change is NON-ADDITIVE: the naive step-discontinuity waveforms are
// replaced IN PLACE (no opt-out param), so an existing project's render
// changes. The tests here are the alias-suppression evidence:
//
//   1. a HIGH saw's folded-partial energy at a known alias frequency DROPS
//      versus a NAIVE reference oscillator generated in-test at the same
//      pitch/sample-rate (the mathematical pre-slice-C definition);
//   2. a LOW note's harmonic structure is PRESERVED (AA must not damage the
//      fundamental tone — PolyBLEP's residual is tiny at low phase
//      increments).
//
// STEP 4 of the deliverable — the "stash the change, build naive, capture the
// buffer" A/B — is served by OscAntiAlias.AaSpectralAbMeasurement: it always
// renders both engines at a high and a low note and PRINTS the alias-band
// energy and spectral centroid (no assertions), so the same test source
// compiled against the naive engine and against the AA engine yields the two
// sides of the comparison. The numbers are reported in the slice's result, not
// asserted.
#include <gtest/gtest.h>

#include "engine/PsyArpEngine.h"
#include "engine/SubtractiveSynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSR     = 44100.0;
constexpr int    kBlock  = 512;
constexpr int    kFftOrd = 12;            // 4096-point analysis window
constexpr int    kFftLen = 1 << kFftOrd;

double noteToHz(int note)
{
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

// Hann-windowed single-bin DFT magnitude at `freq` over [start, start+count) of
// a full-length signal (absolute sample indices keep the phase honest — a
// reference oscillator can start at the same absolute index as the render).
// The Hann window kills leakage from the (much stronger) fundamental into a
// distant alias probe.
double dftMag(const std::vector<float>& x, size_t start, size_t count, double freq,
              size_t absStart = 0)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const double n = (double) count;
    for (size_t i = 0; i < count; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                               * (double) i / (n - 1.0));
        const double ph = 2.0 * juce::MathConstants<double>::pi * freq
                          * (double) (absStart + i) / kSR;
        const double s = (double) x[start + i] * w;
        re += s * std::cos(ph);
        im += s * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

// Sum of |X(f)|^2 sampled every 5 Hz about center `f` (+/- `half` Hz). Band
// summation is robust to the window-phase dependence of a single bin in a
// detuned multi-voice render (a single-bin harmonic ratio can swing ~2x with
// the window position; the summed band does not).
double bandEnergy(const std::vector<float>& x, size_t start, size_t count,
                  double f, double half, size_t absStart = 0)
{
    double e = 0.0;
    for (double fv = f - half; fv <= f + half; fv += 5.0)
    {
        const double m = dftMag(x, start, count, fv, absStart);
        e += m * m;
    }
    return e;
}

double rmsOf(const std::vector<float>& x)
{
    if (x.empty()) return 0.0;
    double e = 0.0;
    for (float v : x) e += (double) v * (double) v;
    return std::sqrt(e / (double) x.size());
}

// ── Naive reference oscillators (the pre-slice-C definition) ────────────────
// Generated in-test so the "drops vs naive" comparison needs no second binary.
// `startSample` is the ABSOLUTE sample index the reference begins at, so its
// phase lines up with the engine's render at the same index (the engine's
// oscillators start at phase 0 when the note begins).

double frac(double v) { return v - std::floor(v); }

std::vector<float> naiveSaw(int startSample, int count, double f0,
                            int voices, double detuneCents)
{
    const double detuneRatio = std::pow(2.0, detuneCents / 1200.0);
    std::vector<float> out((size_t) count, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        const double n = (double) (startSample + i);
        double v = 0.0;
        for (int u = 0; u < voices; ++u)
        {
            const double voiceRatio = (u == 0) ? (1.0 / detuneRatio) : detuneRatio;
            v += 2.0 * frac(n * f0 * voiceRatio / kSR) - 1.0;
        }
        out[(size_t) i] = (float) (v / (double) voices);
    }
    return out;
}

std::vector<float> naiveSquare(int startSample, int count, double f0,
                               int voices, double detuneCents)
{
    const double detuneRatio = std::pow(2.0, detuneCents / 1200.0);
    std::vector<float> out((size_t) count, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        const double n = (double) (startSample + i);
        double v = 0.0;
        for (int u = 0; u < voices; ++u)
        {
            const double voiceRatio = (u == 0) ? (1.0 / detuneRatio) : detuneRatio;
            v += frac(n * f0 * voiceRatio / kSR) < 0.5 ? 1.0 : -1.0;
        }
        out[(size_t) i] = (float) (v / (double) voices);
    }
    return out;
}

std::vector<float> naivePulse(int startSample, int count, double f0, double duty)
{
    std::vector<float> out((size_t) count, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        const double p = frac((double) (startSample + i) * f0 / kSR);
        out[(size_t) i] = (float) (p < duty ? 1.0 : -1.0);
    }
    return out;
}

// The 7 detuned saws of psyarp's SuperSaw (phase offsets, one shared dt).
std::vector<float> naiveSuperSaw(int startSample, int count, double f0)
{
    static constexpr double off[7] = { -0.12, -0.06, -0.02, 0.0, 0.02, 0.06, 0.12 };
    std::vector<float> out((size_t) count, 0.0f);
    for (int i = 0; i < count; ++i)
    {
        const double p = frac((double) (startSample + i) * f0 / kSR);
        double v = 0.0;
        for (int j = 0; j < 7; ++j)
            v += 2.0 * frac(p + off[j]) - 1.0;
        out[(size_t) i] = (float) (v / 7.0);
    }
    return out;
}

// ── Spectral summary (Hann + real FFT) ──────────────────────────────────────

struct Spec
{
    double total = 0.0;       // Σ|X|² over bins [1, nyquist)
    double aliasBand = 0.0;   // Σ|X|² over [100, 4400) Hz (spurious for a
                              // 4978 Hz saw: all legit partials sit above it)
    double centroid = 0.0;    // Σ f·|X|² / Σ|X|²  (Hz)
};

Spec spectrum(const std::vector<float>& x, size_t start)
{
    static const juce::dsp::FFT fft(kFftOrd);
    std::vector<float> d((size_t) (2 * kFftLen), 0.0f);
    for (int i = 0; i < kFftLen; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                               * (double) i / (double) (kFftLen - 1));
        d[(size_t) i] = (float) ((double) x[start + (size_t) i] * w);
    }
    fft.performFrequencyOnlyForwardTransform(d.data(), true);

    const double binHz = kSR / (double) kFftLen;
    Spec s;
    for (int k = 1; k < kFftLen / 2; ++k)
    {
        const double mag2 = (double) d[(size_t) k] * (double) d[(size_t) k];
        const double f = (double) k * binHz;
        s.total += mag2;
        s.centroid += f * mag2;
        if (f >= 100.0 && f < 4400.0)
            s.aliasBand += mag2;
    }
    if (s.total > 0.0)
        s.centroid /= s.total;
    return s;
}

// ── Engine render harnesses ─────────────────────────────────────────────────

// psyarp, FX off, one unison voice, open filter, one held note. The arp gate
// is 0.1 s on / 0.025 s off (80% of a 1/16 at the engine's default 120 BPM),
// so the analysis window [kAnalysisStart, +4096) sits INSIDE one gate-on
// period (the 2nd, 5513..9923 at 44.1 kHz): no gate sidebands enter the FFT.
constexpr size_t kAnalysisStart = 5700;

std::vector<float> renderPsyArp(int note, int oscShape, int blocks)
{
    PsyArpEngine engine;
    engine.setOscShape(oscShape);
    engine.setOscUnisonVoices(1);
    engine.setOscDetuneCents(0.0f);
    engine.setPatternShape(0);          // UpDown; 1 note + 1 octave -> 1 step
    engine.setOctaveRange(1);
    engine.setFilterCutoffHz(20000.0f);
    engine.setFilterResonance(4.0f);    // k = 1.5, well damped
    engine.setFilterSweepBars(16.0f);   // slow: ~static inside the window
    engine.setFilterMode(0);
    engine.setDelayWetLevel(0.0f);
    engine.setReverbWetOnDry(0.0f);
    engine.setReverbWetOnDelay(0.0f);
    engine.setPhaserEnabled(false);
    engine.setOutputLevel(1.0f);
    engine.prepare(kSR, kBlock);

    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;

    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
        engine.render(buffer, midi);
        const auto* p = buffer.getReadPointer(0);
        out.insert(out.end(), p, p + kBlock);
    }
    return out;
}

// sub_synth, single continuous note (no gate), open static filter.
std::vector<float> renderSubSynth(int note, int wave, int samples)
{
    SubtractiveSynthEngine engine;
    engine.prepare(kSR, kBlock);
    engine.setOsc1Wave(wave);
    engine.setOsc1Level(1.0f);
    engine.setOsc2Level(0.0f);
    engine.setSubLevel(0.0f);
    engine.setCutoffHz(20000.0f);
    engine.setResonance(0.1f);
    engine.setFilterType(0);
    engine.setFilterEnvAmount(0.0f);      // static cutoff
    engine.setAttackSeconds(0.005f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.1f);
    engine.setOutputLevel(1.0f);

    juce::AudioBuffer<float> buffer(1, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 127), 0);

    for (int start = 0; start < samples; start += kBlock)
    {
        buffer.clear();
        engine.render(buffer, midi);
        midi.clear();
        const auto* p = buffer.getReadPointer(0);
        out.insert(out.end(), p, p + kBlock);
    }
    return out;
}

double peakOf(const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x) p = std::max(p, (double) std::fabs(v));
    return p;
}

bool allFinite(const std::vector<float>& x)
{
    for (float v : x) if (! std::isfinite(v)) return false;
    return true;
}

// The deterministic high-saw alias case: f0 = MIDI 111 = 4978.0 Hz at 44.1 kHz.
// The 9th harmonic (44802 Hz) folds to |44802 - 44100| = 702 Hz — a LOW alias
// far from every legit partial (the lowest is 4978 Hz), so the 702 Hz probe is
// pure spurious content. A naive saw puts 1/9 of the fundamental there
// (~-19 dB); PolyBLEP must crush it.
constexpr int    kHighNote     = 111;
constexpr double kHighAliasHz  = 9.0 * 4978.03 - 44100.0;   // ~702.3 Hz

} // namespace

// ============================================================================
// 1. Alias suppression vs the naive reference — psyarp
// ============================================================================

TEST(OscAntiAlias, PsyArpHighSawFoldedPartialDropsVsNaive)
{
    const auto out = renderPsyArp(kHighNote, 0 /*Saw*/, 40);
    ASSERT_TRUE(allFinite(out));
    ASSERT_GT(peakOf(out), 0.05) << "psyarp high saw must render non-silent";

    const double f0 = noteToHz(kHighNote);
    const double fund = dftMag(out, kAnalysisStart, kFftLen, f0);
    const double alias = dftMag(out, kAnalysisStart, kFftLen, kHighAliasHz);
    ASSERT_GT(fund, 1e-6);
    const double ratioAA = alias / fund;

    // Naive reference at the same pitch/rate: the 9th harmonic folds to the
    // same 702 Hz bin (the engine's near-flat 21.6 kHz LP is ~unity at both
    // 702 Hz and 4978 Hz, so the ratio is a fair comparison).
    const auto naive = naiveSaw(0, kFftLen, f0, 1, 0.0);
    const double nFund = dftMag(naive, 0, kFftLen, f0);
    const double nAlias = dftMag(naive, 0, kFftLen, kHighAliasHz);
    ASSERT_GT(nFund, 1e-6);
    const double ratioNaive = nAlias / nFund;

    // Diagnostic line (always printed — the measured numbers are the evidence).
    std::printf("AA-AB psyarp saw hi: alias/fund aa=%.6f (%.1f dB) naive=%.6f (%.1f dB)\n",
                ratioAA, 20.0 * std::log10(ratioAA + 1e-12),
                ratioNaive, 20.0 * std::log10(ratioNaive + 1e-12));

    // The naive reference must genuinely exhibit the alias (else the probe is
    // broken and the gate below would pass vacuously).
    EXPECT_GT(ratioNaive, 0.03) << "naive saw should put a strong partial at 702 Hz";

    // PolyBLEP at least ~3x below the naive alias (a 10 dB+ drop), with a
    // small absolute margin so the gate is not brittle against filter
    // coloration differences between the two renders.
    EXPECT_LT(ratioAA, ratioNaive * 0.35 + 0.02)
        << "psyarp saw alias not suppressed vs naive";

    // Absolute sanity: the folded partial sits well under the fundamental
    // (naive is ~-19 dB).
    EXPECT_LT(ratioAA, 0.06)
        << "psyarp saw 702 Hz alias should be >= 24 dB below the fundamental";
}

// psyarp's other step-bearing shapes (Square, Pulse, SuperSaw) are covered by
// the same measurement against their OWN naive references. Each compares the
// engine's 702 Hz folded-harmonic energy to an in-test naive render of the
// same shape at the same pitch: AA must drop it well below naive.
TEST(OscAntiAlias, PsyArpOtherStepShapesSuppressAlias)
{
    const double f0 = noteToHz(kHighNote);

    struct ShapeCase { int shape; const char* name; std::vector<float> naive; };
    std::vector<ShapeCase> cases = {
        { 1 /*Square*/, "square", naiveSquare(0, kFftLen, f0, 1, 0.0) },
        { 2 /*SuperSaw*/, "supersaw", naiveSuperSaw(0, kFftLen, f0) },
        { 3 /*Pulse*/,   "pulse",  naivePulse(0, kFftLen, f0, 0.5) },
    };

    for (auto& c : cases)
    {
        const auto out = renderPsyArp(kHighNote, c.shape, 40);
        ASSERT_TRUE(allFinite(out));
        ASSERT_GT(peakOf(out), 0.02) << c.name << " must render non-silent";

        const double fund = dftMag(out, kAnalysisStart, kFftLen, f0);
        const double alias = dftMag(out, kAnalysisStart, kFftLen, kHighAliasHz);
        ASSERT_GT(fund, 1e-6);
        const double ratioAA = alias / fund;

        const double nFund = dftMag(c.naive, 0, kFftLen, f0);
        const double nAlias = dftMag(c.naive, 0, kFftLen, kHighAliasHz);
        ASSERT_GT(nFund, 1e-6);
        const double ratioNaive = nAlias / nFund;

        std::printf("AA-AB psyarp %s hi: alias/fund aa=%.6f (%.1f dB) naive=%.6f (%.1f dB)\n",
                    c.name, ratioAA, 20.0 * std::log10(ratioAA + 1e-12),
                    ratioNaive, 20.0 * std::log10(ratioNaive + 1e-12));

        EXPECT_GT(ratioNaive, 0.01) << c.name << " naive must exhibit the alias";
        EXPECT_LT(ratioAA, ratioNaive * 0.5 + 0.02)
            << "psyarp " << c.name << " alias not suppressed vs naive";
        EXPECT_LT(ratioAA, 0.06)
            << "psyarp " << c.name << " 702 Hz alias should be well below the fundamental";
    }
}

// ============================================================================
// 2. Alias suppression vs the naive reference — sub_synth
// ============================================================================

TEST(OscAntiAlias, SubSynthHighSawFoldedPartialDropsVsNaive)
{
    constexpr int samples = 32768;
    const auto out = renderSubSynth(kHighNote, 1 /*Saw*/, samples);
    ASSERT_TRUE(allFinite(out));
    ASSERT_GT(peakOf(out), 0.02) << "sub_synth high saw must render non-silent";

    const double f0 = noteToHz(kHighNote);
    const size_t start = (size_t) (samples - kFftLen);
    const double fund = dftMag(out, start, kFftLen, f0);
    const double alias = dftMag(out, start, kFftLen, kHighAliasHz);
    ASSERT_GT(fund, 1e-6);
    const double ratioAA = alias / fund;

    // sub_synth sums TWO unison voices 7 cents apart (kUnisonDetuneCents), so
    // the naive reference uses the same 2-voice detune.
    const auto naive = naiveSaw(0, kFftLen, f0, 2, 7.0);
    const double nFund = dftMag(naive, 0, kFftLen, f0);
    const double nAlias = dftMag(naive, 0, kFftLen, kHighAliasHz);
    ASSERT_GT(nFund, 1e-6);
    const double ratioNaive = nAlias / nFund;

    std::printf("AA-AB sub_synth saw hi: alias/fund aa=%.6f (%.1f dB) naive=%.6f (%.1f dB)\n",
                ratioAA, 20.0 * std::log10(ratioAA + 1e-12),
                ratioNaive, 20.0 * std::log10(ratioNaive + 1e-12));

    EXPECT_GT(ratioNaive, 0.02) << "naive saw should put a strong partial at 702 Hz";
    EXPECT_LT(ratioAA, ratioNaive * 0.35 + 0.02)
        << "sub_synth saw alias not suppressed vs naive";
    EXPECT_LT(ratioAA, 0.06)
        << "sub_synth saw 702 Hz alias should be >= 24 dB below the fundamental";
}

// sub_synth's other AA'd shape (Square, param 2) and its internal sub (Square)
// share the same code path; the Square case is checked here at the same pitch.
TEST(OscAntiAlias, SubSynthHighSquareFoldedPartialDropsVsNaive)
{
    constexpr int samples = 32768;
    const auto out = renderSubSynth(kHighNote, 2 /*Square*/, samples);
    ASSERT_TRUE(allFinite(out));
    ASSERT_GT(peakOf(out), 0.02);

    const double f0 = noteToHz(kHighNote);
    const size_t start = (size_t) (samples - kFftLen);
    const double fund = dftMag(out, start, kFftLen, f0);
    const double alias = dftMag(out, start, kFftLen, kHighAliasHz);
    ASSERT_GT(fund, 1e-6);
    const double ratioAA = alias / fund;

    const auto naive = naiveSquare(0, kFftLen, f0, 2, 7.0);
    const double nFund = dftMag(naive, 0, kFftLen, f0);
    const double nAlias = dftMag(naive, 0, kFftLen, kHighAliasHz);
    ASSERT_GT(nFund, 1e-6);
    const double ratioNaive = nAlias / nFund;

    std::printf("AA-AB sub_synth sq hi: alias/fund aa=%.6f (%.1f dB) naive=%.6f (%.1f dB)\n",
                ratioAA, 20.0 * std::log10(ratioAA + 1e-12),
                ratioNaive, 20.0 * std::log10(ratioNaive + 1e-12));

    EXPECT_GT(ratioNaive, 0.005) << "naive square must exhibit the alias";
    EXPECT_LT(ratioAA, ratioNaive * 0.5 + 0.02)
        << "sub_synth square alias not suppressed vs naive";
}

// ============================================================================
// 3. In-band integrity: a LOW note keeps its harmonic structure
// ============================================================================

namespace
{
// Band-summed harmonic ratios (k = 1..count): sqrt(E_k / E_1) with each E_k a
// +/-40 Hz band sum about the harmonic. Compared engine-vs-naive at the same
// absolute window, band sums are stable against the detuned-voice window
// phase that makes a single bin swing ~2x.
std::vector<double> harmonicRatios(const std::vector<float>& x, size_t start,
                                   double f0, int count, size_t absStart)
{
    std::vector<double> r((size_t) count);
    const double fund = bandEnergy(x, start, kFftLen, f0, 40.0, absStart);
    for (int k = 1; k <= count; ++k)
    {
        const double e = bandEnergy(x, start, kFftLen, f0 * (double) k, 40.0, absStart);
        r[(size_t) (k - 1)] = std::sqrt(e / std::max(1e-20, fund));
    }
    return r;
}
} // namespace

TEST(OscAntiAlias, SubSynthLowNoteHarmonicStructurePreserved)
{
    // A low note (MIDI 36, 65.4 Hz) has dt ~ 0.0015, so PolyBLEP's residual is
    // nil away from the wrap and tiny at it: the harmonic structure must match
    // the naive reference within a small tolerance (AA must not alter the
    // tone). Saw AND Square (the two AA'd shapes).
    constexpr int note = 36;
    constexpr int samples = 32768;
    const double f0 = noteToHz(note);
    const size_t start = (size_t) (samples - kFftLen);

    for (int wave : { 1 /*Saw*/, 2 /*Square*/ })
    {
        const auto out = renderSubSynth(note, wave, samples);
        ASSERT_TRUE(allFinite(out));
        ASSERT_GT(peakOf(out), 0.02);

        // Reference phase-aligned to the engine's absolute window.
        const auto naive = (wave == 1) ? naiveSaw((int) start, kFftLen, f0, 2, 7.0)
                                       : naiveSquare((int) start, kFftLen, f0, 2, 7.0);

        const auto eng = harmonicRatios(out, start, f0, 5, start);
        const auto ref = harmonicRatios(naive, 0, f0, 5, 0);

        for (int k = 0; k < 5; ++k)
        {
            const double denom = std::max(1e-6, std::fabs(ref[(size_t) k]));
            const double rel = std::fabs(eng[(size_t) k] - ref[(size_t) k]) / denom;
            EXPECT_LT(rel, 0.15)
                << "wave " << wave << " harmonic " << (k + 1)
                << ": engine=" << eng[(size_t) k] << " naive=" << ref[(size_t) k];
        }

        // The overall level must be preserved (AA does not add or remove
        // amplitude at a pitch where the residual is negligible).
        const auto naiveFull = (wave == 1) ? naiveSaw(0, samples, f0, 2, 7.0)
                                           : naiveSquare(0, samples, f0, 2, 7.0);
        EXPECT_NEAR(rmsOf(out), rmsOf(naiveFull), 0.05 * rmsOf(naiveFull))
            << "wave " << wave << " RMS moved at a low note";

        std::printf("AA-AB sub_synth low(%s) h2=%.4f/h3=%.4f (naive %.4f/%.4f) rms=%.4f/%.4f\n",
                    wave == 1 ? "saw" : "square",
                    eng[1], eng[2], ref[1], ref[2],
                    rmsOf(out), rmsOf(naiveFull));
    }
}

TEST(OscAntiAlias, PsyArpLowNoteHarmonicStructurePreserved)
{
    // psyarp's arp gate is 0.1 s on / 0.025 s off, so the 4096-sample window
    // sits inside ONE gate-on period (no gate sidebands). At 130.8 Hz the
    // window holds ~12 cycles — enough to resolve the saw harmonics.
    constexpr int note = 48;
    const auto out = renderPsyArp(note, 0 /*Saw*/, 40);
    ASSERT_TRUE(allFinite(out));
    ASSERT_GT(peakOf(out), 0.05);

    const double f0 = noteToHz(note);
    // The engine's oscillator phase resets at the ARP GATE's note-on (the held
    // note is re-triggered each step), and the gate is free-running from
    // sample 0 — so the reference phase is (sample index) * f0 / SR is not the
    // engine's phase. Compare SHAPE, not phase: the harmonic ratios normalized
    // to the fundamental are phase-invariant for a single-voice saw.
    const auto eng = harmonicRatios(out, kAnalysisStart, f0, 5, 0);
    const auto naive = naiveSaw(0, kFftLen, f0, 1, 0.0);
    const auto ref = harmonicRatios(naive, 0, f0, 5, 0);

    for (int k = 0; k < 5; ++k)
    {
        const double rel = std::fabs(eng[(size_t) k] - ref[(size_t) k])
                           / std::max(1e-6, std::fabs(ref[(size_t) k]));
        EXPECT_LT(rel, 0.20)
            << "psyarp harmonic " << (k + 1)
            << ": engine=" << eng[(size_t) k] << " naive=" << ref[(size_t) k];
    }

    std::printf("AA-AB psyarp low h2=%.4f/h3=%.4f (naive %.4f/%.4f)\n",
                eng[1], eng[2], ref[1], ref[2]);

    // AA must add no spurious high-band content at a low note: a 130 Hz saw's
    // legit partials above 8 kHz are tiny, so the 8 kHz probe stays far below
    // the fundamental.
    const double fund = bandEnergy(out, kAnalysisStart, kFftLen, f0, 40.0);
    const double hi = bandEnergy(out, kAnalysisStart, kFftLen, 8000.0, 40.0);
    EXPECT_LT(std::sqrt(hi), 0.5 * std::sqrt(fund))
        << "psyarp low note must not gain hi-band junk";
}

// ============================================================================
// 4. Spectral A/B measurement (no assertions) — run under BOTH builds
// ============================================================================
//
// Renders psyarp and sub_synth at a HIGH saw (the alias case) and a LOW saw
// (in-band integrity) and prints alias-band energy (fraction of total power in
// [100, 4400) Hz, spurious for the high note) and spectral centroid. Compile
// against the naive engine and against the AA engine to read both sides; the
// numbers are the slice-C spectral A/B evidence.

TEST(OscAntiAlias, AaSpectralAbMeasurement)
{
    struct Row { const char* engine; const char* pitch; std::vector<float> buf; size_t start; };
    std::vector<Row> rows = {
        { "psyarp",    "hi-4978Hz", renderPsyArp(kHighNote, 0, 40), kAnalysisStart },
        { "psyarp",    "lo-130Hz",  renderPsyArp(48, 0, 40), kAnalysisStart },
        { "sub_synth", "hi-4978Hz", renderSubSynth(kHighNote, 1, 32768), (size_t) (32768 - kFftLen) },
        { "sub_synth", "lo-65Hz",   renderSubSynth(36, 1, 32768), (size_t) (32768 - kFftLen) },
    };

    for (const auto& r : rows)
    {
        const Spec s = spectrum(r.buf, r.start);
        ASSERT_GT(s.total, 0.0);
        const double aliasFrac = s.aliasBand / s.total;
        std::printf("AA-AB %s %s: aliasBandFrac=%.6f (%.1f dB) centroid=%.1f Hz peak=%.4f rms=%.4f\n",
                    r.engine, r.pitch, aliasFrac, 10.0 * std::log10(aliasFrac + 1e-12),
                    s.centroid, peakOf(r.buf), rmsOf(r.buf));
    }
}
