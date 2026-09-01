// PsyArpEngine regression tests (plan docs/plans/2026-08-31-psyarp-vector-assert-plan.md).
//
// Gate 2 — held-chord stress: Random/Asym332/UpDown x OctaveRange 1-2 with
// ABUTTING note boundaries (a chord's note-offs land on the same SAMPLE as
// the next chord's note-ons), empty-held interleaves, thousands of rendered
// blocks. MSVC debug builds must not fire the <vector> operator[] "vector
// subscript out of range" assertion (line 1939: _STL_VERIFY inside
// vector::operator[]) anywhere in the arp render path.
//
// Gate 6 — grid lock: the arp step clock derives from the transport beat
// position (floor(beat / 0.25)), so an off-grid chord trigger must not
// phase-shift subsequent steps. Onsets are detected from the gated audio
// envelope with FX disabled: the 80% gate leaves a silent ~11 ms gap every
// 16th, so each step shows a clear RMS rising edge at the grid boundary.
//
// Audio-thread contract: fixes stay lock-free and allocation-light (rebuild
// at most once per block on held-set change, same pattern as the original
// first-chord rebuild).

#include <gtest/gtest.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#endif

#include "engine/PsyArpEngine.h"
#include "engine/TrackFXSlot.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// MSVC debug STL asserts (_STL_VERIFY -> _invalid_parameter) pop a MODAL
// dialog in a console test run by default, hanging CI until someone clicks.
// Route them to a handler that prints to stderr and aborts: a regression
// becomes a clean crash the test runner records instead of a silent hang.
#ifdef _MSC_VER
namespace
{
void hdawTestInvalidParameterHandler (const wchar_t*, const wchar_t*,
                                      const wchar_t*, unsigned int, uintptr_t)
{
    std::fputs ("FATAL: CRT invalid parameter (MSVC STL debug assert) fired\n", stderr);
    std::abort();
}
struct HdawAssertRouter
{
    HdawAssertRouter()
    {
        _CrtSetReportMode (_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        _set_invalid_parameter_handler (hdawTestInvalidParameterHandler);
    }
};
const HdawAssertRouter hdawAssertRouter;
} // namespace
#endif

using namespace HDAW;

namespace
{
constexpr double kSampleRate     = 44100.0;
constexpr int    kBlockSize      = 512;
constexpr double kBpm            = 120.0;
constexpr double kBeatsPerSample = kBpm / 60.0 / kSampleRate;
constexpr double kBeatsPerBlock  = (double) kBlockSize * kBeatsPerSample;

// Four bar-chord voicings the stress loop cycles through.
const int kChords[][3] = {
    { 36, 39, 43 },   // Cm
    { 34, 38, 41 },   // Bb-ish
    { 39, 43, 46 },   // Ab
    { 41, 44, 48 },   // F
};
constexpr int kNumChords = 4;

// Renders `cfg.chords` bar-chords with abutting boundaries (previous chord's
// note-offs at sample 0 of the same block as the next chord's note-ons),
// a one-chord-length empty-held interleave every `gapEvery` chords, and
// returns blocks rendered + audio peak + finiteness. `onBeforeOff` flips the
// same-sample event insertion order (both orders must be safe).
struct StressResult
{
    int   blocks      = 0;
    float peak        = 0.0f;
    bool  allFinite   = true;
};

struct StressConfig
{
    int  patternShape = 0;    // 0=UpDown 1=Asym332 2=Random
    int  octaveRange  = 1;
    int  uniVoices    = 2;
    int  oscShape     = 0;
    int  chords       = 8;
    int  gapEvery     = 3;    // 0 = never
    bool onBeforeOff  = false;
    bool fxOn         = true;
};

StressResult runStress (PsyArpEngine& engine, const StressConfig& cfg)
{
    engine.setPatternShape (cfg.patternShape);
    engine.setOctaveRange (cfg.octaveRange);
    engine.setOscUnisonVoices (cfg.uniVoices);
    engine.setOscShape (cfg.oscShape);
    if (! cfg.fxOn)
    {
        engine.setDelayWetLevel (0.0f);
        engine.setReverbWetOnDry (0.0f);
        engine.setReverbWetOnDelay (0.0f);
        engine.setPhaserEnabled (false);
        engine.setOutputLevel (1.0f);
    }
    engine.prepare (kSampleRate, kBlockSize);

    const int chordBlocks = (int) std::floor (8.0 / kBeatsPerBlock);  // 8-beat chords
    StressResult result;

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    int nextChord     = 0;
    int heldChord     = -1;
    int blocks        = cfg.chords * chordBlocks + 16;

    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b % chordBlocks == 0)
        {
            const int chordIdx  = nextChord % kNumChords;
            const bool gapHere  = (cfg.gapEvery > 0 && nextChord > 0
                                   && (nextChord % cfg.gapEvery) == 0);

            auto addOns  = [&midi, chordIdx]()
            {
                for (int n : kChords[chordIdx])
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            };
            auto addOffs = [&]()
            {
                if (heldChord < 0) return;
                for (int n : kChords[heldChord])
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            };

            if (cfg.onBeforeOff)
            {
                if (! gapHere) addOns();
                addOffs();
            }
            else
            {
                addOffs();
                if (! gapHere) addOns();
            }

            heldChord = gapHere ? -1 : chordIdx;
            ++nextChord;
        }

        engine.render (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int s = 0; s < kBlockSize; ++s)
            {
                const float v = p[s];
                if (! std::isfinite (v)) { result.allFinite = false; continue; }
                result.peak = std::max (result.peak, std::fabs (v));
            }
        }
    }

    result.blocks = blocks;
    return result;
}

// RMS-envelope onset detection over a whole rendered signal. A step onset is
// the below->above crossing of the ~3 ms RMS envelope (hysteresis + a small
// refractory window). Returns onset times in beats.
std::vector<double> detectOnsets (const std::vector<float>& x, double beatsPerSample,
                                  double fromBeat, double toBeat)
{
    constexpr int    win        = 128;   // ~2.9 ms
    constexpr int    hop        = 16;
    constexpr double hiThresh   = 0.10;
    constexpr double loThresh   = 0.02;
    constexpr int64_t refractory = 640;  // ~0.06 beats at 120 BPM

    std::vector<double> onsets;
    bool    above     = false;
    int64_t lastOnset = -(2 * refractory);

    int64_t i = win;
    while (i + hop < (int64_t) x.size())
    {
        double s2 = 0.0;
        for (int k = 0; k < win; k += 4)
        {
            const float v = x[(size_t) (i - k)];
            s2 += (double) v * (double) v;
        }
        const double rms  = std::sqrt (s2 / (double) (win / 4));
        const double beat = (double) i * beatsPerSample;

        if (! above)
        {
            if (rms > hiThresh && i - lastOnset > refractory)
            {
                above     = true;
                lastOnset = i;
                if (beat >= fromBeat && beat <= toBeat)
                    onsets.push_back (beat);
            }
        }
        else if (rms < loThresh)
        {
            above = false;
        }

        i += hop;
    }
    return onsets;
}

// Grid-lock scenario: trigger a held chord at `triggerBeat` (block
// quantized), hold for `holdBeats`, collect channel 0.
struct GridResult
{
    std::vector<float>  samples;
    std::vector<double> onsets;
    double firstOnsetBeat = -1.0;
    double peak           = 0.0;
};

GridResult runGridScenario (PsyArpEngine& engine, int patternShape, int octaveRange,
                            double triggerBeat, double holdBeats, double stopBeat,
                            int stepRateIndex = 0)
{
    engine.setPatternShape (patternShape);
    engine.setOctaveRange (octaveRange);
    engine.setStepRateIndex (stepRateIndex);
    engine.setOscShape (1);          // Square: strong, stable envelope
    engine.setOscUnisonVoices (1);   // no detune beating
    engine.setFilterCutoffHz (6000.0f);
    engine.setFilterResonance (20.0f);  // max damping in the SVF: no ringing
                                        // through the gate gap
    engine.setDelayWetLevel (0.0f);
    engine.setReverbWetOnDry (0.0f);
    engine.setReverbWetOnDelay (0.0f);
    engine.setPhaserEnabled (false);
    engine.setOutputLevel (1.0f);
    engine.prepare (kSampleRate, kBlockSize);

    GridResult result;
    const int held[] = { 36, 43, 48 };

    const int totalBlocks = (int) std::ceil (stopBeat / kBeatsPerBlock) + 4;
    const int onBlock     = (int) std::floor (triggerBeat / kBeatsPerBlock);
    const int offBlock    = (int) std::floor ((triggerBeat + holdBeats) / kBeatsPerBlock);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    for (int b = 0; b < totalBlocks; ++b)
    {
        midi.clear();
        if (b == onBlock)
            for (int n : held)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (b == offBlock)
            for (int n : held)
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);

        engine.render (buffer, midi);

        const auto* p = buffer.getReadPointer (0);
        for (int s = 0; s < kBlockSize; ++s)
        {
            result.samples.push_back (p[s]);
            result.peak = std::max (result.peak, (double) std::fabs (p[s]));
        }
    }

    result.onsets = detectOnsets (result.samples, kBeatsPerSample,
                                  triggerBeat - 0.5, stopBeat);
    if (! result.onsets.empty())
        result.firstOnsetBeat = result.onsets.front();
    return result;
}

double gridDistance (double beat)
{
    const double m = std::fmod (beat, 0.25);
    return std::min (m, 0.25 - m);
}

double gridDistanceFor (double beat, double division)
{
    const double m = std::fmod (beat, division);
    return std::min (m, division - m);
}

// Consecutive onset deltas, sorted ascending.
std::vector<double> sortedSpacings (const std::vector<double>& onsets)
{
    std::vector<double> d;
    for (size_t i = 1; i < onsets.size(); ++i)
        d.push_back (onsets[i] - onsets[i - 1]);
    std::sort (d.begin(), d.end());
    return d;
}

} // namespace

// ============================================================================
// Gate 2: held-chord stress — the reported crash matrix
// ============================================================================

TEST (PsyArpEngine, HeldChordStressShapesOctavesAbutting)
{
    // 3 shapes x 2 octave ranges, bar-chords with off==on same-sample
    // boundaries and empty-held interleaves; ~2.7k blocks per scenario,
    // ~17k blocks total with FX tails on. No crash / no STL assert is the
    // gate; finiteness and audibility guard against runaway DSP.
    for (int shape = 0; shape < 3; ++shape)
    {
        for (int oct = 1; oct <= 2; ++oct)
        {
            StressConfig cfg;
            cfg.patternShape = shape;
            cfg.octaveRange  = oct;
            cfg.fxOn         = true;

            PsyArpEngine engine;
            const auto r = runStress (engine, cfg);

            EXPECT_EQ (r.blocks, cfg.chords * (int) std::floor (8.0 / kBeatsPerBlock) + 16);
            EXPECT_TRUE (r.allFinite) << "shape=" << shape << " oct=" << oct;
            EXPECT_GT (r.peak, 0.01f) << "shape=" << shape << " oct=" << oct;
        }
    }
}

TEST (PsyArpEngine, HeldChordStressSameSampleOrderFlipped)
{
    // Same-sample events with the NOTE-ON inserted BEFORE the note-off: the
    // held set transiently unions both chords; the dirty-flag rebuild must
    // still converge to the new chord without touching freed/empty storage.
    StressConfig cfg;
    cfg.patternShape = 2;   // Random (the reported ArpAlt config)
    cfg.octaveRange  = 2;
    cfg.onBeforeOff  = true;

    PsyArpEngine engine;
    const auto r = runStress (engine, cfg);
    EXPECT_TRUE (r.allFinite);
    EXPECT_GT (r.peak, 0.01f);
}

TEST (PsyArpEngine, TrackFxSlotHeldChordStress)
{
    // The real wiring: TrackFXSlot("psyarp") + prepare + process, mirroring
    // the export render path (buffer cleared by the slot, MIDI consumed).
    HDAW::TrackFXSlot slot ("psyarp");
    slot.setInternalParam (3, 2.0f);   // Pattern Shape = Random
    slot.setInternalParam (4, 2.0f);   // Octave Range = 2
    slot.setInternalParam (1, 4.0f);   // Unison Voices = 4 (oscVoices_[4] edge)

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    const int chordBlocks = (int) std::floor (8.0 / kBeatsPerBlock);
    const int blocks = 4 * chordBlocks + 16;
    int nextChord = 0, heldChord = -1;
    double peak = 0.0;
    bool allFinite = true;

    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b % chordBlocks == 0)
        {
            const int chordIdx = nextChord % kNumChords;
            if (heldChord >= 0)
                for (int n : kChords[heldChord])
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            for (int n : kChords[chordIdx])
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            heldChord = chordIdx;
            ++nextChord;
        }

        buffer.clear();
        slot.process (buffer, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int s = 0; s < kBlockSize; ++s)
            {
                const float v = p[s];
                if (! std::isfinite (v)) { allFinite = false; continue; }
                peak = std::max (peak, (double) std::fabs (v));
            }
        }
    }

    EXPECT_TRUE (allFinite);
    EXPECT_GT (peak, 0.01);
}

// ============================================================================
// Gate 1: exact assert sites
// ============================================================================

TEST (PsyArpEngine, RenderBeforePrepareDoesNotAssert)
{
    // render() without prepare(): the reverb comb/allpass buffers are EMPTY.
    // Pre-fix, processReverb subscripted combBuffer[0][0] on the empty
    // vector — the exact MSVC debug "vector subscript out of range" assert
    // (vector operator[], line 1939) — then divided by zero in the position
    // modulo. Post-fix: dry passthrough, no assert.
    PsyArpEngine engine;   // deliberately NOT prepared
    engine.setPatternShape (1);
    engine.setOctaveRange (2);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    for (int n : { 36, 43, 48 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

    for (int b = 0; b < 64; ++b)
    {
        if (b == 16) midi.clear();
        engine.render (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int s = 0; s < kBlockSize; ++s)
                ASSERT_TRUE (std::isfinite (p[s]));
        }
    }
}

TEST (PsyArpEngine, OctaveRangeBeyondDefsClamped)
{
    // setOctaveRange bypassing the slot-layer clamp must not grow the
    // sequence unbounded or index anything out of range.
    PsyArpEngine engine;
    engine.setPatternShape (0);   // UpDown: sequence grows with octaves
    engine.setOctaveRange (50);   // defs max is 4

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    engine.prepare (kSampleRate, kBlockSize);
    for (int n : { 36, 43, 48 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

    for (int b = 0; b < 300; ++b)
    {
        engine.render (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            for (int s = 0; s < kBlockSize; ++s)
                ASSERT_TRUE (std::isfinite (p[s]));
        }
    }
}

// ============================================================================
// Gate 6: grid lock
// ============================================================================

TEST (PsyArpEngine, GridLockOnGridStartStepsEverySixteenth)
{
    // Chord from beat 0 held 32 beats: after arming, every 0.25-beat grid
    // boundary must produce a step (audible rising edge). 32 beats = 128
    // grid cells; first trigger snaps to 0.25, so 127 onsets in (0, 32).
    PsyArpEngine engine;
    const auto r = runGridScenario (engine, /*UpDown*/ 0, /*oct*/ 1,
                                    /*trigger*/ 0.0, /*hold*/ 32.0, /*stop*/ 32.5);

    ASSERT_GT (r.peak, 0.3) << "expected a strong dry arp signal";
    ASSERT_FALSE (r.onsets.empty());

    EXPECT_NEAR (r.firstOnsetBeat, 0.25, 0.06);

    int onGrid = 0;
    for (double t : r.onsets)
        if (gridDistance (t) < 0.06) ++onGrid;
    EXPECT_EQ (onGrid, (int) r.onsets.size()) << "all onsets must sit on the 0.25 grid";

    EXPECT_GE ((int) r.onsets.size(), 122);
    EXPECT_LE ((int) r.onsets.size(), 132);
}

TEST (PsyArpEngine, GridLockOffGridTriggerSnapsToNextBoundary)
{
    // Chord triggered mid-grid at ~beat 3.88 (block-quantized 3.878): the
    // FIRST onset must snap to the next boundary (beat 4.0), and every
    // subsequent onset must stay on the 0.25 grid — the pre-fix accumulated
    // clock would phase-shift everything by ~0.12 beats forever.
    PsyArpEngine engine;
    const auto r = runGridScenario (engine, /*Asym332*/ 1, /*oct*/ 2,
                                    /*trigger*/ 3.87, /*hold*/ 20.0, /*stop*/ 24.5);

    ASSERT_GT (r.peak, 0.3) << "expected a strong dry arp signal";
    ASSERT_FALSE (r.onsets.empty());

    ASSERT_NE (r.firstOnsetBeat, -1.0);
    EXPECT_NEAR (r.firstOnsetBeat, 4.0, 0.06)
        << "first onset must snap to the next grid boundary, not mid-grid";

    int onGrid = 0;
    for (double t : r.onsets)
        if (gridDistance (t) < 0.06) ++onGrid;
    EXPECT_EQ (onGrid, (int) r.onsets.size()) << "phase must stay grid-locked after the off-grid trigger";

    // 20 beats held from 3.878: onsets at 4.0 .. 23.75 = 80 steps.
    EXPECT_GE ((int) r.onsets.size(), 75);
    EXPECT_LE ((int) r.onsets.size(), 85);
}

TEST (PsyArpEngine, GridLockSecondChordKeepsGridPhase)
{
    // Abutting chord change MID-grid: the step clock must NOT re-arm or
    // re-phase on the rebuild; onsets after the change stay on the grid.
    PsyArpEngine engine;
    engine.setPatternShape (2);        // Random
    engine.setOctaveRange (2);
    engine.setOscShape (1);
    engine.setOscUnisonVoices (1);
    engine.setFilterCutoffHz (6000.0f);
    engine.setFilterResonance (20.0f);
    engine.setDelayWetLevel (0.0f);
    engine.setReverbWetOnDry (0.0f);
    engine.setReverbWetOnDelay (0.0f);
    engine.setPhaserEnabled (false);
    engine.setOutputLevel (1.0f);
    engine.prepare (kSampleRate, kBlockSize);

    const int chordA[] = { 36, 43, 48 };
    const int chordB[] = { 39, 46, 51 };
    const int chordAOff = (int) std::floor (7.63 / kBeatsPerBlock);   // off-grid boundary
    const int stopBlock = (int) std::floor (16.5 / kBeatsPerBlock);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> samples;

    for (int b = 0; b < stopBlock; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : chordA)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        if (b == chordAOff)
        {
            // Abutting: chord A off and chord B on at the same sample.
            for (int n : chordA)
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            for (int n : chordB)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        }

        engine.render (buffer, midi);
        const auto* p = buffer.getReadPointer (0);
        for (int s = 0; s < kBlockSize; ++s)
            samples.push_back (p[s]);
    }

    double peak = 0.0;
    for (float v : samples)
        peak = std::max (peak, (double) std::fabs (v));
    const auto onsets = detectOnsets (samples, kBeatsPerSample, 0.05, 16.0);
    ASSERT_GT (peak, 0.3);
    ASSERT_GE ((int) onsets.size(), 40);

    int onGrid = 0;
    for (double t : onsets)
        if (gridDistance (t) < 0.06) ++onGrid;
    EXPECT_EQ (onGrid, (int) onsets.size())
        << "chord change mid-grid must not re-phase the step clock";
}

// ============================================================================
// Gate 7: arp step-rate division (param index 20; 0=1/16, 1=1/8, 2=1/4)
// ============================================================================

TEST (PsyArpEngine, StepRateEighthOnsetsEveryHalfBeat)
{
    // Rate index 1 = 1/8: step onsets every 0.5 beats (pre-fix: hard 0.25).
    PsyArpEngine engine;
    const auto r = runGridScenario (engine, /*UpDown*/ 0, /*oct*/ 1,
                                    /*trigger*/ 0.0, /*hold*/ 32.0, /*stop*/ 32.5,
                                    /*stepRateIndex*/ 1);

    ASSERT_GT (r.peak, 0.3);
    ASSERT_FALSE (r.onsets.empty());
    EXPECT_NEAR (r.firstOnsetBeat, 0.5, 0.06);

    for (double t : r.onsets)
        EXPECT_LT (gridDistanceFor (t, 0.5), 0.06) << "onsets must sit on the 0.5 grid";

    const auto sp = sortedSpacings (r.onsets);
    ASSERT_FALSE (sp.empty());
    for (double d : sp)
        EXPECT_NEAR (d, 0.5, 0.06) << "every onset gap must be one 1/8 step";
    // Steps at 0.5 .. 31.5 -> 63 onsets.
    EXPECT_GE ((int) r.onsets.size(), 58);
    EXPECT_LE ((int) r.onsets.size(), 68);
}

TEST (PsyArpEngine, StepRateQuarterOnsetsEveryBeat)
{
    // Rate index 2 = 1/4: step onsets every 1.0 beat.
    PsyArpEngine engine;
    const auto r = runGridScenario (engine, /*Asym332*/ 1, /*oct*/ 1,
                                    /*trigger*/ 0.0, /*hold*/ 32.0, /*stop*/ 32.5,
                                    /*stepRateIndex*/ 2);

    ASSERT_GT (r.peak, 0.3);
    ASSERT_FALSE (r.onsets.empty());
    EXPECT_NEAR (r.firstOnsetBeat, 1.0, 0.06);

    for (double t : r.onsets)
        EXPECT_LT (gridDistanceFor (t, 1.0), 0.06) << "onsets must sit on the 1.0 grid";

    const auto sp = sortedSpacings (r.onsets);
    ASSERT_FALSE (sp.empty());
    for (double d : sp)
        EXPECT_NEAR (d, 1.0, 0.06) << "every onset gap must be one 1/4 step";
    // Steps at 1.0 .. 31.0 -> 31 onsets.
    EXPECT_GE ((int) r.onsets.size(), 26);
    EXPECT_LE ((int) r.onsets.size(), 36);
}

TEST (PsyArpEngine, StepRateEighthOffGridTriggerSnapsToBoundary)
{
    // Off-grid trigger at block-quantized ~3.878 beats with rate 1/8: the
    // first onset must snap to the next 0.5-beat boundary (4.0) and the
    // phase must stay locked (Random shape also re-covers the OOB fix).
    PsyArpEngine engine;
    const auto r = runGridScenario (engine, /*Random*/ 2, /*oct*/ 2,
                                    /*trigger*/ 3.87, /*hold*/ 20.0, /*stop*/ 24.5,
                                    /*stepRateIndex*/ 1);

    ASSERT_GT (r.peak, 0.3);
    ASSERT_FALSE (r.onsets.empty());
    EXPECT_NEAR (r.firstOnsetBeat, 4.0, 0.06)
        << "first onset must snap to the next 1/8-grid boundary";

    for (double t : r.onsets)
        EXPECT_LT (gridDistanceFor (t, 0.5), 0.06);

    // Steps at 4.0 .. 23.5 -> 40 onsets.
    EXPECT_GE ((int) r.onsets.size(), 35);
    EXPECT_LE ((int) r.onsets.size(), 45);
}

TEST (PsyArpEngine, StepRateSlotDefsAndClamp)
{
    // (d) Param defs: index 20 "Step Rate", default 0 (1/16), range 0..2.
    // Out-of-range slot writes clamp to the def range (lesson 23).
    HDAW::TrackFXSlot slot ("psyarp");
    const auto defs = slot.getInternalParamDefs();
    ASSERT_EQ ((int) defs.size(), 21);
    EXPECT_EQ (defs[20].index, 20);
    EXPECT_EQ (defs[20].name, juce::String ("Step Rate"));
    EXPECT_NEAR (defs[20].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[20].minValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[20].maxValue, 2.0f, 1e-5f);

    slot.setInternalParam (20, 7.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 20], 2.0f, 1e-5f);
    slot.setInternalParam (20, -3.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 20], 0.0f, 1e-5f);
    for (float v : { 0.0f, 1.0f, 2.0f })
    {
        slot.setInternalParam (20, v);
        EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 20], v, 1e-5f)
            << "all three division values must be accepted by the defs clamp";
    }
}

TEST (PsyArpEngine, StepRateSlotWiringPrepareAndLive)
{
    // Full slot wiring: (1) prepare() pushes internalParamValues[20] into the
    // engine (rate 1/8 audible), (2) a LIVE setInternalParam mid-render
    // switches to 1/4 without stopping or re-phasing into garbage.
    HDAW::TrackFXSlot slot ("psyarp");
    slot.setInternalParam (0, 1.0f);     // Square: strong envelope
    slot.setInternalParam (1, 1.0f);     // 1 unison voice
    slot.setInternalParam (6, 6000.0f);  // open filter
    slot.setInternalParam (12, 0.0f);    // delay wet off
    slot.setInternalParam (14, 0.0f);    // reverb wet on dry off
    slot.setInternalParam (15, 0.0f);    // reverb wet on delay off
    slot.setInternalParam (18, 0.0f);    // phaser depth off
    slot.setInternalParam (19, 1.0f);    // unity out
    slot.setInternalParam (20, 1.0f);    // 1/8

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    const int held[] = { 36, 43, 48 };
    const int switchBlock = 300;   // ~6.97 beats: rate 1/8 -> 1/4 mid-render
    const int stopBlock   = (int) std::ceil (20.5 / kBeatsPerBlock) + 4;
    std::vector<float> samples;

    for (int b = 0; b < stopBlock; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : held)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

        slot.process (buffer, midi);
        const auto* p = buffer.getReadPointer (0);
        for (int s = 0; s < kBlockSize; ++s)
            samples.push_back (p[s]);

        if (b == switchBlock)
            slot.setInternalParam (20, 2.0f);   // live: 1/8 -> 1/4
    }

    double peak = 0.0;
    for (float v : samples)
        peak = std::max (peak, (double) std::fabs (v));
    ASSERT_GT (peak, 0.3) << "expected a strong dry arp signal through the slot";

    const auto onsets = detectOnsets (samples, kBeatsPerSample, 0.05, 20.5);
    ASSERT_GE ((int) onsets.size(), 15);

    // Steady-rate regions only (skip the one-step transition window).
    std::vector<double> early, late;
    for (double t : onsets)
    {
        if (t >= 2.0 && t <= 6.0)   early.push_back (t);
        if (t >= 10.0 && t <= 20.0) late.push_back (t);
    }

    ASSERT_GE ((int) early.size(), 6);
    for (double t : early)
        EXPECT_LT (gridDistanceFor (t, 0.5), 0.06) << "early phase must run 1/8";
    for (double d : sortedSpacings (early))
        EXPECT_NEAR (d, 0.5, 0.06);

    ASSERT_GE ((int) late.size(), 8);
    for (double t : late)
        EXPECT_LT (gridDistanceFor (t, 1.0), 0.06) << "late phase must run 1/4";
    for (double d : sortedSpacings (late))
        EXPECT_NEAR (d, 1.0, 0.06);
}
