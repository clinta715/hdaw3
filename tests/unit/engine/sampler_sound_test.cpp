#include <gtest/gtest.h>
#include "engine/SamplerSound.h"

TEST(SamplerSound, HoldsPreloadedBufferAndMetadata)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 2;
    b.nativeSampleRate = 44100.0;
    b.length = 4;
    b.data[0] = std::make_unique<float[]>(4);
    b.data[1] = std::make_unique<float[]>(4);
    b.data[0][0] = 0.5f; b.data[0][3] = -0.25f;
    b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;   // normalized
    auto sound = b.build();
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->numChannels, 2);
    EXPECT_EQ(sound->length, 4);
    EXPECT_FLOAT_EQ(sound->data[0][0], 0.5f);
    EXPECT_FLOAT_EQ(sound->data[0][3], -0.25f);
    EXPECT_EQ(sound->rootNote, 60);
    EXPECT_EQ(sound->startFrame(), 0);
    EXPECT_EQ(sound->endFrame(), 4);
}

TEST(SamplerSound, NormalizedCoordsConvertToFrames)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.nativeSampleRate = 48000.0; b.length = 1000;
    b.data[0] = std::make_unique<float[]>(1000);
    b.sampleStart = 0.25; b.sampleEnd = 0.75;
    b.loopStart = 0.5; b.loopEnd = 0.5;
    auto sound = b.build();
    EXPECT_EQ(sound->startFrame(), 250);
    EXPECT_EQ(sound->endFrame(), 750);
    EXPECT_EQ(sound->loopStartFrame(), 500);
    EXPECT_EQ(sound->loopEndFrame(), 500);
}

TEST(SamplerSound, SlicePointsRoundTrip)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.nativeSampleRate = 44100.0; b.length = 1000;
    b.data[0] = std::make_unique<float[]>(1000);
    b.slicePoints = { 0, 250, 750, 1000 };
    auto sound = b.build();
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->slicePoints.size(), 4u);
    EXPECT_EQ(sound->slicePoints[0], 0);
    EXPECT_EQ(sound->slicePoints[1], 250);
    EXPECT_EQ(sound->slicePoints[2], 750);
    EXPECT_EQ(sound->slicePoints[3], 1000);
}
