#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Regression tests for the 2026-08-31 "export silent after 0.6s" bug family:
// an out-of-range param_N (reverb roomSize=900, documented range [0,1])
// stored in a saved project reached juce::dsp::Reverb unclamped. Freeverb
// maps roomSize -> comb feedback (0.7 + 0.28*roomSize), so loop gain ~252
// diverged to inf/NaN and the WAV writer emitted zeros.
//
// Contract under test: every entry point that feeds internalParamValues into
// DSP (loadParamsFromTree, setInternalParam, prepare) clamps to the type's
// documented param defs, so recursive-feedback DSP can never run away.
// See docs/handoffs/2026-09-17-export-silence-investigation.md follow-up.

namespace {

constexpr double kTestSampleRate = 48000.0;
constexpr int kTestBlockSize = 512;
constexpr int kTestChannels = 2;

// Feed ~3 s of 0.5-amplitude 220 Hz sine through the slot in fixed blocks.
// Returns false if any output sample is non-finite; reports the peak of all
// output samples otherwise.
bool renderSineAndCheckFinite (HDAW::TrackFXSlot& slot, float& peakOut)
{
    peakOut = 0.0f;
    const int totalSamples = static_cast<int> (3.0 * kTestSampleRate);
    juce::AudioBuffer<float> buf (kTestChannels, kTestBlockSize);
    juce::MidiBuffer midi; // unused by these FX types
    const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 220.0 / kTestSampleRate;
    double phase = 0.0;
    for (int done = 0; done < totalSamples; done += kTestBlockSize)
    {
        for (int ch = 0; ch < kTestChannels; ++ch)
        {
            float* data = buf.getWritePointer (ch);
            for (int s = 0; s < kTestBlockSize; ++s)
                data[s] = static_cast<float> (0.5 * std::sin (phase + s * phaseInc));
        }
        phase = std::fmod (phase + kTestBlockSize * phaseInc,
                           2.0 * juce::MathConstants<double>::pi);
        slot.process (buf, midi);
        for (int ch = 0; ch < kTestChannels; ++ch)
        {
            const float* data = buf.getReadPointer (ch);
            for (int s = 0; s < kTestBlockSize; ++s)
            {
                if (! std::isfinite (data[s]))
                    return false;
                peakOut = std::max (peakOut, std::abs (data[s]));
            }
        }
    }
    return true;
}

juce::dsp::ProcessSpec makeSpec()
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kTestSampleRate;
    spec.maximumBlockSize = kTestBlockSize;
    spec.numChannels = kTestChannels;
    return spec;
}

} // namespace

TEST (InternalFxParamClamp, ReverbRunawayClamped)
{
    // Poisoned tree exactly as found in renders/forest_cathedral.hdaw
    // ("AtmoClip"): param_0 = 900.0 with the documented range [0,1].
    HDAW::TrackFXSlot slot ("reverb");
    juce::ValueTree tree (IDs::FX_SLOT);
    tree.setProperty (juce::Identifier ("param_0"), 900.0, nullptr);
    tree.setProperty (juce::Identifier ("param_1"), 0.7, nullptr);
    slot.loadParamsFromTree (tree);

    // The stored room size must be clamped to the def max (1.0), not 900.
    ASSERT_GE (slot.getInternalParamValues().size(), (size_t) 2);
    EXPECT_FLOAT_EQ (slot.getInternalParamValues()[0], 1.0f);

    auto spec = makeSpec();
    slot.prepare (spec);

    // Pre-fix: comb feedback 0.7 + 0.28*900 ~ 252 diverges to inf/NaN within
    // ~0.5 s. Post-fix: roomSize 1.0 -> feedback 0.98, bounded output.
    float peak = 0.0f;
    EXPECT_TRUE (renderSineAndCheckFinite (slot, peak));
    EXPECT_LE (peak, 10.0f);
}

TEST (InternalFxParamClamp, ReverbNegativeClamped)
{
    HDAW::TrackFXSlot slot ("reverb");
    juce::ValueTree tree (IDs::FX_SLOT);
    tree.setProperty (juce::Identifier ("param_0"), -5.0, nullptr);
    tree.setProperty (juce::Identifier ("param_1"), 0.7, nullptr);
    slot.loadParamsFromTree (tree);

    ASSERT_GE (slot.getInternalParamValues().size(), (size_t) 1);
    EXPECT_FLOAT_EQ (slot.getInternalParamValues()[0], 0.0f);

    auto spec = makeSpec();
    slot.prepare (spec);

    float peak = 0.0f;
    EXPECT_TRUE (renderSineAndCheckFinite (slot, peak));
    EXPECT_LE (peak, 10.0f);
}

TEST (InternalFxParamClamp, SetInternalParamClamps)
{
    HDAW::TrackFXSlot slot ("reverb");
    auto spec = makeSpec();
    slot.prepare (spec);

    slot.setInternalParam (0, 900.0f);

    // Stored value clamped to roomSize == 1.0 ...
    ASSERT_GE (slot.getInternalParamValues().size(), (size_t) 1);
    EXPECT_FLOAT_EQ (slot.getInternalParamValues()[0], 1.0f);
    // ... and the normalized getter maps it to the top of the range.
    EXPECT_NEAR (slot.getAutomationParam (0), 1.0f, 1e-5f);

    float peak = 0.0f;
    EXPECT_TRUE (renderSineAndCheckFinite (slot, peak));
    EXPECT_LE (peak, 10.0f);
}

TEST (InternalFxParamClamp, DelayFeedbackClamped)
{
    HDAW::TrackFXSlot slot ("delay");
    auto spec = makeSpec();
    slot.prepare (spec);

    // Feedback (param 1) def range is [0, 0.99]; 5.0 would make the delay
    // loop gain diverge block-over-block (in-phase 220 Hz over the default
    // 0.5 s delay grows ~5x per delay period -> thousands within 3 s).
    slot.setInternalParam (1, 5.0f);

    ASSERT_GE (slot.getInternalParamValues().size(), (size_t) 2);
    EXPECT_FLOAT_EQ (slot.getInternalParamValues()[1], 0.99f);

    float peak = 0.0f;
    EXPECT_TRUE (renderSineAndCheckFinite (slot, peak));
    EXPECT_LE (peak, 10.0f);
}
