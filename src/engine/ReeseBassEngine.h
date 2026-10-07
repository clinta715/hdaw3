#pragma once
// ReeseBassEngine — detuned-supersaw reese / neuro bass with a tempo-synced
// wobble LFO. Global namespace (like GrowlBassEngine / DrumSynthEngine).
//
// Signal path (per note slot):
//   N detuned unison oscillators (PolyBLEP saw / square, naive triangle)
//     [+ hard-sync crossfade]  [+ sub sine]        -> panned stereo sum
//   -> stereo feedback comb (Comb Amount > 0 only)
//   -> stereo drive (4 curves, velocity + LFO drive, Drive Mix)
//   -> stereo TPT SVF (LP / HP / BP) with filter env + key track + LFO cutoff
//   -> amp envelope
//   -> per-channel memoryless soft ceiling (knee 1.0, span 0.5) -> Output Level
//
// Realtime discipline: prepare() allocates nothing (all state is fixed arrays),
// render() allocates nothing, locks nothing, formats no strings and never calls
// juce::Random. There is no stochastic source anywhere, so two fresh engines
// fed the identical MIDI stream are bit-identical. prepare(sampleRate, 0) is
// the deferred-reset entry point and, since nothing here is heap-shaped, it
// can never shrink a live slot's scratch.
//
// PINNED PARAMETER LAYOUT — EXACTLY 40 rows, in this order (index, name,
// default, min, max). Indices 0..39 are FROZEN at ship: TrackFXSlot derives its
// advertised table from paramDefs() below, so renumbering would silently
// repoint every existing project's saved values. Only APPEND.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

class ReeseBassEngine
{
public:
    static constexpr int kNumParams     = 40;
    static constexpr int kMaxVoices     = 7;     // unison saws inside ONE note slot
    static constexpr int kMaxNoteSlots  = 2;     // note slots used by poly mode
    static constexpr int kMaxHeldNotes  = 128;   // fixed held-note stack (no heap)
    static constexpr int kCombBufLen    = 4096;  // per-channel comb delay line

    // 0=Saw, 1=Square, 2=Triangle (Triangle is naive: it has no step).
    enum class OscShape { Saw = 0, Square, Triangle, NumShapes };
    // 0=Tanh, 1=Atan, 2=Hard, 3=Fold.
    enum class DriveCurve { Tanh = 0, Atan, Hard, Fold, NumCurves };
    // 0=LP, 1=HP, 2=BP (the verified InternalFilter mapping).
    enum class FilterMode { LowPass = 0, HighPass, BandPass, NumModes };
    // 0=Sine, 1=Tri, 2=Square, 3=SawDown.
    enum class LfoShape { Sine = 0, Triangle, Square, SawDown, NumShapes };
    enum class EnvStage { Idle = 0, Attack, Decay, Sustain, Release };

    struct ParamDef { const char* name; float def; float min; float max; };

    // The SINGLE source of truth for the 40 rows: TrackFXSlot derives its
    // advertised InternalParamDef list from this table (InternalFilter
    // precedent) and timbre-lib/build_device_map.py parses it straight out of
    // this header, so the DSP's clamp and the advertised surface cannot drift.
    static const std::array<ParamDef, kNumParams>& paramDefs()
    {
        static const std::array<ParamDef, kNumParams> defs = { {
            { "Voice Count",        7.0f,    1.0f,      7.0f },   // unit: scalar (integer)
            { "Detune Cents",      20.0f,    0.0f,    100.0f },   // unit: cents
            { "Stereo Spread",      0.5f,    0.0f,      1.0f },   // unit: scalar
            { "Osc Shape",          0.0f,    0.0f,      2.0f },   // unit: enum (0=Saw, 1=Square, 2=Triangle)
            { "Phase Scatter",      0.5f,    0.0f,      1.0f },   // unit: scalar
            { "Sub Level",          0.3f,    0.0f,      1.0f },   // unit: scalar
            { "Sub Octave",        -1.0f,   -2.0f,      0.0f },   // unit: scalar (integer octave offset)
            { "Sync Amount",        0.0f,    0.0f,      1.0f },   // unit: scalar
            { "Sync Ratio",         1.0f,    1.0f,      4.0f },   // unit: ratio
            { "Comb Amount",        0.0f,    0.0f,      1.0f },   // unit: scalar
            { "Comb Frequency",    80.0f,   20.0f,   2000.0f },   // unit: hz
            { "Comb Feedback",      0.7f,    0.0f,      0.95f },  // unit: scalar
            { "Drive dB",           6.0f,    0.0f,     40.0f },   // unit: db
            { "Drive Type",         0.0f,    0.0f,      3.0f },   // unit: enum (0=Tanh, 1=Atan, 2=Hard, 3=Fold)
            { "Drive Mix",          1.0f,    0.0f,      1.0f },   // unit: scalar
            { "Filter Cutoff",   1200.0f,   20.0f,  20000.0f },   // unit: hz
            { "Filter Res",         2.0f,    0.1f,     20.0f },   // unit: scalar
            { "Filter Type",        0.0f,    0.0f,      2.0f },   // unit: enum (0=LP, 1=HP, 2=BP)
            { "Filter Env Amt",     0.4f,    0.0f,      1.0f },   // unit: scalar
            { "Filter Key Track",   0.0f,    0.0f,      1.0f },   // unit: scalar
            { "Filter Attack",      0.01f,   0.001f,    2.0f },   // unit: seconds
            { "Filter Decay",       0.4f,    0.001f,    5.0f },   // unit: seconds
            { "Filter Sustain",     0.3f,    0.0f,      1.0f },   // unit: scalar
            { "Filter Release",     0.2f,    0.001f,    5.0f },   // unit: seconds
            { "Amp Attack",         0.005f,  0.001f,    2.0f },   // unit: seconds
            { "Amp Decay",          0.2f,    0.001f,    5.0f },   // unit: seconds
            { "Amp Sustain",        0.85f,   0.0f,      1.0f },   // unit: scalar
            { "Amp Release",        0.08f,   0.001f,    5.0f },   // unit: seconds
            { "Output Level",       0.35f,   0.0f,      1.0f },   // unit: scalar
            { "LFO Shape",          0.0f,    0.0f,      3.0f },   // unit: enum (0=Sine, 1=Tri, 2=Square, 3=SawDown)
            { "LFO Rate (beats)",   1.0f,    0.0625f,   8.0f },   // unit: beats
            { "LFO Sync",           1.0f,    0.0f,      1.0f },   // unit: bool (1=tempo sync, 0=Hz)
            { "LFO Cutoff Amt",     0.0f,    0.0f,      1.0f },   // unit: scalar
            { "LFO Pitch Amt",      0.0f,    0.0f,      1.0f },   // unit: scalar
            { "LFO Drive Amt",      0.0f,    0.0f,      1.0f },   // unit: scalar
            { "LFO Phase",          0.0f,    0.0f,      1.0f },   // unit: scalar
            { "Glide",              0.0f,    0.0f,      2.0f },   // unit: seconds
            { "Mono Legato",        1.0f,    0.0f,      1.0f },   // unit: bool (1=mono legato, 0=poly)
            { "Pitch Bend Range",   2.0f,    0.0f,     12.0f },   // unit: scalar (semitones)
            { "Velocity->Drive",    0.3f,    0.0f,      1.0f },   // unit: scalar
        } };
        return defs;
    }

    // jlimit into the row's [min,max]; an out-of-range index returns `value`
    // unchanged (never a table subscript).
    static float clampParam(int index, float value)
    {
        if (index < 0 || index >= kNumParams) return value;
        const auto& d = paramDefs()[(size_t) index];
        return juce::jlimit(d.min, d.max, value);
    }

    // Case-sensitive name -> index; -1 when unknown.
    static int paramIndexForName(const char* name) noexcept;

    // Integer-valued rows (clamped AND rounded on read): 0,3,6,13,17,29,31,37.
    static bool paramIsInteger(int index) noexcept
    {
        switch (index)
        {
            case 0: case 3: case 6: case 13: case 17: case 29: case 31: case 37:
                return true;
            default:
                return false;
        }
    }

    ReeseBassEngine();
    ~ReeseBassEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Project tempo, for the tempo-synced LFO. Called every block from the
    // audio thread (Track::processBlock -> TrackFXSlot::setTempo), so bpm_ is a
    // plain double and NOT atomic: writer and reader are the same thread.
    void setTempo(double bpm) noexcept { bpm_ = bpm; }

    // Lock-free param surface (message thread -> audio thread via atomics).
    void setParam(int index, float value) noexcept;
    float getParam(int index) const noexcept;

    // Sounding note slots (a releasing slot still counts until its amp
    // envelope reaches idle).
    int activeVoiceCount() const noexcept;

    // ── Test-only, pure ──────────────────────────────────────────────────────
    // Pitch of note slot `voiceIndex` in Hz, or 0 when that slot is inactive.
    float currentHzForTest(int voiceIndex) const noexcept;

private:
    // ── Two-envelope state (amp: params 24..27, filter: params 20..23) ──────
    struct Env
    {
        float level = 0.0f;         // 0..1
        float releaseStart = 0.0f;  // level when Release began (linear ramp ref)
        EnvStage stage = EnvStage::Idle;

        bool isIdle() const noexcept { return stage == EnvStage::Idle; }
        // Starts (or restarts) the attack FROM THE CURRENT LEVEL: a retrigger
        // cannot click, and the legato path (which never calls this) keeps the
        // envelope running.
        void trigger() noexcept { stage = EnvStage::Attack; }
        void release() noexcept
        {
            if (stage != EnvStage::Idle)
            {
                releaseStart = level;
                stage = EnvStage::Release;
            }
        }
    };

    // ── One note slot ───────────────────────────────────────────────────────
    struct Voice
    {
        bool  active = false;
        bool  keyDown = false;
        int   note = -1;
        int   lastNote = -1;
        float velocity = 0.0f;      // 0..127 as received
        float hz = 0.0f;            // glided current frequency (un-bent)
        float targetHz = 0.0f;
        float glideSamplesRemaining = 0.0f;

        float phases[kMaxVoices] = {};      // free-running unison phases
        float syncPhases[kMaxVoices] = {};  // hard-synced unison phases
        float masterPhase = 0.0f;           // sync master (hz * SyncRatio)
        float subPhase = 0.0f;
        float lfoPhase = 0.0f;              // 0..1, param 35 offsets it on read

        Env amp;
        Env filter;

        // Stereo comb delay lines + stereo SVF states (the pan runs BEFORE the
        // chain, so each channel carries its own state).
        float combL[kCombBufLen] = {};
        float combR[kCombBufLen] = {};
        int   combWrite = 0;
        float svfL[2] = { 0.0f, 0.0f };
        float svfR[2] = { 0.0f, 0.0f };
    };

    // ── Per-block atomic snapshot: exactly ONE load per param per block ──────
    struct ParamSnapshot
    {
        int   voiceCount = 7;
        float detuneCents = 20.0f;
        float stereoSpread = 0.5f;
        int   oscShape = 0;
        float phaseScatter = 0.5f;
        float subLevel = 0.3f;
        int   subOctave = -1;
        float syncAmount = 0.0f;
        float syncRatio = 1.0f;
        float combAmount = 0.0f;
        float combFrequency = 80.0f;
        float combFeedback = 0.7f;
        float driveDb = 6.0f;
        int   driveType = 0;
        float driveMix = 1.0f;
        float filterCutoff = 1200.0f;
        float filterRes = 2.0f;
        int   filterType = 0;
        float filterEnvAmt = 0.4f;
        float filterKeyTrack = 0.0f;
        float filterAttack = 0.01f;
        float filterDecay = 0.4f;
        float filterSustain = 0.3f;
        float filterRelease = 0.2f;
        float ampAttack = 0.005f;
        float ampDecay = 0.2f;
        float ampSustain = 0.85f;
        float ampRelease = 0.08f;
        float outputLevel = 0.35f;
        int   lfoShape = 0;
        float lfoRate = 1.0f;
        int   lfoSync = 1;
        float lfoCutoffAmt = 0.0f;
        float lfoPitchAmt = 0.0f;
        float lfoDriveAmt = 0.0f;
        float lfoPhase = 0.0f;
        float glide = 0.0f;
        int   monoLegato = 1;
        float pitchBendRange = 2.0f;
        float velocityDrive = 0.3f;

        // ── Derived once per block (from the rows above + the audio-thread
        //    sample rate / tempo), so the per-sample path pays no repeated
        //    transcendental work and no repeated atomic load. ────────────────
        float detuneRatio[kMaxVoices] = {};  // 2^(detuneCents_i / 1200)
        float pan[kMaxVoices] = {};          // -StereoSpread .. +StereoSpread
        float subRatio = 0.5f;               // 2^SubOctave
        int   combDelay = 1;                 // clamped [1, kCombBufLen-1]
        float ampAttackStep = 0.0f;          // 1 / max(1, Attack * sr)
        float ampDecayStep = 0.0f;           // (1-Sustain) / max(1, Decay * sr)
        float ampReleaseScale = 0.0f;        // 1 / max(1, Release * sr)
        float filterAttackStep = 0.0f;
        float filterDecayStep = 0.0f;
        float filterReleaseScale = 0.0f;
        float lfoInc = 0.0f;                 // cycles/sample
        bool  syncOn = false;                // Sync Amount > 0
        bool  combOn = false;                // Comb Amount > 0
        bool  lfoOn = false;                 // any of the three LFO amounts != 0
    };

    // ── DSP helpers (per-sample, allocation-free, file-local in the .cpp) ────
    void  buildSnapshot(ParamSnapshot& p) const noexcept;
    // Standard ADSR step. Both envelopes share this: Release ramps DOWN from
    // the level captured when Release began, so the release time is honoured
    // exactly whatever level the envelope was at.
    void  advanceEnv(Env& e, float attackStep, float decayStep,
                     float sustain, float releaseScale) noexcept;
    // Leave Release without a jump — see the legato note-on in noteOn().
    void  unRelease(Env& e, float sustain) noexcept;
    void  noteOn(int note, int velocity, const ParamSnapshot& p) noexcept;
    void  noteOff(int note, const ParamSnapshot& p) noexcept;
    void  allNotesOff() noexcept;
    void  pushHeld(int note) noexcept;
    void  popHeld(int note) noexcept;
    int   mostRecentHeld() const noexcept;
    void  startVoice(Voice& v, int note, int velocity, const ParamSnapshot& p) noexcept;
    void  retargetVoice(Voice& v, int note, int velocity, const ParamSnapshot& p) noexcept;
    int   allocateSlot(const ParamSnapshot& p) noexcept;
    bool  renderVoiceSample(Voice& v, const ParamSnapshot& p, float& outL, float& outR) noexcept;

    // ── Atomic parameters (message-thread writes, audio-thread reads) ────────
    std::array<std::atomic<float>, kNumParams> params_ {};

    // ── Audio-thread-only state ──────────────────────────────────────────────
    double sampleRate_ = 44100.0;
    double bpm_ = 120.0;
    float  bendRatio_ = 1.0f;

    Voice voices_[kMaxNoteSlots] {};
    int   heldNotes_[kMaxHeldNotes] = {};
    int   heldCount_ = 0;
};
