#include "engine/ReeseBassEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.283185307179586f;

// ── Memoryless soft ceiling, applied to EACH channel's summed voice signal
//    (lesson 45: an N-voice instrument summed into ONE slot cannot be bounded
//    linearly — bounding the 7-saw unison would bury a single detuned saw).
//    The knee sits at 1.0: a voice at unit scale passes through untouched and
//    only the sum above unity is shaped, asymptoting to knee + span = 1.5.
//    Output Level sits AFTER it, so the level param keeps its full 0..1 range
//    (a deliberate overdrive can still exceed unity — the user's choice). ──
inline float softCeilVoice(float x) noexcept
{
    constexpr float kKnee = 1.0f;
    constexpr float kSpan = 0.5f;
    const float a = std::abs(x);
    if (a <= kKnee)
        return x;
    const float shaped = kKnee + kSpan * (1.0f - std::exp(-(a - kKnee) / kSpan));
    return (x < 0.0f) ? -shaped : shaped;
}

// ── PolyBLEP residual (copied verbatim from PsyArpEngine.cpp — it is
//    file-local there; the comment style is kept for the same reason). `t` is
//    the normalized phase in [0,1), `dt` the phase increment (cycles/sample).
//    The discontinuity sits at the phase wrap (t = 0 == 1): the residual is a
//    1-sample quadratic rounded over the two samples adjacent to the step. ──
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

// ── Unison oscillator. 0 = Saw, 1 = Square (both PolyBLEP band-limited),
//    2 = Triangle (naive: it carries no step discontinuity). `phase` is the
//    raw accumulator and is wrapped here; `dt` is the EXACT increment the
//    caller adds afterwards, which is what PolyBLEP needs. ──
inline float oscSampleAt(int shape, float phase, float dt) noexcept
{
    float p = std::fmod(phase, 1.0f);
    if (p < 0.0f) p += 1.0f;

    switch (shape)
    {
        case 1:
            return (p < 0.5f ? 1.0f : -1.0f)
                 + polyBlep(p, dt)
                 - polyBlep(std::fmod(p + 0.5f, 1.0f), dt);

        case 2:
            return 1.0f - 4.0f * std::abs(p - 0.5f);

        default:
            return 2.0f * p - 1.0f - polyBlep(p, dt);
    }
}

// ── Wobble LFO shapes (0=Sine, 1=Tri, 2=Square, 3=SawDown). Tri and SawDown
//    are phase-shifted so every shape leaves 0 at phase 0, which makes LFO
//    Phase (param 35) mean the same thing on every shape. ──
inline float lfoShapeValue(int shape, float phase) noexcept
{
    float p = std::fmod(phase, 1.0f);
    if (p < 0.0f) p += 1.0f;

    switch (shape)
    {
        case 1:
        {
            float q = p + 0.25f;
            q -= std::floor(q);
            return 1.0f - 4.0f * std::abs(q - 0.5f);
        }
        case 2:  return (p < 0.5f) ? 1.0f : -1.0f;
        case 3:  return 1.0f - 2.0f * p;
        default: return std::sin(p * kTwoPi);
    }
}

// ── Triangle wavefold into [-1,1] by repeated reflection (Drive Type 3). ──
inline float foldSample(float x) noexcept
{
    float t = std::fmod(x, 4.0f);
    if (t < 0.0f) t += 4.0f;
    if (t <= 1.0f) return t;
    if (t <= 3.0f) return 2.0f - t;
    return t - 4.0f;
}

// ── Waveshaper (Drive Type 0..3), then the dry/wet Drive Mix blend. A mix of
//    0 returns `x` bit-exactly (0*shaped + 1*x), so Drive Mix is a true bypass
//    control. ──
inline float driveSample(int type, float x, float mix) noexcept
{
    float shaped = 0.0f;

    switch (type)
    {
        case 1:
        {
            // k = 2/pi: the curve saturates at k*atan(inf/k) = k*(pi/2) = 1.
            constexpr float k = 0.6366197723675814f;
            shaped = k * std::atan(x / k);
            break;
        }
        case 2:  shaped = juce::jlimit(-1.0f, 1.0f, x); break;
        case 3:  shaped = foldSample(x); break;
        default: shaped = std::tanh(x); break;
    }

    return mix * shaped + (1.0f - mix) * x;
}

// ── TPT state-variable filter — the PsyArpEngine solve, copied as-is (the
//    comment there calls it the VERIFIED InternalFilter mapping; do not
//    re-derive). `mode` selects the returned combination from the SAME solve:
//    0 = LP, 1 = HP, 2 = BP. `sr` is passed in because this is a free function.
//    HP is input - k*bp - lp: v1 is the bandpass state and v2 the lowpass
//    state, so mode 0 returns v2 verbatim. ──
inline float processSvfTpt(float input, float cutoff, float resonance,
                           float* state, int mode, float sr) noexcept
{
    const float g = std::tan(kPi * std::min(cutoff / sr, 0.49f));
    const float k = 2.0f - 2.0f / resonance;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    const float v3 = input - state[1];
    const float v1 = a1 * state[0] + a2 * v3;
    const float v2 = state[1] + a2 * state[0] + a3 * v3;
    state[0] = 2.0f * v1 - state[0];
    state[1] = 2.0f * v2 - state[1];

    switch (mode)
    {
        case 1: return input - k * v1 - v2;  // highpass
        case 2: return v1;                   // bandpass
        default: return v2;                  // lowpass
    }
}

inline float midiNoteToHz(int note) noexcept
{
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

// Float-parameter rows are read straight; the eight integer-valued rows are
// clamped AND rounded (paramIsInteger lists them).
inline float readParam(const std::array<std::atomic<float>, ReeseBassEngine::kNumParams>& p,
                       int index) noexcept
{
    return p[(size_t) index].load(std::memory_order_relaxed);
}

} // namespace

// ============================================================================
// Construction / preparation
// ============================================================================

ReeseBassEngine::ReeseBassEngine()
{
    const auto& defs = paramDefs();
    for (int i = 0; i < kNumParams; ++i)
        params_[(size_t) i].store(defs[(size_t) i].def, std::memory_order_relaxed);
}

// All allocation happens here — and there is none: every buffer in this engine
// is a fixed-size member (the comb lines are inline arrays inside Voice). A
// deferred reset calls prepare(sampleRate_, 0), so a sampleRate <= 0 keeps the
// current rate rather than clobbering it, and nothing sized here can ever
// shrink. Every piece of DSP state is reset, so two prepare()+render() passes
// over the same engine are bit-identical.
void ReeseBassEngine::prepare(double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;

    for (auto& v : voices_)
        v = Voice {};

    heldCount_ = 0;
    bendRatio_ = 1.0f;
}

// ============================================================================
// Parameter surface
// ============================================================================

void ReeseBassEngine::setParam(int index, float value) noexcept
{
    if (index < 0 || index >= kNumParams)
        return;                       // ignore out-of-range index entirely

    const float clamped = clampParam(index, value);
    const float stored = paramIsInteger(index)
        ? static_cast<float>(std::lround(clamped))
        : clamped;
    params_[(size_t) index].store(stored, std::memory_order_relaxed);
}

float ReeseBassEngine::getParam(int index) const noexcept
{
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return params_[(size_t) index].load(std::memory_order_relaxed);
}

int ReeseBassEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& v : voices_)
        if (v.active)
            ++count;
    return count;
}

float ReeseBassEngine::currentHzForTest(int voiceIndex) const noexcept
{
    if (voiceIndex < 0 || voiceIndex >= kMaxNoteSlots)
        return 0.0f;
    const Voice& v = voices_[voiceIndex];
    // The SOUNDING pitch: the glide-integrated frequency times the current
    // pitch-bend ratio.
    return v.active ? v.hz * bendRatio_ : 0.0f;
}

int ReeseBassEngine::paramIndexForName(const char* name) noexcept
{
    if (name == nullptr)
        return -1;

    const auto& defs = paramDefs();
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp(defs[(size_t) i].name, name) == 0)
            return i;
    return -1;
}

// ============================================================================
// Per-block parameter snapshot
// ============================================================================

void ReeseBassEngine::buildSnapshot(ParamSnapshot& p) const noexcept
{
    p.voiceCount     = juce::jlimit(1, kMaxVoices, juce::roundToInt(readParam(params_, 0)));
    p.detuneCents    = readParam(params_, 1);
    p.stereoSpread   = readParam(params_, 2);
    p.oscShape       = juce::jlimit(0, 2, juce::roundToInt(readParam(params_, 3)));
    p.phaseScatter   = readParam(params_, 4);
    p.subLevel       = readParam(params_, 5);
    p.subOctave      = juce::jlimit(-2, 0, juce::roundToInt(readParam(params_, 6)));
    p.syncAmount     = readParam(params_, 7);
    p.syncRatio      = readParam(params_, 8);
    p.combAmount     = juce::jlimit(0.0f, 1.0f, readParam(params_, 9));
    p.combFrequency  = std::max(1.0f, readParam(params_, 10));
    p.combFeedback   = readParam(params_, 11);
    p.driveDb        = readParam(params_, 12);
    p.driveType      = juce::jlimit(0, 3, juce::roundToInt(readParam(params_, 13)));
    p.driveMix       = readParam(params_, 14);
    p.filterCutoff   = readParam(params_, 15);
    p.filterRes      = std::max(0.1f, readParam(params_, 16));
    p.filterType     = juce::jlimit(0, 2, juce::roundToInt(readParam(params_, 17)));
    p.filterEnvAmt   = readParam(params_, 18);
    p.filterKeyTrack = readParam(params_, 19);
    p.filterAttack   = readParam(params_, 20);
    p.filterDecay    = readParam(params_, 21);
    p.filterSustain  = readParam(params_, 22);
    p.filterRelease  = readParam(params_, 23);
    p.ampAttack      = readParam(params_, 24);
    p.ampDecay       = readParam(params_, 25);
    p.ampSustain     = readParam(params_, 26);
    p.ampRelease     = readParam(params_, 27);
    p.outputLevel    = readParam(params_, 28);
    p.lfoShape       = juce::jlimit(0, 3, juce::roundToInt(readParam(params_, 29)));
    p.lfoRate        = readParam(params_, 30);
    p.lfoSync        = juce::jlimit(0, 1, juce::roundToInt(readParam(params_, 31)));
    p.lfoCutoffAmt   = readParam(params_, 32);
    p.lfoPitchAmt    = readParam(params_, 33);
    p.lfoDriveAmt    = readParam(params_, 34);
    p.lfoPhase       = readParam(params_, 35);
    p.glide          = readParam(params_, 36);
    p.monoLegato     = juce::jlimit(0, 1, juce::roundToInt(readParam(params_, 37)));
    p.pitchBendRange = readParam(params_, 38);
    p.velocityDrive  = readParam(params_, 39);

    const float sr = static_cast<float>(sampleRate_);

    // ── Unison spread + pan. detuneCents_i = Detune * (i/(N-1) - 0.5) so the
    //    spread is symmetric around 0 (+/- Detune/2); N == 1 detunes nothing.
    //    Pan is a LINEAR law: panL = 0.5 - 0.5*pan_i, panR = 0.5 + 0.5*pan_i
    //    (chosen over equal-power and documented here). ──
    const int n = p.voiceCount;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        float cents = 0.0f;
        float pan = 0.0f;
        if (n > 1 && i < n)
        {
            const float t = static_cast<float>(i) / static_cast<float>(n - 1);
            cents = p.detuneCents * (t - 0.5f);
            pan = p.stereoSpread * (2.0f * t - 1.0f);
        }
        p.detuneRatio[i] = std::exp2(cents / 1200.0f);
        p.pan[i] = pan;
    }

    p.subRatio = std::exp2(static_cast<float>(p.subOctave));

    // Comb delay length in samples, clamped to the fixed line (4096 samples
    // covers 44100/20 = 2205; at 96 kHz a 20 Hz comb needs 4800 and is clamped
    // to the longest the array holds — documented in the header).
    p.combDelay = juce::jlimit(1, kCombBufLen - 1,
                               static_cast<int>(sr / p.combFrequency));

    // Envelope slopes. Release ramps from the level captured when Release
    // began (Env::releaseStart), so all four times are honoured exactly.
    p.ampAttackStep    = 1.0f / std::max(1.0f, p.ampAttack * sr);
    p.ampDecayStep     = std::max(0.0f, 1.0f - p.ampSustain) / std::max(1.0f, p.ampDecay * sr);
    p.ampReleaseScale  = 1.0f / std::max(1.0f, p.ampRelease * sr);
    p.filterAttackStep   = 1.0f / std::max(1.0f, p.filterAttack * sr);
    p.filterDecayStep    = std::max(0.0f, 1.0f - p.filterSustain) / std::max(1.0f, p.filterDecay * sr);
    p.filterReleaseScale = 1.0f / std::max(1.0f, p.filterRelease * sr);

    // Wobble LFO: tempo-synced (LFO Rate in BEATS) or free-running (Hz).
    p.lfoInc = (p.lfoSync >= 1)
        ? static_cast<float>(static_cast<double>(p.lfoRate) * bpm_ / 60.0) / sr
        : p.lfoRate / sr;

    // The three inert-by-default gates. Each one skips a whole stage, so the
    // shipped defaults render without the sync solve, the comb delay line and
    // the LFO math at all.
    p.syncOn = p.syncAmount > 0.0f;
    p.combOn = p.combAmount > 0.0f;
    p.lfoOn  = (p.lfoCutoffAmt != 0.0f) || (p.lfoPitchAmt != 0.0f) || (p.lfoDriveAmt != 0.0f);
}

// ============================================================================
// Envelope
// ============================================================================

void ReeseBassEngine::advanceEnv(Env& e, float attackStep, float decayStep,
                                 float sustain, float releaseScale) noexcept
{
    switch (e.stage)
    {
        case EnvStage::Attack:
            e.level += attackStep;
            if (e.level >= 1.0f)
            {
                e.level = 1.0f;
                e.stage = EnvStage::Decay;
            }
            break;

        case EnvStage::Decay:
            e.level -= decayStep;
            if (e.level <= sustain)
            {
                e.level = sustain;
                e.stage = EnvStage::Sustain;
            }
            break;

        case EnvStage::Sustain:
            e.level = sustain;
            break;

        case EnvStage::Release:
            e.level -= e.releaseStart * releaseScale;
            if (e.level <= 0.0f)
            {
                e.level = 0.0f;
                e.stage = EnvStage::Idle;
            }
            break;

        case EnvStage::Idle:
        default:
            e.level = 0.0f;
            break;
    }
}

void ReeseBassEngine::unRelease(Env& e, float sustain) noexcept
{
    if (e.stage != EnvStage::Release)
        return;

    // A legato note-on lands on a voice whose key was already released: stop
    // the dying ramp WITHOUT a jump. Below sustain the envelope re-attacks;
    // at or above it, it settles onto the sustain level.
    e.stage = (e.level < sustain) ? EnvStage::Attack : EnvStage::Sustain;
}

// ============================================================================
// Note management
// ============================================================================

void ReeseBassEngine::pushHeld(int note) noexcept
{
    if (heldCount_ < kMaxHeldNotes)
    {
        heldNotes_[heldCount_++] = note;
        return;
    }
    heldNotes_[kMaxHeldNotes - 1] = note;   // full stack: replace the newest
}

void ReeseBassEngine::popHeld(int note) noexcept
{
    for (int i = heldCount_ - 1; i >= 0; --i)
    {
        if (heldNotes_[i] != note)
            continue;

        for (int j = i; j + 1 < heldCount_; ++j)
            heldNotes_[j] = heldNotes_[j + 1];

        --heldCount_;
        return;
    }
}

int ReeseBassEngine::mostRecentHeld() const noexcept
{
    return (heldCount_ > 0) ? heldNotes_[heldCount_ - 1] : -1;
}

void ReeseBassEngine::startVoice(Voice& v, int note, int velocity,
                                 const ParamSnapshot& p) noexcept
{
    const float hz = midiNoteToHz(note);
    const int n = p.voiceCount;

    v.active = true;
    v.keyDown = true;
    v.note = note;
    v.lastNote = note;
    v.velocity = static_cast<float>(juce::jlimit(0, 127, velocity));
    v.hz = hz;
    v.targetHz = hz;
    v.glideSamplesRemaining = 0.0f;
    v.masterPhase = 0.0f;
    v.subPhase = 0.0f;
    v.lfoPhase = 0.0f;    // param 35 (LFO Phase) is added as a read-time offset

    // Deterministic phase scatter: the i-th unison phase starts at
    // PhaseScatter * i/(N-1), so the detuned copies do not all start in phase.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const float ph = p.phaseScatter
            * (static_cast<float>(i) / static_cast<float>(std::max(1, n - 1)));
        v.phases[i] = ph;
        v.syncPhases[i] = ph;
    }

    // A fresh note restarts the filter states; the comb delay line is NOT
    // cleared (a delay line keeps running across notes, exactly like a bus
    // delay — clearing it would be a 32 KB memset on the audio thread).
    v.svfL[0] = v.svfL[1] = 0.0f;
    v.svfR[0] = v.svfR[1] = 0.0f;

    v.amp = Env {};
    v.amp.trigger();
    v.filter = Env {};
    v.filter.trigger();
}

void ReeseBassEngine::retargetVoice(Voice& v, int note, int velocity,
                                    const ParamSnapshot& p) noexcept
{
    if (!v.active)
    {
        startVoice(v, note, velocity, p);
        return;
    }

    v.lastNote = v.note;
    v.note = note;
    v.keyDown = true;
    v.velocity = static_cast<float>(juce::jlimit(0, 127, velocity));

    const float hz = midiNoteToHz(note);
    v.targetHz = hz;

    if (p.glide > 0.0f)
        v.glideSamplesRemaining = std::max(1.0f, p.glide * static_cast<float>(sampleRate_));
    else
    {
        v.hz = hz;                      // Glide 0 snaps
        v.glideSamplesRemaining = 0.0f;
    }

    unRelease(v.amp, p.ampSustain);
    unRelease(v.filter, p.filterSustain);
}

int ReeseBassEngine::allocateSlot(const ParamSnapshot&) noexcept
{
    for (int i = 0; i < kMaxNoteSlots; ++i)
        if (!voices_[i].active)
            return i;

    // Quietest-steal: release tails die first, held sustain notes last.
    int quietest = 0;
    float quietestLevel = 1.0e30f;
    for (int i = 0; i < kMaxNoteSlots; ++i)
    {
        const float level = voices_[i].amp.level
                          * std::max(1.0f, voices_[i].velocity);
        if (level < quietestLevel)
        {
            quietestLevel = level;
            quietest = i;
        }
    }
    return quietest;
}

void ReeseBassEngine::noteOn(int note, int velocity, const ParamSnapshot& p) noexcept
{
    note = juce::jlimit(0, 127, note);
    velocity = juce::jlimit(0, 127, velocity);
    pushHeld(note);

    if (p.monoLegato >= 1)
    {
        Voice& v = voices_[0];
        if (v.active)
            retargetVoice(v, note, velocity, p);   // legato: envelopes keep running
        else
            startVoice(v, note, velocity, p);
        return;
    }

    // Poly: kMaxNoteSlots note slots only (keeps the arithmetic simple — the
    // amp envelope is a per-sample ramp shared with the mono path).
    const int slot = allocateSlot(p);
    startVoice(voices_[slot], note, velocity, p);
}

void ReeseBassEngine::noteOff(int note, const ParamSnapshot& p) noexcept
{
    note = juce::jlimit(0, 127, note);
    popHeld(note);

    if (p.monoLegato >= 1)
    {
        Voice& v = voices_[0];
        if (!v.active || v.note != note)
            return;

        v.keyDown = false;

        const int next = mostRecentHeld();
        if (next >= 0)
            retargetVoice(v, next, juce::roundToInt(v.velocity), p);
        else
        {
            v.amp.release();
            v.filter.release();
        }
        return;
    }

    for (auto& v : voices_)
    {
        if (v.active && v.keyDown && v.note == note)
        {
            v.keyDown = false;
            v.amp.release();
            v.filter.release();
        }
    }
}

void ReeseBassEngine::allNotesOff() noexcept
{
    for (auto& v : voices_)
    {
        v.active = false;
        v.keyDown = false;
        v.amp = Env {};
        v.filter = Env {};
        v.glideSamplesRemaining = 0.0f;
    }
    heldCount_ = 0;
}

// ============================================================================
// Per-sample voice render
// ============================================================================

// Renders one sample of one note slot into outL/outR (pre soft-ceiling, pre
// Output Level). Returns false when the amp envelope reached idle, i.e. the
// slot just died. Allocation-free, lock-free, string-free, deterministic.
bool ReeseBassEngine::renderVoiceSample(Voice& v, const ParamSnapshot& p,
                                        float& outL, float& outR) noexcept
{
    // ── Envelopes: both sample-accurate, both advanced before the audio ──
    advanceEnv(v.amp, p.ampAttackStep, p.ampDecayStep, p.ampSustain, p.ampReleaseScale);
    advanceEnv(v.filter, p.filterAttackStep, p.filterDecayStep,
               p.filterSustain, p.filterReleaseScale);

    const float ampEnv = v.amp.level;
    const float filterEnv = v.filter.level;

    // ── Glide: linear integration over the remaining samples ──
    if (v.glideSamplesRemaining > 0.0f)
    {
        const float remaining = v.glideSamplesRemaining;
        const float step = (v.targetHz - v.hz) / remaining;
        v.hz += step;
        v.glideSamplesRemaining = remaining - 1.0f;

        if (v.glideSamplesRemaining <= 0.0f
            || (step >= 0.0f ? v.hz >= v.targetHz : v.hz <= v.targetHz))
        {
            v.hz = v.targetHz;
            v.glideSamplesRemaining = 0.0f;
        }
    }
    else
    {
        v.hz = v.targetHz;
    }

    const float sr = static_cast<float>(sampleRate_);
    float baseHz = v.hz * bendRatio_;

    // ── Wobble LFO (skipped entirely while all three amounts are 0) ──
    float lfo = 0.0f;
    if (p.lfoOn)
    {
        lfo = lfoShapeValue(p.lfoShape, v.lfoPhase + p.lfoPhase);
        v.lfoPhase += p.lfoInc;
        if (v.lfoPhase >= 1.0f)
            v.lfoPhase -= std::floor(v.lfoPhase);

        if (p.lfoPitchAmt != 0.0f)
            baseHz *= std::exp2(p.lfoPitchAmt * lfo * 100.0f / 1200.0f);
    }

    // ── Hard sync master. Sync Amount == 0 skips the master (and the whole
    //    synced oscillator) so SyncRatio is inert without it. The master runs
    //    at the voice fundamental and resets every unison phase when it wraps;
    //    the sub oscillator is never touched by sync. ──
    if (p.syncOn)
    {
        v.masterPhase += baseHz * p.syncRatio / sr;
        if (v.masterPhase >= 1.0f)
        {
            v.masterPhase -= std::floor(v.masterPhase);
            for (int i = 0; i < kMaxVoices; ++i)
                v.syncPhases[i] = 0.0f;
        }
    }

    // ── Unison sum (normalized by 1/N so Voice Count is level-invariant) ──
    const int n = p.voiceCount;
    const float invN = 1.0f / static_cast<float>(std::max(1, n));
    float sumL = 0.0f;
    float sumR = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float hzI = baseHz * p.detuneRatio[i];
        const float dt = hzI / sr;

        const float freeS = oscSampleAt(p.oscShape, v.phases[i], dt);
        v.phases[i] += dt;
        if (v.phases[i] >= 1.0f)
            v.phases[i] -= std::floor(v.phases[i]);

        float osc = freeS;
        if (p.syncOn)
        {
            const float syncS = oscSampleAt(p.oscShape, v.syncPhases[i], dt);
            v.syncPhases[i] += dt;
            if (v.syncPhases[i] >= 1.0f)
                v.syncPhases[i] -= std::floor(v.syncPhases[i]);

            osc = (1.0f - p.syncAmount) * freeS + p.syncAmount * syncS;
        }

        const float amp = osc * invN;
        const float pan = p.pan[i];
        sumL += amp * (0.5f - 0.5f * pan);
        sumR += amp * (0.5f + 0.5f * pan);
    }

    // ── Sub sine at hz * 2^SubOctave, centre-panned ──
    if (p.subLevel > 0.0f)
    {
        const float sub = std::sin(v.subPhase * kTwoPi) * p.subLevel * 0.5f;
        sumL += sub;
        sumR += sub;

        v.subPhase += baseHz * p.subRatio / sr;
        if (v.subPhase >= 1.0f)
            v.subPhase -= std::floor(v.subPhase);
    }

    // ── Feedback comb (one per channel, because pan ran above). Skipped
    //    entirely at Comb Amount == 0: y = x + amount*buf[read];
    //    buf[write] = x + feedback*buf[read]. ──
    if (p.combOn)
    {
        int read = v.combWrite - p.combDelay;
        if (read < 0)
            read += kCombBufLen;

        const float bufL = v.combL[read];
        const float bufR = v.combR[read];

        const float yL = sumL + p.combAmount * bufL;
        const float yR = sumR + p.combAmount * bufR;

        v.combL[v.combWrite] = sumL + p.combFeedback * bufL;
        v.combR[v.combWrite] = sumR + p.combFeedback * bufR;

        if (++v.combWrite >= kCombBufLen)
            v.combWrite = 0;

        sumL = yL;
        sumR = yR;
    }

    // ── Drive: velocity IS audible here (unlike growl_bass) and the LFO can
    //    add up to 24 dB of its own. ──
    {
        float driveDb = p.driveDb;
        if (p.velocityDrive != 0.0f)
            driveDb += p.velocityDrive * (v.velocity / 127.0f) * 24.0f;
        if (p.lfoOn && p.lfoDriveAmt != 0.0f)
            driveDb += p.lfoDriveAmt * lfo * 24.0f;

        const float gain = juce::Decibels::decibelsToGain(driveDb);
        sumL = driveSample(p.driveType, sumL * gain, p.driveMix);
        sumR = driveSample(p.driveType, sumR * gain, p.driveMix);
    }

    // ── Resonant filter (LP/HP/BP), cutoff = base * 2^(env/12 octave-scaled)
    //    * 2^(keytrack semitones/12) * LFO octaves. ──
    {
        float cutoff = p.filterCutoff;
        if (p.filterEnvAmt != 0.0f)
            cutoff *= std::exp2((p.filterEnvAmt * filterEnv * 48.0f) / 12.0f);
        if (p.filterKeyTrack != 0.0f)
            cutoff *= std::exp2((p.filterKeyTrack * static_cast<float>(v.note - 60)) / 12.0f);
        if (p.lfoOn && p.lfoCutoffAmt != 0.0f)
            cutoff *= std::exp2(p.lfoCutoffAmt * lfo * 4.0f);

        cutoff = juce::jlimit(20.0f, sr * 0.49f, cutoff);

        const float filteredL = processSvfTpt(sumL, cutoff, p.filterRes, v.svfL, p.filterType, sr);
        const float filteredR = processSvfTpt(sumR, cutoff, p.filterRes, v.svfR, p.filterType, sr);

        outL = filteredL * ampEnv;
        outR = filteredR * ampEnv;
    }

    if (v.amp.stage == EnvStage::Idle)
    {
        v.active = false;
        return false;
    }
    return true;
}

// ============================================================================
// Render
// ============================================================================

void ReeseBassEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Exactly ONE atomic load per parameter per block.
    ParamSnapshot p;
    buildSnapshot(p);

    buffer.clear();

    if (numSamples <= 0 || numChannels <= 0)
    {
        midi.clear();
        return;
    }

    // One output sample: sum the sounding note slots, soft-ceiling EACH
    // channel's sum, then apply Output Level.
    auto renderSample = [&](int s)
    {
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (auto& v : voices_)
        {
            if (!v.active)
                continue;

            float l = 0.0f;
            float r = 0.0f;
            renderVoiceSample(v, p, l, r);   // false == the slot just died
            sumL += l;
            sumR += r;
        }

        const float outL = softCeilVoice(sumL) * p.outputLevel;
        const float outR = softCeilVoice(sumR) * p.outputLevel;

        if (numChannels >= 2)
        {
            buffer.setSample(0, s, outL);
            buffer.setSample(1, s, outR);
        }
        else
        {
            buffer.setSample(0, s, (outL + outR) * 0.5f);
        }
    };

    int samplePos = 0;
    for (const auto metadata : midi)
    {
        const int eventSample = juce::jlimit(0, numSamples, metadata.samplePosition);

        for (int i = samplePos; i < eventSample; ++i)
            renderSample(i);

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), message.getVelocity(), p);
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber(), p);
        else if (message.isPitchWheel())
        {
            // Bend range = param 38 semitones, applied as a ratio to hz.
            bendRatio_ = std::exp2(static_cast<float>(message.getPitchWheelValue() - 8192)
                                   / 8192.0f * p.pitchBendRange / 12.0f);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();

        samplePos = eventSample;
    }

    for (int i = samplePos; i < numSamples; ++i)
        renderSample(i);

    // Channel 0 carries the left voice sum; buffers wider than stereo get that
    // same signal in the extra channels (mono already took the L/R average).
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

    midi.clear();
}
