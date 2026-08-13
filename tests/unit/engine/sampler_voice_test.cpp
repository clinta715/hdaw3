#include <gtest/gtest.h>
#include "engine/SamplerSound.h"
#include "engine/SamplerVoice.h"
#include "engine/AHDSREnvelope.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

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
