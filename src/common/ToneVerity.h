#pragma once
// ToneVerity (Phase 2 of docs/plans/2026-09-22-param-verity-pipeline.md) —
// tone-level verification on ONE rendered WAV from the same offline path as
// ParamVerify: envelope shape, amplitude-modulation rate, spectral-centroid
// trajectory, and a harmonic-product-spectrum f0 estimate, plus optional
// deterministic expectation checks. No LLM in the verdict path.
//
// All measurements are computed offline on the command thread (same pattern as
// MixReportAnalyzer): juce AudioFormatReader input, juce::dsp::FFT frames.

#include <QJsonObject>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "ProjectCommands.h"   // ToneVerityParams / ToneVerityResult

namespace juce { class File; }

namespace HDAW {

inline constexpr double kToneNaN = std::numeric_limits<double>::quiet_NaN();

struct ToneVerityAnalysis
{
    bool ok = false;
    std::string error;

    double duration = 0.0;
    int sampleRate = 0;
    double samplePeak = 0.0;          // max |sample| — the audibility check

    // Envelope (binned RMS trace)
    double binSeconds = 0.01;
    std::vector<double> envelopeRms;  // one value per bin (may be decimated)
    bool envelopeDecimated = false;
    double attackMs = kToneNaN;       // first 10% -> 90% of peak crossing
    double peakRms = 0.0;
    double sustainRatio = kToneNaN;   // mean(last 25% bins) / peakRms
    double trailingSilenceSeconds = kToneNaN;

    // Amplitude modulation of the envelope
    double amDepth = 0.0;             // (max-min)/mean over the central 80% of bins
    double modRateHz = kToneNaN;
    double modProminence = 0.0;       // peak / median magnitude in the search band
    double modCycles = 0.0;           // modRateHz * duration (>= ~3 for a solid verdict)

    // Spectral centroid trajectory (first vs last quarter of the window)
    double centroidStart = kToneNaN;
    double centroidEnd = kToneNaN;
    double centroidMean = kToneNaN;

    // Pitch (HPS at the loudest region)
    double f0Hz = kToneNaN;
    double f0Confidence = 0.0;        // HPS peak minus median, log domain
    int f0Midi = -1;
};

// Analyze one rendered WAV (mono-summed internally). Deterministic.
ToneVerityAnalysis analyzeToneWav(const juce::File& wav, double binSeconds);

// Nearest MIDI note + cents for a measured f0 (69 = A4). Returns midi < 0 when
// f0 is not a positive finite number.
inline int midiNoteForHz(double f0)
{
    if (!(f0 > 0.0) || !std::isfinite(f0)) return -1;
    return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(f0 / 440.0)));
}

// Evaluate the optional expectations (NaN sentinel = absent) against a filled
// ToneVerityResult and fill result.expectations / result.pass /
// result.expectationsChecked. Pure; engine calls this after analysis.
void evaluateToneExpectations(const ProjectCommands::ToneVerityParams& p,
                              ProjectCommands::ToneVerityResult& r);

// The single payload builder behind the MCP `tone_verity` tool and the RPC
// `audio.verifyTone` route (parity by construction).
QJsonObject buildToneVerityPayload(const ProjectCommands::ToneVerityResult& r);

} // namespace HDAW
