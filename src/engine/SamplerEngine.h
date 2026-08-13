#pragma once

// SamplerEngine: the polyphonic voice manager for HDAW's internal sampler.
// Owns a fixed pool of SamplerVoice instances, allocates voices on note-on
// (oldest-first stealing when the pool is exhausted), and stages sample swaps
// at the block boundary so no voice ever dereferences a freed SamplerSound.
//
// Thread model:
//   - setSound / setParams / prepare: message thread (caller).
//   - render: audio thread (realtime). No allocation, no locking, no I/O.
// The sample swap is message-thread → audio-thread via an atomic gate: setSound
// stages into pendingSound_ and raises reloadGate_; the next render() call
// adopts it at the block start (applyPendingSwap) by hard-stopping every voice
// BEFORE replacing activeSound_, so stopped voices (which still carry a raw
// pointer to the old sound) are never rendered again.

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <memory>

#include "engine/AHDSREnvelope.h"
#include "engine/SamplerSound.h"
#include "engine/SamplerVoice.h"
#include "engine/SliceDetector.h"

namespace HDAW {

class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 32;

    struct Params
    {
        AHDSRParams        env;
        SamplerVoice::Mode mode       = SamplerVoice::Mode::Classic;
        bool               reverse    = false;
        bool               mono       = false;
        double             glide      = 0.0;
        int                transpose  = 0;
        int                baseNote   = 60;   // for slice chromatic mapping (Task 7)
        float              sampleStart = 0.0f; // normalized 0..1
    };

    SamplerEngine() = default;

    void prepare (double sr, int blockSize);

    // Message thread: stage a new sample; the audio thread adopts it at the
    // next block start (block-boundary swap).
    void setSound (std::shared_ptr<const SamplerSound> sound);

    // Message thread: store params; mirror transpose/mono/glide to atomics for
    // audio-thread reads; push the envelope to every voice.
    void setParams (const Params& p);

    // Lock-free AHDSR param setters (called from message thread via applyInternalParamToDsp).
    // Each stores to an atomic; render() picks up changes at the next block boundary.
    void setAttack  (float v) noexcept { attackAtom_.store (v, std::memory_order_relaxed); paramsDirty_.store (true, std::memory_order_release); }
    void setDecay   (float v) noexcept { decayAtom_.store  (v, std::memory_order_relaxed); paramsDirty_.store (true, std::memory_order_release); }
    void setSustain (float v) noexcept { sustainAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store (true, std::memory_order_release); }
    void setRelease (float v) noexcept { releaseAtom_.store(v, std::memory_order_relaxed); paramsDirty_.store (true, std::memory_order_release); }
    void setTranspose (int v) noexcept { transposeAtom_.store(v, std::memory_order_relaxed); }

    // Audio thread: adopt any pending sample swap, consume MIDI, render voices.
    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // --- inspection (any thread, but typically tests on the calling thread) ---
    int  activeVoiceCount() const noexcept;
    bool allVoicesReferenceCurrentSound() const noexcept;
    const SamplerSound* currentSound() const noexcept { return activeSound_.get(); }

private:
    SamplerVoice voices_[kMaxVoices];
    int          voiceOrder_[kMaxVoices]{};   // monotonic "last started" counter per voice
    int          nextOrder_ = 0;
    double       sr_ = 44100.0;

    std::shared_ptr<const SamplerSound> activeSound_;
    std::shared_ptr<const SamplerSound> pendingSound_;
    std::atomic<bool> reloadGate_{ false };
    std::atomic<bool> hasSound_{ false };

    Params            params_;
    std::atomic<int>  transposeAtom_{ 0 };
    std::atomic<bool> monoAtom_{ false };
    std::atomic<double> glideAtom_{ 0.0 };

    // Atomic AHDSR params for lock-free message→audio param push (L13 fix).
    std::atomic<float> attackAtom_  { 0.005f };
    std::atomic<float> decayAtom_   { 0.1f   };
    std::atomic<float> sustainAtom_ { 0.9f   };
    std::atomic<float> releaseAtom_ { 0.1f   };
    std::atomic<bool>  paramsDirty_ { false  };

    void         applyPendingSwap();                 // audio thread, block start
    void         applyPendingParams();               // audio thread, block start
    void         handleNoteOn (int note, float vel);
    void         handleNoteOff (int note);
    SamplerVoice* allocateVoice();
};

} // namespace HDAW
