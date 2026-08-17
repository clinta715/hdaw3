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

TEST_F(FmSynthTest, SamePitchRetriggerTransfersPhase)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 256);
    buf.clear();
    engine.render(buf, midi);

    int firstIdx = -1;
    for (int i = 0; i < FmSynthEngine::kMaxVoices; ++i)
        if (engine.getVoiceMidiNoteForTest(i) == 60) { firstIdx = i; break; }
    ASSERT_GE(firstIdx, 0);

    // Retrigger the same pitch — the new voice lands in a different slot
    juce::MidiBuffer midi2;
    midi2.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    juce::AudioBuffer<float> buf2(1, 128);
    buf2.clear();
    engine.render(buf2, midi2);

    int secondIdx = -1;
    for (int i = 0; i < FmSynthEngine::kMaxVoices; ++i)
        if (i != firstIdx && engine.getVoiceMidiNoteForTest(i) == 60) { secondIdx = i; break; }
    ASSERT_GE(secondIdx, 0);

    for (int op = 0; op < 6; ++op)
        EXPECT_EQ(engine.getVoicePhaseForTest(firstIdx, op),
                  engine.getVoicePhaseForTest(secondIdx, op))
            << "operator " << op << " phase mismatch after retrigger";
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

TEST_F(FmSynthTest, MonoLegatoTransfersEnvelopeState)
{
    // Slow-attack patch (rate 1 = 30, sustained at level 99) so a restarted
    // envelope is clearly distinguishable from a carried-over (legato) one.
    uint8_t patch[156] = {};
    for (int op = 0; op < 6; ++op)
    {
        const int off = op * 21;
        patch[off + 0] = 30; // EG Rate 1 (slow attack)
        patch[off + 1] = 99; // EG Rate 2
        patch[off + 2] = 99; // EG Rate 3
        patch[off + 3] = 99; // EG Rate 4
        patch[off + 4] = 99; // EG Level 1
        patch[off + 5] = 99; // EG Level 2
        patch[off + 6] = 99; // EG Level 3
        patch[off + 7] = 0;  // EG Level 4
        patch[off + 16] = 99; // Output level
    }
    engine.loadPatch(patch);
    engine.setMonoMode(true);

    // Note C4; render well past the slow attack into the sustain plateau.
    // (The env advances once per 64-sample compute block; ~62k samples are
    // needed to complete a rate-30 attack, so render ~100k.)
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    for (int block = 0; block < 200; ++block)
    {
        buf.clear();
        engine.render(buf, midi);
        midi.clear();
    }

    FmSynthEngine::FmVoiceStatus before;
    ASSERT_TRUE(engine.peekVoiceStatus(before));
    EXPECT_EQ(engine.activeVoiceCount(), 1);

    // Legato to E4 while C4 is still held — the envelope must CONTINUE, not restart.
    juce::MidiBuffer midi2;
    midi2.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf2(1, 64);
    buf2.clear();
    engine.render(buf2, midi2);

    EXPECT_EQ(engine.activeVoiceCount(), 1); // old voice retired, one live voice

    FmSynthEngine::FmVoiceStatus after;
    ASSERT_TRUE(engine.peekVoiceStatus(after));

    bool anyOpSustained = false;
    for (int i = 0; i < 6; ++i)
        if (before.amp[i] > 1000 && after.amp[i] > before.amp[i] / 2)
        {
            anyOpSustained = true;
            break;
        }
    EXPECT_TRUE(anyOpSustained)
        << "envelope restarted from near-zero after mono legato (release gap bug)";
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
