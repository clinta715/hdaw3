#include "engine/SamplerEngine.h"

#include <limits>

namespace HDAW {

void SamplerEngine::prepare (double sr, int /*blockSize*/)
{
    sr_ = sr;
    for (auto& v : voices_)
        v.prepare (sr);
}

void SamplerEngine::setSound (std::shared_ptr<const SamplerSound> sound)
{
    // Message thread: stage the swap; audio thread adopts at next block start.
    pendingSound_ = std::move (sound);
    reloadGate_.store (true, std::memory_order_release);
}

void SamplerEngine::setParams (const Params& p)
{
    params_ = p;
    transposeAtom_.store (p.transpose, std::memory_order_relaxed);
    monoAtom_.store (p.mono, std::memory_order_relaxed);
    glideAtom_.store (p.glide, std::memory_order_relaxed);
    for (auto& v : voices_)
        v.setEnvelope (p.env);
}

void SamplerEngine::applyPendingSwap()
{
    if (! reloadGate_.load (std::memory_order_acquire))
        return;

    // Hard-stop every voice BEFORE replacing the sound: a stopped voice is
    // marked done and is never rendered again, so its raw SamplerSound pointer
    // is never dereferenced after this point — the use-after-free window is
    // closed at the block boundary.
    for (auto& v : voices_)
        v.stop();

    activeSound_ = std::move (pendingSound_);
    reloadGate_.store (false, std::memory_order_release);
    hasSound_.store (activeSound_ != nullptr, std::memory_order_release);
}

void SamplerEngine::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    applyPendingSwap();

    const int numSamples = buffer.getNumSamples();
    if (! hasSound_.load (std::memory_order_acquire) || numSamples <= 0)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    // Consume MIDI: note-on allocates+starts a voice; note-off releases all
    // active voices (polyphonic note-specific matching is a Task 6 refinement).
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            handleNoteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            handleNoteOff (m.getNoteNumber());
    }
    midi.clear();   // the sampler owns its MIDI (instrument slot, not a clip)

    buffer.clear(); // instrument = source (overwrite, voices ADD into it)
    for (auto& v : voices_)
    {
        if (v.isDone())
            continue;
        v.render (buffer, numSamples);
        if (v.isDone())
            v.stop();
    }
}

SamplerVoice* SamplerEngine::allocateVoice()
{
    // Find a free (done) voice; else steal the oldest (lowest voiceOrder_).
    SamplerVoice* freeV   = nullptr;
    SamplerVoice* oldestV = nullptr;
    int           oldestOrder = std::numeric_limits<int>::max();

    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (voices_[i].isDone())
        {
            freeV = &voices_[i];
            break;
        }
        if (voiceOrder_[i] < oldestOrder)
        {
            oldestOrder = voiceOrder_[i];
            oldestV     = &voices_[i];
        }
    }

    SamplerVoice* target = freeV ? freeV : oldestV;
    if (target != nullptr)
    {
        const int idx = static_cast<int> (target - voices_);
        voiceOrder_[idx] = ++nextOrder_;
    }
    return target;
}

void SamplerEngine::handleNoteOn (int note, float vel)
{
    const auto* s = activeSound_.get();
    if (s == nullptr)
        return;

    // Mono mode: only the most-recent voice may play; cut older ones immediately.
    if (monoAtom_.load (std::memory_order_relaxed))
        for (auto& v : voices_)
            if (! v.isDone())
                v.stop();

    SamplerVoice* v = allocateVoice();
    if (v == nullptr)
        return;

    const Params p = params_;
    const int    noteOffset = note + transposeAtom_.load (std::memory_order_relaxed);
    // start() snapshots the envelope internally, so the voice carries a
    // self-consistent copy of the DSP state for this note's lifetime.
    v->start (s, noteOffset, vel, p.mode, p.env, p.reverse);
}

void SamplerEngine::handleNoteOff (int /*note*/)
{
    // Task 5 scope: release all active voices on any note-off (polyphonic
    // note→voice matching is a Task 6 refinement). OneShot/Slice voices
    // ignore note-off inside SamplerVoice::noteOff().
    for (auto& v : voices_)
        if (! v.isDone())
            v.noteOff();
}

int SamplerEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices_)
        if (! v.isDone())
            ++n;
    return n;
}

bool SamplerEngine::allVoicesReferenceCurrentSound() const noexcept
{
    const auto* cur = activeSound_.get();
    for (const auto& v : voices_)
        if (! v.isDone() && v.sound() != cur)
            return false;   // an active voice points at a stale/freed sound
    return true;
}

} // namespace HDAW
