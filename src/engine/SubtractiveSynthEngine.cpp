#include "engine/SubtractiveSynthEngine.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr int kMinSubOctave = -2;
constexpr int kMaxSubOctave = 0;

} // namespace

void SubtractiveSynthEngine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    voice_ = {};
    heldNoteCount_ = 0;
    sustainPedal_ = false;
    lastFilterResonance_ = -1.0f;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate_;
    spec.maximumBlockSize = static_cast<juce::uint32>(std::max(1, maxBlockSize));
    spec.numChannels = 1;
    filter_.prepare(spec);
    filterHp_.prepare(spec);
    filter_.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    filterHp_.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        polyVoices_[(size_t) i] = {};
        polyFilter_[(size_t) i].prepare(spec);
        polyFilterHp_[(size_t) i].prepare(spec);
        polyFilter_[(size_t) i].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        polyFilterHp_[(size_t) i].setType(juce::dsp::StateVariableTPTFilterType::highpass);
        lastPolyResonance_[(size_t) i] = -1.0f;
    }
}

void SubtractiveSynthEngine::setOsc1Wave(int value) noexcept { osc1Wave_.store(clampWave(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setOsc1Level(float value) noexcept { osc1Level_.store(clampUnit(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setOsc2Wave(int value) noexcept { osc2Wave_.store(clampWave(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setOsc2Level(float value) noexcept { osc2Level_.store(clampUnit(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setOsc2DetuneCents(float value) noexcept { osc2DetuneCents_.store(std::clamp(value, -1200.0f, 1200.0f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setSubLevel(float value) noexcept { subLevel_.store(clampUnit(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setSubOctave(int value) noexcept { subOctave_.store(clampSubOctave(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setCutoffHz(float value) noexcept { cutoffHz_.store(std::clamp(value, 20.0f, 20000.0f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setResonance(float value) noexcept { resonance_.store(std::clamp(value, 0.0f, 0.99f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setDrive(float value) noexcept { drive_.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setAttackSeconds(float value) noexcept { attackSeconds_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setDecaySeconds(float value) noexcept { decaySeconds_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setSustain(float value) noexcept { sustain_.store(clampUnit(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setReleaseSeconds(float value) noexcept { releaseSeconds_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setOutputLevel(float value) noexcept { outputLevel_.store(std::clamp(value, 0.0f, 1.5f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setLegato(bool value) noexcept { legato_.store(value, std::memory_order_relaxed); }
void SubtractiveSynthEngine::setPortamentoSeconds(float value) noexcept { portamentoSeconds_.store(std::clamp(value, 0.0f, 5.0f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterType(int value) noexcept { filterType_.store(clampFilterType(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterEnvAmount(float value) noexcept { filterEnvAmount_.store(std::clamp(value, 0.0f, 48.0f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterAttackSeconds(float value) noexcept { filterEnvAttack_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterDecaySeconds(float value) noexcept { filterEnvDecay_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterSustain(float value) noexcept { filterEnvSustain_.store(clampUnit(value), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setFilterReleaseSeconds(float value) noexcept { filterEnvRelease_.store(clampPositive(value, 0.001f), std::memory_order_relaxed); }
void SubtractiveSynthEngine::setPitchBendRange(float value) noexcept { pitchBendRange_.store(std::clamp(value, 0.0f, 12.0f), std::memory_order_relaxed); }

int SubtractiveSynthEngine::activeNoteCount() const noexcept
{
    if (! poly_.load(std::memory_order_relaxed))
        return voice_.active ? 1 : 0;
    int count = 0;
    for (int i = 0; i < kMaxPolyVoices; ++i)
        if (polyVoices_[(size_t) i].active)
            ++count;
    return count;
}

float SubtractiveSynthEngine::currentFrequencyForTest() const noexcept { return voice_.currentHz; }
float SubtractiveSynthEngine::targetFrequencyForTest() const noexcept { return voice_.targetHz; }
float SubtractiveSynthEngine::envelopeForTest() const noexcept { return voice_.envelope; }
int SubtractiveSynthEngine::currentNoteForTest() const noexcept { return voice_.note; }
int SubtractiveSynthEngine::filterTypeForTest() const noexcept { return filterType_.load(std::memory_order_relaxed); }
float SubtractiveSynthEngine::filterEnvForTest() const noexcept { return voice_.filterEnv; }
float SubtractiveSynthEngine::pitchBendRatioForTest() const noexcept { return voice_.bendRatio; }

int SubtractiveSynthEngine::clampWave(int value) noexcept
{
    return std::clamp(value, 0, 3);
}

int SubtractiveSynthEngine::clampFilterType(int value) noexcept
{
    return std::clamp(value, 0, 3);
}

int SubtractiveSynthEngine::clampSubOctave(int value) noexcept
{
    return std::clamp(value, kMinSubOctave, kMaxSubOctave);
}

float SubtractiveSynthEngine::clampUnit(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

float SubtractiveSynthEngine::clampPositive(float value, float fallback) noexcept
{
    return (value > 0.0f) ? value : fallback;
}

float SubtractiveSynthEngine::midiNoteToHz(int note) noexcept
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

float SubtractiveSynthEngine::phaseToSample(Waveform wave, float phase) noexcept
{
    phase -= std::floor(phase);

    switch (wave)
    {
        case Waveform::Sine:
            return std::sin(phase * 2.0f * juce::MathConstants<float>::pi);
        case Waveform::Saw:
            return 2.0f * phase - 1.0f;
        case Waveform::Square:
            return (phase < 0.5f) ? 1.0f : -1.0f;
        case Waveform::Triangle:
            return 1.0f - 4.0f * std::abs(phase - 0.5f);
    }

    return std::sin(phase * 2.0f * juce::MathConstants<float>::pi);
}

void SubtractiveSynthEngine::resetVoice(int note, int velocity) noexcept
{
    voice_.active = true;
    voice_.releasing = false;
    voice_.note = note;
    voice_.velocity = velocity;
    voice_.osc1Phase.fill(0.0f);
    voice_.osc2Phase.fill(0.0f);
    voice_.subPhase.fill(0.0f);
    voice_.envelope = 0.0f;
    voice_.filterEnv = 0.0f;
    voice_.filterEnvReleasing = false;
    voice_.sustainHold = false;
    voice_.bendRatio = 1.0f;
    voice_.currentHz = midiNoteToHz(note);
    voice_.targetHz = voice_.currentHz;
    voice_.glideSamplesRemaining = 0;
    filter_.reset();
    filterHp_.reset();
}

void SubtractiveSynthEngine::retargetVoice(int note, int velocity, bool resetEnvelope) noexcept
{
    if (!voice_.active)
    {
        resetVoice(note, velocity);
        return;
    }

    voice_.releasing = false;
    voice_.note = note;
    voice_.velocity = velocity;

    const float targetHz = midiNoteToHz(note);
    voice_.targetHz = targetHz;

    if (resetEnvelope)
    {
        voice_.envelope = 0.0f;
        voice_.filterEnv = 0.0f;
        voice_.filterEnvReleasing = false;
        voice_.currentHz = targetHz;
        voice_.glideSamplesRemaining = 0;
        voice_.osc1Phase.fill(0.0f);
        voice_.osc2Phase.fill(0.0f);
        voice_.subPhase.fill(0.0f);
        filter_.reset();
        filterHp_.reset();
        return;
    }

    voice_.filterEnvReleasing = false;

    const float portamento = portamentoSeconds_.load(std::memory_order_relaxed);
    if (portamento <= 0.0f || voice_.currentHz <= 0.0f)
    {
        voice_.currentHz = targetHz;
        voice_.glideSamplesRemaining = 0;
        return;
    }

    voice_.glideSamplesRemaining = std::max(1, juce::roundToInt(portamento * static_cast<float>(sampleRate_)));
}

void SubtractiveSynthEngine::noteOn(int note, int velocity) noexcept
{
    updateHeldNote(note, true);
    voice_.sustainHold = false;

    if (poly_.load(std::memory_order_relaxed))
    {
        polyNoteOn(note, velocity);
        return;
    }

    if (voice_.active || voice_.releasing)
    {
        if (legato_.load(std::memory_order_relaxed))
            retargetVoice(note, velocity, false);
        else
            resetVoice(note, velocity);
        return;
    }

    resetVoice(note, velocity);
}

// ── Polyphony bank (param 24) ────────────────────────────────────────────
// Each note-on allocates a fresh voice (standard poly convention — legato
// and portamento are mono-mode features; the heldNotes stack is mono-only).

int SubtractiveSynthEngine::allocPolyVoice() noexcept
{
    // 1) A fully idle voice.
    for (int i = 0; i < kMaxPolyVoices; ++i)
        if (! polyVoices_[(size_t) i].active && ! polyVoices_[(size_t) i].releasing)
            return i;
    // 2) Steal the quietest voice (lowest env*velocity — release tails die
    //    first, held sustain notes last).
    int quietest = 0;
    float quietestLevel = 1e9f;
    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        const float level = polyVoices_[(size_t) i].envelope
                          * static_cast<float>(std::max(1, polyVoices_[(size_t) i].velocity));
        if (level < quietestLevel)
        {
            quietestLevel = level;
            quietest = i;
        }
    }
    return quietest;
}

void SubtractiveSynthEngine::resetPolyVoice(int index, int note, int velocity) noexcept
{
    auto& v = polyVoices_[(size_t) index];
    v.active = true;
    v.releasing = false;
    v.note = note;
    v.velocity = velocity;
    v.envelope = 0.0f;
    v.filterEnv = 0.0f;
    v.filterEnvReleasing = false;
    v.sustainHold = false;
    v.bendRatio = 1.0f;
    v.currentHz = midiNoteToHz(note);
    v.targetHz = v.currentHz;
    v.glideSamplesRemaining = 0;
    polyFilter_[(size_t) index].reset();
    polyFilterHp_[(size_t) index].reset();
    lastPolyResonance_[(size_t) index] = -1.0f;
}

void SubtractiveSynthEngine::polyNoteOn(int note, int velocity) noexcept
{
    const int idx = allocPolyVoice();
    resetPolyVoice(idx, note, velocity);
}

void SubtractiveSynthEngine::polyNoteOff(int note) noexcept
{
    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        auto& v = polyVoices_[(size_t) i];
        if (! v.active || v.releasing || v.note != note)
            continue;
        if (sustainPedal_)
        {
            v.sustainHold = true;
            return;
        }
        v.releasing = true;
        v.filterEnvReleasing = true;
        return; // one voice per note (retrigger allocates a new voice)
    }
}

void SubtractiveSynthEngine::setPolyphony(bool value) noexcept
{
    const bool target = value;
    if (poly_.exchange(target, std::memory_order_relaxed) == target)
        return;
    // Mode switch: clear sound on BOTH banks so no voice survives with stale
    // filter state (a mono glide into a poly bank would otherwise keep the
    // old mono voice rendering under the poly allocator's blind spot).
    voice_ = {};
    heldNoteCount_ = 0;
    filter_.reset();
    filterHp_.reset();
    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        polyVoices_[(size_t) i] = {};
        polyFilter_[(size_t) i].reset();
        polyFilterHp_[(size_t) i].reset();
        lastPolyResonance_[(size_t) i] = -1.0f;
    }
}

void SubtractiveSynthEngine::releaseCurrentVoice() noexcept
{
    if (! voice_.active)
        return;

    voice_.releasing = true;
    voice_.filterEnvReleasing = true;
}

void SubtractiveSynthEngine::allNotesOff() noexcept
{
    heldNoteCount_ = 0;
    voice_ = {};
    sustainPedal_ = false;
    filter_.reset();
    filterHp_.reset();

    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        polyVoices_[(size_t) i] = {};
        polyFilter_[(size_t) i].reset();
        polyFilterHp_[(size_t) i].reset();
        lastPolyResonance_[(size_t) i] = -1.0f;
    }
}

void SubtractiveSynthEngine::noteOff(int note) noexcept
{
    updateHeldNote(note, false);

    if (poly_.load(std::memory_order_relaxed))
    {
        polyNoteOff(note);
        return;
    }

    if (voice_.active && voice_.note == note)
    {
        if (heldNoteCount_ > 0)
        {
            const int nextNote = heldNotes_[static_cast<size_t>(heldNoteCount_ - 1)];
            if (legato_.load(std::memory_order_relaxed))
                retargetVoice(nextNote, voice_.velocity, false);
            else
                resetVoice(nextNote, voice_.velocity);
            return;
        }

        if (sustainPedal_)
        {
            voice_.sustainHold = true;
            return;
        }

        releaseCurrentVoice();
    }
}

void SubtractiveSynthEngine::updateHeldNote(int note, bool pressed) noexcept
{
    if (pressed)
    {
        if (heldNoteCount_ < kMaxHeldNotes)
            heldNotes_[static_cast<size_t>(heldNoteCount_++)] = note;
        else
            heldNotes_[static_cast<size_t>(kMaxHeldNotes - 1)] = note;
        return;
    }

    for (int i = heldNoteCount_ - 1; i >= 0; --i)
    {
        if (heldNotes_[static_cast<size_t>(i)] != note)
            continue;

        for (int j = i; j + 1 < heldNoteCount_; ++j)
            heldNotes_[static_cast<size_t>(j)] = heldNotes_[static_cast<size_t>(j + 1)];

        --heldNoteCount_;
        break;
    }
}

void SubtractiveSynthEngine::advanceEnvelope(Voice& v) noexcept
{
    const float sr = static_cast<float>(sampleRate_);
    const float attack = attackSeconds_.load(std::memory_order_relaxed);
    const float decay = decaySeconds_.load(std::memory_order_relaxed);
    const float sustain = sustain_.load(std::memory_order_relaxed);
    const float release = releaseSeconds_.load(std::memory_order_relaxed);

    const float filterAttack = filterEnvAttack_.load(std::memory_order_relaxed);
    const float filterDecay = filterEnvDecay_.load(std::memory_order_relaxed);
    const float filterSustain = filterEnvSustain_.load(std::memory_order_relaxed);
    const float filterRelease = filterEnvRelease_.load(std::memory_order_relaxed);

    const float attackStep = 1.0f / std::max(1.0f, attack * sr);
    const float decayStep = std::max(0.0f, (1.0f - sustain)) / std::max(1.0f, decay * sr);
    const float releaseStep = 1.0f / std::max(1.0f, release * sr);
    const float filterAttackStep = 1.0f / std::max(1.0f, filterAttack * sr);
    const float filterDecayStep = std::max(0.0f, (1.0f - filterSustain)) / std::max(1.0f, filterDecay * sr);
    const float filterReleaseStep = 1.0f / std::max(1.0f, filterRelease * sr);

    if (! v.active)
        return;

    if (v.sustainHold)
        return;

    if (! v.filterEnvReleasing)
    {
        if (v.filterEnv < 1.0f)
        {
            v.filterEnv += filterAttackStep;
            if (v.filterEnv >= 1.0f)
                v.filterEnv = 1.0f;
        }
        else if (v.filterEnv > filterSustain)
        {
            v.filterEnv -= filterDecayStep;
            if (v.filterEnv <= filterSustain)
                v.filterEnv = filterSustain;
        }
    }
    else if (v.filterEnv > 0.0f)
    {
        v.filterEnv -= filterReleaseStep;
        if (v.filterEnv <= 0.0f)
            v.filterEnv = 0.0f;
    }

    if (! v.releasing)
    {
        if (v.envelope < 1.0f)
        {
            v.envelope += attackStep;
            if (v.envelope >= 1.0f)
                v.envelope = 1.0f;
            return;
        }

        if (v.envelope > sustain)
        {
            v.envelope -= decayStep;
            if (v.envelope <= sustain)
                v.envelope = sustain;
        }
        return;
    }

    if (v.envelope > 0.0f)
    {
        v.envelope -= releaseStep;
        if (v.envelope <= 0.0f)
        {
            v.envelope = 0.0f;
            v.active = false;
            v.releasing = false;
        }
    }
    else
    {
        v.active = false;
        v.releasing = false;
    }
}

float SubtractiveSynthEngine::renderVoiceSample() noexcept
{
    const float sample = renderVoiceSampleCore(voice_, filter_, filterHp_, lastFilterResonance_);
    return sample * outputLevel_.load(std::memory_order_relaxed);
}

float SubtractiveSynthEngine::renderVoiceSampleCore(
    Voice& v,
    juce::dsp::StateVariableTPTFilter<float>& filter,
    juce::dsp::StateVariableTPTFilter<float>& filterHp,
    float& lastResonance) noexcept
{
    const float baseHz = v.currentHz * v.bendRatio;
    const float detuneRatio = std::pow(2.0f, kUnisonDetuneCents / 1200.0f);
    const float osc2Ratio = std::pow(2.0f, osc2DetuneCents_.load(std::memory_order_relaxed) / 1200.0f);
    const float subHzRatio = std::pow(2.0f, static_cast<float>(subOctave_.load(std::memory_order_relaxed)));

    const Waveform osc1Wave = static_cast<Waveform>(clampWave(osc1Wave_.load(std::memory_order_relaxed)));
    const Waveform osc2Wave = static_cast<Waveform>(clampWave(osc2Wave_.load(std::memory_order_relaxed)));

    float sample = 0.0f;
    for (int unison = 0; unison < 2; ++unison)
    {
        const float voiceDetune = (unison == 0) ? (1.0f / detuneRatio) : detuneRatio;
        const float voiceHz = baseHz * voiceDetune;
        const float voiceOsc2Hz = voiceHz * osc2Ratio;
        const float voiceSubHz = voiceHz * subHzRatio;

        const float osc1 = phaseToSample(osc1Wave, v.osc1Phase[static_cast<size_t>(unison)]);
        const float osc2 = phaseToSample(osc2Wave, v.osc2Phase[static_cast<size_t>(unison)]);
        const float sub = phaseToSample(Waveform::Square, v.subPhase[static_cast<size_t>(unison)]);

        v.osc1Phase[static_cast<size_t>(unison)] += voiceHz / static_cast<float>(sampleRate_);
        v.osc2Phase[static_cast<size_t>(unison)] += voiceOsc2Hz / static_cast<float>(sampleRate_);
        v.subPhase[static_cast<size_t>(unison)] += voiceSubHz / static_cast<float>(sampleRate_);

        if (v.osc1Phase[static_cast<size_t>(unison)] >= 1.0f)
            v.osc1Phase[static_cast<size_t>(unison)] -= std::floor(v.osc1Phase[static_cast<size_t>(unison)]);
        if (v.osc2Phase[static_cast<size_t>(unison)] >= 1.0f)
            v.osc2Phase[static_cast<size_t>(unison)] -= std::floor(v.osc2Phase[static_cast<size_t>(unison)]);
        if (v.subPhase[static_cast<size_t>(unison)] >= 1.0f)
            v.subPhase[static_cast<size_t>(unison)] -= std::floor(v.subPhase[static_cast<size_t>(unison)]);

        sample += osc1 * osc1Level_.load(std::memory_order_relaxed)
               + osc2 * osc2Level_.load(std::memory_order_relaxed)
               + sub * subLevel_.load(std::memory_order_relaxed);
    }

    sample *= 0.5f;

    const float drive = drive_.load(std::memory_order_relaxed);
    if (drive > 0.0f)
        sample = std::tanh(sample * (1.0f + 6.0f * drive));

    const float cutoffHz = cutoffHz_.load(std::memory_order_relaxed);
    const float resonance = std::max(resonance_.load(std::memory_order_relaxed), 0.1f);
    const int filterType = clampFilterType(filterType_.load(std::memory_order_relaxed));

    const float filterEnvAmount = filterEnvAmount_.load(std::memory_order_relaxed);
    float envCut = cutoffHz;
    if (filterEnvAmount > 0.0f)
        envCut = cutoffHz * std::exp2(filterEnvAmount * (v.filterEnv - 1.0f) / 12.0f);
    envCut = std::clamp(envCut, 20.0f, static_cast<float>(sampleRate_) * 0.49f);

    if (resonance != lastResonance)
    {
        lastResonance = resonance;
        filter.setResonance(resonance);
        filterHp.setResonance(resonance);
    }

    filter.setType(filterType == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                  : filterType == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                    : juce::dsp::StateVariableTPTFilterType::lowpass);
    filterHp.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    filter.setCutoffFrequency(envCut);
    filterHp.setCutoffFrequency(envCut);

    const float filtered = filter.processSample(0, sample);
    const float filteredHp = filterHp.processSample(0, sample);
    sample = (filterType == 3) ? (filtered + filteredHp) : filtered;

    advancePitch(v);
    advanceEnvelope(v);
    sample *= v.envelope * v.velocity / 127.0f;

    return sample;
}

void SubtractiveSynthEngine::advancePitch(Voice& v) noexcept
{
    if (!v.active)
        return;

    if (v.glideSamplesRemaining <= 0)
    {
        v.currentHz = v.targetHz;
        return;
    }

    const float remaining = static_cast<float>(v.glideSamplesRemaining);
    const float step = (v.targetHz - v.currentHz) / remaining;
    v.currentHz += step;
    --v.glideSamplesRemaining;

    if (v.glideSamplesRemaining <= 0
        || (step >= 0.0f ? v.currentHz >= v.targetHz : v.currentHz <= v.targetHz))
    {
        v.currentHz = v.targetHz;
        v.glideSamplesRemaining = 0;
    }
}

void SubtractiveSynthEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    int samplePos = 0;
    for (const auto metadata : midi)
    {
        const int eventSample = std::clamp(metadata.samplePosition, 0, numSamples);
        for (int i = samplePos; i < eventSample; ++i)
        {
            const float sample = renderOutputSample();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, sample);
        }

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), juce::roundToInt(message.getFloatVelocity() * 127.0f));
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber());
        else if (message.isPitchWheel())
        {
            const float range = pitchBendRange_.load(std::memory_order_relaxed);
            const float ratio = std::exp2(static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f * range / 12.0f);
            voice_.bendRatio = ratio;
            for (int i = 0; i < kMaxPolyVoices; ++i)
                polyVoices_[(size_t) i].bendRatio = ratio;
        }
        else if (message.isController() && message.getControllerNumber() == 64)
        {
            const bool down = message.getControllerValue() >= 64;
            sustainPedal_ = down;
            if (!down && voice_.sustainHold)
            {
                voice_.sustainHold = false;
                if (heldNoteCount_ == 0)
                    releaseCurrentVoice();
            }
            if (!down)
            {
                for (int i = 0; i < kMaxPolyVoices; ++i)
                {
                    auto& v = polyVoices_[(size_t) i];
                    if (v.sustainHold)
                    {
                        v.sustainHold = false;
                        v.releasing = true;
                        v.filterEnvReleasing = true;
                    }
                }
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();

        samplePos = eventSample;
    }

    for (int i = samplePos; i < numSamples; ++i)
    {
        const float sample = renderOutputSample();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, sample);
    }

    midi.clear();
}

// One output sample: mono path is exactly the pre-polyphony behavior;
// poly sums every sounding voice at half gain (a solo poly note is at
// mono level; N simultaneous voices sum like a real poly synth).
float SubtractiveSynthEngine::renderOutputSample() noexcept
{
    if (! poly_.load(std::memory_order_relaxed))
        return voice_.active ? renderVoiceSample() : 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < kMaxPolyVoices; ++i)
    {
        auto& v = polyVoices_[(size_t) i];
        if (! v.active)
            continue;
        sum += renderVoiceSampleCore(v, polyFilter_[(size_t) i], polyFilterHp_[(size_t) i],
                                     lastPolyResonance_[(size_t) i]);
    }
    return sum * 0.5f * outputLevel_.load(std::memory_order_relaxed);
}
