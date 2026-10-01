#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// DrumSynthEngine — independently implemented, no third-party source vendored.
// Per-sample, realtime-safe 11-voice TR-909-style analog drum kit. Global
// namespace (like FmSynthEngine).
//
// PINNED PARAMETER LAYOUT — EXACTLY 51 rows, in this order
// (index, name, default, min, max):
//
//   0 Output Level      0.8    0.0 .. 1.5
//   1 Kit Tune          0.0  -24.0 .. 24.0   (semitones)
//   2 Decay Scale       1.0    0.1 .. 4.0
//   3 Accent            0.5    0.0 .. 1.0
//   4 Voice             0.0    0.0 .. 10.0   (integer)
//   5 Note Map          0.0    0.0 .. 1.0    (0=Fixed, 1=GM)
//   6 Key Track         1.0    0.0 .. 1.0
//   per instrument i = 0..10, base = 7 + i*4:
//     base+0 Instrument Level  0.8   0.0 .. 1.5
//     base+1 Instrument Tune   0.0  -24.0 .. 24.0
//     base+2 Instrument Decay  0.5   0.0 .. 1.0
//     base+3 Instrument Tone   0.5   0.0 .. 1.0
//
// Instrument indices (pinned order):
//   0 Kick  1 Snare  2 Clap  3 Rim  4 TomLow  5 TomMid  6 TomHigh
//   7 ClosedHat  8 OpenHat  9 Crash  10 Ride
//
// Note resolution:
//   Note Map = 0 (Fixed): every incoming note triggers instrument `Voice`.
//   Note Map = 1 (GM):    see instrumentForNote() — the pinned GM table.
//
// Pitch model (pitched voices: Kick, TomLow, TomMid, TomHigh):
//   freq = baseFreq * 2^((KeyTrack * (note - baseNote) + kitTune + instTune)/12)
//   clamped to 20..20000 Hz. Base table (baseNote, baseFreq Hz):
//   Kick (45, 55.0), TomLow (45, 90.0), TomMid (45, 125.0), TomHigh (45, 170.0).
//   Unpitched voices (Snare, Clap, Rim, ClosedHat, OpenHat, Crash, Ride) ignore
//   the incoming note for pitch and use only the Tune params.
//
// Determinism: each voice's noise xorshift32 is seeded from a pure function of
// (instrument, note) only — never a counter, pointer, time, or juce::Random.
// ─────────────────────────────────────────────────────────────────────────────
class DrumSynthEngine
{
public:
    static constexpr int kNumInstruments = 11;
    static constexpr int kNumParams      = 51;

    enum Instrument { Kick = 0, Snare, Clap, Rim, TomLow, TomMid, TomHigh,
                      ClosedHat, OpenHat, Crash, Ride };

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setOutputLevel(float v) noexcept;              // param 0
    void setKitTune(float semitones) noexcept;          // param 1
    void setDecayScale(float v) noexcept;               // param 2
    void setAccent(float v) noexcept;                   // param 3
    void setVoice(int v) noexcept;                      // param 4 (clamp 0..10)
    void setNoteMap(int v) noexcept;                    // param 5 (0=Fixed, 1=GM)
    void setKeyTrack(float v) noexcept;                 // param 6
    void setInstrumentLevel(int inst, float v) noexcept;        // param 7 + i*4 + 0
    void setInstrumentTune(int inst, float semitones) noexcept; // param 7 + i*4 + 1
    void setInstrumentDecay(int inst, float v) noexcept;        // param 7 + i*4 + 2
    void setInstrumentTone(int inst, float v) noexcept;         // param 7 + i*4 + 3

    int activeVoiceCount() const noexcept;

    // test-only / pure helpers
    static int   instrumentForNote(int note, int fallbackVoice) noexcept;
    int          lastTriggerInstrumentForTest() const noexcept;
    float        lastTriggerHzForTest() const noexcept;
    bool         instrumentVoiceActiveForTest(int inst) const noexcept;

private:
    // ── Two voice slots per instrument: slot 0 = the sounding voice, slot 1 =
    //    the 2 ms declick tail of the voice it retriggered (linear fade). ──
    struct Voice
    {
        bool  active = false;
        int   note = 60;
        int   velocity = 0;
        float amp = 0.0f;             // velocity amplitude (accent-shaped)
        float env = 0.0f;             // main amp envelope
        float envCoef = 0.9f;         // per-sample exponential decay
        float attackSamples = 1.0f;   // ~1 ms linear attack
        float attackPos = 0.0f;
        float phaseA = 0.0f;
        float phaseB = 0.0f;
        std::array<float, 6> metalPhase {};   // 6-oscillator "metal" source
        float pitchEnv = 0.0f;        // 0..1, decays to 0
        float pitchEnvCoef = 0.9f;
        float clickEnv = 0.0f;        // transient / burst envelope
        float clickCoef = 0.9f;
        float freq = 55.0f;           // Hz (pitched) or reporting Hz (unpitched)
        float tuneMul = 1.0f;         // 2^((kitTune + instTune)/12)
        float tone = 0.5f;
        float noiseMix = 0.0f;        // Crash/Ride noise blend
        float noiseLp = 0.0f;         // one-pole lowpass state
        float hpLp = 0.0f;            // one-pole highpass (as LP) state
        float svfLow = 0.0f;          // Chamberlin SVF states
        float svfBand = 0.0f;
        float svfF = 0.3f;            // SVF frequency coefficient (precomputed)
        float svfQ = 0.85f;           // SVF damping (1/Q)
        int   burst = 0;              // Clap: bursts still to fire
        float burstTimer = 0.0f;      // Clap: samples until the next burst
        float burstEnv = 0.0f;
        float burstSpacing = 1.0f;
        uint32_t rng = 0x9E3779B9u;   // xorshift32, seeded from (inst, note)
        int   releaseTotal = 0;       // declick tail length (0 = none)
        int   releaseRemaining = 0;
    };

    struct ParamSnapshot
    {
        float outputLevel = 0.8f;
        float kitTune = 0.0f;
        float decayScale = 1.0f;
        float accent = 0.5f;
        int   voice = 0;
        int   noteMap = 0;
        float keyTrack = 1.0f;
        std::array<float, kNumInstruments> level {};
        std::array<float, kNumInstruments> tune {};
        std::array<float, kNumInstruments> decay {};
        std::array<float, kNumInstruments> tone {};
    };

    double sampleRate_ = 44100.0;
    float  srF_ = 44100.0f;
    int    tailSamples_ = 88;         // 2 ms
    float  attackSamples_ = 44.0f;    // 1 ms
    float  hpCoef_ = 0.17f;           // one-pole highpass coefficient

    std::array<std::array<Voice, 2>, kNumInstruments> voices_ {};

    std::atomic<float> outputLevel_ { 0.8f };
    std::atomic<float> kitTune_ { 0.0f };
    std::atomic<float> decayScale_ { 1.0f };
    std::atomic<float> accent_ { 0.5f };
    std::atomic<int>   voice_ { 0 };
    std::atomic<int>   noteMap_ { 0 };
    std::atomic<float> keyTrack_ { 1.0f };

    std::array<std::atomic<float>, kNumInstruments> instLevel_ {
        0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
    std::array<std::atomic<float>, kNumInstruments> instTune_ {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<std::atomic<float>, kNumInstruments> instDecay_ {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    std::array<std::atomic<float>, kNumInstruments> instTone_ {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    // Written on the audio thread, read by tests after render (non-realtime use).
    int   lastTriggerInst_ = -1;
    float lastTriggerHz_ = 0.0f;

    void  buildSnapshot(ParamSnapshot& p) const noexcept;
    void  resetVoices() noexcept;
    void  trigger(int inst, int note, int velocity, const ParamSnapshot& p) noexcept;
    void  startVoice(Voice& v, int inst, int note, int velocity, const ParamSnapshot& p) noexcept;
    void  chokeOpenHat() noexcept;
    void  allVoicesOff() noexcept;
    float renderSample(const ParamSnapshot& p) noexcept;
    float renderVoice(int inst, Voice& v) noexcept;
    float voiceSample(int inst, Voice& v) noexcept;
};
