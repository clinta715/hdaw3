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

    // Per-voice post-carrier filters (slice B): prepared once here, so the
    // render path only ever calls the allocation-free processSample(). The
    // current atomics are pushed in so a param written BEFORE prepare()
    // (the slot's prepare ordering) already shapes the first block.
    for (auto& f : voiceFilters_)
    {
        f.prepare (sampleRate);
        f.setParam (HDAW::InternalFilter::Cutoff,
                    filterCutoff_.load (std::memory_order_relaxed));
        f.setParam (HDAW::InternalFilter::Resonance,
                    filterResonance_.load (std::memory_order_relaxed));
        f.setParam (HDAW::InternalFilter::ModeParam,
                    static_cast<float> (filterType_.load (std::memory_order_relaxed)));
    }

    // Default matrix route: the feedback LFO undulates Op6 feedback, so a
    // psy_fm slot has audible feedback movement even when no explicit route
    // was configured. THIS route is what moves the feedback — the retired
    // track-level "LFO target 306" never reached the engine (pid 306 is the
    // >=100 audio-FX compound's slot 2 param 6).
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

// ── Post-carrier filter (slice B) ──

void PsyFmEngine::setFilterParam (int index, float value) noexcept
{
    // Lesson 23: clamp at EVERY entry. A NaN would bypass jlimit's comparison
    // chain entirely (both comparisons are false for NaN), so reject it here
    // and keep the previous value rather than poisoning a coefficient.
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case FilterCutoff:
            filterCutoff_.store (juce::jlimit (20.0f, 20000.0f, value),
                                 std::memory_order_relaxed);
            break;
        case FilterResonance:
            filterResonance_.store (juce::jlimit (0.1f, 10.0f, value),
                                    std::memory_order_relaxed);
            break;
        case FilterType:
            // Int enum: ROUND (a fractional automation value must report what
            // the DSP runs), the TrackFXSlot/InternalFilter contract.
            filterType_.store (juce::jlimit (0, 2, juce::roundToInt (value)),
                               std::memory_order_relaxed);
            break;
        case FilterKeyTrack:
            filterKeyTrack_.store (juce::jlimit (0.0f, 1.0f, value),
                                   std::memory_order_relaxed);
            break;
        case FilterEnvAmount:
            filterEnvAmount_.store (juce::jlimit (0.0f, 1.0f, value),
                                    std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

float PsyFmEngine::getFilterParam (int index) const noexcept
{
    switch (index)
    {
        case FilterCutoff:    return filterCutoff_.load (std::memory_order_relaxed);
        case FilterResonance: return filterResonance_.load (std::memory_order_relaxed);
        case FilterType:      return static_cast<float> (filterType_.load (std::memory_order_relaxed));
        case FilterKeyTrack:  return filterKeyTrack_.load (std::memory_order_relaxed);
        case FilterEnvAmount: return filterEnvAmount_.load (std::memory_order_relaxed);
        default:              return 0.0f;
    }
}

bool PsyFmEngine::isFilterEngaged() const noexcept
{
    // The back-compat bypass: a 20 kHz LP is NOT transparent, so the default
    // patch must SKIP the filter entirely rather than run a "neutral" one.
    return filterCutoff_.load (std::memory_order_relaxed) < 19999.0f
        || filterKeyTrack_.load (std::memory_order_relaxed) != 0.0f
        || filterEnvAmount_.load (std::memory_order_relaxed) != 0.0f;
}

// ── Modulation matrix ──

void PsyFmEngine::setModMatrix (PsyFmModMatrix matrix)
{
    // Lesson 13 / Gate 3: the audio thread reads matrix_ every block; the
    // swap (vector realloc inside the moved-in matrix) must not race it.
    const juce::SpinLock::ScopedLockType lock (matrixLock_);
    matrix_ = std::move (matrix);
}

void PsyFmEngine::snapshotModState (std::vector<PsyFmModRoute>& outRoutes,
                                    float outBaseRatios[kNumOperators],
                                    float& outBaseFeedback,
                                    PsyFmModSourcePool& outPool)
{
    // Same lock contract as setModMatrix: render() TryLocks and skips the
    // matrix pass on contention, so a short hold here is benign. All copies
    // are small (a handful of routes + fixed-size params).
    const juce::SpinLock::ScopedLockType lock (matrixLock_);
    outRoutes = matrix_.getRoutes();
    for (int i = 0; i < kNumOperators; ++i)
        outBaseRatios[i] = baseRatios_[i];
    outBaseFeedback = baseFeedback_;
    outPool = sources_;
}

// ── Bar clock ──

void PsyFmEngine::onBarBoundary (int barCounter)
{
    // Idempotent per bar: Track::processBlock forwards the transport bar index
    // every block, so this runs many times per bar. Only the first call for a
    // new bar may move the pool.
    if (barCounter == lastBar_)
        return;

    if (barCounter < lastBar_)
    {
        // Rewind / loop back to an earlier bar: restore the base sweep rate so
        // the riser does not keep the acceleration it accumulated last pass.
        sources_.ratioSweepLFORateHz = baseRatioSweepRateHz_;
    }
    else if (barCounter % 8 == 0)
    {
        // Riser accelerate-every-8-bars route.
        sources_.ratioSweepLFORateHz = juce::jmin (sources_.ratioSweepLFORateHz * 1.3f, 40.0f);
    }

    lastBar_ = barCounter;

    // Phrase position within the 4-bar phrase: 0, 0.25, 0.5, 0.75. `%` on a
    // negative bar (a rewind past bar 0) must stay non-negative, hence the
    // double modulo.
    const int phraseBar = ((barCounter % 4) + 4) % 4;
    sources_.barClockValue = static_cast<float> (phraseBar) * 0.25f;
}

void PsyFmEngine::setBaseRatioSweepRateHz (float hz) noexcept
{
    baseRatioSweepRateHz_ = hz;
    sources_.ratioSweepLFORateHz = hz;
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

    // Clear the recycled voice's filter integrator state so the previous
    // note's tail cannot ring into this one (a no-op at the neutral default,
    // where the filter is bypassed and never written).
    voiceFilters_[static_cast<size_t> (v - voices_)].reset();

    // Velocity is an ENGINE-WIDE pool source, not per voice: the matrix is
    // applied once per block (before the voice loop), so a per-voice velocity
    // could not reach it — block rate is the only granularity the matrix has.
    sources_.velocityValue = static_cast<float> (velocity) / 127.0f;

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
            // Freeze the polyphony normalization at release: the tail keeps
            // the scale it had while held (see render()).
            int held = 0;
            for (const auto& other : voices_)
                if (other.live && other.keydown)
                    ++held;
            v.releaseScale = 1.0f / static_cast<float> (std::max (1, held));
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
        else if (msg.isController() && msg.getControllerNumber() == 1)
            // CC1 (mod wheel) -> pool source. Runs BEFORE the advanceControlRate/
            // matrix_.apply pass below in the same render call, so a CC landing
            // in this block already shapes it.
            sources_.modWheelValue = static_cast<float> (msg.getControllerValue()) / 127.0f;
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

    // Polyphony normalization — prevents additive clipping. Held voices
    // share 1/held; release-tail voices keep the scale captured at
    // note-off (Voice::releaseScale), so a monophonic riff's newest note
    // always renders at full normalization instead of sinking under its
    // own stacked release tails.
    int heldCount = 0;
    for (auto& v : voices_)
        if (v.live && v.keydown) ++heldCount;

    // Post-carrier filter params (slice B), read ONCE per block from the
    // atomics. filterEngaged is the back-compat bypass: at the neutral
    // defaults no voice's filter is touched at all.
    const bool  filterEngaged = isFilterEngaged();
    const float filtBaseCutoff = filterCutoff_.load (std::memory_order_relaxed);
    const float filtResonance  = filterResonance_.load (std::memory_order_relaxed);
    const int   filtMode       = filterType_.load (std::memory_order_relaxed);
    const float filtKeyTrack   = filterKeyTrack_.load (std::memory_order_relaxed);
    const float filtEnvAmount  = filterEnvAmount_.load (std::memory_order_relaxed);

    for (int vi = 0; vi < kMaxVoices; ++vi)
    {
        auto& v = voices_[vi];
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

        // Voice reaping — gate on the CARRIER (op 0) only. Each algorithm
        // renders a SUBSET of the six operators (e.g. acidLeadAlgorithm uses
        // ops 0+5), so the remaining operators' juce::ADSR envelopes are
        // started by noteOn() but never advanced by renderBlock(): their
        // isActive() stays true forever. The old all-operators check never
        // fired, live voices accumulated up to kMaxVoices and the block
        // normalization pinned the render gain at 1/min(N, kMaxVoices).
        // The carrier (op 0) is rendered by every algorithm; when its
        // envelope decays the voice is inaudible regardless of the mod
        // operators (they only feed op 0's phase input), so this is the
        // correct "voice is done" signal.
        const bool carrierActive = v.operators[0].isActive();
        if (! carrierActive)
        {
            v.live = false;
            v.keydown = false;
            continue;
        }

        // Post-carrier filter (slice B) — per voice, on the carrier output,
        // BEFORE the voice is summed into the buffer. Bypassed entirely at the
        // neutral defaults (back-compat hard gate). Effective cutoff:
        //   eff = base * 2^((midiNote-60)/12)                  [key-track]
        //   eff += envAmount * (20000 - eff) * carrierEnvLevel [env amount]
        // clamped to the def range; the amplitude envelope is the CARRIER's
        // existing ADSR level (no new envelope — the plan's contract).
        if (filterEngaged)
        {
            float eff = filtBaseCutoff;
            if (filtKeyTrack != 0.0f)
            {
                const float semis = static_cast<float> (v.midiNote - 60);
                eff *= std::pow (2.0f, filtKeyTrack * semis / 12.0f);
            }
            if (filtEnvAmount != 0.0f)
            {
                const float eg = juce::jlimit (0.0f, 1.0f, v.operators[0].getCurrentEnvValue());
                eff += filtEnvAmount * (20000.0f - eff) * eg;
            }
            eff = juce::jlimit (20.0f, 20000.0f, eff);

            auto& vf = voiceFilters_[vi];
            vf.setParam (HDAW::InternalFilter::Cutoff, eff);
            vf.setParam (HDAW::InternalFilter::Resonance, filtResonance);
            vf.setParam (HDAW::InternalFilter::ModeParam, static_cast<float> (filtMode));

            float* mix = carrierMixBuffer_.data();
            for (int i = 0; i < numSamples; ++i)
                mix[i] = vf.processSample (0, mix[i]);
        }

        // Accumulate into output buffer
        const float voiceScale = v.keydown
            ? 1.0f / static_cast<float> (std::max (1, heldCount))
            : v.releaseScale;
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
