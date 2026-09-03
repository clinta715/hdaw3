#pragma once

#include <algorithm>
#include <cmath>

class SaturatorEngine
{
public:
    enum Type
    {
        SoftTanh = 0,
        SoftAtan = 1,
        Hard = 2,
        Bitcrush = 3
    };

    SaturatorEngine() noexcept
    {
        setSampleRate(defaultSampleRate_);
    }

    void setType(int type) noexcept
    {
        type_ = std::clamp(type, static_cast<int>(SoftTanh), static_cast<int>(Bitcrush));
    }

    void setDriveDb(float driveDb) noexcept
    {
        if (!std::isfinite(driveDb))
            driveDb = 0.0f;

        driveGain_ = std::pow(10.0f, std::clamp(driveDb, 0.0f, 40.0f) / 20.0f);
    }

    void setAsymmetry(float asymmetry) noexcept
    {
        asymmetry_ = std::isfinite(asymmetry) ? std::clamp(asymmetry, -1.0f, 1.0f) : 0.0f;
    }

    void setBits(float bits) noexcept
    {
        if (!std::isfinite(bits))
            bits = 8.0f;

        bits_ = static_cast<int>(std::round(std::clamp(bits, 2.0f, 16.0f)));
    }

    void setSampleRate(double sampleRate) noexcept
    {
        if (!std::isfinite(sampleRate))
            sampleRate = defaultSampleRate_;

        sampleRate = std::max(sampleRate, minimumSampleRate_);
        dcCoefficient_ = static_cast<float>(
            std::exp(-2.0 * pi_ * dcBlockerCutoffHz_ / sampleRate));
    }

    void reset() noexcept
    {
        dcX_ = 0.0f;
        dcY_ = 0.0f;
    }

    float processSample(float input) const noexcept
    {
        // Invalid samples are silence and never enter either shaping or recursive state.
        if (!std::isfinite(input))
            return 0.0f;

        const float driven = input * driveGain_;
        float shaped = 0.0f;

        switch (type_)
        {
            case SoftTanh:
            {
                const float scale = driven >= 0.0f ? 1.0f + 0.3f * asymmetry_
                                                   : 1.0f - 0.3f * asymmetry_;
                shaped = std::tanh(driven * scale);
                break;
            }

            case SoftAtan:
            {
                const float scale = driven >= 0.0f ? 1.0f + 0.3f * asymmetry_
                                                   : 1.0f - 0.3f * asymmetry_;
                // At +24 dB the old normalized candidate measured peak 2.596540,
                // THD 31.8870%; the literal measured 0.937110 at the same THD.
                // This calibration is chosen for unity slope and a smooth +/-1.5 asymptote.
                constexpr float inputScale = 1.04719755119659774615f;
                constexpr float outputScale = 0.95492965855137201461f;
                shaped = outputScale * std::atan(inputScale * driven * scale);
                break;
            }

            case Hard:
            {
                const float positiveLimit = 1.0f + 0.3f * asymmetry_;
                const float negativeLimit = 1.0f - 0.3f * asymmetry_;
                shaped = std::clamp(driven, -negativeLimit, positiveLimit);
                break;
            }

            case Bitcrush:
            default:
            {
                const auto levels = static_cast<unsigned int>(1u << bits_);
                const float bounded = std::clamp(driven, -1.0f, 1.0f);
                const float maxIndex = static_cast<float>(levels - 1u);
                const float index = std::round((bounded + 1.0f) * 0.5f * maxIndex);
                const float quantized = -1.0f + 2.0f * index / maxIndex;
                // Quantize to exactly 2^bits bipolar codes, then apply tanh safety shaping.
                shaped = std::tanh(quantized);
                break;
            }
        }

        return std::isfinite(shaped) ? shaped : 0.0f;
    }

    float processSampleDCBlocked(float input) noexcept
    {
        if (!std::isfinite(dcX_) || !std::isfinite(dcY_))
            reset();

        const float shaped = snapNearZero(processSample(input));
        float output = shaped - dcX_ + dcCoefficient_ * dcY_;
        dcX_ = shaped;

        if (!std::isfinite(output))
        {
            reset();
            return 0.0f;
        }

        output = snapNearZero(output);
        dcY_ = output;

        // Shaping is bounded to +/-1.5. The unclipped high-pass output may reach
        // +/-3 during a polarity transition before the later slot output trim.
        return output;
    }

private:
    static float snapNearZero(float value) noexcept
    {
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static constexpr double pi_ = 3.14159265358979323846;
    static constexpr double dcBlockerCutoffHz_ = 20.0;
    static constexpr double defaultSampleRate_ = 48000.0;
    static constexpr double minimumSampleRate_ = 1000.0;

    int type_ = SoftTanh;
    float driveGain_ = 1.0f;
    float asymmetry_ = 0.0f;
    int bits_ = 8;
    float dcCoefficient_ = 0.0f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
};
