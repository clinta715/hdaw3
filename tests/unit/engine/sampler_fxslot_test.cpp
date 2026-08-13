#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "engine/SamplerEngine.h"
#include "engine/SamplerSound.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "model/ProjectModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

static std::shared_ptr<const HDAW::SamplerSound> makeTestSine (int len, double sr)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.data[0] = std::make_unique<float[]> (static_cast<size_t> (len));
    for (int i = 0; i < len; ++i)
        b.data[0][i] = static_cast<float> (std::sin (6.2831853 * 440.0 * i / sr));
    return b.build();
}

TEST (SamplerFxSlot, ProcessProducesAudioFromMidi)
{
    HDAW::TrackFXSlot slot ("sampler");
    ASSERT_FALSE (slot.isPlugin());
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 64;
    spec.numChannels = 1;
    slot.prepare (spec);
    slot.setSamplerSoundForTest (makeTestSine (1000, 44100.0));

    juce::AudioBuffer<float> buf (1, 64);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    slot.process (buf, midi);

    bool anyNonZero = false;
    for (int i = 0; i < 64; ++i)
        if (std::abs (buf.getSample (0, i)) > 1e-6f)
            anyNonZero = true;
    EXPECT_TRUE (anyNonZero);
}

TEST (SamplerFxSlot, BypassProducesSilence)
{
    HDAW::TrackFXSlot slot ("sampler");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 64;
    spec.numChannels = 1;
    slot.prepare (spec);
    slot.setSamplerSoundForTest (makeTestSine (1000, 44100.0));
    slot.setBypassed (true);

    juce::AudioBuffer<float> buf (1, 64);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    slot.process (buf, midi);

    for (int i = 0; i < 64; ++i)
        EXPECT_FLOAT_EQ (buf.getSample (0, i), 0.0f);
}

TEST (SamplerFxSlot, SetInternalParamRoutesToEngine)
{
    HDAW::TrackFXSlot slot ("sampler");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 64;
    spec.numChannels = 1;
    slot.prepare (spec);
    slot.setSamplerSoundForTest (makeTestSine (5000, 44100.0));

    // Set attack to 1.0 via internal param
    slot.setInternalParam (0, 1.0f);

    // Verify the param was stored
    auto vals = slot.getInternalParamValues();
    ASSERT_GE (vals.size(), 1u);
    EXPECT_FLOAT_EQ (vals[0], 1.0f);
}

TEST (SamplerFxSlot, RebuildRestoresSampleAndParams)
{
    AudioEngine engine;
    engine.initialize();

    auto* mp = engine.getMainProcessor();
    ASSERT_NE (mp, nullptr);
    auto* tr = mp->getTrack (0);
    ASSERT_NE (tr, nullptr);

    auto trackTree = engine.getProjectModel().getTrackListTree().getChild (0);
    ASSERT_TRUE (trackTree.isValid());

    juce::ValueTree fxChain (IDs::FX_CHAIN);
    juce::ValueTree slot (IDs::FX_SLOT);
    slot.setProperty (IDs::fxType, "sampler", nullptr);
    slot.setProperty (IDs::bypassed, false, nullptr);
    slot.setProperty (juce::Identifier ("param_0"), 1.0f, nullptr);
    slot.setProperty (juce::Identifier ("param_2"), 0.3f, nullptr);
    fxChain.addChild (slot, -1, nullptr);

    auto existingFX = trackTree.getChildWithName (IDs::FX_CHAIN);
    if (existingFX.isValid())
        trackTree.removeChild (existingFX, nullptr);
    trackTree.addChild (fxChain, -1, nullptr);

    mp->rebuildRoutingGraph();

    tr = mp->getTrack (0);
    ASSERT_NE (tr, nullptr);
    auto& chain = tr->getFXChain();
    ASSERT_GE (chain.size(), 1u);
    ASSERT_NE (chain[0], nullptr);
    EXPECT_EQ (chain[0]->getType(), "sampler");

    auto vals = chain[0]->getInternalParamValues();
    ASSERT_GE (vals.size(), 3u);
    EXPECT_FLOAT_EQ (vals[0], 1.0f);
    EXPECT_FLOAT_EQ (vals[2], 0.3f);

    auto* samplerEngine = chain[0]->samplerEngineForTest();
    ASSERT_NE (samplerEngine, nullptr);
}
