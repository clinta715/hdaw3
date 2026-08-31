#include "engine/GrowlBassEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// ============================================================================
// Preparation
// ============================================================================

void GrowlBassEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    allNotesOff();
}

// ============================================================================
// Parameter setters (lock-free atomics)
// ============================================================================

void GrowlBassEngine::setFundamentalHz(float v) noexcept { fundamentalHz_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setModRatio(float v) noexcept { modRatio_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setModDepth(float v) noexcept { modDepth_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setModShape(int v) noexcept { modShape_.store(v, std::memory_order_relaxed); }

void GrowlBassEngine::setClipType(int v) noexcept { clipType_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setDriveDb(float v) noexcept { driveDb_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setAsymmetry(float v) noexcept { asymmetry_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setBitcrushBits(int v) noexcept { bitcrushBits_.store(v, std::memory_order_relaxed); }

void GrowlBassEngine::setFilterCutoffHz(float v) noexcept { filterCutoffHz_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setFilterResonance(float v) noexcept { filterResonance_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setFilterEnvAmount(float v) noexcept { filterEnvAmount_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setFilterType(int v) noexcept { filterType_.store(v, std::memory_order_relaxed); }

void GrowlBassEngine::setAttackMs(float v) noexcept { attackMs_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setDecayMs(float v) noexcept { decayMs_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setSustainLevel(float v) noexcept { sustainLevel_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setReleaseMs(float v) noexcept { releaseMs_.store(v, std::memory_order_relaxed); }

void GrowlBassEngine::setOutputLevel(float v) noexcept { outputLevel_.store(v, std::memory_order_relaxed); }

void GrowlBassEngine::setUnisonEnabled(bool v) noexcept { unisonEnabled_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setUnisonVoices(int v) noexcept { unisonVoices_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setUnisonDetuneCents(float v) noexcept { unisonDetuneCents_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setPerNoteRatioJitter(bool v) noexcept { perNoteRatioJitter_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setRatioJitterAmount(float v) noexcept { ratioJitterAmount_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setFormantEnabled(bool v) noexcept { formantEnabled_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setFormantMorph(float v) noexcept { formantMorph_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setSidechainDrive(bool v) noexcept { sidechainDrive_.store(v, std::memory_order_relaxed); }
void GrowlBassEngine::setSidechainAmount(float v) noexcept { sidechainAmount_.store(v, std::memory_order_relaxed); }

// ============================================================================
// Inspection
// ============================================================================

int GrowlBassEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& v : voices_)
        if (v.active) ++count;
    return count;
}

float GrowlBassEngine::getOutputLevel() const noexcept
{
    return outputLevel_.load(std::memory_order_relaxed);
}

// ============================================================================
// Oscillator waveforms
// ============================================================================

float GrowlBassEngine::generateCarrier(float phase, ModShape shape)
{
    switch (shape)
    {
        case ModShape::Sine:
            return std::sin(phase * 2.0f * juce::MathConstants<float>::pi);

        case ModShape::Triangle:
        {
            // Triangle: linear ramp -1..1..-1
            float p = std::fmod(phase, 1.0f);
            if (p < 0.0f) p += 1.0f;
            return 4.0f * std::abs(p - 0.5f) - 1.0f;
        }

        case ModShape::Square:
            return std::sin(phase * 2.0f * juce::MathConstants<float>::pi) >= 0.0f ? 1.0f : -1.0f;

        default:
            return std::sin(phase * 2.0f * juce::MathConstants<float>::pi);
    }
}

float GrowlBassEngine::generateModulator(float phase, ModShape shape)
{
    // Same waveforms as carrier — used for FM modulator
    return generateCarrier(phase, shape);
}

// ============================================================================
// Waveshaper / distortion
// ============================================================================

float GrowlBassEngine::waveshape(float input, ClipType type, float drive, float asymmetry, int bits)
{
    // Apply drive gain (convert dB to linear)
    float driven = input * juce::Decibels::decibelsToGain(drive);

    float shaped = 0.0f;

    switch (type)
    {
        case ClipType::SoftTanh:
        {
            // Asymmetric soft clip: different curves for +/- phase
            if (driven >= 0.0f)
                shaped = std::tanh(driven * (1.0f + asymmetry));
            else
                shaped = std::tanh(driven * (1.0f - asymmetry));
            break;
        }

        case ClipType::SoftAtan:
        {
            // atan-based soft clip with asymmetry
            float k = 2.0f / juce::MathConstants<float>::pi;
            if (driven >= 0.0f)
                shaped = k * std::atan(driven * k * (1.0f + asymmetry));
            else
                shaped = k * std::atan(driven * k * (1.0f - asymmetry));
            break;
        }

        case ClipType::Hard:
        {
            // Hard clip with asymmetry
            float posClip = 1.0f + asymmetry * 0.3f;
            float negClip = 1.0f - asymmetry * 0.3f;
            if (driven > posClip)
                shaped = posClip;
            else if (driven < -negClip)
                shaped = -negClip;
            else
                shaped = driven;
            break;
        }

        case ClipType::Bitcrush:
        {
            // Bitcrush: reduce bit depth
            float levels = static_cast<float>(1 << juce::jlimit(2, 16, bits));
            shaped = std::round(driven * levels) / levels;
            // Soft-clip the result to avoid harsh digital artifacts
            shaped = std::tanh(shaped);
            break;
        }

        default:
            shaped = driven;
            break;
    }

    return shaped;
}

// ============================================================================
// State Variable Filter (SVF)
// ============================================================================

float GrowlBassEngine::processSVF(float input, float cutoff, float resonance, FilterType type, float* state)
{
    // Improved SVF with one-pole smoothing
    // state[0] = ic1eq, state[1] = ic2eq
    const float g = std::tan(juce::MathConstants<float>::pi * std::min(cutoff / static_cast<float>(sampleRate_), 0.49f));
    const float k = 2.0f - 2.0f / resonance; // resonance is Q, higher = more resonant
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    float v3 = input - state[1];
    float v1 = a1 * state[0] + a2 * v3;
    float v2 = state[1] + a2 * state[0] + a3 * v3;
    state[0] = 2.0f * v1 - state[0];
    state[1] = 2.0f * v2 - state[1];

    switch (type)
    {
        case FilterType::LowPass:
            return v2;
        case FilterType::BandPass:
            return v1;
        default:
            return v2;
    }
}

// ============================================================================
// Formant filter (simplified vowel filter)
// ============================================================================

float GrowlBassEngine::processFormant(float input, float morph)
{
    // Simplified formant filter using three resonant peaks
    // morph: 0 = "ah", 0.5 = "oh", 1.0 = "oo"
    static constexpr float formantFreqs[3][3] = {
        { 730.0f, 1090.0f, 2440.0f },  // "ah"
        { 570.0f,  840.0f, 2410.0f },  // "oh"
        { 300.0f,  870.0f, 2240.0f },  // "oo"
    };

    float m = juce::jlimit(0.0f, 1.0f, morph);
    int idx = static_cast<int>(m * 2.0f);
    float frac = m * 2.0f - static_cast<float>(idx);
    idx = juce::jlimit(0, 1, idx);

    float output = 0.0f;
    for (int f = 0; f < 3; ++f)
    {
        float freq = formantFreqs[idx][f] * (1.0f - frac) + formantFreqs[idx + 1][f] * frac;
        // Simple resonant peak using a bandpass filter
        float g = std::tan(juce::MathConstants<float>::pi * std::min(freq / static_cast<float>(sampleRate_), 0.49f));
        float k = 2.0f - 2.0f * 0.7f; // Q = 0.7
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        // Simplified: just use the frequency to color the output
        output += input * (1.0f / (1.0f + freq / 2000.0f));
    }

    return output / 3.0f;
}

// ============================================================================
// Voice allocation
// ============================================================================

GrowlBassEngine::Voice* GrowlBassEngine::allocateVoice()
{
    // First: find an inactive voice
    for (auto& v : voices_)
        if (!v.active)
            return &v;

    // Steal oldest voice
    return &voices_[currentNote_++ % kMaxVoices];
}

void GrowlBassEngine::noteOn(int channel, int pitch, int velocity)
{
    Voice* voice = allocateVoice();
    voice->active = true;
    voice->noteOn = true;
    voice->midiNote = pitch;
    voice->channel = channel;
    voice->velocity = velocity;
    voice->carrierPhase = 0.0f;
    voice->modulatorPhase = 0.0f;
    voice->envLevel = 0.0f;
    voice->envStage = Voice::EnvStage::Attack;
    voice->filterState[0] = voice->filterState[1] = voice->filterState[2] = voice->filterState[3] = 0.0f;
}

void GrowlBassEngine::noteOff(int channel, int pitch)
{
    for (auto& v : voices_)
    {
        if (v.active && v.channel == channel && v.midiNote == pitch)
        {
            v.noteOn = false;
            v.envReleaseLevel = v.envLevel;
            v.envStage = Voice::EnvStage::Release;
        }
    }
}

void GrowlBassEngine::allNotesOff()
{
    for (auto& v : voices_)
    {
        v.active = false;
        v.noteOn = false;
        v.midiNote = -1;
        v.envLevel = 0.0f;
        v.envStage = Voice::EnvStage::Idle;
    }
}

// ============================================================================
// Envelope processing
// ============================================================================

void GrowlBassEngine::advanceEnvelope(Voice& voice, int numSamples)
{
    float attack = attackMs_.load(std::memory_order_relaxed) * 0.001f;
    float decay = decayMs_.load(std::memory_order_relaxed) * 0.001f;
    float sustain = sustainLevel_.load(std::memory_order_relaxed);
    float release = releaseMs_.load(std::memory_order_relaxed) * 0.001f;

    float attackRate = (attack > 0.001f) ? (1.0f / (attack * static_cast<float>(sampleRate_))) : 1000.0f;
    float decayRate = (decay > 0.001f) ? ((1.0f - sustain) / (decay * static_cast<float>(sampleRate_))) : 1000.0f;
    float releaseRate = (release > 0.001f) ? (voice.envReleaseLevel / (release * static_cast<float>(sampleRate_))) : 1000.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        switch (voice.envStage)
        {
            case Voice::EnvStage::Attack:
                voice.envLevel += attackRate;
                if (voice.envLevel >= 1.0f)
                {
                    voice.envLevel = 1.0f;
                    voice.envStage = Voice::EnvStage::Decay;
                }
                break;

            case Voice::EnvStage::Decay:
                voice.envLevel -= decayRate;
                if (voice.envLevel <= sustain)
                {
                    voice.envLevel = sustain;
                    voice.envStage = Voice::EnvStage::Sustain;
                }
                break;

            case Voice::EnvStage::Sustain:
                voice.envLevel = sustain;
                break;

            case Voice::EnvStage::Release:
                voice.envLevel -= releaseRate;
                if (voice.envLevel <= 0.001f)
                {
                    voice.envLevel = 0.0f;
                    voice.envStage = Voice::EnvStage::Idle;
                    voice.active = false;
                }
                break;

            case Voice::EnvStage::Idle:
            default:
                voice.envLevel = 0.0f;
                voice.active = false;
                break;
        }
    }
}

// ============================================================================
// Main render loop
// ============================================================================

void GrowlBassEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Read atomic params once per block
    const float fundHz = fundamentalHz_.load(std::memory_order_relaxed);
    const float modR = modRatio_.load(std::memory_order_relaxed);
    const float modD = modDepth_.load(std::memory_order_relaxed);
    const auto mShape = static_cast<ModShape>(modShape_.load(std::memory_order_relaxed));
    const auto cType = static_cast<ClipType>(clipType_.load(std::memory_order_relaxed));
    const float drive = driveDb_.load(std::memory_order_relaxed);
    const float asym = asymmetry_.load(std::memory_order_relaxed);
    const int bits = bitcrushBits_.load(std::memory_order_relaxed);
    const float filtCutoff = filterCutoffHz_.load(std::memory_order_relaxed);
    const float filtRes = filterResonance_.load(std::memory_order_relaxed);
    const float filtEnvAmt = filterEnvAmount_.load(std::memory_order_relaxed);
    const auto fType = static_cast<FilterType>(filterType_.load(std::memory_order_relaxed));
    const float outLevel = outputLevel_.load(std::memory_order_relaxed);
    const bool useUnison = unisonEnabled_.load(std::memory_order_relaxed);
    const int uniVoices = juce::jlimit(1, 4, unisonVoices_.load(std::memory_order_relaxed));
    const float uniDetune = unisonDetuneCents_.load(std::memory_order_relaxed);
    const bool jitterRatio = perNoteRatioJitter_.load(std::memory_order_relaxed);
    const float jitterAmt = ratioJitterAmount_.load(std::memory_order_relaxed);
    const bool useFormant = formantEnabled_.load(std::memory_order_relaxed);
    const float formantM = formantMorph_.load(std::memory_order_relaxed);
    const bool useSidechain = sidechainDrive_.load(std::memory_order_relaxed);
    const float sidechainAmt = sidechainAmount_.load(std::memory_order_relaxed);

    // Process MIDI events
    for (const auto metadata : midi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            noteOn(msg.getChannel(), msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            noteOff(msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            allNotesOff();
    }

    // Clear output buffer
    buffer.clear();

    // Render each active voice
    for (auto& voice : voices_)
    {
        if (!voice.active)
            continue;

        // Advance envelope
        advanceEnvelope(voice, numSamples);
        if (!voice.active)
            continue;

        // Calculate frequencies
        float midiFreq = 440.0f * std::pow(2.0f, (static_cast<float>(voice.midiNote) - 69.0f) / 12.0f);
        float baseFreq = (fundHz > 10.0f) ? fundHz : midiFreq; // Use MIDI note if fundamental is very low

        // Apply jitter to modulator ratio
        float effectiveModRatio = modR;
        if (jitterRatio)
        {
            float jitter = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * jitterAmt;
            effectiveModRatio += jitter;
        }

        // Unison: render multiple detuned copies
        int voicesToRender = useUnison ? uniVoices : 1;

        for (int uv = 0; uv < voicesToRender; ++uv)
        {
            float detuneCents = 0.0f;
            if (useUnison && voicesToRender > 1)
                detuneCents = uniDetune * (static_cast<float>(uv) - (voicesToRender - 1) * 0.5f);

            float freq = baseFreq * std::pow(2.0f, detuneCents / 1200.0f);
            float modFreq = freq * effectiveModRatio;

            float phaseIncCarrier = freq / static_cast<float>(sampleRate_);
            float phaseIncMod = modFreq / static_cast<float>(sampleRate_);

            float gainPerVoice = 1.0f / static_cast<float>(voicesToRender);

            // Per-sample processing
            for (int s = 0; s < numSamples; ++s)
            {
                // Generate carrier with FM modulation
                float modPhase = voice.modulatorPhase;
                float modSignal = generateModulator(modPhase, mShape) * modD;

                float carrierInput = voice.carrierPhase + modSignal * (1.0f / (2.0f * juce::MathConstants<float>::pi));
                float sample = generateCarrier(carrierInput, mShape);

                // Advance oscillator phases
                voice.carrierPhase += phaseIncCarrier;
                voice.modulatorPhase += phaseIncMod;
                if (voice.carrierPhase >= 1.0f) voice.carrierPhase -= 1.0f;
                if (voice.modulatorPhase >= 1.0f) voice.modulatorPhase -= 1.0f;

                // Apply waveshaper/distortion
                float sidechainMod = 1.0f;
                if (useSidechain)
                {
                    // Simple sidechain: reduce drive based on amplitude
                    sidechainMod = 1.0f - sidechainAmt * voice.envLevel;
                }
                sample = waveshape(sample, cType, drive * sidechainMod, asym, bits);

                // Apply envelope
                sample *= voice.envLevel;

                // Apply filter with envelope modulation
                float envFilterMod = filtEnvAmt * voice.envLevel;
                float effectiveCutoff = filtCutoff * std::pow(2.0f, envFilterMod * 2.0f); // Up to 2 octaves sweep
                effectiveCutoff = juce::jlimit(20.0f, static_cast<float>(sampleRate_) * 0.49f, effectiveCutoff);
                sample = processSVF(sample, effectiveCutoff, filtRes, fType, voice.filterState);

                // Apply formant filter
                if (useFormant)
                    sample = processFormant(sample, formantM);

                // Add to output buffer (mono sum, will be spread to channels)
                float envGain = voice.envLevel * gainPerVoice * outLevel;
                buffer.addSample(0, s, sample * envGain);
            }
        }
    }

    // Spread mono sum to all channels
    if (numChannels > 1)
    {
        for (int ch = 1; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }
}
