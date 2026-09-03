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
    double sum = 0.0;
    for (int i = 0; i < settlingSamples + measuredSamples; ++i)
    {
        const float input = std::sin(2.0f * pi * 1000.0f * static_cast<float>(i)
                                     / static_cast<float>(sampleRate));
        const float output = saturator.processSampleDCBlocked(input);
        if (i >= settlingSamples)
            sum += static_cast<double>(output);
    }

    EXPECT_LT(std::abs(sum / static_cast<double>(measuredSamples)), 1.0e-3);
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
