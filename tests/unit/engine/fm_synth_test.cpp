#include <gtest/gtest.h>
#include "engine/FmSynthEngine.h"

class FmSynthTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.prepare(44100.0, 512);
    }
    FmSynthEngine engine;
};

TEST_F(FmSynthTest, InitialStateNoActiveVoices) {
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, NoteOnActivatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, NoteOffReleasesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 256);
    buf.clear();
    engine.render(buf, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    // Render enough blocks for release to complete
    for (int i = 0; i < 100; i++) {
        buf.clear();
        midi.clear();
        if (i == 0) midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        engine.render(buf, midi);
    }
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, ProducesNonZeroOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxVal = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++)
        maxVal = std::max(maxVal, std::abs(buf.getSample(0, i)));
    EXPECT_GT(maxVal, 0.0f);
}

TEST_F(FmSynthTest, AllNotesOffClearsVoices) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(FmSynthTest, LoadPatchDoesNotCrash) {
    uint8_t patch[156] = {};
    patch[134] = 0; // algorithm 0
    engine.loadPatch(patch);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    EXPECT_NO_THROW(engine.render(buf, midi));
}

TEST_F(FmSynthTest, SetParametersDoesNotCrash) {
    engine.setAlgorithm(15);
    engine.setFeedback(3);
    engine.setOutputLevel(0.5f);
    engine.setOpLevel(0, 0.9f);
    engine.setOpCoarse(1, 5);
    engine.setOpFine(2, 50);
    engine.setOpDetune(3, 10);
    engine.setLfoRate(0.7f);
    engine.setLfoDelay(0.2f);
    engine.setLfoPitchDepth(0.3f);
    engine.setLfoAmpDepth(0.1f);
    engine.setLfoWaveform(2);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    EXPECT_NO_THROW(engine.render(buf, midi));
}

TEST_F(FmSynthTest, StereoOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++) {
        maxL = std::max(maxL, std::abs(buf.getSample(0, i)));
        maxR = std::max(maxR, std::abs(buf.getSample(1, i)));
    }
    EXPECT_GT(maxL, 0.0f);
    EXPECT_GT(maxR, 0.0f);
}

TEST_F(FmSynthTest, PolyphonyLimit) {
    juce::MidiBuffer midi;
    // Send 20 note-ons (more than kMaxVoices=16)
    for (int note = 40; note < 60; note++)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_LE(engine.activeVoiceCount(), 16); // Should not exceed kMaxVoices
}

TEST_F(FmSynthTest, DifferentAlgorithms) {
    // Test that algorithms 0, 15, 31 all produce output without crashing
    for (int algo : {0, 15, 31}) {
        engine.setAlgorithm(algo);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        juce::AudioBuffer<float> buf(1, 256);
        buf.clear();
        engine.render(buf, midi);
        float maxVal = 0.0f;
        for (int i = 0; i < buf.getNumSamples(); i++)
            maxVal = std::max(maxVal, std::abs(buf.getSample(0, i)));
        EXPECT_GT(maxVal, 0.0f) << "Algorithm " << algo << " produced silence";
        // Clean up
        midi.clear();
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        buf.clear();
        engine.render(buf, midi);
    }
}
