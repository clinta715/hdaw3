#include <gtest/gtest.h>
#include "engine/AHDSREnvelope.h"

TEST(AHDSREnvelope, AttackRampsToUnity)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0); // 1 sample == 1 ms
    env.set({ 0.010f, 0.0f, 0.010f, 0.5f, 0.020f }); // A10ms H0 D10ms S0.5 R20ms
    env.noteOn();
    EXPECT_FLOAT_EQ(env.next(), 0.0f);     // very start of attack
    for (int i = 0; i < 9; ++i) env.next();
    EXPECT_NEAR(env.next(), 1.0f, 0.02f);  // ~10ms in -> near unity (attack end)
}

TEST(AHDSREnvelope, SustainHoldsAtSustainLevel)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0);
    env.set({ 0.0f, 0.0f, 0.010f, 0.5f, 0.020f });
    env.noteOn();
    for (int i = 0; i < 20; ++i) env.next(); // through decay
    EXPECT_NEAR(env.next(), 0.5f, 0.02f);
    EXPECT_NEAR(env.next(), 0.5f, 0.02f);    // holds at sustain
}

TEST(AHDSREnvelope, ReleaseDecaysToZero)
{
    HDAW::AHDSREnvelope env;
    env.setSampleRate(1000.0);
    env.set({ 0.0f, 0.0f, 0.0f, 1.0f, 0.020f });
    env.noteOn();
    env.next(); // at unity
    env.noteOff();
    for (int i = 0; i < 20; ++i) env.next();
    EXPECT_NEAR(env.next(), 0.0f, 0.02f);    // released
    EXPECT_FALSE(env.isActive());
}
