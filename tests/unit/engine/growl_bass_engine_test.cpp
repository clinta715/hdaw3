#include <gtest/gtest.h>
#include "engine/GrowlBassEngine.h"
#include "engine/TrackFXSlot.h"

#include <cstdio>
#include <cstring>

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

// ─── Follow-MIDI fundamental (Fundamental Hz 0) ──────────────────────────────
//
// The DSP branch was always there ((fundHz > 10) ? fundHz : midiFreq), but the
// exposed param minimum (20 Hz) sat ABOVE the threshold, so the MIDI branch was
// unreachable through every real surface. The minimum is now 0 and the
// threshold 0: exactly 0 means "follow the MIDI note", any positive value pins
// the oscillator (unchanged for every existing project, whose values are >= 20).

// The def contract the whole chain (set_internal_fx_param / list_fx_params /
// device map / automation) derives from. The DEFAULT must stay 55.0f —
// bit-identity for existing projects depends on it.
TEST_F(GrowlBassEngineTest, FundamentalDefAllowsZeroMinKeepsDefault)
{
    const auto defs = HDAW::TrackFXSlot::getParamDefsForType("growl_bass");
    ASSERT_FALSE(defs.empty());
    EXPECT_EQ(defs[0].index, 0);
    EXPECT_EQ(defs[0].name, juce::String("Fundamental Hz"));
    EXPECT_FLOAT_EQ(defs[0].minValue, 0.0f);
    EXPECT_FLOAT_EQ(defs[0].defaultValue, 55.0f);
    EXPECT_FLOAT_EQ(defs[0].maxValue, 200.0f);
}

namespace {

// Render one note on a fresh engine at the given fundamental, returning the
// mono buffer. Separate instances per note so nothing but `note` differs
// between the two renders (same seedless, deterministic default state).
juce::AudioBuffer<float> renderNote(float fundamentalHz, int note, int numSamples)
{
    GrowlBassEngine engine;
    engine.prepare(44100.0, 512);
    engine.setFundamentalHz(fundamentalHz);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();
    engine.render(buffer, midi);
    return buffer;
}

bool buffersEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    return std::memcmp(a.getReadPointer(0), b.getReadPointer(0),
                       sizeof(float) * static_cast<size_t>(a.getNumSamples())) == 0;
}

double rms(const juce::AudioBuffer<float>& b)
{
    return static_cast<double>(b.getRMSLevel(0, 0, b.getNumSamples()));
}

// Zero-crossing rate: a cheap, mixing-robust pitch proxy (a 2x higher
// oscillator crosses zero ~2x as often through the same waveshaper/filter).
double meanAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                   int n)
{
    const float* pa = a.getReadPointer(0);
    const float* pb = b.getReadPointer(0);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += std::abs(static_cast<double>(pa[i]) - static_cast<double>(pb[i]));
    return sum / static_cast<double>(n);
}

double zeroCrossingRate(const juce::AudioBuffer<float>& b, int n)
{
    const float* p = b.getReadPointer(0);
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((p[i - 1] < 0.0f) != (p[i] < 0.0f))
            ++crossings;
    return static_cast<double>(crossings);
}

} // namespace

// DEFAULT params (55 Hz, pinned): the note affects nothing but pitch, and the
// pitch is pinned — so note 36 and note 48 must render BIT-IDENTICALLY.
TEST_F(GrowlBassEngineTest, FixedFundamentalIgnoresMidiNote)
{
    const int n = 4096;
    const auto low  = renderNote(55.0f, 36, n);
    const auto high = renderNote(55.0f, 48, n);

    EXPECT_TRUE(buffersEqual(low, high))
        << "pinned fundamental: the MIDI note must not reach the render "
           "(rms low=" << rms(low) << " high=" << rms(high) << ")";
    EXPECT_GT(rms(low), 0.0);
}

// Fundamental Hz 0 = follow the MIDI note. Note 48 is an octave above note 36,
// so the render must DIFFER and be audibly higher/richer — measured, not
// assumed (a growl/FM/waveshaped signal does not guarantee a textbook 2.0).
TEST_F(GrowlBassEngineTest, ZeroFundamentalFollowsMidiNote)
{
    const int n = 4096;
    const auto low  = renderNote(0.0f, 36, n);
    const auto high = renderNote(0.0f, 48, n);

    EXPECT_FALSE(buffersEqual(low, high))
        << "Fundamental Hz 0 must reach the pitch branch";

    const double zcrLow  = zeroCrossingRate(low, n);
    const double zcrHigh = zeroCrossingRate(high, n);
    const double mad = meanAbsDiff(low, high, n);
    const double rmsLow = rms(low), rmsHigh = rms(high);
    std::printf("[growl follow-MIDI] zcr(36)=%.0f zcr(48)=%.0f ratio=%.3f "
                "meanAbsDiff=%.6f rms(36)=%.6f rms(48)=%.6f\n",
                zcrLow, zcrHigh, zcrLow > 0.0 ? zcrHigh / zcrLow : 0.0,
                mad, rmsLow, rmsHigh);

    // Robust directional assert: the higher note crosses zero clearly more
    // often (a whole octave; the rich harmonic content spreads the ratio
    // around 2.0, so require a clear margin rather than exactly 2.0).
    EXPECT_GT(zcrHigh, zcrLow * 1.5)
        << "note 48 must sound clearly higher than note 36 when the "
           "fundamental follows MIDI (zcr " << zcrLow << " vs " << zcrHigh << ")";
    EXPECT_GT(mad, 1e-4) << "the two notes must differ materially";
}

