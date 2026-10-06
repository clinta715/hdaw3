#include "PsyArpEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>

// ============================================================================
// Preparation
// ============================================================================

void PsyArpEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Reset arp state
    arp_ = ArpState{};
    filterSweepPhase_ = 0.0f;
    std::fill(filterState_, filterState_ + 2, 0.0f);

    // Reset the white-noise generator to its seed (deterministic per prepare,
    // same discipline as the other DSP state resets above).
    noiseRng_ = 0x9E3779B9u;

    // Reset oscillator voices
    for (auto& v : oscVoices_)
        v = OscVoice{};

    // Reset phaser
    phaser_ = PhaserState{};

    // Allocate delay buffers (max ~5 seconds at 48kHz)
    const int maxDelaySamples = static_cast<int>(sampleRate * 5.0) + 1;
    delay_.bufferL.resize(maxDelaySamples, 0.0f);
    delay_.bufferR.resize(maxDelaySamples, 0.0f);
    delay_.writePosL = 0;
    delay_.writePosR = 0;

    // Allocate reverb buffers
    // Comb filter delays (in samples) for Schroeder reverb
    const int combDelays[4] = {
        static_cast<int>(0.0297 * sampleRate),
        static_cast<int>(0.0371 * sampleRate),
        static_cast<int>(0.0411 * sampleRate),
        static_cast<int>(0.0437 * sampleRate)
    };
    for (int i = 0; i < 4; ++i)
    {
        reverb_.combBuffer[i].resize(combDelays[i] + 1, 0.0f);
        reverb_.combPos[i] = 0;
    }
    // Allpass delays
    const int allpassDelays[2] = {
        static_cast<int>(0.0053 * sampleRate),
        static_cast<int>(0.0017 * sampleRate)
    };
    for (int i = 0; i < 2; ++i)
    {
        reverb_.allpassBuffer[i].resize(allpassDelays[i] + 1, 0.0f);
        reverb_.allpassPos[i] = 0;
    }
}

// ============================================================================
// Arp sequence builder
// ============================================================================

void PsyArpEngine::rebuildArpSequence()
{
    arp_.sequence.clear();
    if (arp_.heldNotes.empty()) return;

    // Collect sorted base notes
    std::vector<int> base(arp_.heldNotes.begin(), arp_.heldNotes.end());
    std::sort(base.begin(), base.end());
    if (base.empty())
        return;  // defense in depth: every caller guards, but the modulo
                 // branches below divide by base.size()

    // Upper clamp: the param def range is 1..4; a raw/legacy value (set via
    // setOctaveRange bypassing the slot clamp) must not grow the sequence
    // unbounded. Lower clamp mirrors std::max(1, ...) below.
    const int octaves = juce::jlimit(1, 8, octaveRange_.load(std::memory_order_relaxed));
    const auto shape = static_cast<PatternShape>(patternShape_.load(std::memory_order_relaxed));

    switch (shape)
    {
        case PatternShape::UpDown:
        {
            // Classic up-down across octaves
            for (int oct = 0; oct < octaves; ++oct)
                for (int n : base)
                    arp_.sequence.push_back(n + 12 * oct);
            // Append reverse (skip first and last to avoid repetition)
            if (arp_.sequence.size() > 2)
            {
                std::vector<int> rev(arp_.sequence.rbegin(), arp_.sequence.rend());
                arp_.sequence.insert(arp_.sequence.end(),
                    rev.begin() + 1, rev.end() - 1);
            }
            break;
        }

        case PatternShape::Asymmetric332:
        {
            // Asymmetric 3+3+2 grouping within 16ths — the rolling,
            // hypnotic feel. Build a pattern of indices into the note set
            // that creates an off-square rhythm.
            for (int oct = 0; oct < octaves; ++oct)
            {
                const int baseIdx = static_cast<int>(arp_.sequence.size());
                const int noteCount = static_cast<int>(base.size());

                // 3+3+2 grouping: indices into the note array
                // Pattern: 0,1,2, 0,1,2, 0,1 (8 steps per octave)
                static const int pat332[] = { 0, 1, 2, 0, 1, 2, 0, 1 };
                for (int step = 0; step < 8; ++step)
                {
                    int noteIdx = pat332[step] % noteCount;
                    arp_.sequence.push_back(base[noteIdx] + 12 * oct);
                }
                juce::ignoreUnused(baseIdx);
            }
            break;
        }

        case PatternShape::Random:
        {
            // Random walk through notes across octaves
            std::mt19937 rng(static_cast<unsigned>(
                std::chrono::steady_clock::now().time_since_epoch().count()));
            for (int i = 0; i < octaves * 8; ++i)
            {
                int oct = i % octaves;
                // rng() yields uint32: casting FIRST wraps half of all draws to
                // negative ints, and C++ % keeps the dividend's sign, so
                // base[noteIdx] subscripted a negative index (MSVC debug:
                // <vector> "vector subscript out of range" assert; release:
                // OOB read -> garbage pitches). Modulo in the unsigned domain
                // instead; base.size() <= 128 so the int cast stays exact.
                int noteIdx = static_cast<int>(rng() % base.size());
                arp_.sequence.push_back(base[noteIdx] + 12 * oct);
            }
            break;
        }

        default:
            // Fallback: simple up
            for (int oct = 0; oct < octaves; ++oct)
                for (int n : base)
                    arp_.sequence.push_back(n + 12 * oct);
            break;
    }
}

// ============================================================================
// Oscillator waveforms
// ============================================================================

// Memoryless soft ceiling for the summed oscillator signal (lesson 45). It is
// applied ONLY when Noise Level > 0, so the default noise-free path never
// calls it and stays bit-identical to the pre-change render. The knee sits at
// 1.0: one voice at full scale passes untouched, and only noise-augmented sums
// above unity are shaped (asymptote 1.5). Same exponential shape as the
// drum_synth kit ceiling.
namespace
{
inline float softCeilOsc(float x) noexcept
{
    constexpr float kKnee = 1.0f;
    constexpr float kSpan = 0.5f;
    const float a = std::abs(x);
    if (a <= kKnee)
        return x;
    const float shaped = kKnee + kSpan * (1.0f - std::exp(-(a - kKnee) / kSpan));
    return (x < 0.0f) ? -shaped : shaped;
}

// PolyBLEP residual. `t` is the normalized phase in [0,1), `dt` the phase
// increment (cycles/sample). The discontinuity sits at the phase wrap
// (t = 0 == 1): the residual is a 1-sample quadratic rounded over the two
// samples adjacent to the step.
inline float polyBlep(float t, float dt) noexcept
{
    if (dt <= 0.0f)
        return 0.0f;

    if (t < dt)
    {
        // Just after the step (t = 0): rising edge correction.
        t /= dt;
        return 2.0f * t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        // Just before the step (t -> 1): falling edge correction. `t` is
        // negative here (t in (1-dt, 1) -> (t-1) in (-dt, 0)).
        t = (t - 1.0f) / dt;
        return t * t + 2.0f * t + 1.0f;
    }
    return 0.0f;
}
} // namespace

// xorshift32 -> [-1, 1). Pure, branch-free, deterministic, allocation-free.
float PsyArpEngine::whiteNoise() noexcept
{
    noiseRng_ ^= noiseRng_ << 13;
    noiseRng_ ^= noiseRng_ >> 17;
    noiseRng_ ^= noiseRng_ << 5;
    return static_cast<float>(static_cast<int32_t>(noiseRng_)) * (1.0f / 2147483648.0f);
}

float PsyArpEngine::generateOscillator(float phase, float dt, OscShape shape)
{
    float p = std::fmod(phase, 1.0f);
    if (p < 0.0f) p += 1.0f;

    // Slice C: Saw/Square/Pulse/SuperSaw carry a step discontinuity and are
    // band-limited IN PLACE with PolyBLEP (non-additive — naive aliasing is a
    // defect, not a feature; no opt-out branch). `dt` is the voice's phase
    // increment, so the correction is correct at every pitch.
    switch (shape)
    {
        case OscShape::Saw:
            return 2.0f * p - 1.0f - polyBlep(p, dt);

        case OscShape::Square:
            return (p < 0.5f ? 1.0f : -1.0f)
                 + polyBlep(p, dt)
                 - polyBlep(std::fmod(p + 0.5f, 1.0f), dt);

        case OscShape::SuperSaw:
        {
            // 7 detuned saws summed (Juno-style supersaw approximation). The
            // detune is a phase OFFSET, so all 7 share the SAME `dt` — each
            // saw gets its own PolyBLEP at its own wrapped phase.
            static constexpr int kNumVoices = 7;
            static constexpr float detuneAmounts[kNumVoices] = {
                -0.12f, -0.06f, -0.02f, 0.0f, 0.02f, 0.06f, 0.12f
            };
            float sum = 0.0f;
            for (int i = 0; i < kNumVoices; ++i)
            {
                float shifted = std::fmod(p + detuneAmounts[i], 1.0f);
                if (shifted < 0.0f) shifted += 1.0f;
                sum += 2.0f * shifted - 1.0f - polyBlep(shifted, dt);
            }
            return sum / static_cast<float>(kNumVoices);
        }

        case OscShape::Pulse:
        {
            // Variable-duty pulse. Duty is clamped in the setter; the local
            // clamp is defense in depth (lesson 23). Two step edges, so two
            // PolyBLEP residuals: at the phase wrap and at the falling edge
            // (t = duty), reached at wrapped phase frac(p + 1 - duty).
            const float duty = juce::jlimit(0.05f, 0.95f,
                                            pulseWidth_.load(std::memory_order_relaxed));
            return (p < duty ? 1.0f : -1.0f)
                 + polyBlep(p, dt)
                 - polyBlep(std::fmod(p + (1.0f - duty), 1.0f), dt);
        }

        case OscShape::Noise:
            return whiteNoise();

        default:
            return 2.0f * p - 1.0f - polyBlep(p, dt);
    }
}

// ============================================================================
// SVF filter (LP / HP / BP by mode)
// ============================================================================

float PsyArpEngine::processSVF(float input, float cutoff, float resonance, float* state, int mode)
{
    const float g = std::tan(juce::MathConstants<float>::pi
        * std::min(cutoff / static_cast<float>(sampleRate_), 0.49f));
    const float k = 2.0f - 2.0f / resonance;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    float v3 = input - state[1];
    float v1 = a1 * state[0] + a2 * v3;
    float v2 = state[1] + a2 * state[0] + a3 * v3;
    state[0] = 2.0f * v1 - state[0];
    state[1] = 2.0f * v2 - state[1];

    // The solve above is the same in every mode; only the returned
    // combination differs. v1 is the bandpass state and v2 the lowpass state
    // (v1 = bp, v2 = lp in InternalFilter's algebra), so HP is
    // input - k*bp - lp — the VERIFIED InternalFilter mapping (do not
    // "improve" it). Mode 0 returns v2 verbatim, so the default patch is
    // bit-identical to the pre-change lowpass-only return.
    switch (mode)
    {
        case 1: return input - k * v1 - v2;  // highpass
        case 2: return v1;                   // bandpass
        default: return v2;                  // lowpass
    }
}

// ============================================================================
// Ping-pong delay
// ============================================================================

void PsyArpEngine::processDelay(float& inL, float& inR, float& outL, float& outR)
{
    // Convert delay time from beats to samples
    const float samplesPerBeat = static_cast<float>(sampleRate_ * 60.0) / static_cast<float>(bpm_);
    delay_.delaySamples = static_cast<int>(delayTimeBeats_.load(std::memory_order_relaxed) * samplesPerBeat);

    const float wetLevel = delayWetLevel_.load(std::memory_order_relaxed);
    const float feedback = delayFeedback_.load(std::memory_order_relaxed);
    const float pingPong = delayPingPongWidth_.load(std::memory_order_relaxed);

    if (wetLevel <= 0.001f || delay_.bufferL.empty()
        || delay_.bufferR.size() != delay_.bufferL.size())
    {
        outL = inL;
        outR = inR;
        return;
    }

    const int bufLen = static_cast<int>(delay_.bufferL.size());
    const int delaySamps = std::max(1, std::min(delay_.delaySamples, bufLen - 1));

    // Read from delay buffers
    int readPosL = (delay_.writePosL - delaySamps + bufLen) % bufLen;
    int readPosR = (delay_.writePosR - delaySamps + bufLen) % bufLen;
    float delayedL = delay_.bufferL[readPosL];
    float delayedR = delay_.bufferR[readPosR];

    // Cross-feed for ping-pong: L→R and R→L
    float crossL = delayedL * (1.0f - pingPong) + delayedR * pingPong;
    float crossR = delayedR * (1.0f - pingPong) + delayedL * pingPong;

    // Write into delay buffers (input + feedback)
    delay_.bufferL[delay_.writePosL] = inL + crossL * feedback;
    delay_.bufferR[delay_.writePosR] = inR + crossR * feedback;

    // Advance write positions
    delay_.writePosL = (delay_.writePosL + 1) % bufLen;
    delay_.writePosR = (delay_.writePosR + 1) % bufLen;

    // Mix dry + wet
    outL = inL * (1.0f - wetLevel) + crossL * wetLevel;
    outR = inR * (1.0f - wetLevel) + crossR * wetLevel;
}

// ============================================================================
// Reverb (simple Schroeder-style)
// ============================================================================

void PsyArpEngine::processReverb(float inL, float inR, float& outL, float& outR)
{
    const float wetOnDry = reverbWetOnDry_.load(std::memory_order_relaxed);
    const float wetOnDelay = reverbWetOnDelay_.load(std::memory_order_relaxed);

    // Use wetOnDelay for delay-tap signal, wetOnDry for dry signal
    float mix = std::max(wetOnDry, wetOnDelay);

    if (mix <= 0.001f)
    {
        outL = inL;
        outR = inR;
        return;
    }

    // The comb/allpass buffers are allocated in prepare(). A render() that
    // reaches here without a prepare() (or with a 0 Hz prepare) would
    // subscript EMPTY vectors (MSVC debug: "vector subscript out of range"
    // assert in <vector> operator[]) and divide by zero in the position
    // modulo — bail out to dry signal instead (same contract as
    // processDelay's empty guard).
    if (reverb_.combBuffer[0].empty() || reverb_.combBuffer[1].empty()
        || reverb_.combBuffer[2].empty() || reverb_.combBuffer[3].empty()
        || reverb_.allpassBuffer[0].empty() || reverb_.allpassBuffer[1].empty())
    {
        outL = inL;
        outR = inR;
        return;
    }

    float mono = (inL + inR) * 0.5f;

    // Comb filters (with feedback)
    static constexpr float combFeedback = 0.84f;
    float combOut = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        int& pos = reverb_.combPos[i];
        auto& buf = reverb_.combBuffer[i];
        int len = static_cast<int>(buf.size());

        float sample = buf[pos];
        buf[pos] = mono + sample * combFeedback;
        pos = (pos + 1) % len;
        combOut += sample;
    }
    combOut *= 0.25f;

    // Allpass filters
    static constexpr float allpassFeedback = 0.5f;
    float apOut = combOut;
    for (int i = 0; i < 2; ++i)
    {
        int& pos = reverb_.allpassPos[i];
        auto& buf = reverb_.allpassBuffer[i];
        int len = static_cast<int>(buf.size());

        float sample = buf[pos];
        buf[pos] = apOut + sample * allpassFeedback;
        apOut = sample - apOut * allpassFeedback;
        pos = (pos + 1) % len;
    }

    // Stereo spread
    reverb_.mixL = reverb_.mixL * 0.99f + apOut * 0.01f;
    reverb_.mixR = reverb_.mixR * 0.99f + apOut * 0.01f;

    outL = inL * (1.0f - mix) + (inL + reverb_.mixL) * mix;
    outR = inR * (1.0f - mix) + (inR + reverb_.mixR) * mix;
}

// ============================================================================
// Phaser (simplified 4-stage)
// ============================================================================

float PsyArpEngine::processPhaser(float input, PhaserState& state, float lfoPhase)
{
    const float rate = phaserRateHz_.load(std::memory_order_relaxed);
    const float depth = phaserDepth_.load(std::memory_order_relaxed);

    if (depth <= 0.001f) return input;

    // LFO: triangle wave for smooth sweep
    float lfo = std::fmod(lfoPhase, 1.0f);
    if (lfo < 0.0f) lfo += 1.0f;
    lfo = 4.0f * std::abs(lfo - 0.5f) - 1.0f;  // bipolar -1..1
    lfo *= depth;

    // Centre frequency modulated by LFO
    float centreFreq = 1000.0f * std::pow(2.0f, lfo * 2.0f); // ±2 octaves

    // 4-stage allpass phaser
    float output = input;
    for (int i = 0; i < 4; ++i)
    {
        float freq = centreFreq * std::pow(2.0f, static_cast<float>(i) * 0.5f);
        float g = std::tan(juce::MathConstants<float>::pi
            * std::min(freq / static_cast<float>(sampleRate_), 0.49f));
        float a1 = 1.0f / (1.0f + g);
        float v = a1 * (output + state.stage[i]);
        state.stage[i] = output - g * v;
        output = v;
    }

    // Wet/dry mix (phaser at 50%)
    return input * 0.5f + output * 0.5f;
}

// ============================================================================
// Inspection
// ============================================================================

int PsyArpEngine::activeNoteCount() const noexcept
{
    return static_cast<int>(arp_.heldNotes.size());
}

// ============================================================================
// Main render loop
// ============================================================================

void PsyArpEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Read atomic params once per block
    // Clamp the shape to the def range (0..4): a raw/hand-edited value past
    // Noise must not select an undefined waveform (the switch's default
    // already falls back to saw, but clamping keeps the enum honest).
    const auto oShape = static_cast<OscShape>(juce::jlimit(0, 4,
        oscShape_.load(std::memory_order_relaxed)));
    const int uniVoices = juce::jlimit(1, 4, oscUnisonVoices_.load(std::memory_order_relaxed));
    const float uniDetune = oscDetuneCents_.load(std::memory_order_relaxed);
    const float filtCutoff = filterCutoffHz_.load(std::memory_order_relaxed);
    const float filtRes = std::max(0.1f, filterResonance_.load(std::memory_order_relaxed));
    const float sweepBars = std::max(0.5f, filterSweepBars_.load(std::memory_order_relaxed));
    const int filtMode = juce::jlimit(0, 2, filterMode_.load(std::memory_order_relaxed));
    const float noiseLevel = juce::jlimit(0.0f, 1.0f, noiseLevel_.load(std::memory_order_relaxed));
    const float outLevel = outputLevel_.load(std::memory_order_relaxed);
    const bool usePhaser = phaserEnabled_.load(std::memory_order_relaxed);
    const bool noiseActive = noiseLevel > 0.0f;

    // Get transport info for tempo sync
    // (bpm_ is set externally or defaults to 120)
    const float beatsPerSample = static_cast<float>(bpm_) / 60.0f
        / static_cast<float>(sampleRate_);

    // ── Process MIDI: update held notes ──
    for (const auto metadata : midi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            arp_.heldNotes.insert(msg.getNoteNumber());
            arp_.channel = msg.getChannel();
            arp_.sequenceDirty = true;
        }
        else if (msg.isNoteOff())
        {
            arp_.heldNotes.erase(msg.getNoteNumber());
            arp_.sequenceDirty = true;
        }
    }
    midi.clear();  // We consume all MIDI — this is a synth, not a pass-through

    // ── Arp pattern advancement ──
    // Advance the arp sequencer based on transport beat position.
    // Step rate division (param 20): 0 = 1/16 (0.25 beats, default for
    // backward compat), 1 = 1/8 (0.5), 2 = 1/4 (1.0). Drives BOTH the grid
    // step clock and the 80% gate release below. Read once per block.
    static constexpr float kStepRateBeats[3] = { 0.25f, 0.5f, 1.0f };
    const float arpRateBeats =
        kStepRateBeats[juce::jlimit(0, 2,
            arpStepRateIndex_.load(std::memory_order_relaxed))];
    const float currentBeat = static_cast<float>(arp_.lastBeat);
    const float blockEndBeat = currentBeat + static_cast<float>(numSamples) * beatsPerSample;

    // Rebuild the sequence whenever the held-note set changed (chord change,
    // including abutting off==on boundaries within one block), or whenever the
    // sequence is somehow empty while notes are held. Previously the sequence
    // was only built once (on the first chord) and then went stale: later
    // chords kept playing the first chord's pitches, and a stale seqIndex
    // survived rebuilds that shrank the sequence. Rebuilds happen at most
    // once per block, on the audio thread, same allocation pattern as before.
    if (arp_.sequenceDirty || (arp_.sequence.empty() && !arp_.heldNotes.empty()))
    {
        arp_.sequenceDirty = false;
        rebuildArpSequence();
        // Re-anchor: a rebuild can shrink the sequence while seqIndex is
        // larger than its new size.
        if (arp_.sequence.empty())
            arp_.seqIndex = 0;
        else
            arp_.seqIndex %= static_cast<int>(arp_.sequence.size());
    }

    // ── Per-sample synthesis ──
    buffer.clear();

    // Filter sweep LFO: slow, independent of note clock
    const float sweepInc = (1.0f / (sweepBars * 4.0f))  // 4 beats per bar
        / static_cast<float>(sampleRate_) * static_cast<float>(numSamples);

    float filterPhase = filterSweepPhase_;

    for (int s = 0; s < numSamples; ++s)
    {
        // ── Arp step check ──
        float sampleBeat = currentBeat + static_cast<float>(s) * beatsPerSample;

        // Grid-locked arp step clock. Steps are derived from the transport
        // beat position (floor(sampleBeat / arpRateBeats)), NOT accumulated
        // from the first trigger: a chord landing mid-16th (off-grid) must
        // not phase-shift every subsequent step off the transport grid.
        // Step boundaries sit at k * arpRateBeats; a step triggers exactly
        // when floor(sampleBeat / arpRateBeats) changes.
        if (!arp_.sequence.empty() && arp_.heldNotes.empty())
        {
            // All notes released — stop the arp voice so the oscillator
            // silences (delay/reverb tails decay naturally). The old code
            // left currentNote sounding forever here.
            arp_.currentNote = -1;
        }
        else if (!arp_.sequence.empty())
        {
            // Release the current note when its 80% gate expires. Checked
            // every sample — the old code only evaluated the release when a
            // new step fired, so notes sustained through the gate gap.
            if (arp_.currentNote >= 0 && sampleBeat >= arp_.noteOffBeat)
                arp_.currentNote = -1;

            const double beatPos = static_cast<double>(sampleBeat);
            if (std::isfinite(beatPos))
            {
                const long long stepIndex = static_cast<long long>(
                    std::floor(beatPos / static_cast<double>(arpRateBeats)));

                if (!arp_.stepClockStarted)
                {
                    // Arm the clock on the current grid cell WITHOUT
                    // triggering: the first note snaps to the NEXT grid
                    // boundary, never mid-grid (no phase shift from an
                    // off-grid chord start).
                    arp_.stepClockStarted = true;
                    arp_.lastStepIndex = stepIndex;
                }
                else if (stepIndex != arp_.lastStepIndex)
                {
                    arp_.lastStepIndex = stepIndex;

                    // Advance to the next step and trigger the new note.
                    arp_.seqIndex = (arp_.seqIndex + 1)
                        % static_cast<int>(arp_.sequence.size());
                    // Defensive: no path may subscript sequence out of range,
                    // even if a future edit breaks the rebuild invariants
                    // (MSVC debug: "vector subscript out of range" assert).
                    arp_.seqIndex = std::clamp(arp_.seqIndex, 0,
                        static_cast<int>(arp_.sequence.size()) - 1);

                    arp_.currentNote = arp_.sequence[arp_.seqIndex];
                    // 80% gate: release at 0.8 steps into the grid cell.
                    arp_.noteOffBeat = (static_cast<double>(stepIndex) + 0.8)
                        * static_cast<double>(arpRateBeats);
                }
            }
        }

        // ── Oscillator ──
        float oscSample = 0.0f;
        if (arp_.currentNote >= 0)
        {
            float midiFreq = 440.0f * std::pow(2.0f,
                (static_cast<float>(arp_.currentNote) - 69.0f) / 12.0f);

            for (int v = 0; v < uniVoices; ++v)
            {
                float detuneCents = 0.0f;
                if (uniVoices > 1)
                    detuneCents = uniDetune * (static_cast<float>(v)
                        - (uniVoices - 1) * 0.5f);

                float freq = midiFreq * std::pow(2.0f, detuneCents / 1200.0f);
                float phaseInc = freq / static_cast<float>(sampleRate_);

                oscVoices_[v].phase += phaseInc;
                if (oscVoices_[v].phase >= 1.0f)
                    oscVoices_[v].phase -= 1.0f;

                // Slice C: `phaseInc` is the PolyBLEP increment (`dt`) for
                // this voice — the exact value added to the phase above.
                oscSample += generateOscillator(oscVoices_[v].phase, phaseInc, oShape);
            }
            oscSample /= static_cast<float>(uniVoices);

            // ── Additive noise layer (param 23) ──
            // Noise Level mixes per-sample white noise onto the oscillator sum
            // on ANY shape, gated by the note like the oscillator. Only
            // engaged when > 0, so the default noise-free path is untouched
            // (bit-identical render). The memoryless soft ceiling caps the sum
            // once the noise-augmented level can exceed unity (lesson 45: knee
            // above the normal-voice range) — inside this branch for the same
            // reason.
            if (noiseActive)
            {
                oscSample += noiseLevel * whiteNoise();
                oscSample = softCeilOsc(oscSample);
            }
        }

        // ── Filter (slow sweep — the signature element) ──
        // Sweep LFO: triangle wave, cycle length = sweepBars * 4 beats
        float sweepLfo = std::fmod(filterPhase, 1.0f);
        if (sweepLfo < 0.0f) sweepLfo += 1.0f;
        sweepLfo = 4.0f * std::abs(sweepLfo - 0.5f) - 1.0f; // -1..1

        // Map sweep to cutoff: centre at filtCutoff, sweep ±2 octaves
        float sweepCutoff = filtCutoff * std::pow(2.0f, sweepLfo * 2.0f);
        sweepCutoff = std::max(20.0f, std::min(sweepCutoff,
            static_cast<float>(sampleRate_) * 0.49f));

        float filtered = processSVF(oscSample, sweepCutoff, filtRes, filterState_, filtMode);

        // ── Phaser ──
        float phased = filtered;
        if (usePhaser)
        {
            phaser_.lfoPhase += phaserRateHz_.load(std::memory_order_relaxed)
                / static_cast<float>(sampleRate_);
            if (phaser_.lfoPhase >= 1.0f) phaser_.lfoPhase -= 1.0f;
            phased = processPhaser(filtered, phaser_, phaser_.lfoPhase);
        }

        // ── Delay ──
        float delayInL = phased;
        float delayInR = phased;
        float delayOutL = 0.0f, delayOutR = 0.0f;
        processDelay(delayInL, delayInR, delayOutL, delayOutR);

        // ── Reverb ──
        float reverbOutL = 0.0f, reverbOutR = 0.0f;
        processReverb(delayOutL, delayOutR, reverbOutL, reverbOutR);

        // ── Output ──
        if (numChannels >= 2)
        {
            buffer.addSample(0, s, reverbOutL * outLevel);
            buffer.addSample(1, s, reverbOutR * outLevel);
        }
        else if (numChannels == 1)
        {
            buffer.addSample(0, s, (reverbOutL + reverbOutR) * 0.5f * outLevel);
        }

        // Advance filter sweep phase
        filterPhase += sweepInc;
    }

    filterSweepPhase_ = filterPhase;
    arp_.lastBeat = blockEndBeat;
}
