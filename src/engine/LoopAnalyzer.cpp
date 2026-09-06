#include "LoopAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace HDAW {

namespace {

struct Onset {
    double time = 0.0;
    double strength = 0.0;
};

// Alignment score for a candidate period T and grid phase phi: the sum of
// onset strengths whose onset falls within tol of phi + k*T for some integer k.
double gridAlignmentScore(const std::vector<Onset>& onsets, double T, double phi, double tol)
{
    double score = 0.0;
    for (const auto& o : onsets)
    {
        const double kf = std::round((o.time - phi) / T);
        const double grid = phi + kf * T;
        if (std::abs(o.time - grid) <= tol)
            score += o.strength;
    }
    return score;
}

} // namespace

LoopAnalysis LoopAnalyzer::analyze(const juce::String& sourceFile,
                                   juce::AudioFormatManager& formatManager,
                                   double maxSeconds, double sensitivity)
{
    LoopAnalysis result;
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(juce::File(sourceFile)));
    if (reader == nullptr) return result;
    if (reader->numChannels <= 0 || reader->lengthInSamples <= 0) return result;

    const int64_t maxSamples = static_cast<int64_t>(maxSeconds * reader->sampleRate);
    const int64_t totalSamples = (std::min)(reader->lengthInSamples, maxSamples);
    if (totalSamples <= 0) return result;

    const int numSamples = static_cast<int>(totalSamples);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    // Mix to mono.
    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom(0, 0, buffer, ch, 0, numSamples);
    mono.applyGain(1.0f / static_cast<float>(std::max(1, buffer.getNumChannels())));

    return analyzeBuffer(mono, reader->sampleRate, sensitivity);
}

LoopAnalysis LoopAnalyzer::analyzeBuffer(const juce::AudioBuffer<float>& mono,
                                         double sampleRate, double sensitivity)
{
    LoopAnalysis result;
    const int numSamples = mono.getNumSamples();
    if (numSamples <= 0 || sampleRate <= 0.0) return result;

    constexpr int FFT_SIZE = 1024;
    constexpr int HOP_SIZE = 256;
    if (numSamples < FFT_SIZE) return result;

    const float* data = mono.getReadPointer(0);
    const int numFrames = (numSamples - FFT_SIZE) / HOP_SIZE;
    if (numFrames < 8) return result;

    // --- 1. Spectral-flux onset curve (Hann 1024 / hop 256, positive diff) ---
    juce::dsp::FFT fft(10);  // 2^10 = 1024
    std::vector<float> prevFrame(FFT_SIZE / 2 + 1, 0.0f);
    std::vector<double> flux(numFrames, 0.0);
    std::vector<float> fftBuffer(FFT_SIZE * 2, 0.0f);
    std::vector<float> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0 * juce::MathConstants<float>::pi * i / (FFT_SIZE - 1)));

    std::vector<float> mag(FFT_SIZE / 2 + 1, 0.0f);
    for (int frame = 0; frame < numFrames; ++frame)
    {
        const int offset = frame * HOP_SIZE;
        for (int i = 0; i < FFT_SIZE; ++i)
            fftBuffer[i] = data[offset + i] * window[i];
        std::fill(fftBuffer.begin() + FFT_SIZE, fftBuffer.end(), 0.0f);
        fft.performRealOnlyForwardTransform(fftBuffer.data());

        // JUCE FFT output format: [DC_real, Nyquist_real, Re(1), Im(1), Re(2), ...]
        mag[0] = std::abs(fftBuffer[0]);
        mag[FFT_SIZE / 2] = std::abs(fftBuffer[1]);
        for (int i = 1; i < FFT_SIZE / 2; ++i)
        {
            const float re = fftBuffer[i * 2];
            const float im = fftBuffer[i * 2 + 1];
            mag[i] = std::sqrt(re * re + im * im);
        }

        double f = 0.0;
        for (int i = 0; i <= FFT_SIZE / 2; ++i)
        {
            // Energy (squared-magnitude) spectral flux. A real attack jumps
            // the window energy up; a note that ends inside the window loses
            // energy (its spectral energy is conserved but the window holds
            // less of the note), so the note-end spectral-leakage splatter
            // that linear magnitude flux reports as a spurious onset becomes
            // a negative difference here and is naturally suppressed.
            const double cur = static_cast<double>(mag[i]) * mag[i];
            const double prev = static_cast<double>(prevFrame[i]) * prevFrame[i];
            const double d = cur - prev;
            if (d > 0.0) f += d;
        }
        flux[frame] = f;
        prevFrame = mag;
    }

    // Adaptive threshold: local mean over a ~1s window plus a delta scaled to
    // the local flux spread. Runs on the command/RPC thread only (no RT code).
    const int winFrames = std::max(1, static_cast<int>(1.0 * sampleRate / HOP_SIZE));
    std::vector<double> localMean(numFrames, 0.0);
    std::vector<double> localStd(numFrames, 0.0);
    for (int i = 0; i < numFrames; ++i)
    {
        const int lo = std::max(0, i - winFrames / 2);
        const int hi = std::min(numFrames - 1, i + winFrames / 2);
        const int cnt = hi - lo + 1;
        double sum = 0.0, sumSq = 0.0;
        for (int j = lo; j <= hi; ++j)
        {
            sum += flux[j];
            sumSq += flux[j] * flux[j];
        }
        const double m = sum / cnt;
        const double var = std::max(0.0, sumSq / cnt - m * m);
        localMean[i] = m;
        localStd[i] = std::sqrt(var);
    }

    // Peak picking with local max check, adaptive threshold and min spacing.
    const double minInterval = 0.04;  // 40 ms
    const int minFrames = std::max(1, static_cast<int>(minInterval * sampleRate / HOP_SIZE));

    std::vector<Onset> onsets;
    int lastPeak = -minFrames;
    for (int i = 0; i < numFrames; ++i)
    {
        const bool isPeak = (i == 0) ? flux[i] > flux[i + 1]
                            : (i == numFrames - 1) ? flux[i] > flux[i - 1]
                            : (flux[i] > flux[i - 1] && flux[i] > flux[i + 1]);
        if (!isPeak) continue;
        if (flux[i] <= localMean[i] + (0.5 + sensitivity) * localStd[i]) continue;
        // Prominence gate: a real attack enters the FFT window in ~1 hop, so
        // its flux peak towers over the frames immediately before it. The
        // spectral-leakage splatter a sustained note leaves as it exits the
        // window builds gradually over several frames, so its peak is only a
        // small multiple of the preceding frames' flux and is rejected here.
        if (i >= 3 && flux[i] < 2.0 * ((flux[i - 1] + flux[i - 2] + flux[i - 3]) / 3.0)) continue;
        if (i - lastPeak < minFrames) continue;
        onsets.push_back({ i * static_cast<double>(HOP_SIZE) / sampleRate, flux[i] });
        lastPeak = i;
    }

    if (onsets.size() < 2) return result;

    // Drop weak note-end splatter onsets. With energy flux the leakage
    // splatter a sustained note leaves as it exits the FFT window is still
    // detectable but only ~13% of a real attack's strength; removing onsets
    // below a fifth of the strongest onset (the first onset is always kept to
    // anchor the phase) leaves the beat-defining attacks. This is a heuristic
    // tuned for loop grids (quiet layers below 20% of the loudest onset do
    // not define the beat anyway).
    {
        double maxStrength = onsets.front().strength;
        for (const auto& o : onsets)
            maxStrength = (std::max)(maxStrength, o.strength);
        const double keepFloor = 0.2 * maxStrength;
        std::vector<Onset> filtered;
        filtered.reserve(onsets.size());
        for (size_t i = 0; i < onsets.size(); ++i)
            if (i == 0 || onsets[i].strength >= keepFloor)
                filtered.push_back(onsets[i]);
        onsets = std::move(filtered);
        if (onsets.size() < 2) return result;
    }

    double totalStrength = 0.0;
    for (const auto& o : onsets) totalStrength += o.strength;
    if (totalStrength <= 0.0) return result;

    // --- 2. Beat interval: autocorrelation of the flux curve (60-200 BPM) ---
    const double minT = 60.0 / 200.0;   // 0.3 s  = 200 BPM
    const double maxT = 60.0 / 60.0;    // 1.0 s  = 60 BPM
    const int minLag = std::max(1, static_cast<int>(minT * sampleRate / HOP_SIZE));
    const int maxLag = std::min(numFrames - 1, static_cast<int>(maxT * sampleRate / HOP_SIZE));
    if (maxLag <= minLag) return result;

    std::vector<double> autocorr(maxLag - minLag + 1, 0.0);
    int bestLag = minLag;
    double bestAC = -1.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0;
        for (int i = 0; i + lag < numFrames; ++i)
            ac += flux[i] * flux[i + lag];
        autocorr[lag - minLag] = ac;
        if (ac > bestAC) { bestAC = ac; bestLag = lag; }
    }

    // Candidate periods: local maxima of the autocorrelation (plus the global
    // max, and the harmonic/subharmonic of each), deduplicated.
    std::vector<int> peakLags;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        bool localMax = true;
        if (lag > minLag && autocorr[lag - minLag] <= autocorr[lag - minLag - 1]) localMax = false;
        if (lag < maxLag && autocorr[lag - minLag] <= autocorr[lag - minLag + 1]) localMax = false;
        if (localMax) peakLags.push_back(lag);
    }
    if (peakLags.empty())
        peakLags.push_back(bestLag);
    if (std::find(peakLags.begin(), peakLags.end(), bestLag) == peakLags.end())
        peakLags.push_back(bestLag);

    std::vector<double> candidates;
    for (int lag : peakLags)
    {
        const double T = static_cast<double>(lag) * HOP_SIZE / sampleRate;
        for (double cand : { T, T * 2.0, T * 0.5 })
        {
            const double c = juce::jlimit(minT, maxT, cand);
            if (std::find(candidates.begin(), candidates.end(), c) == candidates.end())
                candidates.push_back(c);
        }
    }
    std::sort(candidates.begin(), candidates.end());

    // Comb refinement: for each candidate T (and its phase search) compute the
    // alignment score; keep the T with the highest normalized score, breaking
    // ties toward the LARGER period (a half-period harmonic always aligns when
    // the true period does, so prefer the slower of two equally-good fits).
    struct CandidateScore { double T = 0.0; double phase = 0.0; double score = 0.0; };
    std::vector<CandidateScore> candScores;
    candScores.reserve(candidates.size());
    for (double T : candidates)
    {
        const double tol = 0.12 * T;
        constexpr int kPhases = 24;  // phase steps of T/24
        double bestScore = -1.0, bestPhase = 0.0;
        for (int p = 0; p < kPhases; ++p)
        {
            const double phi = p * (T / kPhases);
            const double s = gridAlignmentScore(onsets, T, phi, tol);
            if (s > bestScore) { bestScore = s; bestPhase = phi; }
        }
        candScores.push_back({ T, bestPhase, bestScore });
    }

    const CandidateScore* best = nullptr;
    for (const auto& c : candScores)
    {
        if (best == nullptr
            || c.score > best->score + 1e-9
            || (std::fabs(c.score - best->score) <= 1e-9 && c.T > best->T))
            best = &c;
    }
    if (best == nullptr) return result;

    const double T = best->T;
    const double tol = 0.12 * T;
    const double confidence = best->score / totalStrength;
    if (confidence < 0.3) return result;

    // --- 3/4. Phase (already the best phase for T) + downbeat refinement. ---
    // Of the four beat offsets that could be the bar origin, pick the one that
    // maximizes onset strength on bar starts (grid origin + multiples of 4*T).
    double downbeat = best->phase;
    // Downbeat refinement: among the four beat offsets that could be the bar
    // origin, pick the one with the most onsets on bar starts (grid origin +
    // multiples of 4*T). For a uniform loop every beat carries an onset and
    // all four candidates tie on bar-start hits, so the tie-break falls back
    // to the smallest m (= the beat phase itself), which keeps a 4-on-floor
    // loop's grid origin at its detected phase. Strength is not compared here:
    // per-onset flux varies with the content and would flip the choice on
    // noise for loops where every beat has an equal-strength onset.
    int bestBarHits = -1;
    for (int m = 0; m < 4; ++m)
    {
        const double origin = best->phase + m * T;
        int hits = 0;
        for (const auto& o : onsets)
        {
            const double kf = std::round((o.time - origin) / (4.0 * T));
            if (std::abs(o.time - (origin + kf * 4.0 * T)) <= tol) ++hits;
        }
        if (hits > bestBarHits) { bestBarHits = hits; downbeat = origin; }
    }

    // --- 5. Integer bar count. ---
    const double lastOnset = onsets.back().time;
    const double firstOnset = onsets.front().time;

    int bars = 0;
    // Prefer a bar count whose loop end lands within a quarter beat of the
    // last onset (either side — the grid period is hop-quantized, so a real
    // loop end can sit a few ms before or after the computed boundary); on a
    // tie prefer the larger b. If none qualifies (e.g. a loop whose final
    // onset is one full beat before the boundary), fall back to the smallest
    // |E - lastOnset| with E not more than a quarter beat short of it.
    const double preferred = 0.25 * T;
    for (int b : { 8, 4, 2, 1 })
    {
        const double E = downbeat + b * 4.0 * T;
        if (E < lastOnset - preferred) continue;
        if (std::abs(E - lastOnset) <= preferred) { bars = b; break; }
    }
    if (bars == 0)
    {
        double bestResidual = std::numeric_limits<double>::max();
        for (int b : { 1, 2, 4, 8 })
        {
            const double E = downbeat + b * 4.0 * T;
            if (E < lastOnset - preferred) continue;
            const double residual = std::abs(E - lastOnset);
            if (residual <= bestResidual) { bestResidual = residual; bars = b; }
        }
    }
    if (bars == 0) return result;

    // --- 6. Assemble the result. ---
    const double loopEnd = downbeat + bars * 4.0 * T;
    result.ok = true;
    result.bpm = 60.0 / T;
    result.confidence = confidence;
    result.beatInterval = T;
    result.downbeatOffset = downbeat;
    result.bars = bars;
    result.beatsPerBar = 4;
    result.loopSpanSourceSeconds = bars * 4.0 * T;
    result.leadingSlack = std::max(0.0, firstOnset - downbeat);
    result.trailingSlack = std::max(0.0, loopEnd - lastOnset);
    result.onsetTimes.reserve(onsets.size());
    for (const auto& o : onsets)
        result.onsetTimes.push_back(o.time);
    return result;
}

} // namespace HDAW