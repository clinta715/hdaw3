#include <gtest/gtest.h>
#include "engine/SamplerEngine.h"
#include "engine/SamplerSound.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

static std::shared_ptr<const HDAW::SamplerSound> sine(int len, double sr)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.data[0] = std::make_unique<float[]>(len);
    for (int i = 0; i < len; ++i) b.data[0][i] = std::sin(6.2831853 * 440.0 * i / sr);
    return b.build();
}

TEST(SamplerEngine, NoteOnActivatesAVoice)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(1000, 44100.0));
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST(SamplerEngine, PolyphonyUpTo32)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(10000, 44100.0));
    juce::MidiBuffer midi;
    for (int n = 0; n < 32; ++n) midi.addEvent(juce::MidiMessage::noteOn(1, 36 + n, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 32);
}

TEST(SamplerEngine, SampleSwapStopsAllVoices_NoDangle)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    auto s1 = sine(1000, 44100.0);
    engine.setSound(s1);
    juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, on);
    ASSERT_GT(engine.activeVoiceCount(), 0);
    // swap to a new sound
    auto s2 = sine(2000, 44100.0);
    engine.setSound(s2);
    { juce::MidiBuffer empty; buf.clear(); engine.render(buf, empty); }
    // after swap, all old voices stopped
    EXPECT_EQ(engine.activeVoiceCount(), 0);
    // a NEW note-on after swap must land on the NEW sound (no stale pointer)
    { juce::MidiBuffer on2; on2.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); buf.clear(); engine.render(buf, on2); }
    EXPECT_EQ(engine.activeVoiceCount(), 1);
    EXPECT_TRUE(engine.allVoicesReferenceCurrentSound());
    EXPECT_EQ(engine.currentSound(), s2.get());
}

TEST(SamplerEngine, MonoModeKeepsSingleVoice)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(5000, 44100.0));
    HDAW::SamplerEngine::Params p; p.mono = true;
    engine.setParams(p);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST(SamplerEngine, OneShotIgnoresNoteOff)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sine(2000, 44100.0));
    HDAW::SamplerEngine::Params p; p.mode = HDAW::SamplerVoice::Mode::OneShot;
    engine.setParams(p);
    juce::MidiBuffer on; on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64); buf.clear(); engine.render(buf, on);
    int after = engine.activeVoiceCount();
    ASSERT_GT(after, 0);
    juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buf.clear(); engine.render(buf, off);
    EXPECT_EQ(engine.activeVoiceCount(), after); // one-shot ignores note-off
}

static std::shared_ptr<const HDAW::SamplerSound> sineWithSlices(int len, double sr,
                                                                std::vector<int64_t> slices)
{
    // Rebuild the sine with explicit slice boundaries (sine() does not set them).
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.data[0] = std::make_unique<float[]>(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i)
        b.data[0][i] = std::sin(6.2831853 * 440.0 * i / sr);
    b.slicePoints = std::move(slices);
    return b.build();
}

TEST(SamplerEngine, TriggerSliceAuditionsSlice)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sineWithSlices(1024, 44100.0, {0, 256, 512, 1024}));
    engine.triggerSlice(1, 0.8f);

    juce::MidiBuffer empty;
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, empty);
    EXPECT_GT(engine.activeVoiceCount(), 0);

    // The auditioned slice [256,512) is ~242 samples long at root pitch plus a
    // release tail, so it is still playing after a second block — don't over-assert.
    buf.clear(); engine.render(buf, empty);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST(SamplerEngine, TriggerSliceWithoutSlicesDoesNothing)
{
    HDAW::SamplerEngine engine;
    engine.prepare(44100.0, 64);
    engine.setSound(sineWithSlices(1024, 44100.0, {}));
    engine.triggerSlice(0, 0.8f);

    juce::MidiBuffer empty;
    juce::AudioBuffer<float> buf(1, 64); buf.clear();
    engine.render(buf, empty);
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}
