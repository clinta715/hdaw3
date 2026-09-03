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

    void reset() noexcept
    {
        dcX_ = 0.0f;
        dcY_ = 0.0f;
    }

    float processSample(float input) const noexcept
    {
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
                constexpr float k = 0.63661977236758134308f;
                const float scale = driven >= 0.0f ? 1.0f + 0.3f * asymmetry_
                                                   : 1.0f - 0.3f * asymmetry_;
                // 1 s, 220 Hz, +24 dB A/B: normalized peak 2.596540 vs literal
                // 0.937110; both THD 31.8870%. Normalize for unity at 0 dB,
                // then bound the effect output rather than retaining both curves.
                shaped = std::atan(driven * scale * k) / std::atan(k);
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
                const float levels = static_cast<float>(1 << bits_);
                shaped = std::tanh(std::round(driven * levels) / levels);
                break;
            }
        }

        return std::clamp(shaped, -1.5f, 1.5f);
    }

    float processSampleDCBlocked(float input) noexcept
    {
        const float shaped = processSample(input);
        float output = shaped - dcX_ + 0.995f * dcY_;
        dcX_ = shaped;

        if (std::abs(output) < 1.0e-20f)
            output = 0.0f;
        dcY_ = output;
        return output;
    }

private:
    int type_ = SoftTanh;
    float driveGain_ = 1.0f;
    float asymmetry_ = 0.0f;
    int bits_ = 8;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
};
