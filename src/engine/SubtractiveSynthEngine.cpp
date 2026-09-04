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
    return voice_.active ? 1 : 0;
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

void SubtractiveSynthEngine::releaseCurrentVoice() noexcept
{
    if (! voice_.active)
        return;

    voice_.releasing = true;
    voice_.filterEnvReleasing = true;
}

void SubtractiveSynthEngine::noteOff(int note) noexcept
{
    updateHeldNote(note, false);

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

void SubtractiveSynthEngine::allNotesOff() noexcept
{
    heldNoteCount_ = 0;
    voice_ = {};
    sustainPedal_ = false;
    filter_.reset();
    filterHp_.reset();
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

void SubtractiveSynthEngine::advanceEnvelope() noexcept
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

    if (! voice_.active)
        return;

    if (voice_.sustainHold)
        return;

    if (! voice_.filterEnvReleasing)
    {
        if (voice_.filterEnv < 1.0f)
        {
            voice_.filterEnv += filterAttackStep;
            if (voice_.filterEnv >= 1.0f)
                voice_.filterEnv = 1.0f;
        }
        else if (voice_.filterEnv > filterSustain)
        {
            voice_.filterEnv -= filterDecayStep;
            if (voice_.filterEnv <= filterSustain)
                voice_.filterEnv = filterSustain;
        }
    }
    else if (voice_.filterEnv > 0.0f)
    {
        voice_.filterEnv -= filterReleaseStep;
        if (voice_.filterEnv <= 0.0f)
            voice_.filterEnv = 0.0f;
    }

    if (! voice_.releasing)
    {
        if (voice_.envelope < 1.0f)
        {
            voice_.envelope += attackStep;
            if (voice_.envelope >= 1.0f)
                voice_.envelope = 1.0f;
            return;
        }

        if (voice_.envelope > sustain)
        {
            voice_.envelope -= decayStep;
            if (voice_.envelope <= sustain)
                voice_.envelope = sustain;
        }
        return;
    }

    if (voice_.envelope > 0.0f)
    {
        voice_.envelope -= releaseStep;
        if (voice_.envelope <= 0.0f)
        {
            voice_.envelope = 0.0f;
            voice_.active = false;
            voice_.releasing = false;
        }
    }
    else
    {
        voice_.active = false;
        voice_.releasing = false;
    }
}

float SubtractiveSynthEngine::renderVoiceSample() noexcept
{
    const float baseHz = voice_.currentHz * voice_.bendRatio;
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

        const float osc1 = phaseToSample(osc1Wave, voice_.osc1Phase[static_cast<size_t>(unison)]);
        const float osc2 = phaseToSample(osc2Wave, voice_.osc2Phase[static_cast<size_t>(unison)]);
        const float sub = phaseToSample(Waveform::Square, voice_.subPhase[static_cast<size_t>(unison)]);

        voice_.osc1Phase[static_cast<size_t>(unison)] += voiceHz / static_cast<float>(sampleRate_);
        voice_.osc2Phase[static_cast<size_t>(unison)] += voiceOsc2Hz / static_cast<float>(sampleRate_);
        voice_.subPhase[static_cast<size_t>(unison)] += voiceSubHz / static_cast<float>(sampleRate_);

        if (voice_.osc1Phase[static_cast<size_t>(unison)] >= 1.0f)
            voice_.osc1Phase[static_cast<size_t>(unison)] -= std::floor(voice_.osc1Phase[static_cast<size_t>(unison)]);
        if (voice_.osc2Phase[static_cast<size_t>(unison)] >= 1.0f)
            voice_.osc2Phase[static_cast<size_t>(unison)] -= std::floor(voice_.osc2Phase[static_cast<size_t>(unison)]);
        if (voice_.subPhase[static_cast<size_t>(unison)] >= 1.0f)
            voice_.subPhase[static_cast<size_t>(unison)] -= std::floor(voice_.subPhase[static_cast<size_t>(unison)]);

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
        envCut = cutoffHz * std::exp2(filterEnvAmount * (voice_.filterEnv - 1.0f) / 12.0f);
    envCut = std::clamp(envCut, 20.0f, static_cast<float>(sampleRate_) * 0.49f);

    if (resonance != lastFilterResonance_)
    {
        lastFilterResonance_ = resonance;
        filter_.setResonance(resonance);
        filterHp_.setResonance(resonance);
    }

    filter_.setType(filterType == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                  : filterType == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                    : juce::dsp::StateVariableTPTFilterType::lowpass);
    filterHp_.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    filter_.setCutoffFrequency(envCut);
    filterHp_.setCutoffFrequency(envCut);

    const float filtered = filter_.processSample(0, sample);
    const float filteredHp = filterHp_.processSample(0, sample);
    sample = (filterType == 3) ? (filtered + filteredHp) : filtered;

    advancePitch();
    advanceEnvelope();
    sample *= voice_.envelope * voice_.velocity / 127.0f;
    sample *= outputLevel_.load(std::memory_order_relaxed);

    return sample;
}

void SubtractiveSynthEngine::advancePitch() noexcept
{
    if (!voice_.active)
        return;

    if (voice_.glideSamplesRemaining <= 0)
    {
        voice_.currentHz = voice_.targetHz;
        return;
    }

    const float remaining = static_cast<float>(voice_.glideSamplesRemaining);
    const float step = (voice_.targetHz - voice_.currentHz) / remaining;
    voice_.currentHz += step;
    --voice_.glideSamplesRemaining;

    if (voice_.glideSamplesRemaining <= 0
        || (step >= 0.0f ? voice_.currentHz >= voice_.targetHz : voice_.currentHz <= voice_.targetHz))
    {
        voice_.currentHz = voice_.targetHz;
        voice_.glideSamplesRemaining = 0;
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
            const float sample = voice_.active ? renderVoiceSample() : 0.0f;
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
            voice_.bendRatio = std::exp2(static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f * range / 12.0f);
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
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();

        samplePos = eventSample;
    }

    for (int i = samplePos; i < numSamples; ++i)
    {
        const float sample = voice_.active ? renderVoiceSample() : 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, sample);
    }

    midi.clear();
}
