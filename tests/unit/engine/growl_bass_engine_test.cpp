#include <gtest/gtest.h>
#include "engine/GrowlBassEngine.h"

class GrowlBassEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        engine.prepare(44100.0, 512);
    }

    GrowlBassEngine engine;
};

TEST_F(GrowlBassEngineTest, InitialState)
{
    EXPECT_EQ(engine.activeVoiceCount(), 0);
    EXPECT_FLOAT_EQ(engine.getOutputLevel(), 0.4f);
}

TEST_F(GrowlBassEngineTest, NoteOnActivatesVoice)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, NoteOffReleasesVoice)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    buffer.clear();
    engine.render(buffer, midi);

    // Voice should still be active (releasing)
    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, AllNotesOffClearsVoices)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(GrowlBassEngineTest, OutputLevelParameter)
{
    engine.setOutputLevel(0.8f);
    EXPECT_FLOAT_EQ(engine.getOutputLevel(), 0.8f);
}

TEST_F(GrowlBassEngineTest, RenderProducesNonZeroOutput)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    engine.render(buffer, midi);

    // Check that at least some samples are non-zero
    bool hasNonZero = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs(data[i]) > 0.0001f)
            {
                hasNonZero = true;
                break;
            }
        }
        if (hasNonZero) break;
    }
    EXPECT_TRUE(hasNonZero);
}

TEST_F(GrowlBassEngineTest, StereoSpread)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    engine.render(buffer, midi);

    // Both channels should have the same content (mono sum spread)
    const float* left = buffer.getReadPointer(0);
    const float* right = buffer.getReadPointer(1);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        EXPECT_FLOAT_EQ(left[i], right[i]);
    }
}

TEST_F(GrowlBassEngineTest, ModulatorShapeParameter)
{
    engine.setModShape(1); // Triangle
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, ClipTypeParameter)
{
    engine.setClipType(2); // Hard clip
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, FilterTypeParameter)
{
    engine.setFilterType(1); // BandPass
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, UnisonParameter)
{
    engine.setUnisonEnabled(true);
    engine.setUnisonVoices(3);
    engine.setUnisonDetuneCents(15.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, FormantParameter)
{
    engine.setFormantEnabled(true);
    engine.setFormantMorph(0.5f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 1);
}

TEST_F(GrowlBassEngineTest, MultipleVoices)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 3);
}

TEST_F(GrowlBassEngineTest, VoiceStealing)
{
    // Fill all voices
    juce::MidiBuffer midi;
    for (int note = 0; note < 8; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 8);

    // Add one more — should steal a voice
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.activeVoiceCount(), 8); // Still 8 (stolen)
}

TEST_F(GrowlBassEngineTest, EnvelopeDecay)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, 512);

    // Render initial attack
    buffer.clear();
    engine.render(buffer, midi);
    float initialLevel = buffer.getRMSLevel(0, 0, 512);

    // Render more blocks (decay should reduce level)
    for (int i = 0; i < 10; ++i)
    {
        buffer.clear();
        juce::MidiBuffer emptyMidi;
        engine.render(buffer, emptyMidi);
    }
    float laterLevel = buffer.getRMSLevel(0, 0, 512);

    // Later level should be lower (decaying toward sustain)
    EXPECT_LE(laterLevel, initialLevel + 0.001f); // Allow small tolerance
}
