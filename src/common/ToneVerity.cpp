// ToneVerity implementation — see ToneVerity.h for the contract.
#include "ToneVerity.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <deque>
#include <numeric>

namespace HDAW {

namespace {

constexpr int kFftOrder = 12;              // 4096-point frames (MixReport parity)
constexpr int kFftSize = 1 << kFftOrder;

struct MonoAudio
{
    std::vector<float> samples;   // mono-summed
    double sampleRate = 0.0;
    double samplePeak = 0.0;
};

MonoAudio readMono(const juce::File& wav, std::string& err)
{
    MonoAudio out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(wav));
    if (reader == nullptr)
    {
        err = "cannot read audio file";
        return out;
    }
    out.sampleRate = reader->sampleRate;
    const auto numSamples = static_cast<int64_t>(reader->lengthInSamples);
    if (numSamples <= 0 || reader->numChannels <= 0)
    {
        err = "empty audio";
        return out;
    }

    const int chunk = kFftSize;
    juce::AudioBuffer<float> buf((int) reader->numChannels, chunk);
    out.samples.reserve(static_cast<size_t>(numSamples));
    int64_t pos = 0;
    while (pos < numSamples)
    {
        const auto toRead = std::min<int64_t>(chunk, numSamples - pos);
        reader->read(&buf, 0, (int) toRead, pos, true, true);
        for (int i = 0; i < (int) toRead; ++i)
        {
            float v = 0.0f;
            for (int c = 0; c < (int) reader->numChannels; ++c)
                v += buf.getSample(c, i);
            v /= static_cast<float>(reader->numChannels);
            out.samplePeak = std::max(out.samplePeak, (double) std::abs(v));
            out.samples.push_back(v);
        }
        pos += toRead;
    }
    return out;
}

double frameSpectralCentroid(const float* data, int n, double sampleRate,
                             juce::dsp::FFT& fft,
                             std::vector<float>& windowed,
                             std::vector<float>& spec)
{
    windowed.assign(kFftSize, 0.0f);
    for (int i = 0; i < n; ++i) windowed[(size_t) i] = data[i];
    juce::dsp::WindowingFunction<float> win((size_t) kFftSize,
        juce::dsp::WindowingFunction<float>::hann, false);
    win.multiplyWithWindowingTable(windowed.data(), (size_t) kFftSize);
    // Real-only transform REQUIRES an array of 2*fftSize floats (JUCE doc):
    // first half raw input, output complex interleaved.
    spec.assign((size_t) kFftSize * 2, 0.0f);
    std::copy(windowed.begin(), windowed.end(), spec.begin());
    fft.performRealOnlyForwardTransform(spec.data(), true);
    const int bins = kFftSize / 2;
    double num = 0.0, den = 0.0;
    for (int b = 1; b < bins; ++b)
    {
        const float re = spec[(size_t) (2 * b)];
        const float im = spec[(size_t) (2 * b + 1)];
        const double mag = std::sqrt((double) re * re + (double) im * im);
        const double f = (double) b * sampleRate / (double) kFftSize;
        num += f * mag;
        den += mag;
    }
    return den > 1e-12 ? num / den : kToneNaN;
}

} // namespace

ToneVerityAnalysis analyzeToneWav(const juce::File& wav, double binSeconds)
{
    ToneVerityAnalysis out;
    if (!(binSeconds >= 0.002 && binSeconds <= 0.05))
    {
        out.error = "binSeconds must be in [0.002, 0.05]";
        return out;
    }
    const MonoAudio audio = readMono(wav, out.error);
    if (!out.error.empty()) return out;
    const int n = static_cast<int>(audio.samples.size());
    const double sr = audio.sampleRate;
    out.sampleRate = (int) std::lround(sr);
    out.duration = (double) n / sr;
    out.samplePeak = audio.samplePeak;
    out.binSeconds = binSeconds;

    // ── Envelope: binned RMS ────────────────────────────────────────────────
    const int binSamples = std::max(1, (int) std::lround(binSeconds * sr));
    std::vector<double> bins;
    bins.reserve((size_t)(n / binSamples + 1));
    double acc = 0.0;
    int accCount = 0;
    for (int i = 0; i < n; ++i)
    {
        const double v = audio.samples[(size_t) i];
        acc += v * v;
        ++accCount;
        if (accCount == binSamples)
        {
            bins.push_back(std::sqrt(acc / (double) accCount));
            acc = 0.0; accCount = 0;
        }
    }
    if (accCount > 0)
        bins.push_back(std::sqrt(acc / (double) accCount));
    if (bins.empty())
    {
        out.error = "no envelope bins";
        return out;
    }

    out.peakRms = *std::max_element(bins.begin(), bins.end());
    // Decimate the returned trace if it is very long (payload size guard).
    out.envelopeRms = bins;
    if (bins.size() > 4000)
    {
        const size_t factor = bins.size() / 4000 + 1;
        std::vector<double> dec;
        for (size_t i = 0; i < bins.size(); i += factor)
        {
            double m = 0.0; size_t cnt = 0;
            for (size_t j = i; j < std::min(bins.size(), i + factor); ++j)
            { m += bins[j]; ++cnt; }
            dec.push_back(m / (double) cnt);
        }
        out.envelopeRms = std::move(dec);
        out.envelopeDecimated = true;
    }

    // Attack: first 10% -> first >= 90% of peak.
    {
        const double t10 = 0.1 * out.peakRms, t90 = 0.9 * out.peakRms;
        int i10 = -1, i90 = -1;
        for (size_t i = 0; i < bins.size(); ++i)
        {
            if (i10 < 0 && bins[i] >= t10) i10 = (int) i;
            if (i10 >= 0 && bins[i] >= t90) { i90 = (int) i; break; }
        }
        if (i10 >= 0 && i90 >= 0)
            out.attackMs = (double) (i90 - i10) * binSeconds * 1000.0;
    }

    // Sustain: mean of the last quarter / peak.
    {
        const size_t start = bins.size() - std::max<size_t>(1, bins.size() / 4);
        double sum = 0.0; size_t cnt = 0;
        for (size_t i = start; i < bins.size(); ++i) { sum += bins[i]; ++cnt; }
        const double mean = sum / (double) cnt;
        out.sustainRatio = out.peakRms > 1e-12 ? mean / out.peakRms : kToneNaN;
    }

    // Trailing silence: time from the last bin above 5% of peak to the end.
    {
        for (int i = (int) bins.size() - 1; i >= 0; --i)
            if (bins[(size_t) i] > 0.05 * out.peakRms)
            {
                out.trailingSilenceSeconds =
                    (double) ((int) bins.size() - 1 - i) * binSeconds;
                break;
            }
    }

    // AM depth first: (max-min)/mean over the central 80% of bins (edges
    // excluded — a hard onset/end is not modulation). A flat tone has depth
    // ~0; the prominence metric below is only meaningful when depth is not.
    {
        const size_t lo80 = bins.size() / 10;
        const size_t hi80 = bins.size() - bins.size() / 10;
        if (hi80 > lo80 + 3)
        {
            double mn = 1e30, mx = 0.0, sum = 0.0; size_t cnt = 0;
            for (size_t i = lo80; i < hi80; ++i)
            { mn = std::min(mn, bins[i]); mx = std::max(mx, bins[i]); sum += bins[i]; ++cnt; }
            const double mean = sum / (double) cnt;
            out.amDepth = mean > 1e-9 ? (mx - mn) / mean : 0.0;
        }
    }

    // ── AM rate: FFT of the mean-removed envelope ───────────────────────────
    if (bins.size() >= 16)
    {
        const double binRate = 1.0 / binSeconds;
        const size_t N = bins.size();
        double mean = 0.0;
        for (const double b : bins) mean += b;
        mean /= (double) N;

        int fftOrder = 10;
        while ((1 << fftOrder) < (int) (4 * N)) ++fftOrder;
        const int F = 1 << fftOrder;
        // Real-only transform REQUIRES an array of 2*fftSize floats (JUCE doc).
        std::vector<float> env((size_t) F * 2, 0.0f);
        juce::dsp::WindowingFunction<float> win((size_t) N,
            juce::dsp::WindowingFunction<float>::hann, false);
        for (size_t i = 0; i < N; ++i)
            env[i] = (float) (bins[i] - mean);
        win.multiplyWithWindowingTable(env.data(), (size_t) N);
        juce::dsp::FFT fft(fftOrder);
        fft.performRealOnlyForwardTransform(env.data(), true);

        const double fRes = binRate / (double) F;
        const double fLo = 0.05, fHi = std::min(25.0, binRate * 0.45);
        const int bLo = std::max(1, (int) std::ceil(fLo / fRes));
        const int bHi = std::min(F / 2 - 1, (int) std::floor(fHi / fRes));
        std::vector<double> mags;
        for (int b = bLo; b <= bHi; ++b)
        {
            const double re = env[(size_t) (2 * b)];
            const double im = env[(size_t) (2 * b + 1)];
            mags.push_back(std::sqrt(re * re + im * im));
        }
        if (!mags.empty())
        {
            const size_t peakIdx =
                (size_t) (std::max_element(mags.begin(), mags.end()) - mags.begin());
            std::vector<double> sorted(mags);
            std::sort(sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            out.modRateHz = (double) (bLo + (int) peakIdx) * fRes;
            out.modProminence = (median > 1e-15 && out.amDepth > 0.05)
                ? mags[peakIdx] / median : 0.0;
            out.modCycles = out.modRateHz * out.duration;
        }
    }

    // ── Spectral centroid trajectory (per-frame, quarter aggregation) ───────
    {
        juce::dsp::FFT fft(kFftOrder);
        std::vector<float> windowed, spec;
        std::vector<double> cent; cent.reserve((size_t)(n / kFftSize + 1));
        for (int pos = 0; pos + kFftSize <= n; pos += kFftSize)
        {
            // Frame RMS guard: skip near-silent frames.
            double sq = 0.0;
            for (int i = 0; i < kFftSize; ++i)
            {
                const double v = audio.samples[(size_t)(pos + i)];
                sq += v * v;
            }
            if (std::sqrt(sq / (double) kFftSize) < 1e-4) continue;
            cent.push_back(frameSpectralCentroid(&audio.samples[(size_t) pos],
                                                 kFftSize, sr, fft, windowed, spec));
        }
        if (!cent.empty())
        {
            double sum = 0.0; int cnt = 0;
            for (const double c : cent) if (std::isfinite(c)) { sum += c; ++cnt; }
            if (cnt > 0) out.centroidMean = sum / (double) cnt;
            const size_t q = std::max<size_t>(1, cent.size() / 4);
            auto quarterMean = [&](size_t lo, size_t hi) {
                double s = 0.0; int c = 0;
                for (size_t i = lo; i < std::min(hi, cent.size()); ++i)
                    if (std::isfinite(cent[i])) { s += cent[i]; ++c; }
                return c > 0 ? s / (double) c : kToneNaN;
            };
            out.centroidStart = quarterMean(0, q);
            out.centroidEnd = quarterMean(cent.size() - q, cent.size());
        }
    }

    // ── f0: lowest strong spectral peak at the loudest region ───────────────
    // NOTE: HPS ties on harmonic-free tones (every 220/k subharmonic scores
    // identically), so we use the leakage-robust rule: f0 = the LOWEST bin in
    // [40, 2000] Hz within 3 dB of the maximum bin magnitude. Fundamental-
    // strong tones (sine, saw, typical FM) resolve correctly; true missing-
    // fundamental signals are out of scope for Phase 2 (confidence reported).
    {
        const int W = std::min(1 << 15, n);
        if (W >= 4096)
        {
            // Center the window on the loudest envelope bin.
            int peakBin = 0;
            for (int i = 1; i < (int) bins.size(); ++i)
                if (bins[(size_t) i] > bins[(size_t) peakBin]) peakBin = i;
            int center = (int) ((peakBin + 0.5) * binSamples);
            int start = std::clamp(center - W / 2, 0, n - W);

            const int F = 1 << 15;
            // Real-only transform REQUIRES an array of 2*fftSize floats (JUCE doc).
            std::vector<float> buf((size_t) F * 2, 0.0f);
            double mean = 0.0;
            for (int i = 0; i < W; ++i) mean += audio.samples[(size_t)(start + i)];
            mean /= (double) W;
            for (int i = 0; i < W; ++i)
                buf[(size_t) i] = (float) (audio.samples[(size_t)(start + i)] - mean);
            juce::dsp::WindowingFunction<float> win((size_t) W,
                juce::dsp::WindowingFunction<float>::hann, false);
            win.multiplyWithWindowingTable(buf.data(), (size_t) W);
            juce::dsp::FFT fft(15);
            fft.performRealOnlyForwardTransform(buf.data(), true);

            const int bins2 = F / 2;
            const double fRes = sr / (double) F;
            const int lo = std::max(2, (int) std::ceil(40.0 / fRes));
            const int hi = std::min(bins2 - 1, (int) std::floor(2000.0 / fRes));
            if (hi > lo)
            {
                std::vector<double> mags((size_t)(hi - lo + 1), 0.0);
                double maxMag = 0.0;
                for (int b = lo; b <= hi; ++b)
                {
                    const double re = buf[(size_t) (2 * b)];
                    const double im = buf[(size_t) (2 * b + 1)];
                    const double m = std::sqrt(re * re + im * im);
                    mags[(size_t)(b - lo)] = m;
                    maxMag = std::max(maxMag, m);
                }
                std::vector<double> sorted(mags);
                std::sort(sorted.begin(), sorted.end());
                const double median = sorted[sorted.size() / 2];
                out.f0Confidence = (maxMag > 1e-15 && median > 1e-15)
                    ? 20.0 * std::log10(maxMag / median) : 0.0;

                // Lowest bin within 3 dB of the maximum + parabolic refinement.
                const double thresh = maxMag * std::pow(10.0, -3.0 / 20.0);
                int peakIdx = -1;
                for (int i = 0; i < (int) mags.size(); ++i)
                    if (mags[(size_t) i] >= thresh) { peakIdx = i; break; }
                if (peakIdx >= 0)
                {
                    double frac = 0.0;
                    if (peakIdx > 0 && peakIdx + 1 < (int) mags.size())
                    {
                        const double a = mags[(size_t)(peakIdx - 1)],
                                     b = mags[(size_t) peakIdx],
                                     c = mags[(size_t)(peakIdx + 1)];
                        const double denom = a - 2.0 * b + c;
                        if (std::abs(denom) > 1e-12)
                            frac = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
                    }
                    out.f0Hz = (double) (lo + peakIdx + frac) * fRes;
                    out.f0Midi = midiNoteForHz(out.f0Hz);
                }
            }
        }
    }

    out.ok = true;
    return out;
}

void evaluateToneExpectations(const ProjectCommands::ToneVerityParams& p,
                              ProjectCommands::ToneVerityResult& r)
{
    auto add = [&r](const char* name, bool pass, double measured, double expected,
                    const char* note = "")
    {
        r.expectations.push_back({ name, pass, measured, expected, note });
    };

    if (!std::isnan(p.attackMsMin) || !std::isnan(p.attackMsMax))
    {
        const bool pass = !std::isnan(r.attackMs)
            && (std::isnan(p.attackMsMin) || r.attackMs >= p.attackMsMin)
            && (std::isnan(p.attackMsMax) || r.attackMs <= p.attackMsMax);
        add("attackMs", pass, r.attackMs,
            !std::isnan(p.attackMsMin) ? p.attackMsMin : p.attackMsMax,
            std::isnan(r.attackMs) ? "peak bin never reached 90% of peak" : "");
        ++r.expectationsChecked;
    }
    if (!std::isnan(p.sustainRatioMin))
    {
        add("sustainRatio", !std::isnan(r.sustainRatio)
            && r.sustainRatio >= p.sustainRatioMin, r.sustainRatio, p.sustainRatioMin);
        ++r.expectationsChecked;
    }
    if (!std::isnan(p.modRateHz))
    {
        const bool pass = !std::isnan(r.modRateHz)
            && std::abs(r.modRateHz - p.modRateHz)
                   <= std::max(1e-9, p.modRateHz * p.modRateTolPct / 100.0);
        add("modRateHz", pass, r.modRateHz, p.modRateHz,
            std::isnan(r.modRateHz) ? "window too short for AM analysis" : "");
        ++r.expectationsChecked;
    }
    if (!std::isnan(p.centroidRiseMin))
    {
        const bool pass = !std::isnan(r.centroidStart) && !std::isnan(r.centroidEnd)
            && r.centroidStart > 1e-9
            && (r.centroidEnd / r.centroidStart) >= p.centroidRiseMin;
        add("centroidRise", pass,
            (std::isnan(r.centroidStart) || std::isnan(r.centroidEnd))
                ? kToneNaN : r.centroidEnd / r.centroidStart,
            p.centroidRiseMin);
        ++r.expectationsChecked;
    }
    if (!std::isnan(p.f0Hz))
    {
        const double cents = (!std::isnan(r.f0Hz) && r.f0Hz > 0.0)
            ? 1200.0 * std::log2(r.f0Hz / p.f0Hz) : kToneNaN;
        const bool pass = !std::isnan(cents) && std::abs(cents) <= p.f0CentsMax;
        add("f0Hz", pass, r.f0Hz, p.f0Hz,
            !std::isnan(cents)
                ? ("cents=" + std::to_string(cents)).c_str()
                : "no f0 estimate");
        r.f0Cents = cents;
        ++r.expectationsChecked;
    }

    r.pass = true;
    for (const auto& e : r.expectations)
        if (!e.pass) { r.pass = false; break; }
}

QJsonObject buildToneVerityPayload(const ProjectCommands::ToneVerityResult& r)
{
    QJsonArray env;
    for (const double v : r.envelopeRms) env.append(v);
    QJsonArray exps;
    for (const auto& e : r.expectations)
        exps.append(QJsonObject{
            { "name", QString::fromStdString(e.name) },
            { "pass", e.pass },
            { "measured", e.measured },
            { "expected", e.expected },
            { "note", QString::fromStdString(e.note) } });

    QJsonObject root{
        { "ok", r.ok },
        { "trackIndex", r.trackIndex },
        { "windowSeconds", r.windowSeconds },
        { "duration", r.duration },
        { "sampleRate", r.sampleRate },
        { "samplePeak", r.samplePeak },
        { "binSeconds", r.binSeconds },
        { "envelopeRms", env },
        { "envelopeDecimated", r.envelopeDecimated },
        { "attackMs", r.attackMs },
        { "peakRms", r.peakRms },
        { "sustainRatio", r.sustainRatio },
        { "trailingSilenceSeconds", r.trailingSilenceSeconds },
        { "amDepth", r.amDepth },
        { "modRateHz", r.modRateHz },
        { "modProminence", r.modProminence },
        { "modCycles", r.modCycles },
        { "centroidStart", r.centroidStart },
        { "centroidEnd", r.centroidEnd },
        { "centroidMean", r.centroidMean },
        { "f0Hz", r.f0Hz },
        { "f0Confidence", r.f0Confidence },
        { "f0Midi", r.f0Midi },
        { "f0Cents", r.f0Cents },
        { "baselineAudible", r.baselineAudible },
        { "pass", r.pass },
        { "expectationsChecked", r.expectationsChecked },
        { "expectations", exps }
    };
    if (!r.error.empty())
        root.insert("error", QString::fromStdString(r.error));
    return root;
}

} // namespace HDAW
