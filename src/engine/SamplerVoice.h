#pragma once

// SamplerVoice: a single playing instance of a SamplerSound. Fractional-reads
// the sound with 4-point Lagrange interpolation, applies the voice's AHDSR
// amplitude envelope, and ADDS the result into an output buffer (the engine
// clears the buffer first, so accumulation = polyphony mix).
//
// Realtime-safe by construction: render() is noexcept, performs no heap
// allocation, no locking, no I/O. All pitch/envelope setup happens in
// start()/prepare() which the engine calls on the message thread at note-on.

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/AHDSREnvelope.h"
#include "engine/SamplerInterpolator.h"
#include "engine/SamplerSound.h"

namespace HDAW {

class SamplerVoice
{
public:
    enum class Mode { Classic, OneShot, Slice };

    SamplerVoice() = default;

    void prepare (double sr) noexcept
    {
        sr_ = sr;
        env_.setSampleRate (sr);
    }

    void setEnvelope (const AHDSRParams& params) noexcept
    {
        env_.set (params);
    }

    void start (const SamplerSound* sound, int note, float velocity,
                Mode mode, const AHDSRParams& params, bool reverse) noexcept
    {
        sound_    = sound;
        mode_     = mode;
        velocity_ = velocity;
        dir_      = reverse ? -1.0 : 1.0;

        if (sound != nullptr)
        {
            // Playback rate relative to the engine sample rate: pitch shift
            // from the note/rootNote offset, then resample ratio between the
            // sound's native SR and the engine SR.
            pitchRatio_ = std::pow (2.0, (note - sound->rootNote) / 12.0)
                        * (sound->nativeSampleRate / sr_);

            readPos_ = reverse
                ? static_cast<double> (sound->endFrame() - 1)
                : static_cast<double> (sound->startFrame());
        }
        else
        {
            pitchRatio_ = 1.0;
            readPos_    = 0.0;
        }

        env_.setSampleRate (sr_);
        env_.set (params);
        env_.noteOn();
        inRelease_ = false;
        useSliceBounds_ = false;
        done_      = false;
    }

    void noteOff() noexcept
    {
        if (mode_ == Mode::OneShot || mode_ == Mode::Slice)
            return; // one-shot/slice play to end; ignore note-off
        env_.noteOff();
        inRelease_ = true;
    }

    // Start a voice in slice mode with explicit start/end frames (override the sound's defaults).
    void startSlice (const SamplerSound* sound, int note, float velocity,
                     const AHDSRParams& params, int64_t sliceStartFrame, int64_t sliceEndFrame) noexcept
    {
        sound_    = sound;
        mode_     = Mode::Slice;
        velocity_ = velocity;
        dir_      = 1.0;

        if (sound != nullptr)
        {
            pitchRatio_ = std::pow (2.0, (note - sound->rootNote) / 12.0)
                        * (sound->nativeSampleRate / sr_);
            readPos_ = static_cast<double> (sliceStartFrame);
        }
        else
        {
            pitchRatio_ = 1.0;
            readPos_    = 0.0;
        }

        // Store the slice boundaries so the render loop uses them instead of the sound's defaults.
        sliceStart_ = sliceStartFrame;
        sliceEnd_   = sliceEndFrame;
        useSliceBounds_ = true;

        env_.setSampleRate (sr_);
        env_.set (params);
        env_.noteOn();
        inRelease_ = false;
        done_      = false;
    }

    void stop() noexcept
    {
        // Immediate cut: mark done so render() produces no further output.
        done_ = true;
    }

    bool   isDone()       const noexcept { return done_; }
    double readPosition() const noexcept { return readPos_; }

    // The sound this voice is currently rendering (nullptr if idle/done).
    // Used by the engine to verify the block-boundary swap invariant: every
    // non-done voice must point at the engine's active sound, never a freed one.
    const SamplerSound* sound() const noexcept { return sound_; }

    // Renders numSamples into `out`, ADDING into whatever is already there.
    // RT-safe: noexcept, no allocation, no locking.
    void render (juce::AudioBuffer<float>& out, int numSamples) noexcept
    {
        juce::ScopedNoDenormals dnd;

        if (done_ || sound_ == nullptr || numSamples <= 0)
            return;

        const int    outCh = out.getNumChannels();
        const int    sn    = sound_->numChannels;
        const float  vel   = velocity_;
        const double startF = useSliceBounds_
            ? static_cast<double> (sliceStart_)
            : static_cast<double> (sound_->startFrame());
        const double endF   = useSliceBounds_
            ? static_cast<double> (sliceEnd_)
            : static_cast<double> (sound_->endFrame());
        const bool   looping = sound_->loopEnabled && !inRelease_;
        const double loopStartF = static_cast<double> (sound_->loopStartFrame());
        const double loopEndF   = static_cast<double> (sound_->loopEndFrame());

        for (int i = 0; i < numSamples; ++i)
        {
            if (done_)
                break;

            const float g = env_.next() * vel;

            // Channel-0 interpolation computed once: used directly for mono
            // sounds and as the source copied to every output channel when the
            // sound has fewer channels than the buffer. Stereo+ sounds read
            // EACH channel independently (the buggy draft read data[c][0] for
            // the second channel -- a constant; we interpolate per channel).
            const float sample0 =
                (sn >= 1 && sound_->data[0] != nullptr)
                    ? lagrange4 (sound_->data[0], sound_->length, readPos_)
                    : 0.0f;

            for (int c = 0; c < outCh; ++c)
            {
                float s;
                if (sn > 1 && c < sn && sound_->data[c] != nullptr)
                    s = lagrange4 (sound_->data[c], sound_->length, readPos_);
                else
                    s = sample0; // mono: same value into every output channel

                out.addSample (c, i, s * g);
            }

            // Envelope finished its release tail -> voice is silent and done.
            if (! env_.isActive() && inRelease_)
            {
                done_ = true;
                break;
            }

            // Advance the read head and apply loop / end-boundary rules.
            readPos_ += dir_ * pitchRatio_;

            if (mode_ == Mode::Classic)
            {
                if (looping)
                {
                    if (dir_ > 0.0 && readPos_ >= loopEndF)
                        readPos_ = loopStartF + (readPos_ - loopEndF);
                    else if (dir_ < 0.0 && readPos_ <= loopStartF)
                        readPos_ = loopEndF - (loopStartF - readPos_);
                }
                else
                {
                    // Classic non-loop: hitting either end finishes the voice.
                    if (dir_ > 0.0 && readPos_ >= endF)
                    {
                        readPos_ = endF - 1.0;
                        done_ = true;
                    }
                    else if (dir_ < 0.0 && readPos_ <= startF)
                    {
                        readPos_ = startF;
                        done_ = true;
                    }
                }
            }
            else // OneShot / Slice: reaching an end triggers the release tail.
            {
                bool hitEnd = false;
                if (dir_ > 0.0 && readPos_ >= endF)
                {
                    readPos_ = endF - 1.0;
                    hitEnd = true;
                }
                else if (dir_ < 0.0 && readPos_ <= startF)
                {
                    readPos_ = startF;
                    hitEnd = true;
                }
                if (hitEnd && ! inRelease_)
                {
                    env_.noteOff();
                    inRelease_ = true;
                }
            }
        }
    }

private:
    const SamplerSound* sound_ = nullptr;
    AHDSREnvelope       env_;
    Mode                mode_ = Mode::Classic;

    double sr_          = 44100.0;
    double pitchRatio_  = 1.0;
    double readPos_     = 0.0;
    double dir_         = 1.0;     // +1 forward, -1 reverse

    float  velocity_    = 1.0f;
    bool   inRelease_   = false;
    bool   done_        = true;    // nothing started yet -> done

    int64_t sliceStart_ = 0;
    int64_t sliceEnd_   = 0;
    bool    useSliceBounds_ = false;
};

} // namespace HDAW
