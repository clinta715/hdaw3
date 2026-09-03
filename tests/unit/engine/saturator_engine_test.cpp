#include <gtest/gtest.h>

#include "engine/SaturatorEngine.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr float pi = 3.14159265358979323846f;

float processConfigured(int type, float driveDb, float asymmetry, float bits, float input)
{
    SaturatorEngine saturator;
    saturator.setType(type);
    saturator.setDriveDb(driveDb);
    saturator.setAsymmetry(asymmetry);
    saturator.setBits(bits);
    return saturator.processSample(input);
}
}

TEST(SaturatorEngine, SmallSignalNearIdentity)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftTanh);
    saturator.setDriveDb(0.0f);
    saturator.setAsymmetry(0.0f);

    constexpr float input = 0.001f;
    EXPECT_NEAR(saturator.processSample(input), input, 1.0e-6f);
}

TEST(SaturatorEngine, SoftAtanHasUnitySmallSignalSlopeAndNoHardClampPlateau)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftAtan);
    saturator.setDriveDb(0.0f);
    saturator.setAsymmetry(0.0f);

    constexpr float tinyInput = 1.0e-4f;
    EXPECT_NEAR(saturator.processSample(tinyInput) / tinyInput, 1.0f, 1.0e-4f);

    const float large = saturator.processSample(10.0f);
    const float larger = saturator.processSample(100.0f);
    EXPECT_LT(large, larger);
    EXPECT_LT(larger, 1.5f);
}

TEST(SaturatorEngine, DriveSweepFinite)
{
    SaturatorEngine saturator;
    for (int type = SaturatorEngine::SoftTanh; type <= SaturatorEngine::Bitcrush; ++type)
    {
        saturator.setType(type);
        for (float driveDb : { 0.0f, 12.0f, 24.0f, 40.0f })
        {
            saturator.setDriveDb(driveDb);
            for (float input : { -1.0f, -0.3f, 0.0f, 0.3f, 1.0f })
            {
                const float output = saturator.processSample(input);
                EXPECT_TRUE(std::isfinite(output)) << "type=" << type << " driveDb=" << driveDb;
                EXPECT_LE(std::abs(output), 1.5f) << "type=" << type << " driveDb=" << driveDb;
            }
        }
    }
}

TEST(SaturatorEngine, AsymmetryEmitsNoDC)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftTanh);
    saturator.setDriveDb(24.0f);
    saturator.setAsymmetry(1.0f);

    constexpr int sampleRate = 48000;
    constexpr int settlingSamples = sampleRate;
    constexpr int measuredSamples = sampleRate;
    double rawSum = 0.0;
    double blockedSum = 0.0;
    for (int i = 0; i < settlingSamples + measuredSamples; ++i)
    {
        const float input = std::sin(2.0f * pi * 1000.0f * static_cast<float>(i)
                                     / static_cast<float>(sampleRate));
        const float raw = saturator.processSample(input);
        const float output = saturator.processSampleDCBlocked(input);
        if (i >= settlingSamples)
        {
            rawSum += static_cast<double>(raw);
            blockedSum += static_cast<double>(output);
        }
    }

    const double rawMean = rawSum / static_cast<double>(measuredSamples);
    const double blockedMean = blockedSum / static_cast<double>(measuredSamples);
    ASSERT_GT(std::abs(rawMean), 1.0e-3);
    EXPECT_LT(std::abs(blockedMean), 1.0e-3);
}

TEST(SaturatorEngine, NonFiniteSamplesAreSilenceAndRecoverImmediately)
{
    constexpr std::array<float, 3> nonFiniteInputs {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    for (int type = SaturatorEngine::SoftTanh; type <= SaturatorEngine::Bitcrush; ++type)
    {
        for (float nonFinite : nonFiniteInputs)
        {
            SaturatorEngine saturator;
            saturator.setType(type);
            saturator.setDriveDb(12.0f);
            saturator.setAsymmetry(0.5f);
            saturator.setBits(6.0f);

            EXPECT_FLOAT_EQ(saturator.processSample(nonFinite), 0.0f) << "type=" << type;
            const float rawRecovered = saturator.processSample(0.125f);
            EXPECT_TRUE(std::isfinite(rawRecovered)) << "type=" << type;

            saturator.reset();
            EXPECT_FLOAT_EQ(saturator.processSampleDCBlocked(nonFinite), 0.0f) << "type=" << type;
            const float blockedRecovered = saturator.processSampleDCBlocked(0.125f);
            EXPECT_FLOAT_EQ(blockedRecovered, rawRecovered) << "type=" << type;
        }
    }
}

TEST(SaturatorEngine, DCBlockerHasEquivalentDecayAcrossSampleRates)
{
    const auto outputAfter = [](double sampleRate, double seconds)
    {
        SaturatorEngine saturator;
        saturator.setType(SaturatorEngine::Hard);
        saturator.setSampleRate(sampleRate);

        float output = 0.0f;
        const int sampleCount = static_cast<int>(std::round(sampleRate * seconds));
        for (int i = 0; i < sampleCount; ++i)
            output = saturator.processSampleDCBlocked(0.5f);
        return output;
    };

    constexpr double durationSeconds = 0.05;
    const float at44100 = outputAfter(44100.0, durationSeconds);
    const float at48000 = outputAfter(48000.0, durationSeconds);
    const float at192000 = outputAfter(192000.0, durationSeconds);

    EXPECT_NEAR(at44100, at48000, 2.0e-5f);
    EXPECT_NEAR(at48000, at192000, 2.0e-5f);

    SaturatorEngine defaultRate;
    SaturatorEngine invalidRate;
    invalidRate.setSampleRate(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FLOAT_EQ(defaultRate.processSampleDCBlocked(0.5f),
                    invalidRate.processSampleDCBlocked(0.5f));

    SaturatorEngine minimumRate;
    SaturatorEngine clampedRate;
    minimumRate.setSampleRate(1000.0);
    clampedRate.setSampleRate(1.0);
    for (int i = 0; i < 32; ++i)
        EXPECT_FLOAT_EQ(minimumRate.processSampleDCBlocked(0.5f),
                        clampedRate.processSampleDCBlocked(0.5f));
}

TEST(SaturatorEngine, ResetRestoresDeterministicDCState)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftTanh);
    saturator.setDriveDb(18.0f);
    saturator.setAsymmetry(0.75f);

    std::array<float, 128> firstPass {};
    for (size_t i = 0; i < firstPass.size(); ++i)
    {
        const float input = std::sin(2.0f * pi * 440.0f * static_cast<float>(i) / 48000.0f);
        firstPass[i] = saturator.processSampleDCBlocked(input);
    }

    saturator.reset();
    for (size_t i = 0; i < firstPass.size(); ++i)
    {
        const float input = std::sin(2.0f * pi * 440.0f * static_cast<float>(i) / 48000.0f);
        EXPECT_FLOAT_EQ(saturator.processSampleDCBlocked(input), firstPass[i]);
    }

    saturator.processSampleDCBlocked(1.0f);
    saturator.reset();
    EXPECT_FLOAT_EQ(saturator.processSampleDCBlocked(0.25f), saturator.processSample(0.25f));
}

TEST(SaturatorEngine, SettersClampRoundAndRejectNonFiniteValues)
{
    constexpr float input = 0.314159f;

    EXPECT_FLOAT_EQ(processConfigured(-10, 12.0f, 0.0f, 8.0f, input),
                    processConfigured(SaturatorEngine::SoftTanh, 12.0f, 0.0f, 8.0f, input));
    EXPECT_FLOAT_EQ(processConfigured(10, 12.0f, 0.0f, 8.0f, input),
                    processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 8.0f, input));

    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::SoftTanh, 12.0f, -2.0f, 8.0f, input),
                    processConfigured(SaturatorEngine::SoftTanh, 12.0f, -1.0f, 8.0f, input));
    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::SoftTanh, 12.0f, 2.0f, 8.0f, input),
                    processConfigured(SaturatorEngine::SoftTanh, 12.0f, 1.0f, 8.0f, input));

    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, -4.0f, input),
                    processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 2.0f, input));
    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 2.49f, input),
                    processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 2.0f, input));
    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 2.51f, input),
                    processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 3.0f, input));
    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 30.0f, input),
                    processConfigured(SaturatorEngine::Bitcrush, 12.0f, 0.0f, 16.0f, input));

    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Hard, -20.0f, 0.0f, 8.0f, 0.01f),
                    processConfigured(SaturatorEngine::Hard, 0.0f, 0.0f, 8.0f, 0.01f));
    EXPECT_FLOAT_EQ(processConfigured(SaturatorEngine::Hard, 100.0f, 0.0f, 8.0f, 0.01f),
                    processConfigured(SaturatorEngine::Hard, 40.0f, 0.0f, 8.0f, 0.01f));

    SaturatorEngine saturator;
    saturator.setDriveDb(std::numeric_limits<float>::quiet_NaN());
    saturator.setAsymmetry(std::numeric_limits<float>::infinity());
    saturator.setBits(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isfinite(saturator.processSampleDCBlocked(input)));
}

TEST(SaturatorEngine, SilenceRemainsFiniteAndSettlesToZero)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftTanh);
    saturator.setDriveDb(40.0f);
    saturator.setAsymmetry(1.0f);

    for (int i = 0; i < 1024; ++i)
        saturator.processSampleDCBlocked(1.0f);

    float output = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        output = saturator.processSampleDCBlocked(0.0f);
        ASSERT_TRUE(std::isfinite(output));
    }

    EXPECT_FLOAT_EQ(output, 0.0f);
}

TEST(SaturatorEngine, DenormalInputSettlesAndStaysAtExactZero)
{
    SaturatorEngine saturator;
    const float tiny = std::numeric_limits<float>::denorm_min() * 16.0f;

    EXPECT_FLOAT_EQ(saturator.processSampleDCBlocked(tiny), 0.0f);
    for (int i = 0; i < 256; ++i)
        EXPECT_FLOAT_EQ(saturator.processSampleDCBlocked(0.0f), 0.0f);
}

TEST(SaturatorEngine, DCBlockerPolarityTransitionStaysWithinDocumentedTransientBound)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::SoftAtan);
    saturator.setDriveDb(40.0f);
    saturator.setSampleRate(48000.0);

    for (int i = 0; i < 48000; ++i)
        ASSERT_TRUE(std::isfinite(saturator.processSampleDCBlocked(1.0f)));

    for (int i = 0; i < 48000; ++i)
    {
        const float output = saturator.processSampleDCBlocked(-1.0f);
        ASSERT_TRUE(std::isfinite(output));
        EXPECT_LE(std::abs(output), 3.0f + 1.0e-6f);
    }
}

TEST(SaturatorEngine, BitDepthUsesPowerOfTwoBipolarCodebookBeforeSafetyShaping)
{
    SaturatorEngine saturator;
    saturator.setType(SaturatorEngine::Bitcrush);
    saturator.setDriveDb(0.0f);
    saturator.setBits(2.0f);

    constexpr std::array<float, 4> codebook { -1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f };
    for (float code : codebook)
        EXPECT_NEAR(saturator.processSample(code), std::tanh(code), 1.0e-6f);

    EXPECT_FLOAT_EQ(saturator.processSample(-0.95f), saturator.processSample(-0.75f));
    EXPECT_NE(saturator.processSample(-0.75f), saturator.processSample(-0.60f));
}
