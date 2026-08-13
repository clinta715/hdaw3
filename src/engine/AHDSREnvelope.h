#pragma once

#include <algorithm>
#include <cmath>

namespace HDAW {

struct AHDSRParams
{
    float attack  = 0.005f;
    float hold    = 0.0f;
    float decay   = 0.1f;
    float sustain = 0.9f;
    float release = 0.1f;
};

// Per-voice AHDSR amplitude envelope. Header-only; next() is realtime-safe
// (noexcept, no allocation, no locking) and intended to be called once per
// sample on the audio thread.
//
// Ordering convention: compute-then-increment. On noteOn() the phase starts
// at 0, so the first next() yields the start of attack (~0.0). A stage
// completes after exactly <stage>Seconds worth of samples; the sample that
// crosses the boundary clamps to the target and transitions to the next
// stage on the same call.
class AHDSREnvelope
{
public:
    void setSampleRate (double sr) noexcept { sr_ = sr; recalc(); }
    void set (const AHDSRParams& p) noexcept { params_ = p; recalc(); }

    void noteOn() noexcept
    {
        stage_  = Attack;
        phase_  = 0.0;
        gain_   = 0.0f;
        active_ = true;
    }

    void noteOff() noexcept
    {
        if (stage_ != Idle && stage_ != Released)
        {
            releaseStart_ = gain_;
            stage_        = Released;
            phase_        = 0.0;
        }
    }

    inline float next() noexcept;
    float current() const noexcept { return gain_; }
    bool   isActive() const noexcept { return active_; }

private:
    void recalc() noexcept
    {
        attackSamples_  = std::max (1.0, params_.attack  * sr_);
        holdSamples_    = std::max (0.0, params_.hold    * sr_);
        decaySamples_   = std::max (1.0, params_.decay   * sr_);
        releaseSamples_ = std::max (1.0, params_.release * sr_);
    }

    enum Stage { Idle, Attack, Hold, Decay, Sustain, Released } stage_ = Idle;

    AHDSRParams params_;
    double sr_ = 44100.0;
    double attackSamples_  = 1.0;
    double holdSamples_    = 0.0;
    double decaySamples_   = 1.0;
    double releaseSamples_ = 1.0;
    double phase_          = 0.0;
    float  gain_           = 0.0f;
    float  releaseStart_   = 0.0f;
    bool   active_         = false;
};

inline float AHDSREnvelope::next() noexcept
{
    switch (stage_)
    {
        case Idle:
            gain_ = 0.0f;
            break;

        case Attack:
        {
            // Compute current gain from phase, then advance.
            double a = phase_ / attackSamples_;
            gain_ = static_cast<float> (a);
            phase_ += 1.0;
            if (phase_ >= attackSamples_)
            {
                gain_   = 1.0f;                       // clamp at attack peak
                stage_  = (holdSamples_ > 0.0) ? Hold : Decay;
                phase_  = 0.0;
            }
            break;
        }

        case Hold:
        {
            gain_ = 1.0f;
            phase_ += 1.0;
            if (phase_ >= holdSamples_)
            {
                stage_ = Decay;
                phase_ = 0.0;
            }
            break;
        }

        case Decay:
        {
            double d = phase_ / decaySamples_;
            gain_ = static_cast<float> (1.0 - (1.0 - params_.sustain) * d);
            phase_ += 1.0;
            if (phase_ >= decaySamples_)
            {
                gain_  = params_.sustain;             // clamp at sustain level
                stage_ = Sustain;
                phase_ = 0.0;
            }
            break;
        }

        case Sustain:
            gain_ = params_.sustain;
            break;

        case Released:
        {
            double r = phase_ / releaseSamples_;
            gain_ = static_cast<float> (releaseStart_ * (1.0 - r));
            phase_ += 1.0;
            if (phase_ >= releaseSamples_)
            {
                gain_   = 0.0f;
                stage_  = Idle;
                active_ = false;
            }
            break;
        }
    }
    return gain_;
}

} // namespace HDAW
