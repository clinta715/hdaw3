#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace HDAW {

// ─────────────────────────────────────────────────────────────────────────────
// MixReport — offline mix analysis of a rendered audio file (WAV render
// verification). Pure analysis: reads the file via juce::AudioFormatManager on
// the CALLING thread, touches no audio engine state, never runs on the audio
// thread. Band energies are computed with a windowed real FFT
// (juce::dsp::FFT, order 12 → 4096-point frames, Hann window, 50% overlap).
//
// Metrics & units (the documented contract, mirrored by the MCP `mix_report`
// tool):
//   * sections/windows are in SECONDS, half-open [start, end); sample ranges
//     are [round(start*sr), round(end*sr)).
//   * rms      — linear amplitude RMS, sqrt(mean(x^2)) over the span.
//   * peak     — max |sample| over the span.
//   * bandEnergy[4] — mean power per FFT window in each band, plain power sum
//     (linear amplitude^2, NOT dB): sum over bins of |mag|^2 for bins whose
//     centre frequency falls in the band, averaged over the FFT frames fully
//     inside the analyzed span. Band cutoffs (Hz):
//         sub:  [40, 110)   bass: [90, 300)
//         body: [300, 2000) high:  [6000, nyquist)
//     (sub/bass intentionally overlap 90-110 Hz — they are spectral-region
//     energies, not filters).
//   * pumpDepth — per-beat loudness pump: within each section, RMS of every
//     beat (beatLen = 60/bpm seconds; beats are consecutive windows starting
//     at the section start, last one possibly partial), then
//     (max - min) / mean of those beat RMS values. Depth is averaged over
//     sections with >= kMinPumpBeats beats. hasPumpDepth is false (tool omits
//     pumpDepth) when bpm <= 0 or no section qualifies. No pump → depth ~ 0.
//   * kickProminence — whole-file kick-vs-low-mid energy ratio,
//     E(35-110) / (E(35-110) + E(120-320)) in [0,1]; 0 when the denominator
//     is zero (no content in either region).
// ─────────────────────────────────────────────────────────────────────────────

enum MixBandIndex {
    kMixBandSub = 0,
    kMixBandBass,
    kMixBandBody,
    kMixBandHigh,
    kMixNumBands
};

struct SectionWindow {
    juce::String name;   // free-form label
    double start = 0.0;  // seconds, inclusive
    double end = 0.0;    // seconds, EXCLUSIVE — window is [start, end)
};

struct SectionReport {
    juce::String name;
    double start = 0.0, end = 0.0;   // seconds, [start, end)
    double rms = 0.0;                // linear amplitude RMS
    double peak = 0.0;               // max |sample|
    double bandEnergy[kMixNumBands] = {};  // see MixReportAnalyzer docs
};

struct MixReport {
    double duration = 0.0;    // seconds
    double sampleRate = 0.0;  // Hz
    double peak = 0.0;        // whole file
    double rms = 0.0;         // whole file
    double bands[kMixNumBands] = {};  // whole-file band energies
    bool hasPumpDepth = false;
    double pumpDepth = 0.0;
    double kickProminence = 0.0;
    std::vector<SectionReport> sections;
};

class MixReportAnalyzer {
public:
    // Analyze `wav`. `windows` are analyst-supplied sections (seconds);
    // when empty, a single "whole" window [0, duration) is used. `bpm` enables
    // pump-depth (<= 0 disables it). Returns false with a human-readable `err`
    // on missing/invalid file, degenerate or out-of-file sections.
    static bool analyze(const juce::File& wav,
                        const std::vector<SectionWindow>& windows,
                        double bpm,
                        MixReport& out,
                        juce::String& err);
private:
    static constexpr int kFftOrder = 12;        // 4096-point FFT
    static constexpr int kMinPumpBeats = 8;

    struct SampleStats { double sum = 0.0, sumSq = 0.0, peak = 0.0; long count = 0; };
    struct Spectral {
        double band[kMixNumBands] = {};
        double kick = 0.0;      // 35-110 Hz
        double kickMid = 0.0;   // 120-320 Hz
        long windows = 0;
    };
    struct RangeResult {
        SampleStats stats;
        Spectral spectral;
        std::vector<double> beatRms;
    };

    static void addBandPower(double freq, double sampleRate, double power, Spectral& sp);
    static void streamRange(juce::AudioFormatReader& reader, int64_t s0, int64_t s1,
                            int numChannels, double sampleRate,
                            double beatLenSamples, RangeResult& out);
};


// ── implementation (header-local) ─────────────────────────────


// ── implementation (header-local) ───────────────────────────────────────────

inline void MixReportAnalyzer::addBandPower(double freq, double sampleRate, double power, Spectral& sp)
{
    if (freq >= 40.0 && freq < 110.0)  sp.band[kMixBandSub]  += power;
    if (freq >= 90.0 && freq < 300.0)  sp.band[kMixBandBass] += power;
    if (freq >= 300.0 && freq < 2000.0) sp.band[kMixBandBody] += power;
    if (freq >= 6000.0 && freq < sampleRate * 0.5) sp.band[kMixBandHigh] += power;
    if (freq >= 35.0 && freq < 110.0)  sp.kick    += power;
    if (freq >= 120.0 && freq < 320.0) sp.kickMid += power;
}

inline void MixReportAnalyzer::streamRange(juce::AudioFormatReader& reader,
                                           int64_t s0, int64_t s1,
                                           int numChannels, double sampleRate,
                                           double beatLenSamples, RangeResult& out)
{
    const int n = 1 << kFftOrder;      // 4096 samples per FFT frame
    const int hop = n / 2;             // 50% overlap
    const int ringSize = 2 * n;

    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> winTable((size_t)n,
        juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<std::complex<float>> specIn(n), specOut(n);
    std::vector<float> ring(ringSize, 0.0f);
    std::vector<float> frame(n);
    juce::AudioBuffer<float> buf(numChannels, n);

    int64_t nextWinStart = s0;
    int64_t beatIdx = 0;
    bool haveBeat = false;
    double beatSumSq = 0.0;
    long beatCount = 0;
    auto flushBeat = [&]() {
        if (!haveBeat)
            return;
        if (beatCount > 0)
            out.beatRms.push_back(std::sqrt(beatSumSq / (double)beatCount));
        haveBeat = false;
        beatSumSq = 0.0;
        beatCount = 0;
    };

    int64_t pos = s0;
    while (pos < s1)
    {
        const int want = static_cast<int>(std::min<int64_t>(n, s1 - pos));
        if (!reader.read(&buf, 0, want, pos, true, true))
            break;

        for (int i = 0; i < want; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += buf.getSample(c, i);
            mono /= static_cast<float>(numChannels);

            ring[(pos + i) % ringSize] = mono;

            const double d = static_cast<double>(mono);
            out.stats.sum += d;
            out.stats.sumSq += d * d;
            ++out.stats.count;
            if (std::fabs(d) > out.stats.peak)
                out.stats.peak = std::fabs(d);

            if (beatLenSamples > 0.0)
            {
                const int64_t bi = static_cast<int64_t>(
                    (static_cast<double>(pos + i - s0)) / beatLenSamples);
                if (haveBeat && bi != beatIdx)
                    flushBeat();
                beatIdx = bi;
                haveBeat = true;
                beatSumSq += d * d;
                ++beatCount;
            }
        }

        pos += want;
        const int64_t appended = pos;

        // Emit FFT frames that are now fully inside the appended range.
        while (nextWinStart + n <= appended)
        {
            for (int i = 0; i < n; ++i)
                frame[i] = ring[(nextWinStart + i) % ringSize];
            winTable.multiplyWithWindowingTable(frame.data(), (size_t)n);
            for (int i = 0; i < n; ++i)
                specIn[i] = std::complex<float>(frame[i], 0.0f);
            fft.perform(specIn.data(), specOut.data(), false);
            const double binHz = sampleRate / static_cast<double>(n);
            for (int k = 0; k <= n / 2; ++k)
            {
                const double freq = static_cast<double>(k) * binHz;
                const double power = static_cast<double>(std::norm(specOut[k]));
                addBandPower(freq, sampleRate, power, out.spectral);
            }
            ++out.spectral.windows;
            nextWinStart += hop;
        }
    }
    flushBeat();
}

inline bool MixReportAnalyzer::analyze(const juce::File& wav,
                                       const std::vector<SectionWindow>& windows,
                                       double bpm, MixReport& out, juce::String& err)
{
    out = MixReport();

    if (!wav.existsAsFile()) { err = "file not found"; return false; }

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(wav));
    if (!reader) { err = "cannot open audio file"; return false; }

    const double sampleRate = reader->sampleRate;
    const int64_t totalSamples = reader->lengthInSamples;
    if (sampleRate <= 0.0 || totalSamples <= 0) { err = "no audio data in file"; return false; }
    const int numChannels = static_cast<int>(reader->numChannels);
    if (numChannels < 1) { err = "no audio channels"; return false; }

    const double duration = static_cast<double>(totalSamples) / sampleRate;
    out.duration = duration;
    out.sampleRate = sampleRate;

    // Empty window list = one whole-file section (same default the MCP tool uses).
    std::vector<SectionWindow> winList = windows;
    if (winList.empty())
        winList.push_back(SectionWindow{ "whole", 0.0, duration });

    for (const auto& w : winList)
    {
        if (!(w.end > w.start)) { err = "section '" + w.name + "' has end <= start"; return false; }
        const int64_t s0 = static_cast<int64_t>(std::llround(w.start * sampleRate));
        const int64_t s1 = static_cast<int64_t>(std::llround(w.end * sampleRate));
        if (s1 <= s0 || s0 >= totalSamples || s1 > totalSamples)
        {
            err = "section '" + w.name + "' [" + juce::String(w.start) + ", " + juce::String(w.end)
                  + ") is outside file duration " + juce::String(duration) + " s";
            return false;
        }
    }

    // Whole-file pass (peak, rms, bands, kick prominence). No per-beat RMS here.
    RangeResult whole;
    streamRange(*reader, 0, totalSamples, numChannels, sampleRate, 0.0, whole);
    if (whole.stats.count > 0)
    {
        out.peak = whole.stats.peak;
        out.rms = std::sqrt(whole.stats.sumSq / static_cast<double>(whole.stats.count));
    }
    for (int b = 0; b < kMixNumBands; ++b)
        out.bands[b] = whole.spectral.windows > 0
            ? whole.spectral.band[b] / static_cast<double>(whole.spectral.windows) : 0.0;
    const double kickDenom = whole.spectral.kick + whole.spectral.kickMid;
    out.kickProminence = kickDenom > 0.0 ? whole.spectral.kick / kickDenom : 0.0;

    // Section pass: rms/peak/bands per section + per-beat RMS for pump depth.
    const double beatLenSamples = bpm > 0.0 ? sampleRate * 60.0 / bpm : 0.0;
    double pumpSum = 0.0;
    int pumpCount = 0;
    for (const auto& w : winList)
    {
        const int64_t s0 = static_cast<int64_t>(std::llround(w.start * sampleRate));
        const int64_t s1 = static_cast<int64_t>(std::llround(w.end * sampleRate));

        SectionReport sr;
        sr.name = w.name;
        sr.start = static_cast<double>(s0) / sampleRate;
        sr.end = static_cast<double>(s1) / sampleRate;

        RangeResult rr;
        streamRange(*reader, s0, s1, numChannels, sampleRate, beatLenSamples, rr);
        if (rr.stats.count > 0)
        {
            sr.rms = std::sqrt(rr.stats.sumSq / static_cast<double>(rr.stats.count));
            sr.peak = rr.stats.peak;
        }
        for (int b = 0; b < kMixNumBands; ++b)
            sr.bandEnergy[b] = rr.spectral.windows > 0
                ? rr.spectral.band[b] / static_cast<double>(rr.spectral.windows) : 0.0;

        // Pump depth from this section's per-beat RMS arc.
        if (beatLenSamples > 0.0 && static_cast<int>(rr.beatRms.size()) >= kMinPumpBeats)
        {
            double mn = rr.beatRms[0], mx = rr.beatRms[0], mean = 0.0;
            for (double v : rr.beatRms) { mn = std::min(mn, v); mx = std::max(mx, v); mean += v; }
            mean /= static_cast<double>(rr.beatRms.size());
            if (mean > 1e-12)
            {
                pumpSum += (mx - mn) / mean;
                ++pumpCount;
            }
        }
        out.sections.push_back(std::move(sr));
    }

    if (beatLenSamples > 0.0 && pumpCount > 0)
    {
        out.hasPumpDepth = true;
        out.pumpDepth = pumpSum / static_cast<double>(pumpCount);
    }
    return true;
}

} // namespace HDAW
