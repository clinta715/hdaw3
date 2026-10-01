#include "engine/DrumSynthEngine.h"

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Independent implementation of a TR-909-flavoured drum kit using standard
// drum-synthesis techniques. No source code is vendored from any third-party
// project. Per-sample, no allocation, no locks, no strings, no file I/O
// anywhere reachable from render(). Every voice's noise source is a xorshift32
// seeded from a pure function of (instrument, note), so two prepare()+render()
// runs with identical input are bit-identical.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.283185307179586f;

// Envelope floor / "decay time" reference: an exponential whose per-sample
// coefficient is built from ln(kEnvFloor)/samples reaches kEnvFloor after the
// requested duration, so Instrument Decay maps to an audible decay time.
constexpr float kEnvFloor = 1.0e-3f;
constexpr float kLnEnvFloor = -6.907755278982137f;   // ln(1e-3)

// Metal (hi-hat / cymbal) partial bank. SIX mutually INHARMONIC square
// partials, DERIVED here rather than taken from any third-party voice: a
// geometric series at a major-third ratio, f_k = 2500 * 1.25^k for k = 0..5,
// i.e. 2500, 3125, 3906, 4883, 6104, 7630 Hz. The 1.25 ratio keeps every pair
// of partials non-integer-related, so the bank rings as a clangorous metal
// instead of collapsing to a pitched tone. The six-oscillator count and the
// downstream 6-bit quantisation are public TR-909 circuit lore (the cymbal
// source ROM is 6-bit PCM); no value here is copied from any source file.
constexpr float kMetalFreqs[6] = { 2500.0f, 3125.0f, 3906.0f, 4883.0f, 6104.0f, 7630.0f };
constexpr float kMetalDetune[6] = { -0.0080f, +0.0055f, -0.0040f, +0.0070f, -0.0060f, +0.0030f };

// Kit soft ceiling. The eleven voices are summed into ONE slot, so a full-kit
// unison hit reaches ~3.56x unity at DEFAULT levels (measured), while a single
// kick sits at a raw sum of ~0.79 — no linear trim can serve both (bounding the
// unison linearly would need ~0.27, dropping one kick to ~0.2). The knee is
// therefore placed ABOVE one and two voices: a single default kick passes
// through bit-identically, and only 3+ simultaneous voices engage the soft
// region, which asymptotes to kCeilingKnee + kCeilingSpan = 0.95.
// The ceiling sits BEFORE outputLevel, so Output Level keeps its full 0..1.5
// range (a deliberate overdrive can still exceed unity — the user's choice).
constexpr float kCeilingKnee = 0.80f;   // linear at or below this
constexpr float kCeilingSpan = 0.15f;   // soft region: knee .. knee+span (0.95)

inline float kitCeiling(float x) noexcept
{
    const float a = std::abs(x);
    if (a <= kCeilingKnee)
        return x;
    const float shaped = kCeilingKnee
        + kCeilingSpan * (1.0f - std::exp(-(a - kCeilingKnee) / kCeilingSpan));
    return (x < 0.0f) ? -shaped : shaped;
}

inline float clampUnit(float v) noexcept
{
    return std::clamp(v, 0.0f, 1.0f);
}

inline float clampFreq(float hz) noexcept
{
    return std::clamp(hz, 20.0f, 20000.0f);
}

inline float wrapPhase(float p) noexcept
{
    if (p >= 1.0f)
        p -= 1.0f;
    else if (p < 0.0f)
        p += 1.0f;
    return p;
}

// xorshift32 -> [-1, 1). Pure, branch-free, deterministic.
inline float xorshiftNext(uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
}

// Per-sample multiplier of an exponential that reaches kEnvFloor after
// `seconds`.
inline float decayCoef(double sampleRate, float seconds) noexcept
{
    const double samples = static_cast<double>(seconds) * sampleRate;
    if (samples <= 1.0)
        return 0.0f;
    return static_cast<float>(std::exp(static_cast<double>(kLnEnvFloor) / samples));
}

// Pitched voices are Kick(0), TomLow(4), TomMid(5), TomHigh(6).
inline bool isPitched(int inst) noexcept
{
    return inst == 0 || inst == 4 || inst == 5 || inst == 6;
}

inline float pitchedBaseFreq(int inst) noexcept
{
    switch (inst)
    {
        case 0:  return 55.0f;    // Kick
        case 4:  return 90.0f;    // TomLow
        case 5:  return 125.0f;   // TomMid
        case 6:  return 170.0f;   // TomHigh
        default: return 0.0f;
    }
}

// Base (nominal) decay time per instrument, before Instrument Decay and the
// global Decay Scale.
inline float baseDecaySeconds(int inst) noexcept
{
    switch (inst)
    {
        case 0:  return 0.400f;   // Kick
        case 1:  return 0.200f;   // Snare
        case 2:  return 0.220f;   // Clap (tail after the bursts)
        case 3:  return 0.035f;   // Rim
        case 4:
        case 5:
        case 6:  return 0.450f;   // Toms
        case 7:  return 0.075f;   // ClosedHat
        case 8:  return 0.420f;   // OpenHat
        case 9:  return 1.600f;   // Crash
        default: return 1.100f;   // Ride
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

void DrumSynthEngine::prepare(double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;   // fully per-sample; no block-sized scratch needed

    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    srF_ = static_cast<float>(sampleRate_);
    tailSamples_ = std::max(1, static_cast<int>(0.002 * sampleRate_));      // 2 ms
    attackSamples_ = std::max(1.0f, static_cast<float>(0.001 * sampleRate_)); // 1 ms
    hpCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1200.0 / sampleRate_));

    resetVoices();
}

void DrumSynthEngine::resetVoices() noexcept
{
    for (auto& slots : voices_)
        for (auto& v : slots)
            v = Voice{};

    lastTriggerInst_ = -1;
    lastTriggerHz_ = 0.0f;
}

// ── Param setters (message thread -> audio thread via relaxed atomics) ───────

void DrumSynthEngine::setOutputLevel(float v) noexcept
{
    outputLevel_.store(std::clamp(v, 0.0f, 1.5f), std::memory_order_relaxed);
}

void DrumSynthEngine::setKitTune(float semitones) noexcept
{
    kitTune_.store(std::clamp(semitones, -24.0f, 24.0f), std::memory_order_relaxed);
}

void DrumSynthEngine::setDecayScale(float v) noexcept
{
    decayScale_.store(std::clamp(v, 0.1f, 4.0f), std::memory_order_relaxed);
}

void DrumSynthEngine::setAccent(float v) noexcept
{
    accent_.store(clampUnit(v), std::memory_order_relaxed);
}

void DrumSynthEngine::setVoice(int v) noexcept
{
    voice_.store(std::clamp(v, 0, kNumInstruments - 1), std::memory_order_relaxed);
}

void DrumSynthEngine::setNoteMap(int v) noexcept
{
    noteMap_.store(std::clamp(v, 0, 1), std::memory_order_relaxed);
}

void DrumSynthEngine::setKeyTrack(float v) noexcept
{
    keyTrack_.store(clampUnit(v), std::memory_order_relaxed);
}

void DrumSynthEngine::setInstrumentLevel(int inst, float v) noexcept
{
    if (inst < 0 || inst >= kNumInstruments)
        return;
    instLevel_[(size_t) inst].store(std::clamp(v, 0.0f, 1.5f), std::memory_order_relaxed);
}

void DrumSynthEngine::setInstrumentTune(int inst, float semitones) noexcept
{
    if (inst < 0 || inst >= kNumInstruments)
        return;
    instTune_[(size_t) inst].store(std::clamp(semitones, -24.0f, 24.0f), std::memory_order_relaxed);
}

void DrumSynthEngine::setInstrumentDecay(int inst, float v) noexcept
{
    if (inst < 0 || inst >= kNumInstruments)
        return;
    instDecay_[(size_t) inst].store(clampUnit(v), std::memory_order_relaxed);
}

void DrumSynthEngine::setInstrumentTone(int inst, float v) noexcept
{
    if (inst < 0 || inst >= kNumInstruments)
        return;
    instTone_[(size_t) inst].store(clampUnit(v), std::memory_order_relaxed);
}

// ── Inspection / pure helpers ────────────────────────────────────────────────

int DrumSynthEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& slots : voices_)
        for (const auto& v : slots)
            if (v.active)
                ++count;
    return count;
}

int DrumSynthEngine::lastTriggerInstrumentForTest() const noexcept
{
    return lastTriggerInst_;
}

float DrumSynthEngine::lastTriggerHzForTest() const noexcept
{
    return lastTriggerHz_;
}

bool DrumSynthEngine::instrumentVoiceActiveForTest(int inst) const noexcept
{
    if (inst < 0 || inst >= kNumInstruments)
        return false;
    return voices_[(size_t) inst][0].active || voices_[(size_t) inst][1].active;
}

int DrumSynthEngine::instrumentForNote(int note, int fallbackVoice) noexcept
{
    switch (note)
    {
        case 35: case 36: return Kick;
        case 37:          return Rim;
        case 38: case 40: return Snare;
        case 39:          return Clap;
        case 41: case 43: return TomLow;
        case 45: case 47: return TomMid;
        case 48: case 50: return TomHigh;
        case 42: case 44: return ClosedHat;
        case 46:          return OpenHat;
        case 49: case 57: return Crash;
        case 51: case 59: return Ride;
        default:          return std::clamp(fallbackVoice, 0, kNumInstruments - 1);
    }
}

// ── Triggering ───────────────────────────────────────────────────────────────

void DrumSynthEngine::buildSnapshot(ParamSnapshot& p) const noexcept
{
    p.outputLevel = outputLevel_.load(std::memory_order_relaxed);
    p.kitTune = kitTune_.load(std::memory_order_relaxed);
    p.decayScale = decayScale_.load(std::memory_order_relaxed);
    p.accent = accent_.load(std::memory_order_relaxed);
    p.voice = voice_.load(std::memory_order_relaxed);
    p.noteMap = noteMap_.load(std::memory_order_relaxed);
    p.keyTrack = keyTrack_.load(std::memory_order_relaxed);

    for (int i = 0; i < kNumInstruments; ++i)
    {
        p.level[(size_t) i] = instLevel_[(size_t) i].load(std::memory_order_relaxed);
        p.tune[(size_t) i] = instTune_[(size_t) i].load(std::memory_order_relaxed);
        p.decay[(size_t) i] = instDecay_[(size_t) i].load(std::memory_order_relaxed);
        p.tone[(size_t) i] = instTone_[(size_t) i].load(std::memory_order_relaxed);
    }
}

void DrumSynthEngine::chokeOpenHat() noexcept
{
    // Classic 909 CH -> OH choke: force the open hat into a 2 ms release.
    for (int s = 0; s < 2; ++s)
    {
        Voice& v = voices_[(size_t) OpenHat][(size_t) s];
        if (v.active && v.releaseTotal == 0)
        {
            v.releaseTotal = tailSamples_;
            v.releaseRemaining = tailSamples_;
        }
    }
}

void DrumSynthEngine::allVoicesOff() noexcept
{
    for (auto& slots : voices_)
        for (auto& v : slots)
            if (v.active && v.releaseTotal == 0)
            {
                v.releaseTotal = tailSamples_;
                v.releaseRemaining = tailSamples_;
            }
}

void DrumSynthEngine::trigger(int inst, int note, int velocity, const ParamSnapshot& p) noexcept
{
    if (inst < 0 || inst >= kNumInstruments || velocity <= 0)
        return;

    if (inst == ClosedHat)
        chokeOpenHat();

    Voice& head = voices_[(size_t) inst][0];
    Voice& tail = voices_[(size_t) inst][1];

    // Declick retrigger: the currently sounding voice slides into the tail slot
    // with a 2 ms linear fade; the new voice starts clean in slot 0.
    if (head.active)
    {
        tail = head;
        tail.releaseTotal = tailSamples_;
        tail.releaseRemaining = tailSamples_;
    }

    startVoice(head, inst, note, velocity, p);

    lastTriggerInst_ = inst;
    lastTriggerHz_ = head.freq;
}

void DrumSynthEngine::startVoice(Voice& v, int inst, int note, int velocity,
                                 const ParamSnapshot& p) noexcept
{
    v = Voice{};
    v.active = true;
    v.note = note;
    v.velocity = velocity;
    v.tone = p.tone[(size_t) inst];

    // Deterministic noise seed: pure function of (instrument, note) only.
    uint32_t seed = 0x9E3779B9u
                  ^ (static_cast<uint32_t>(inst) * 0x85EBCA6Bu)
                  ^ (static_cast<uint32_t>(note) * 0xC2B2AE35u);
    if (seed == 0u)
        seed = 0x9E3779B9u;   // xorshift32 must never be seeded all-zero
    v.rng = seed;

    // Velocity amplitude, accent-shaped: amp = (vel/127)^(2.0 - 1.5*accent).
    const float normVel = clampUnit(static_cast<float>(velocity) / 127.0f);
    v.amp = std::pow(normVel, 2.0f - 1.5f * p.accent);

    v.attackSamples = attackSamples_;
    v.attackPos = 0.0f;
    v.env = 0.0f;

    const float dmul = (0.15f + 1.85f * p.decay[(size_t) inst]) * p.decayScale;
    v.envCoef = decayCoef(sampleRate_, baseDecaySeconds(inst) * dmul);

    const float tuneMul = std::exp2((p.kitTune + p.tune[(size_t) inst]) / 12.0f);
    v.tuneMul = tuneMul;

    if (isPitched(inst))
    {
        const float base = pitchedBaseFreq(inst);
        v.freq = clampFreq(base * std::exp2((p.keyTrack * static_cast<float>(note - 45)
                                             + p.kitTune + p.tune[(size_t) inst]) / 12.0f));
    }

    switch (inst)
    {
        case Kick:
        {
            v.pitchEnv = 1.0f;
            v.pitchEnvCoef = decayCoef(sampleRate_, std::max(0.005f, 0.045f * dmul));
            v.clickEnv = 1.0f;
            v.clickCoef = decayCoef(sampleRate_, 0.008f);
            break;
        }
        case Snare:
        {
            v.freq = 180.0f * tuneMul;   // reporting / body fundamental
            break;
        }
        case Clap:
        {
            v.freq = 1000.0f * tuneMul;
            v.burst = 4;
            v.burstTimer = 0.0f;         // first burst fires on the first sample
            v.burstEnv = 0.0f;
            v.burstSpacing = std::max(1.0f, 0.009f * srF_);   // ~9 ms apart
            v.clickCoef = decayCoef(sampleRate_, 0.004f);
            break;
        }
        case Rim:
        {
            v.freq = 780.0f * tuneMul;
            v.clickEnv = 1.0f;
            v.clickCoef = decayCoef(sampleRate_, 0.006f);
            break;
        }
        case TomLow:
        case TomMid:
        case TomHigh:
        {
            v.pitchEnv = 1.0f;
            v.pitchEnvCoef = decayCoef(sampleRate_, std::max(0.005f, 0.060f * dmul));
            v.clickEnv = 1.0f;
            v.clickCoef = decayCoef(sampleRate_, 0.010f);
            break;
        }
        case ClosedHat:
        case OpenHat:
        case Crash:
        case Ride:
        {
            v.freq = kMetalFreqs[0] * tuneMul;   // reporting fundamental

            const float bandScale = (inst == Crash) ? 1.35f : (inst == Ride) ? 0.75f : 1.0f;
            const float damping   = (inst == Crash) ? 1.10f : (inst == Ride) ? 0.55f : 0.85f;
            v.noiseMix = (inst == Crash) ? 0.45f : (inst == Ride) ? 0.10f : 0.05f;

            // Tone = bandpass centre.
            const float centre = std::clamp(6000.0f * std::exp2(1.6f * (v.tone - 0.5f)) * bandScale,
                                            200.0f, 0.24f * srF_);
            const float f = 2.0f * std::sin(kPi * centre / srF_);
            v.svfF = std::min(f, 0.95f);
            v.svfQ = damping;
            break;
        }
        default:
            break;
    }
}

// ── Per-sample synthesis ─────────────────────────────────────────────────────

float DrumSynthEngine::voiceSample(int inst, Voice& v) noexcept
{
    const float sr = srF_;

    // Generic envelope: ~1 ms linear attack, then exponential decay.
    if (v.attackPos < v.attackSamples)
    {
        v.attackPos += 1.0f;
        v.env = v.attackPos / v.attackSamples;
    }
    else
    {
        v.env *= v.envCoef;
    }

    float out = 0.0f;

    switch (inst)
    {
        case Kick:
        {
            // Sine with an exponential pitch envelope: starts ~2.6x the target
            // frequency and sweeps down to it, plus a short 2nd-harmonic click.
            const float f = v.freq * (1.0f + 1.6f * v.pitchEnv);
            v.phaseA = wrapPhase(v.phaseA + f / sr);
            out = std::sin(kTwoPi * v.phaseA);

            v.phaseB = wrapPhase(v.phaseB + (2.0f * f) / sr);
            out += 0.5f * v.tone * v.clickEnv * std::sin(kTwoPi * v.phaseB);

            v.pitchEnv *= v.pitchEnvCoef;
            v.clickEnv *= v.clickCoef;
            out *= v.env;
            break;
        }
        case Snare:
        {
            // Two body oscillators (~180 / ~330 Hz) + highpassed noise.
            // Tone = noise-vs-body balance.
            const float f1 = 180.0f * v.tuneMul;
            const float f2 = 330.0f * v.tuneMul;
            v.phaseA = wrapPhase(v.phaseA + f1 / sr);
            v.phaseB = wrapPhase(v.phaseB + f2 / sr);

            const float body = 0.6f * std::sin(kTwoPi * v.phaseA)
                             + 0.4f * std::sin(kTwoPi * v.phaseB);

            const float n = xorshiftNext(v.rng);
            v.noiseLp += 0.35f * (n - v.noiseLp);
            const float bright = n - v.noiseLp;

            out = (1.0f - v.tone) * body + v.tone * bright * 1.4f;
            out *= v.env;
            break;
        }
        case Clap:
        {
            // 4 spaced noise bursts (~9 ms apart) + a longer noise tail.
            // Tone = burst brightness (one-pole lowpass coefficient).
            const float n = xorshiftNext(v.rng);
            const float c = 0.25f + 0.55f * v.tone;
            v.noiseLp += c * (n - v.noiseLp);

            if (v.burst > 0)
            {
                if (v.burstTimer <= 0.0f)
                {
                    v.burstEnv = 1.0f;
                    --v.burst;
                    v.burstTimer = v.burstSpacing;
                }
                else
                {
                    v.burstTimer -= 1.0f;
                }
            }
            v.burstEnv *= v.clickCoef;

            out = v.noiseLp * 2.2f * (v.burstEnv + 0.35f * v.env);
            break;
        }
        case Rim:
        {
            // Short tonal click + a tiny noise transient, ~30 ms. 780 Hz is a
            // design choice: it sits between the snare body (180-330 Hz) and
            // the hat band, keeping the kit's spectral slots distinct.
            const float f = 780.0f * v.tuneMul;
            v.phaseA = wrapPhase(v.phaseA + f / sr);
            const float n = xorshiftNext(v.rng);

            out = std::sin(kTwoPi * v.phaseA) + 0.35f * n * v.clickEnv;
            v.clickEnv *= v.clickCoef;
            out *= v.env;
            break;
        }
        case TomLow:
        case TomMid:
        case TomHigh:
        {
            // Triangle with a pitch envelope; Tone = attack click amount.
            const float f = v.freq * (1.0f + 0.5f * v.pitchEnv);
            v.phaseA = wrapPhase(v.phaseA + f / sr);
            const float tri = 1.0f - 4.0f * std::abs(v.phaseA - 0.5f);

            const float n = xorshiftNext(v.rng);
            out = tri + 0.4f * v.tone * v.clickEnv * n;

            v.pitchEnv *= v.pitchEnvCoef;
            v.clickEnv *= v.clickCoef;
            out *= v.env;
            break;
        }
        case ClosedHat:
        case OpenHat:
        case Crash:
        case Ride:
        {
            // 6 detuned square oscillators -> highpass -> bandpass -> 6-bit
            // quantise -> exponential decay.
            float metal = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                const float fk = kMetalFreqs[k] * (1.0f + kMetalDetune[k]) * v.tuneMul;
                v.metalPhase[(size_t) k] = wrapPhase(v.metalPhase[(size_t) k] + fk / sr);
                metal += (v.metalPhase[(size_t) k] < 0.5f) ? 1.0f : -1.0f;
            }
            metal *= (1.0f / 6.0f);

            v.hpLp += hpCoef_ * (metal - v.hpLp);
            const float hp = metal - v.hpLp;

            // Chamberlin state-variable bandpass.
            v.svfLow += v.svfF * v.svfBand;
            const float svfHigh = hp - v.svfLow - v.svfQ * v.svfBand;
            v.svfBand += v.svfF * svfHigh;
            float sig = v.svfBand;

            if (v.noiseMix > 0.0f)
                sig += v.noiseMix * xorshiftNext(v.rng);

            if (inst == Ride)
            {
                // Bell emphasis: two extra tonal partials.
                v.phaseA = wrapPhase(v.phaseA + (4200.0f * v.tuneMul) / sr);
                v.phaseB = wrapPhase(v.phaseB + (6300.0f * v.tuneMul) / sr);
                sig += 0.13f * (std::sin(kTwoPi * v.phaseA) + 0.7f * std::sin(kTwoPi * v.phaseB));
            }

            sig = std::clamp(sig, -1.7f, 1.7f);
            sig = std::floor(sig * 32.0f + 0.5f) * (1.0f / 32.0f);   // 6-bit
            out = sig * v.env * 0.85f;   // headroom trim: one hit stays under unity
            break;
        }
        default:
            break;
    }

    return out;
}

float DrumSynthEngine::renderVoice(int inst, Voice& v) noexcept
{
    float s = voiceSample(inst, v) * v.amp;

    if (v.releaseTotal > 0)
    {
        s *= static_cast<float>(v.releaseRemaining) / static_cast<float>(v.releaseTotal);
        if (--v.releaseRemaining <= 0)
        {
            v.active = false;
            return s;
        }
    }

    if (v.env < kEnvFloor && v.releaseTotal == 0)
        v.active = false;

    return s;
}

float DrumSynthEngine::renderSample(const ParamSnapshot& p) noexcept
{
    float sum = 0.0f;

    for (int inst = 0; inst < kNumInstruments; ++inst)
    {
        auto& slots = voices_[(size_t) inst];
        const bool a = slots[0].active;
        const bool b = slots[1].active;
        if (!a && !b)
            continue;

        float s = 0.0f;
        if (a)
            s += renderVoice(inst, slots[0]);
        if (b)
            s += renderVoice(inst, slots[1]);

        sum += p.level[(size_t) inst] * s;
    }

    return kitCeiling(sum) * p.outputLevel;
}

// ── Render ───────────────────────────────────────────────────────────────────

void DrumSynthEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    if (numSamples <= 0 || numChannels <= 0)
    {
        midi.clear();
        return;
    }

    ParamSnapshot p;
    buildSnapshot(p);

    int samplePos = 0;
    for (const auto metadata : midi)
    {
        const int eventSample = std::clamp(metadata.samplePosition, 0, numSamples);

        for (int i = samplePos; i < eventSample; ++i)
        {
            const float s = renderSample(p);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, s);
        }

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            const int vel = juce::roundToInt(message.getFloatVelocity() * 127.0f);
            const int inst = (p.noteMap == 0) ? p.voice : instrumentForNote(note, p.voice);
            trigger(inst, note, vel, p);   // velocity 0 is not a trigger
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            allVoicesOff();
        }

        samplePos = eventSample;
    }

    for (int i = samplePos; i < numSamples; ++i)
    {
        const float s = renderSample(p);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, s);
    }

    midi.clear();
}
