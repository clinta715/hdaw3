// Master bus FX chain tests (2026-09-08 master-bus FX plan, Gates 1/2/4).
//
// Gates covered:
// - Gate 2 (full-path trace): limiter ceiling observable on a rendered buffer;
//   EQ boost observable in RMS.
// - Gate 1 (rebuild restore): master FX params set via the command layer
//   survive rebuildRoutingGraph() on the LIVE processor.
// - Gate 4 (lesson-23 clamp): out-of-range writes clamp to the defs.
//
// Audio-thread safety (Gate 3) is by construction: params are atomics, the
// master processBlock performs no allocation/lock (per-block coefficient
// recompute). The direct-processor tests below drive the exact processBlock
// path used in playback/export.

#include <gtest/gtest.h>
#include <cmath>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/MasterBusProcessor.h"
#include "common/MasterFxDefs.h"

namespace {

// Render a mono-source stereo sine through the master bus and return peak.
float renderPeak(double sampleRate, int samples, float freqHz, float amplitude,
                 int slotIndex, bool bypassed, const std::vector<float>& params,
                 const char* slotKind = "limiter")
{
    HDAW::MasterBusProcessor master;
    master.prepareToPlay(sampleRate, 512);
    // Slot type must be explicit: fresh slots default to empty (no DSP runs).
    master.setSlotType(slotIndex, juce::String(slotKind));
    master.setSlotBypassed(slotIndex, bypassed);
    for (size_t i = 0; i < params.size(); ++i)
        master.setSlotParam(slotIndex, (int) i, params[i]);

    juce::AudioBuffer<float> buf(2, samples);
    const double twoPi = 6.28318530717958647692;
    for (int s = 0; s < samples; ++s)
    {
        const float v = amplitude * (float) std::sin(twoPi * (double) freqHz * (double) s / sampleRate);
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    juce::MidiBuffer midi;
    // Drive in prepared-block-size chunks (lesson 14: fixed-size scratch).
    for (int start = 0; start < samples; start += 512)
    {
        const int n = std::min(512, samples - start);
        juce::AudioBuffer<float> chunk(2, n);
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom(ch, 0, buf, ch, start, n);
        master.processBlock(chunk, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample(ch, start + i, chunk.getSample(ch, i));
    }

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < samples; ++s)
            peak = std::max(peak, std::abs(buf.getSample(ch, s)));
    return peak;
}

float renderRms(double sampleRate, int samples, float freqHz, float amplitude,
                int slotIndex, bool bypassed, const std::vector<float>& params,
                const char* slotKind = "eq")
{
    HDAW::MasterBusProcessor master;
    master.prepareToPlay(sampleRate, 512);
    master.setSlotType(slotIndex, juce::String(slotKind));
    master.setSlotBypassed(slotIndex, bypassed);
    for (size_t i = 0; i < params.size(); ++i)
        master.setSlotParam(slotIndex, (int) i, params[i]);

    juce::AudioBuffer<float> buf(2, samples);
    const double twoPi = 6.28318530717958647692;
    for (int s = 0; s < samples; ++s)
    {
        const float v = amplitude * (float) std::sin(twoPi * (double) freqHz * (double) s / sampleRate);
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    juce::MidiBuffer midi;
    for (int start = 0; start < samples; start += 512)
    {
        const int n = std::min(512, samples - start);
        juce::AudioBuffer<float> chunk(2, n);
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom(ch, 0, buf, ch, start, n);
        master.processBlock(chunk, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample(ch, start + i, chunk.getSample(ch, i));
    }
    double sum = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < samples; ++s)
            sum += (double) buf.getSample(ch, s) * (double) buf.getSample(ch, s);
    return (float) std::sqrt(sum / (2.0 * (double) samples));
}

} // namespace

// ---- Gate 2: limiter ceiling is observable --------------------------------

TEST(MasterBusFx, LimiterCapsPeakWhenEnabled)
{
    // JUCE dsp::Limiter semantics (verified against juce_Limiter.cpp): two
    // compressor stages + threshold-derived MAKEUP gain + hard clip at +/-1.0.
    // The output ceiling is always 0 dBFS; the threshold controls how much
    // compression+makeup (loudness drive) is applied. So the observable
    // ceiling contract is: input above 1.0 comes out at <= 1.0 when the
    // limiter is enabled, and passes through untouched when bypassed.
    const std::vector<float> limiterParams = { -12.0f, 80.0f };
    const float bypassedPeak = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, true, limiterParams, "limiter");
    const float limitedPeak  = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, limiterParams, "limiter");

    EXPECT_NEAR(bypassedPeak, 1.5f, 0.02f);   // bypass = passthrough
    EXPECT_LT(limitedPeak, 1.01f);             // hard clip ceiling at 0 dBFS
    EXPECT_GT(limitedPeak, 0.90f);             // makeup keeps it near full scale, not muted
}

TEST(MasterBusFx, LimiterBypassedPassesThrough)
{
    const std::vector<float> limiterParams = { -12.0f, 80.0f };
    const float peak = renderPeak(44100.0, 44100, 200.0f, 0.5f, 1, true, limiterParams, "limiter");
    EXPECT_NEAR(peak, 0.5f, 0.02f);
}

// ---- Gate 2: EQ boost/cut observable in RMS --------------------------------

TEST(MasterBusFx, EqBoostRaisesRmsAtBandCenter)
{
    // +12 dB peak boost at 4 kHz on a 4 kHz sine: bypassed RMS ~0.354
    // (sine RMS of 0.5 amplitude), boosted RMS clearly higher.
    const std::vector<float> eqParams = { 4000.0f, 0.7f, 12.0f };
    const float bypassedRms = renderRms(44100.0, 44100, 4000.0f, 0.5f, 0, true, eqParams, "eq");
    const float boostedRms  = renderRms(44100.0, 44100, 4000.0f, 0.5f, 0, false, eqParams, "eq");

    EXPECT_NEAR(bypassedRms, 0.354f, 0.03f);
    EXPECT_GT(boostedRms, bypassedRms * 1.6f); // +12 dB approx 4x; ringing/edge -> 1.6x floor
}

TEST(MasterBusFx, EqCutLowersRmsAtBandCenter)
{
    const std::vector<float> eqParams = { 4000.0f, 0.7f, -18.0f };
    const float bypassedRms = renderRms(44100.0, 44100, 4000.0f, 0.5f, 0, true, eqParams, "eq");
    const float cutRms      = renderRms(44100.0, 44100, 4000.0f, 0.5f, 0, false, eqParams, "eq");
    EXPECT_LT(cutRms, bypassedRms * 0.45f);
}

// ---- Gate 4: lesson-23 clamp at the command entry --------------------------

TEST(MasterBusFx, ParamWriteClampsToDefs)
{
    AudioEngine engine;
    engine.initialize();

    // Limiter threshold def range is -24..0; +5 is out of range -> clamps to 0.
    const float written = engine.getProjectCommands().setMasterFxParam(1, 0, 5.0f);
    EXPECT_FLOAT_EQ(written, 0.0f);

    // In-range write lands verbatim.
    const float written2 = engine.getProjectCommands().setMasterFxParam(1, 0, -6.0f);
    EXPECT_FLOAT_EQ(written2, -6.0f);

    // The tree carries the clamped value (source of truth for rebuild/export).
    auto masterFx = engine.getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    ASSERT_TRUE(masterFx.isValid());
    EXPECT_FLOAT_EQ(
        (float) (double) masterFx.getChild(1).getProperty("param_0", 0.0), -6.0f);
}

TEST(MasterBusFx, OutOfRangeParamIndexIsRejected)
{
    AudioEngine engine;
    engine.initialize();
    // eq slot has 3 params; index 7 is out of range -> tree unchanged.
    const double before = (double) engine.getProjectModel().getTree()
        .getChildWithName(IDs::MASTER_FX).getChild(0).getProperty("param_7", -999.0);
    engine.getProjectCommands().setMasterFxParam(0, 7, 1.0f);
    const double after = (double) engine.getProjectModel().getTree()
        .getChildWithName(IDs::MASTER_FX).getChild(0).getProperty("param_7", -999.0);
    EXPECT_DOUBLE_EQ(before, after);
}

// ---- Gate 1: rebuild restores master FX state on the LIVE processor --------

TEST(MasterBusFx, RestoredAfterRoutingGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    // Enable the limiter and give it a distinctive threshold.
    engine.getProjectCommands().setMasterFxBypassed(1, false);
    engine.getProjectCommands().setMasterFxParam(1, 0, -9.0f);
    engine.getProjectCommands().setMasterFxParam(0, 2, 3.5f); // eq gain dB

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* mb = engine.getMainProcessor()->getRoutingManager()->getMasterBus();
    ASSERT_NE(mb, nullptr);
    EXPECT_FALSE(mb->isSlotBypassed(1));
    EXPECT_FLOAT_EQ(mb->getSlotParam(1, 0), -9.0f);
    EXPECT_FLOAT_EQ(mb->getSlotParam(0, 2), 3.5f);
    EXPECT_EQ(mb->getSlotType(1), juce::String("limiter"));
    EXPECT_EQ(mb->getSlotType(0), juce::String("eq"));
}

TEST(MasterBusFx, RebuildRestoresBypassedDefaults)
{
    AudioEngine engine;
    engine.initialize();

    // Default stamp: eq + limiter, BOTH bypassed. Rebuild must not flip them on.
    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* mb = engine.getMainProcessor()->getRoutingManager()->getMasterBus();
    ASSERT_NE(mb, nullptr);
    EXPECT_TRUE(mb->isSlotBypassed(0));
    EXPECT_TRUE(mb->isSlotBypassed(1));
}

// ---- Backward-compat migration ------------------------------------------

TEST(MasterBusFx, EnsureMasterFxNodeIsIdempotent)
{
    AudioEngine engine;
    engine.initialize();

    auto tree = engine.getProjectModel().getTree();
    ASSERT_TRUE(tree.getChildWithName(IDs::MASTER_FX).isValid());

    // Removing the node and re-running the migration must restore the
    // default stamp exactly once (old-project load path).
    tree.removeChild(tree.getChildWithName(IDs::MASTER_FX), nullptr);
    EXPECT_FALSE(tree.getChildWithName(IDs::MASTER_FX).isValid());

    engine.getProjectModel().ensureMasterFxNode();
    auto masterFx = tree.getChildWithName(IDs::MASTER_FX);
    ASSERT_TRUE(masterFx.isValid());
    ASSERT_EQ(masterFx.getNumChildren(), 2);
    EXPECT_EQ(masterFx.getChild(0).getProperty(IDs::fxType).toString(), juce::String("eq"));
    EXPECT_EQ(masterFx.getChild(1).getProperty(IDs::fxType).toString(), juce::String("limiter"));
    // Both default-bypassed: migration alone must not change audio behavior.
    EXPECT_TRUE(masterFx.getChild(0).getProperty("bypassed", false));
    EXPECT_TRUE(masterFx.getChild(1).getProperty("bypassed", false));

    // Idempotent: second call is a no-op.
    engine.getProjectModel().ensureMasterFxNode();
    EXPECT_EQ(masterFx.getNumChildren(), 2);
}

// ---- Live listener path: command -> tree -> listener -> live atomics -------

TEST(MasterBusFx, CommandUpdatesLiveProcessorWithoutRebuild)
{
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().setMasterFxBypassed(1, false);
    engine.getProjectCommands().setMasterFxParam(1, 0, -6.0f);

    // No rebuildRoutingGraph() here - the ValueTree listener must have pushed
    // the change onto the live processor's atomics.
    auto* mb = engine.getMainProcessor()->getRoutingManager()->getMasterBus();
    ASSERT_NE(mb, nullptr);
    EXPECT_FALSE(mb->isSlotBypassed(1));
    EXPECT_FLOAT_EQ(mb->getSlotParam(1, 0), -6.0f);
}
