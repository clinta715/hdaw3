#include <gtest/gtest.h>
#include "engine/SamplerSound.h"
#include "engine/SamplerVoice.h"
#include "engine/AHDSREnvelope.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

static std::shared_ptr<const HDAW::SamplerSound> makeSineSound(int len, double sr)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.data[0] = std::make_unique<float[]>(len);
    for (int i = 0; i < len; ++i)
        b.data[0][i] = std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
    return b.build();
}

TEST(SamplerVoice, NoteOnProducesNonSilence)
{
    auto sound = makeSineSound(1000, 44100.0);
    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    v.start(sound.get(), /*note*/60, /*vel*/1.0f, HDAW::SamplerVoice::Mode::Classic,
            HDAW::AHDSRParams{}, /*reverse*/false);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    v.render(buf, 64);
    bool anyNonZero = false;
    for (int i = 0; i < 64; ++i) if (std::abs(buf.getSample(0, i)) > 1e-6f) anyNonZero = true;
    EXPECT_TRUE(anyNonZero);
}

TEST(SamplerVoice, HigherNoteReachesFurtherIntoBuffer)
{
    auto sound = makeSineSound(2000, 44100.0);
    HDAW::SamplerVoice lo, hi;
    lo.prepare(44100.0); hi.prepare(44100.0);
    lo.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, HDAW::AHDSRParams{}, false);
    hi.start(sound.get(), 72, 1.0f, HDAW::SamplerVoice::Mode::Classic, HDAW::AHDSRParams{}, false);
    juce::AudioBuffer<float> bLo(1, 64), bHi(1, 64);
    lo.render(bLo, 64); hi.render(bHi, 64);
    // An octave up reads ~2x faster -> after 64 samples hi is ~2x further in.
    EXPECT_GT(hi.readPosition(), lo.readPosition());
}

static std::shared_ptr<const HDAW::SamplerSound> makeLoopingSineSound(
    int len, double sr, double loopStart, double loopEnd, bool loopEnabled)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.loopStart = loopStart; b.loopEnd = loopEnd; b.loopEnabled = loopEnabled;
    b.data[0] = std::make_unique<float[]>(len);
    for (int i = 0; i < len; ++i)
        b.data[0][i] = static_cast<float>(std::sin(2.0 * 3.14159265 * 440.0 * i / sr));
    return b.build();
}

static float maxSampleJump(const std::vector<float>& output)
{
    float maxJump = 0.0f;
    for (size_t i = 1; i < output.size(); ++i)
    {
        float jump = std::abs(output[i] - output[i - 1]);
        if (jump > maxJump) maxJump = jump;
    }
    return maxJump;
}

TEST(LoopCrossfade, LoopTransitionHasNoLargeDiscontinuity)
{
    const int len = 44100;
    auto sound = makeLoopingSineSound(len, 44100.0, 0.0, 1.0, true);
    ASSERT_GT(sound->crossfadeLength, 0);

    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    HDAW::AHDSRParams env;
    env.attack = 0.0f; env.decay = 0.0f; env.sustain = 1.0f; env.release = 0.0f;
    v.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, env, false);

    const int totalSamples = 3 * 44100;
    juce::AudioBuffer<float> buf(1, totalSamples);
    buf.clear();
    v.render(buf, totalSamples);

    std::vector<float> output(totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        output[i] = buf.getSample(0, i);

    float jump = maxSampleJump(output);
    EXPECT_LT(jump, 0.3f) << "loop crossfade has audible discontinuity";
}

TEST(LoopCrossfade, NonLoopingSoundUnaffected)
{
    auto sound = makeLoopingSineSound(44100, 44100.0, 0.0, 1.0, false);
    EXPECT_EQ(sound->crossfadeLength, 0);

    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    HDAW::AHDSRParams env;
    env.attack = 0.0f; env.decay = 0.0f; env.sustain = 1.0f; env.release = 999.0f;
    v.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, env, false);

    juce::AudioBuffer<float> buf(1, 44100);
    buf.clear();
    v.render(buf, 44100);
    EXPECT_TRUE(v.isDone());
}

TEST(LoopCrossfade, ShortLoopSkipsCrossfade)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = 100; b.nativeSampleRate = 44100.0; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.loopStart = 0.0; b.loopEnd = 0.03; b.loopEnabled = true;
    b.data[0] = std::make_unique<float[]>(100);
    for (int i = 0; i < 100; ++i)
        b.data[0][i] = static_cast<float>(std::sin(2.0 * 3.14159265 * 440.0 * i / 44100.0));
    auto sound = b.build();
    EXPECT_EQ(sound->crossfadeLength, 0);

    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    HDAW::AHDSRParams env;
    env.attack = 0.0f; env.decay = 0.0f; env.sustain = 1.0f; env.release = 0.0f;
    v.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, env, false);

    juce::AudioBuffer<float> buf(1, 44100);
    buf.clear();
    v.render(buf, 44100);
    SUCCEED();
}

TEST(LoopCrossfade, ReverseLoopCrossfade)
{
    const int len = 44100;
    auto sound = makeLoopingSineSound(len, 44100.0, 0.0, 1.0, true);
    ASSERT_GT(sound->crossfadeLength, 0);

    HDAW::SamplerVoice v;
    v.prepare(44100.0);
    HDAW::AHDSRParams env;
    env.attack = 0.0f; env.decay = 0.0f; env.sustain = 1.0f; env.release = 0.0f;
    v.start(sound.get(), 60, 1.0f, HDAW::SamplerVoice::Mode::Classic, env, true);

    const int totalSamples = 3 * 44100;
    juce::AudioBuffer<float> buf(1, totalSamples);
    buf.clear();
    v.render(buf, totalSamples);

    std::vector<float> output(totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        output[i] = buf.getSample(0, i);

    float jump = maxSampleJump(output);
    EXPECT_LT(jump, 0.3f) << "reverse loop crossfade has audible discontinuity";
}
