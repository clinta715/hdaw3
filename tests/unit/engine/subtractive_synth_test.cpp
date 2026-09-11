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

// ── Polyphony (param 24, 2026-09-08) ───────────────────────────────────────
// Mono is the default; these tests enable poly and verify the poly bank.

TEST(SubtractiveSynthEngine, PolyTwoNotesSoundSimultaneously)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setPolyphony(true);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 1);

    EXPECT_GT(renderRms(engine, midi, 256), 0.0f);
    EXPECT_EQ(engine.activeNoteCount(), 2);
}

TEST(SubtractiveSynthEngine, PolyNoteOffReleasesOnlyItsVoice)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setPolyphony(true);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 1);
    renderRms(engine, midi, 128);
    EXPECT_EQ(engine.activeNoteCount(), 2);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    renderRms(engine, midi, 64);

    // Render until voice 60's release envelope fully dies (release length is
    // a param — don't hardcode a sample window; break-early is robust).
    juce::MidiBuffer silent;
    for (int i = 0; i < 64 && engine.activeNoteCount() == 2; ++i)
        renderRms(engine, silent, 512);

    // Voice 60 is gone; voice 67 keeps sounding until IT is released.
    EXPECT_EQ(engine.activeNoteCount(), 1);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    renderRms(engine, midi, 64);
    for (int i = 0; i < 64 && engine.activeNoteCount() > 0; ++i)
        renderRms(engine, silent, 512);
    EXPECT_EQ(engine.activeNoteCount(), 0);
}

TEST(SubtractiveSynthEngine, PolyVoiceStealingCapsAtEight)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setPolyphony(true);

    juce::MidiBuffer midi;
    for (int note = 48; note < 48 + 12; ++note)  // 12 notes > 8 voices
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);

    renderRms(engine, midi, 256);
    EXPECT_EQ(engine.activeNoteCount(), 8);      // kMaxPolyVoices
}

TEST(SubtractiveSynthEngine, MonoDefaultStaysMonoWhenPolyNotesOverlap)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    // Polyphony NOT enabled — overlapping note-ons must behave exactly like
    // the pre-polyphony engine (one voice, retrigger/note-memory).
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 1);

    renderRms(engine, midi, 64);
    EXPECT_EQ(engine.activeNoteCount(), 1);
}

TEST(SubtractiveSynthEngine, ModeSwitchClearsSound)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setPolyphony(true);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    renderRms(engine, midi, 64);
    EXPECT_EQ(engine.activeNoteCount(), 1);

    engine.setPolyphony(false);  // switch must not orphan the poly voice
    EXPECT_EQ(engine.activeNoteCount(), 0);

    // And the engine still renders in mono after the switch.
    juce::MidiBuffer on;
    on.addEvent(juce::MidiMessage::noteOn(1, 55, 0.9f), 0);
    EXPECT_GT(renderRms(engine, on, 64), 0.0f);
    EXPECT_EQ(engine.activeNoteCount(), 1);
}

// ── Virus-emulation upgrades 1+2 (params 25/26) ────────────────────────────
// Both are DEFAULT-OFF and must leave the default render bit-identical.

namespace {

juce::AudioBuffer<float> renderNoteBuffer(SubtractiveSynthEngine& engine, int note,
                                           int numSamples)
{
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
    engine.render(buffer, midi);
    return buffer;
}

float maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max(peak, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    return peak;
}

// DFT magnitude at one probe frequency over [start, start+count) — N is
// chosen so the probe lands on an exact bin (no windowing needed).
float probeMagnitude(const juce::AudioBuffer<float>& buffer, int start, int count,
                     float probeHz, double sampleRate)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * probeHz
            * static_cast<double>(i) / sampleRate;
        const float s = buffer.getSample(0, start + i);
        re += s * std::cos(phase);
        im += s * std::sin(phase);
    }
    return static_cast<float>(std::sqrt(re * re + im * im) / count);
}

} // namespace

TEST(SubtractiveSynthEngine, DefaultUpgradesOffIsBitIdentical)
{
    // A default engine vs one with the upgrades EXPLICITLY set to their
    // defaults must render bit-for-bit identical audio.
    SubtractiveSynthEngine ref;
    ref.prepare(44100.0, 512);
    SubtractiveSynthEngine dut;
    dut.prepare(44100.0, 512);
    dut.setOsc2FmAmount(0.0f);
    dut.setFilterSlope24(false);

    auto a = renderNoteBuffer(ref, 60, 4096);
    auto b = renderNoteBuffer(dut, 60, 4096);
    for (int i = 0; i < a.getNumSamples(); ++i)
        EXPECT_EQ(a.getSample(0, i), b.getSample(0, i)) << "sample " << i;
}

TEST(SubtractiveSynthEngine, Osc2FmAmountChangesOsc1Output)
{
    SubtractiveSynthEngine off;
    off.prepare(44100.0, 512);
    SubtractiveSynthEngine on;
    on.prepare(44100.0, 512);
    on.setOsc2FmAmount(0.5f);

    auto a = renderNoteBuffer(off, 60, 4096);
    auto b = renderNoteBuffer(on, 60, 4096);
    // FM=0.5 deviates osc1 phase by up to +/-0.5 cycles — the renders must
    // differ well above numerical noise (audible hard-FM character).
    EXPECT_GT(maxAbsDiff(a, b), 0.01f);
}

TEST(SubtractiveSynthEngine, FilterSlope24Attenuates2kHzMoreThan12dB)
{
    // Saw note (rich harmonics) through a static 500 Hz lowpass; compare the
    // 2 kHz probe energy in 12 dB vs 24 dB mode over the sustain tail.
    constexpr double kRate = 44100.0;
    constexpr int kTotal = 88200;   // 2 s
    constexpr int kTail = 44100;    // last 1 s (steady sustain)
    auto render = [&](bool slope24) {
        SubtractiveSynthEngine e;
        e.prepare(kRate, 512);
        e.setOsc1Wave(1);           // saw
        e.setOsc1Level(0.8f);
        e.setOsc2Level(0.0f);
        e.setSubLevel(0.0f);
        e.setCutoffHz(500.0f);
        e.setResonance(0.15f);
        e.setFilterEnvAmount(0.0f); // static cutoff (no env sweep)
        e.setAttackSeconds(0.005f);
        e.setDecaySeconds(0.1f);
        e.setSustain(1.0f);
        e.setFilterSlope24(slope24);
        return renderNoteBuffer(e, 69, kTotal);
    };

    auto lp12 = render(false);
    auto lp24 = render(true);
    const float mag12 = probeMagnitude(lp12, kTotal - kTail, kTail, 2000.0f, kRate);
    const float mag24 = probeMagnitude(lp24, kTotal - kTail, kTail, 2000.0f, kRate);
    EXPECT_GT(mag12, 1e-6f) << "probe must see signal in 12 dB mode";
    // 2 kHz is 2 octaves above the 500 Hz cutoff: ~24 dB down at 12 dB/oct
    // vs ~48 dB at 24 dB/oct, i.e. amplitude ratio ~0.06. Assert < 0.5.
    EXPECT_LT(mag24, 0.5f * mag12);
}


TEST(SubtractiveSynthEngine, InternalLfoDefaultsAreBitIdentical)
{
    SubtractiveSynthEngine ref;
    ref.prepare(44100.0, 512);
    SubtractiveSynthEngine dut;
    dut.prepare(44100.0, 512);
    dut.setModLfoWave(0);
    dut.setModLfoRateHz(0.5f);
    dut.setModLfoCutoffAmount(0.0f);
    dut.setModLfoPitchAmountCents(0.0f);
    dut.setModLfoAmpAmount(0.0f);
    dut.setModLfoFmAmount(0.0f);

    auto a = renderNoteBuffer(ref, 60, 4096);
    auto b = renderNoteBuffer(dut, 60, 4096);
    for (int i = 0; i < a.getNumSamples(); ++i)
        EXPECT_EQ(a.getSample(0, i), b.getSample(0, i)) << "sample " << i;
}

TEST(SubtractiveSynthEngine, InternalLfoCutoffModulationChangesOutput)
{
    SubtractiveSynthEngine off;
    off.prepare(44100.0, 512);
    SubtractiveSynthEngine on;
    on.prepare(44100.0, 512);
    on.setOsc1Wave(1);
    on.setModLfoRateHz(5.0f);
    on.setModLfoCutoffAmount(24.0f);

    auto a = renderNoteBuffer(off, 60, 8192);
    auto b = renderNoteBuffer(on, 60, 8192);
    EXPECT_GT(maxAbsDiff(a, b), 0.001f);
    EXPECT_GT(on.modLfoPhaseForTest(), 0.0f);
}

TEST(SubtractiveSynthEngine, InternalLfoFmModulationChangesOutput)
{
    SubtractiveSynthEngine off;
    off.prepare(44100.0, 512);
    SubtractiveSynthEngine on;
    on.prepare(44100.0, 512);
    on.setModLfoRateHz(8.0f);
    on.setModLfoFmAmount(0.7f);

    auto a = renderNoteBuffer(off, 60, 8192);
    auto b = renderNoteBuffer(on, 60, 8192);
    EXPECT_GT(maxAbsDiff(a, b), 0.001f);
}
