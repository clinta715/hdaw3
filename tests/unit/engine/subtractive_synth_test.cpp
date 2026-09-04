#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "engine/SubtractiveSynthEngine.h"

namespace {

float renderPeak(SubtractiveSynthEngine& engine, juce::MidiBuffer& midi, int numSamples)
{
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();
    engine.render(buffer, midi);

    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(buffer.getSample(0, i)));
    return peak;
}

float midiNoteToHz(int note)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

float renderRms(SubtractiveSynthEngine& engine, juce::MidiBuffer& midi, int numSamples)
{
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();
    engine.render(buffer, midi);

    float sum = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float s = buffer.getSample(0, i);
        sum += s * s;
    }
    return std::sqrt(sum / static_cast<float>(buffer.getNumSamples()));
}

int zeroCrossings(juce::AudioBuffer<float>& buffer)
{
    const float* data = buffer.getReadPointer(0);
    int count = 0;
    for (int i = 1; i < buffer.getNumSamples(); ++i)
    {
        if ((data[i - 1] < 0.0f && data[i] >= 0.0f) || (data[i - 1] > 0.0f && data[i] <= 0.0f))
            ++count;
    }
    return count;
}

} // namespace

TEST(SubtractiveSynthEngine, NoteOnProducesOutput)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

    const float peak = renderPeak(engine, midi, 256);
    EXPECT_GT(peak, 0.0f);
    EXPECT_EQ(engine.activeNoteCount(), 1);
}

TEST(SubtractiveSynthEngine, NoteOffReleasesVoice)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    EXPECT_GT(renderPeak(engine, midi, 128), 0.0f);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);

    for (int i = 0; i < 128; ++i)
    {
        renderPeak(engine, midi, 64);
        midi.clear();
    }

    EXPECT_EQ(engine.activeNoteCount(), 0);
}

TEST(SubtractiveSynthEngine, ParameterClampingIsSafe)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);

    engine.setOsc1Wave(99);
    engine.setOsc2Wave(-12);
    engine.setOsc1Level(-3.0f);
    engine.setOsc2Level(5.0f);
    engine.setOsc2DetuneCents(12000.0f);
    engine.setSubLevel(2.0f);
    engine.setSubOctave(-99);
    engine.setCutoffHz(999999.0f);
    engine.setResonance(99.0f);
    engine.setDrive(4.0f);
    engine.setAttackSeconds(-1.0f);
    engine.setDecaySeconds(-5.0f);
    engine.setSustain(2.0f);
    engine.setReleaseSeconds(-3.0f);
    engine.setOutputLevel(2.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    const float peak = renderPeak(engine, midi, 256);
    EXPECT_TRUE(std::isfinite(peak));
    EXPECT_GT(peak, 0.0f);
}

TEST(SubtractiveSynthEngine, MonoRetriggerHardResetsEnvelope)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAttackSeconds(0.5f);
    engine.setDecaySeconds(0.5f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.5f);
    engine.setLegato(false);
    engine.setPortamentoSeconds(0.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    EXPECT_GT(renderPeak(engine, midi, 2048), 0.0f);

    const float envBefore = engine.envelopeForTest();
    EXPECT_EQ(engine.currentNoteForTest(), 60);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);

    juce::AudioBuffer<float> buffer(1, 32);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.currentNoteForTest(), 64);
    EXPECT_NEAR(engine.targetFrequencyForTest(), midiNoteToHz(64), 0.01f);
    EXPECT_NEAR(engine.currentFrequencyForTest(), midiNoteToHz(64), 0.01f);
    EXPECT_LT(engine.envelopeForTest(), envBefore);
}

TEST(SubtractiveSynthEngine, LegatoRetiggerKeepsEnvelopeAlive)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAttackSeconds(0.5f);
    engine.setDecaySeconds(0.5f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.5f);
    engine.setLegato(true);
    engine.setPortamentoSeconds(0.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    EXPECT_GT(renderPeak(engine, midi, 2048), 0.0f);

    const float envBefore = engine.envelopeForTest();

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);

    juce::AudioBuffer<float> buffer(1, 32);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_EQ(engine.currentNoteForTest(), 64);
    EXPECT_NEAR(engine.currentFrequencyForTest(), midiNoteToHz(64), 0.01f);
    EXPECT_NEAR(engine.targetFrequencyForTest(), midiNoteToHz(64), 0.01f);
    EXPECT_GE(engine.envelopeForTest(), envBefore * 0.9f);
}

TEST(SubtractiveSynthEngine, PortamentoGlidesTowardTargetPitch)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAttackSeconds(0.01f);
    engine.setDecaySeconds(0.1f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.1f);
    engine.setLegato(true);
    engine.setPortamentoSeconds(0.5f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    EXPECT_GT(renderPeak(engine, midi, 256), 0.0f);

    const float startHz = engine.currentFrequencyForTest();
    const float targetHz = midiNoteToHz(64);
    EXPECT_NEAR(startHz, midiNoteToHz(60), 0.01f);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);

    juce::AudioBuffer<float> buffer(1, 1);
    buffer.clear();
    engine.render(buffer, midi);

    const float midHz = engine.currentFrequencyForTest();
    EXPECT_GT(midHz, startHz);
    EXPECT_LT(midHz, targetHz);
    EXPECT_NEAR(engine.targetFrequencyForTest(), targetHz, 0.01f);

    juce::MidiBuffer empty;
    buffer.setSize(1, 512, false, false, true);
    buffer.clear();
    engine.render(buffer, empty);

    EXPECT_GT(engine.currentFrequencyForTest(), midHz);
    EXPECT_LT(engine.currentFrequencyForTest(), targetHz);
}

TEST(SubtractiveSynthEngine, FilterTypesAllRender)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setOsc1Wave(1); // saw
    engine.setOsc2Level(0.0f);
    engine.setSubLevel(0.0f);
    engine.setCutoffHz(500.0f);
    engine.setResonance(0.2f);
    engine.setAttackSeconds(0.001f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.1f);
    engine.setOutputLevel(0.8f);

    float rmsLP = 0.0f;
    float rmsHP = 0.0f;
    for (int type = 0; type <= 3; ++type)
    {
        engine.setFilterType(type);
        EXPECT_EQ(engine.filterTypeForTest(), type);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0); // 65 Hz, low note
        const float rms = renderRms(engine, midi, 2048);
        EXPECT_TRUE(std::isfinite(rms));
        EXPECT_GT(rms, 0.0f);
        if (type == 0) rmsLP = rms;
        if (type == 1) rmsHP = rms;
    }

    // HP at 500 Hz must attenuate a 65 Hz fundamental far below LP's passband.
    EXPECT_GT(rmsLP, rmsHP * 5.0f);
}

TEST(SubtractiveSynthEngine, FilterEnvelopeOpensCutoff)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setOsc1Wave(1); // saw
    engine.setOsc2Level(0.0f);
    engine.setSubLevel(0.0f);
    engine.setCutoffHz(5000.0f);
    engine.setResonance(0.1f);
    engine.setAttackSeconds(0.001f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.1f);
    engine.setFilterType(0); // LP
    engine.setFilterEnvAmount(48.0f);
    engine.setFilterAttackSeconds(1.0f); // slow filter attack
    engine.setFilterDecaySeconds(1.0f);
    engine.setFilterSustain(1.0f);
    engine.setFilterReleaseSeconds(0.1f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 96, 0.9f), 0); // 2093 Hz saw, rich harmonics

    const float rmsClosed = renderRms(engine, midi, 256); // filter env ~0 -> cutoff ~312 Hz
    EXPECT_GT(engine.filterEnvForTest(), 0.0f);
    EXPECT_LT(engine.filterEnvForTest(), 0.1f);

    juce::MidiBuffer empty;
    const float rmsOpen = renderRms(engine, empty, 44100); // filter env -> 1 -> cutoff 5000 Hz
    EXPECT_NEAR(engine.filterEnvForTest(), 1.0f, 0.01f);
    EXPECT_GT(rmsOpen, rmsClosed * 2.0f);
}

TEST(SubtractiveSynthEngine, PitchBendShiftsFrequency)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setOsc1Wave(0); // sine
    engine.setOsc2Level(0.0f);
    engine.setSubLevel(0.0f);
    engine.setCutoffHz(1800.0f);
    engine.setResonance(0.1f);
    engine.setAttackSeconds(0.001f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.1f);
    engine.setPitchBendRange(12.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0); // 261.6 Hz
    juce::AudioBuffer<float> baseBuf(1, 8192);
    baseBuf.clear();
    engine.render(baseBuf, midi);
    const int zcBase = zeroCrossings(baseBuf);
    EXPECT_EQ(engine.pitchBendRatioForTest(), 1.0f);

    // +4096 of 16383 = +50% of range -> +6 semitones at range 12 -> ratio 2^0.5.
    juce::MidiBuffer bend;
    bend.addEvent(juce::MidiMessage::pitchWheel(1, 8192 + 4096), 0);
    juce::AudioBuffer<float> bendBuf(1, 8192);
    bendBuf.clear();
    engine.render(bendBuf, bend);
    const int zcBent = zeroCrossings(bendBuf);
    EXPECT_NEAR(engine.pitchBendRatioForTest(), std::pow(2.0f, 0.5f), 0.01f);
    EXPECT_GT(zcBent, static_cast<int>(zcBase * 1.3f));
}

TEST(SubtractiveSynthEngine, SustainPedalHoldsVoice)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAttackSeconds(0.001f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.01f);

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    renderPeak(engine, noteOn, 128); // let the amp envelope reach sustain

    juce::MidiBuffer pedalDown;
    pedalDown.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    renderPeak(engine, pedalDown, 64);

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    renderPeak(engine, noteOff, 64);
    EXPECT_EQ(engine.activeNoteCount(), 1); // note-off deferred by sustain

    juce::MidiBuffer empty;
    for (int i = 0; i < 16; ++i)
        renderPeak(engine, empty, 256);
    EXPECT_EQ(engine.activeNoteCount(), 1); // still held

    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0); // pedal up
    renderPeak(engine, up, 64);
    EXPECT_EQ(engine.activeNoteCount(), 1); // releasing, still audible

    for (int i = 0; i < 64; ++i)
        renderPeak(engine, empty, 256);
    EXPECT_EQ(engine.activeNoteCount(), 0); // release finished
}

TEST(SubtractiveSynthEngine, SustainPedalReleaseAtZeroEnvelopeDeactivatesVoice)
{
    // Pedal pressed at the same instant as note-on freezes the envelope at 0;
    // lifting the pedal must still deactivate the (already-silent) voice.
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAttackSeconds(0.001f);
    engine.setDecaySeconds(0.05f);
    engine.setSustain(1.0f);
    engine.setReleaseSeconds(0.01f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0); // pedal down
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    renderPeak(engine, midi, 64);
    EXPECT_EQ(engine.activeNoteCount(), 1);

    juce::MidiBuffer up;
    up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0); // pedal up
    renderPeak(engine, up, 64);

    juce::MidiBuffer empty;
    for (int i = 0; i < 8; ++i)
        renderPeak(engine, empty, 256);
    EXPECT_EQ(engine.activeNoteCount(), 0);
}
