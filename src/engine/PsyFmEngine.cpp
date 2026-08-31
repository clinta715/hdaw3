#include "PsyFmEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace HDAW {

// ── prepare ──

void PsyFmEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        for (auto& op : v.operators)
            op.prepare (sampleRate);

    scratchBuffers_.resize (kNumOperators);
    for (auto& buf : scratchBuffers_)
        buf.resize (static_cast<size_t> (maxBlockSize), 0.0f);
    carrierMixBuffer_.resize (static_cast<size_t> (maxBlockSize), 0.0f);

    // Default matrix route: feedback LFO undulates Op6 feedback, so the
    // track-level LFO target 306 (feedbackOffset) always has audible effect
    // even when no explicit route was configured.
    if (matrix_.getRoutes().empty())
        matrix_.addRoute ({ PsyFmModRoute::Source::FeedbackLFO,
                            PsyFmModRoute::Dest::Op6Feedback, 0.35f });
}

// ── Algorithm ──

void PsyFmEngine::setAlgorithm (AlgorithmFn fn)
{
    algorithmFn_ = std::move (fn);
}

// ── Base params ──

void PsyFmEngine::setBaseRatios (const float ratios[kNumOperators])
{
    std::copy (ratios, ratios + kNumOperators, baseRatios_);
}

void PsyFmEngine::setBaseFeedback (float fb)
{
    baseFeedback_ = fb;
}

void PsyFmEngine::setOpEnvelope (int opIndex, const juce::ADSR::Parameters& p)
{
    if (opIndex >= 0 && opIndex < kNumOperators)
    {
        for (auto& v : voices_)
            v.operators[opIndex].setEnvelopeParams (p);
    }
}

void PsyFmEngine::setOutputLevel (float v) noexcept
{
    outputLevelAtom_.store (v, std::memory_order_relaxed);
}

// ── Modulation matrix ──

void PsyFmEngine::setModMatrix (PsyFmModMatrix matrix)
{
    // Lesson 13 / Gate 3: the audio thread reads matrix_ every block; the
    // swap (vector realloc inside the moved-in matrix) must not race it.
    const juce::SpinLock::ScopedLockType lock (matrixLock_);
    matrix_ = std::move (matrix);
}

// ── Bar clock ──

void PsyFmEngine::onBarBoundary (int barCounter)
{
    // Example: every 8 bars, speed up the riser LFO
    if (barCounter % 8 == 0)
        sources_.ratioSweepLFORateHz = juce::jmin (
            sources_.ratioSweepLFORateHz * 1.3f, 40.0f);
}

// ── Inspection ──

int PsyFmEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices_)
        if (v.live)
            ++n;
    return n;
}

float PsyFmEngine::getOpEgLevel (int op) const noexcept
{
    return (op >= 0 && op < kNumOperators)
        ? opEgLevel_[op].load (std::memory_order_relaxed)
        : 0.0f;
}

// ── Algorithm function helpers ──

float* PsyFmEngine::getScratch (int opIndex)
{
    return scratchBuffers_[static_cast<size_t> (opIndex)].data();
}

PsyFmOperator& PsyFmEngine::op (int index)
{
    // Returns operator 0 of voice 0 — algorithm functions operate on a single
    // voice's operators. The render loop iterates voices and calls the algorithm
    // per-voice, so the helper always references the current voice being rendered.
    // This is set up by the render loop below.
    return voices_[0].operators[index];
}

std::vector<float>& PsyFmEngine::carrierMix()
{
    return carrierMixBuffer_;
}

// ── Voice allocator ──

PsyFmEngine::Voice* PsyFmEngine::allocateVoice()
{
    Voice* freeV = nullptr;
    Voice* oldestV = nullptr;
    int32_t oldestSeq = std::numeric_limits<int32_t>::max();

    for (int i = 0; i < kMaxVoices; ++i)
    {
        Voice& v = voices_[i];
        if (! v.live)
        {
            freeV = &v;
            break;
        }
    }

    // If no free voice, steal the oldest
    if (freeV == nullptr)
    {
        // Simple round-robin steal
        oldestV = &voices_[currentNote_];
        currentNote_ = (currentNote_ + 1) % kMaxVoices;
    }

    Voice* target = freeV ? freeV : oldestV;
    if (target != nullptr && target->live)
    {
        // Retire the stolen voice — release its envelopes
        for (auto& op : target->operators)
            op.noteOff();
        target->live = false;
        target->keydown = false;
    }
    return target;
}

// ── MIDI handling ──

void PsyFmEngine::noteOn (int channel, int pitch, int velocity)
{
    Voice* v = allocateVoice();
    if (v == nullptr)
        return;

    v->midiNote = pitch;
    v->channel = channel;
    v->keydown = true;
    v->live = true;

    float freqHz = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (pitch));
    for (auto& op : v->operators)
    {
        op.noteOn();
        op.setBlockParams (baseRatios_[0], baseFeedback_, freqHz);
    }
}

void PsyFmEngine::noteOff (int channel, int pitch)
{
    for (auto& v : voices_)
    {
        if (v.live && v.midiNote == pitch && v.channel == channel && v.keydown)
        {
            v.keydown = false;
            for (auto& op : v.operators)
                op.noteOff();
            return;
        }
    }
}

void PsyFmEngine::allNotesOff()
{
    for (auto& v : voices_)
    {
        if (v.live)
        {
            for (auto& op : v.operators)
                op.noteOff();
            v.live = false;
            v.keydown = false;
        }
    }
}

// ── render ──

void PsyFmEngine::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();

    // Process MIDI events
    for (const auto metadata : midi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            noteOn (msg.getChannel(), msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            allNotesOff();
    }

    float outGain = outputLevelAtom_.load (std::memory_order_relaxed);

    // Block-rate modulation pass
    sources_.advanceControlRate (numSamples, sampleRate_);
    float liveRatios[kNumOperators];
    float liveFeedback;

    // Try-lock the matrix: on contention (a concurrent setModMatrix swap)
    // keep the base params for this block rather than blocking or reading a
    // half-moved vector. apply() itself resets out params to base values, but
    // the skip path needs them initialized here.
    for (int i = 0; i < kNumOperators; ++i)
        liveRatios[i] = baseRatios_[i];
    liveFeedback = baseFeedback_;
    {
        const juce::SpinLock::ScopedTryLockType lock (matrixLock_);
        if (lock.isLocked())
            matrix_.apply (sources_, baseRatios_, baseFeedback_, liveRatios, liveFeedback);
    }

    // Per-voice render
    buffer.clear();

    // Polyphony normalization — prevents additive clipping
    int liveCount = 0;
    for (auto& v : voices_)
        if (v.live) ++liveCount;
    const float voiceScale = (liveCount > 0) ? (1.0f / static_cast<float> (liveCount)) : 1.0f;

    for (auto& v : voices_)
    {
        if (! v.live)
            continue;

        // Set block params on all operators for this voice
        float freqHz = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (v.midiNote));
        for (int op = 0; op < kNumOperators; ++op)
            v.operators[op].setBlockParams (liveRatios[op], liveFeedback, freqHz);

        // Call the algorithm function to render this voice's operators
        if (algorithmFn_)
        {
            std::swap (voices_[0], v);
            carrierMixBuffer_.resize (static_cast<size_t> (numSamples));
            std::fill (carrierMixBuffer_.begin(), carrierMixBuffer_.end(), 0.0f);
            algorithmFn_ (*this, numSamples);
            std::swap (voices_[0], v);
        }
        else
        {
            carrierMixBuffer_.resize (static_cast<size_t> (numSamples));
            v.operators[0].renderBlock (carrierMixBuffer_.data(), nullptr, numSamples);
        }

        // Check if all operators are done AFTER rendering (envelopes may have finished during this block)
        bool anyActive = false;
        for (auto& op : v.operators)
            if (op.isActive()) { anyActive = true; break; }
        if (! anyActive)
        {
            v.live = false;
            v.keydown = false;
            continue;
        }

        // Accumulate into output buffer
        for (int i = 0; i < numSamples; ++i)
            buffer.addSample (0, i, carrierMixBuffer_[static_cast<size_t> (i)] * outGain * voiceScale);
    }

    // Copy to right channel if stereo
    if (buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // Update analysis atomics (for frontend visualization)
    for (int op = 0; op < kNumOperators; ++op)
    {
        float maxLevel = 0.0f;
        for (auto& v : voices_)
            if (v.live)
                maxLevel = juce::jmax (maxLevel, v.operators[op].getCurrentEnvValue());
        opEgLevel_[op].store (maxLevel, std::memory_order_relaxed);
    }
}

} // namespace HDAW
