#include "engine/FmSynthEngine.h"
#include "msfa/sin.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"

#include <algorithm>
#include <cmath>
#include <limits>

FmSynthEngine::~FmSynthEngine()
{
    for (auto& v : voices_)
    {
        delete v.note;
        v.note = nullptr;
    }
}

void FmSynthEngine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    Sin::init();
    Exp2::init();
    Tanh::init();
    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);

    controllers_.core = &engineMsfa_;
    controllers_.values_[kControllerPitch] = 0x2000;
    controllers_.values_[kControllerPitchRangeUp] = 2;
    controllers_.values_[kControllerPitchRangeDn] = 2;
    controllers_.values_[kControllerPitchStep] = 0;

    // Seed a DX7 "init" patch so envelopes open immediately on note-on.
    // Each operator is 21 bytes: EG rates [0..3], EG levels [4..7], ...
    // rates=99 = fastest attack/decay/release, levels: 99,99,99,0 = full sustain.
    std::memset(patchData_, 0, kPatchSize);
    for (int op = 0; op < 6; ++op)
    {
        const int off = op * 21;
        patchData_[off + 0] = 99; // EG Rate 1 (attack)
        patchData_[off + 1] = 99; // EG Rate 2 (decay 1)
        patchData_[off + 2] = 99; // EG Rate 3 (decay 2)
        patchData_[off + 3] = 99; // EG Rate 4 (release)
        patchData_[off + 4] = 99; // EG Level 1
        patchData_[off + 5] = 99; // EG Level 2
        patchData_[off + 6] = 99; // EG Level 3
        patchData_[off + 7] = 0;  // EG Level 4 (silence)
        patchData_[off + 16] = 99; // Output level (full)
    }
    patchData_[134] = 0; // Algorithm 0
    patchData_[135] = 0; // Feedback 0

    for (auto& v : voices_)
    {
        if (v.note == nullptr)
            v.note = new Dx7Note();
    }

    lfo_.reset(patchData_ + 137);

    extra_buf_size_ = 0;
    std::memset(extra_buf_, 0, sizeof(extra_buf_));
}

void FmSynthEngine::setAlgorithm(int v) noexcept
{
    algorithmAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setFeedback(int v) noexcept
{
    feedbackAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setOutputLevel(float v) noexcept
{
    outputLevelAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setMonoMode(bool v) noexcept
{
    monoModeAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setOpLevel(int op, float v) noexcept
{
    if (op >= 0 && op < 6)
    {
        opLevelAtom_[op].store(v, std::memory_order_relaxed);
        paramsDirty_.store(true, std::memory_order_release);
    }
}

void FmSynthEngine::setOpCoarse(int op, int v) noexcept
{
    if (op >= 0 && op < 6)
    {
        opCoarseAtom_[op].store(v, std::memory_order_relaxed);
        paramsDirty_.store(true, std::memory_order_release);
    }
}

void FmSynthEngine::setOpFine(int op, int v) noexcept
{
    if (op >= 0 && op < 6)
    {
        opFineAtom_[op].store(v, std::memory_order_relaxed);
        paramsDirty_.store(true, std::memory_order_release);
    }
}

void FmSynthEngine::setOpDetune(int op, int v) noexcept
{
    if (op >= 0 && op < 6)
    {
        opDetuneAtom_[op].store(v, std::memory_order_relaxed);
        paramsDirty_.store(true, std::memory_order_release);
    }
}

void FmSynthEngine::setLfoRate(float v) noexcept
{
    lfoRateAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setLfoDelay(float v) noexcept
{
    lfoDelayAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setLfoPitchDepth(float v) noexcept
{
    lfoPitchDepthAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setLfoAmpDepth(float v) noexcept
{
    lfoAmpDepthAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::setLfoWaveform(int v) noexcept
{
    lfoWaveformAtom_.store(v, std::memory_order_relaxed);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::loadPatch(const uint8_t patch[kPatchSize])
{
    std::memcpy(patchData_, patch, kPatchSize);
    lfo_.reset(patchData_ + 137);
    paramsDirty_.store(true, std::memory_order_release);
}

void FmSynthEngine::applyPendingParams()
{
    if (! paramsDirty_.load(std::memory_order_acquire))
        return;

    const int algorithm = std::clamp(algorithmAtom_.load(std::memory_order_relaxed), 0, 31);
    const int feedback = std::clamp(feedbackAtom_.load(std::memory_order_relaxed), 0, 7);

    patchData_[134] = static_cast<uint8_t>(algorithm);
    patchData_[135] = static_cast<uint8_t>(feedback);

    for (int op = 0; op < 6; ++op)
    {
        const int off = op * 21;
        const float levelFloat = opLevelAtom_[op].load(std::memory_order_relaxed);
        patchData_[off + 16] = static_cast<uint8_t>(std::clamp(levelFloat * 99.0f, 0.0f, 99.0f));
        patchData_[off + 18] = static_cast<uint8_t>(std::clamp(opCoarseAtom_[op].load(std::memory_order_relaxed), 0, 31));
        patchData_[off + 19] = static_cast<uint8_t>(std::clamp(opFineAtom_[op].load(std::memory_order_relaxed), 0, 99));
        patchData_[off + 20] = static_cast<uint8_t>(std::clamp(opDetuneAtom_[op].load(std::memory_order_relaxed), 0, 14));
    }

    patchData_[137] = static_cast<uint8_t>(std::clamp(lfoRateAtom_.load(std::memory_order_relaxed) * 99.0f, 0.0f, 99.0f));
    patchData_[138] = static_cast<uint8_t>(std::clamp(lfoDelayAtom_.load(std::memory_order_relaxed) * 99.0f, 0.0f, 99.0f));
    patchData_[139] = static_cast<uint8_t>(std::clamp(lfoPitchDepthAtom_.load(std::memory_order_relaxed) * 99.0f, 0.0f, 99.0f));
    patchData_[140] = static_cast<uint8_t>(std::clamp(lfoAmpDepthAtom_.load(std::memory_order_relaxed) * 99.0f, 0.0f, 99.0f));
    patchData_[142] = static_cast<uint8_t>(std::clamp(lfoWaveformAtom_.load(std::memory_order_relaxed), 0, 4));
    lfo_.reset(patchData_ + 137);

    for (auto& v : voices_)
    {
        if (v.live && v.note != nullptr)
            v.note->update(patchData_, v.midiNote, v.velocity, v.channel);
    }

    paramsDirty_.store(false, std::memory_order_release);
}

FmSynthEngine::Voice* FmSynthEngine::allocateVoice()
{
    Voice* freeV = nullptr;
    Voice* oldestV = nullptr;
    int32_t oldestSeq = std::numeric_limits<int32_t>::max();

    for (int i = 0; i < kMaxVoices; ++i)
    {
        Voice& v = voices_[i];
        if (! v.live || v.note == nullptr)
        {
            freeV = &v;
            break;
        }
        if (v.keydownSeq < oldestSeq)
        {
            oldestSeq = v.keydownSeq;
            oldestV = &v;
        }
    }

    Voice* target = freeV ? freeV : oldestV;
    if (target != nullptr && target->live && target->note != nullptr)
    {
        target->note->keyup();
        target->live = false;
        target->keydown = false;
        target->sustained = false;
    }
    return target;
}

void FmSynthEngine::noteOn(int channel, int pitch, int velocity)
{
    if (monoModeAtom_.load(std::memory_order_relaxed))
    {
        for (auto& v : voices_)
        {
            if (v.live && v.note != nullptr)
            {
                v.note->keyup();
                v.live = false;
                v.keydown = false;
                v.sustained = false;
            }
        }
    }

    Voice* v = allocateVoice();
    if (v == nullptr)
        return;

    v->midiNote = pitch;
    v->channel = channel;
    v->velocity = velocity;
    v->keydown = true;
    v->sustained = false;
    v->live = true;
    v->keydownSeq = nextKeydownSeq_++;

    v->note->init(patchData_, pitch, velocity, channel, &controllers_);
}

void FmSynthEngine::noteOff(int channel, int pitch)
{
    for (auto& v : voices_)
    {
        if (v.live && v.midiNote == pitch && v.channel == channel && v.keydown)
        {
            if (sustain_)
            {
                v.sustained = true;
            }
            else
            {
                v.note->keyup();
                v.keydown = false;
            }
            break;
        }
    }
}

void FmSynthEngine::allNotesOff()
{
    for (auto& v : voices_)
    {
        if (v.live && v.note != nullptr)
        {
            v.note->keyup();
            v.note->oscSync();
            v.live = false;
            v.keydown = false;
            v.sustained = false;
        }
    }
    sustain_ = false;
}

void FmSynthEngine::handleNoteOn(int note, float vel, int channel)
{
    noteOn(channel, note, static_cast<int>(vel * 127.0f + 0.5f));
}

void FmSynthEngine::handleNoteOff(int note, int channel)
{
    noteOff(channel, note);
}

int FmSynthEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices_)
        if (v.live)
            ++n;
    return n;
}

void FmSynthEngine::computeBlock(int32_t lfoVal, int32_t lfoDelay, float* dest, int count)
{
    for (auto& v : voices_)
    {
        if (! v.live || v.note == nullptr)
            continue;

        if (! v.note->isPlaying())
        {
            v.live = false;
            v.keydown = false;
            v.sustained = false;
            continue;
        }

        int32_t tempBuf[kBlockSize]{};
        v.note->compute(tempBuf, lfoVal, lfoDelay, &controllers_);

        for (int k = 0; k < count; ++k)
        {
            int32_t val = tempBuf[k] >> 4;
            val = std::max(-32768, std::min(32767, val));
            dest[k] += static_cast<float>(val) / 32768.0f;
        }
        for (int k = count; k < kBlockSize; ++k)
        {
            int32_t val = tempBuf[k] >> 4;
            val = std::max(-32768, std::min(32767, val));
            extra_buf_[k - count] += static_cast<float>(val) / 32768.0f;
        }
    }
}

void FmSynthEngine::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    applyPendingParams();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            handleNoteOn(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel());
        else if (m.isNoteOff())
            handleNoteOff(m.getNoteNumber(), m.getChannel());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allNotesOff();
        else if (m.isSustainPedalOn())
            sustain_ = true;
        else if (m.isSustainPedalOff())
        {
            sustain_ = false;
            for (auto& v : voices_)
            {
                if (v.live && v.sustained)
                {
                    v.note->keyup();
                    v.keydown = false;
                    v.sustained = false;
                }
            }
        }
    }
    midi.clear();

    buffer.clear();

    const float outputLevel = outputLevelAtom_.load(std::memory_order_relaxed);
    const int numChannels = buffer.getNumChannels();
    float* chL = buffer.getWritePointer(0, 0);

    int i = 0;
    int n = numSamples;

    if (extra_buf_size_ > 0)
    {
        const int flushLen = std::min(extra_buf_size_, n);
        for (int k = 0; k < flushLen; ++k)
            chL[k] += extra_buf_[k];

        if (extra_buf_size_ > n)
        {
            const int remainder = extra_buf_size_ - n;
            for (int k = 0; k < remainder; ++k)
                extra_buf_[k] = extra_buf_[k + n];
            extra_buf_size_ = remainder;
            n = 0;
        }
        else
        {
            i = extra_buf_size_;
            n -= extra_buf_size_;
            extra_buf_size_ = 0;
        }
    }

    while (n >= kBlockSize)
    {
        const int32_t lfoVal = lfo_.getsample();
        const int32_t lfoDelay = lfo_.getdelay();
        computeBlock(lfoVal, lfoDelay, chL + i, kBlockSize);
        i += kBlockSize;
        n -= kBlockSize;
    }

    if (n > 0)
    {
        for (int k = 0; k < kBlockSize; ++k)
            extra_buf_[k] = 0.0f;
        const int32_t lfoVal = lfo_.getsample();
        const int32_t lfoDelay = lfo_.getdelay();
        computeBlock(lfoVal, lfoDelay, chL + i, n);
        extra_buf_size_ = kBlockSize - n;
    }

    if (outputLevel < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGain(ch, 0, numSamples, outputLevel);
    }

    if (numChannels > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
}
