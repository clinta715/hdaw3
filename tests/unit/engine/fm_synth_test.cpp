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

TEST_F(FmSynthTest, PartialBlockRendersBitIdenticalToSingleCall)
{
    // Engine A: render 300 samples in one call
    FmSynthEngine engineA;
    engineA.prepare(44100.0, 512);
    juce::MidiBuffer midiA;
    midiA.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> bufA(1, 300);
    bufA.clear();
    engineA.render(bufA, midiA);

    // Engine B: render 300 samples in 3 calls of 100 (100 is not a multiple of 64)
    FmSynthEngine engineB;
    engineB.prepare(44100.0, 512);
    juce::MidiBuffer midiB;
    midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> bufB(1, 300);
    bufB.clear();
    juce::MidiBuffer emptyMidi;  // note-on only in first chunk
    int offset = 0;
    const int chunk = 100;
    while (offset < 300)
    {
        juce::AudioBuffer<float> temp(1, chunk);
        temp.clear();
        engineB.render(temp, (offset == 0) ? midiB : emptyMidi);
        for (int i = 0; i < chunk; ++i)
            bufB.setSample(0, offset + i, temp.getSample(0, i));
        offset += chunk;
    }

    for (int i = 0; i < 300; ++i)
        EXPECT_FLOAT_EQ(bufA.getSample(0, i), bufB.getSample(0, i))
            << "mismatch at sample " << i;
}

TEST_F(FmSynthTest, SmallChunkRendersBitIdenticalToSingleCall)
{
    // 128 samples as 8 calls of 16 vs one call of 128
    FmSynthEngine engineA;
    engineA.prepare(44100.0, 512);
    juce::MidiBuffer midiA;
    midiA.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> bufA(1, 128);
    bufA.clear();
    engineA.render(bufA, midiA);

    FmSynthEngine engineB;
    engineB.prepare(44100.0, 512);
    juce::MidiBuffer midiB;
    midiB.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> bufB(1, 128);
    bufB.clear();
    for (int off = 0; off < 128; off += 16)
    {
        juce::AudioBuffer<float> temp(1, 16);
        temp.clear();
        juce::MidiBuffer chunkMidi = (off == 0) ? midiB : juce::MidiBuffer{};
        engineB.render(temp, chunkMidi);
        for (int i = 0; i < 16; ++i)
            bufB.setSample(0, off + i, temp.getSample(0, i));
    }

    for (int i = 0; i < 128; ++i)
        EXPECT_FLOAT_EQ(bufA.getSample(0, i), bufB.getSample(0, i))
            << "mismatch at sample " << i;
}

TEST_F(FmSynthTest, PeekVoiceStatusReportsCarrierAmps)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 256);
    buf.clear();
    engine.render(buf, midi);

    FmSynthEngine::FmVoiceStatus status;
    ASSERT_TRUE(engine.peekVoiceStatus(status));
    bool anyNonzero = false;
    for (int i = 0; i < 6; ++i)
        if (status.amp[i] > 0) { anyNonzero = true; break; }
    EXPECT_TRUE(anyNonzero);
}

TEST_F(FmSynthTest, PeekVoiceStatusFalseWhenIdle)
{
    FmSynthEngine::FmVoiceStatus status;
    juce::AudioBuffer<float> buf(1, 64);
    buf.clear();
    juce::MidiBuffer midi;
    engine.render(buf, midi);
    EXPECT_FALSE(engine.peekVoiceStatus(status));
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
