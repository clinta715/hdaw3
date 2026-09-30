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
#include "engine/FxBusProcessor.h"
#include "common/MasterFxDefs.h"

namespace {

// Build the mono-source stereo sine the master-bus tests drive (identical on
// both channels).
juce::AudioBuffer<float> makeSineBuffer(double sampleRate, int samples, float freqHz,
                                        float amplitude)
{
    juce::AudioBuffer<float> buf(2, samples);
    const double twoPi = 6.28318530717958647692;
    for (int s = 0; s < samples; ++s)
    {
        const float v = amplitude * (float) std::sin(twoPi * (double) freqHz * (double) s / sampleRate);
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    return buf;
}

// Render `src` through a fresh master bus and return the processed buffer.
// masterGain is pinned BEFORE prepareToPlay, so the smoothed gain starts at
// exactly that value (no ramp) and the render is deterministic — that is what
// makes the bit-exact post-gain assertions below meaningful.
juce::AudioBuffer<float> renderMasterBuffer(const juce::AudioBuffer<float>& src, double sampleRate,
                                           int slotIndex, bool bypassed,
                                           const std::vector<float>& params,
                                           const char* slotKind = "limiter",
                                           float masterGain = 1.0f)
{
    HDAW::MasterBusProcessor master;
    master.setGain(masterGain);
    master.prepareToPlay(sampleRate, 512);
    // Slot type must be explicit: fresh slots default to empty (no DSP runs).
    master.setSlotType(slotIndex, juce::String(slotKind));
    master.setSlotBypassed(slotIndex, bypassed);
    for (size_t i = 0; i < params.size(); ++i)
        master.setSlotParam(slotIndex, (int) i, params[i]);

    juce::AudioBuffer<float> buf(src);
    juce::MidiBuffer midi;
    // Drive in prepared-block-size chunks (lesson 14: fixed-size scratch).
    for (int start = 0; start < buf.getNumSamples(); start += 512)
    {
        const int n = std::min(512, buf.getNumSamples() - start);
        juce::AudioBuffer<float> chunk(2, n);
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom(ch, 0, buf, ch, start, n);
        master.processBlock(chunk, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample(ch, start + i, chunk.getSample(ch, i));
    }
    return buf;
}

// Render a mono-source stereo sine through the master bus and return peak.
float renderPeak(double sampleRate, int samples, float freqHz, float amplitude,
                 int slotIndex, bool bypassed, const std::vector<float>& params,
                 const char* slotKind = "limiter", float masterGain = 1.0f)
{
    auto buf = renderMasterBuffer(makeSineBuffer(sampleRate, samples, freqHz, amplitude),
                                  sampleRate, slotIndex, bypassed, params, slotKind, masterGain);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
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

// Render a stereo sine through an EQ RETURN BUS (FxBusProcessor) and return
// RMS. The three EQ params are pushed through the AUDIO-THREAD automation
// entry (setAutomationValue, real units), so the coefficients are built by
// processBlock's dirty-consume pass -> applyParamToDsp — the exact path an
// automated/LFO-driven EQ return takes.
float renderEqBusRms(double sampleRate, int samples, float freqHz, float amplitude,
                     float gainDb)
{
    HDAW::FxBusProcessor bus("EQ", "eq");
    bus.prepareToPlay(sampleRate, 512);
    bus.setAutomationValue(0, freqHz);
    bus.setAutomationValue(1, 0.7f);
    bus.setAutomationValue(2, gainDb);

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
        bus.processBlock(chunk, midi);
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

TEST(MasterBusFx, LimiterCeilingCapsBelowFullScale)
{
    // B10 (Modular Dawn audit): the limiter's output ceiling was pinned at
    // 0 dBFS — engaging it could never yield peak < 1.0. Ceiling (param 2,
    // 0.5..1.0 linear) post-clamps the limiter output.
    const std::vector<float> params = { -12.0f, 80.0f, 0.8f };
    const float peak = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, params, "limiter");
    EXPECT_LE(peak, 0.81f) << "ceiling 0.8 must cap the rendered peak";
    EXPECT_GE(peak, 0.5f) << "ceiling must clamp, not mute";
}

TEST(MasterBusFx, LimiterCeilingUnsetBehavesAsUnity)
{
    // Legacy/direct-constructed slots that never stored param_2 (resetSlotsTo
    // Defaults zeroes it) must keep the pre-Ceiling contract: hard clip at
    // 0 dBFS, makeup near full scale.
    const std::vector<float> params = { -12.0f, 80.0f };
    const float peak = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, params, "limiter");
    EXPECT_LT(peak, 1.01f);
    EXPECT_GT(peak, 0.90f);
}

TEST(MasterBusFx, LimiterCeilingClampsAtCommandLayer)
{
    AudioEngine engine;
    engine.initialize();

    // Ceiling def range is 0.5..1.0; 0.3 clamps to 0.5, 0.8 lands verbatim.
    EXPECT_FLOAT_EQ(engine.getProjectCommands().setMasterFxParam(1, 2, 0.3f), 0.5f);
    EXPECT_FLOAT_EQ(engine.getProjectCommands().setMasterFxParam(1, 2, 0.8f), 0.8f);

    auto masterFx = engine.getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    ASSERT_TRUE(masterFx.isValid());
    EXPECT_FLOAT_EQ(
        (float) (double) masterFx.getChild(1).getProperty("param_2", 0.0), 0.8f);
}

TEST(MasterBusFx, LimiterCeilingSurvivesRebuildOnLiveProcessor)
{
    // Gate 1/10: the ceiling travels tree -> command -> LIVE master bus
    // processor, and the live processor's processBlock renders peak <= ceiling.
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().setMasterFxBypassed(1, false);
    engine.getProjectCommands().setMasterFxParam(1, 0, -12.0f);
    engine.getProjectCommands().setMasterFxParam(1, 2, 0.8f);
    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* mb = engine.getMainProcessor()->getRoutingManager()->getMasterBus();
    ASSERT_NE(mb, nullptr);
    EXPECT_FLOAT_EQ(mb->getSlotParam(1, 2), 0.8f);

    // Drive the LIVE processor's master bus with a hot stereo sine: the
    // limiter's makeup drives into its 0 dBFS clipper, the ceiling clamps
    // the result to 0.8.
    const int samples = 44100;
    juce::AudioBuffer<float> buf(2, samples);
    const double twoPi = 6.28318530717958647692;
    for (int s = 0; s < samples; ++s)
    {
        const float v = 1.5f * (float) std::sin(twoPi * 200.0 * s / 44100.0);
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int start = 0; start < samples; start += 512)
    {
        const int n = std::min(512, samples - start);
        juce::AudioBuffer<float> chunk(2, n);
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom(ch, 0, buf, ch, start, n);
        mb->processBlock(chunk, midi);
        peak = std::max(peak, chunk.getMagnitude(0, 0, n));
    }
    EXPECT_LE(peak, 0.81f) << "live master bus must honor the ceiling (B10)";
    EXPECT_GE(peak, 0.5f);
}

TEST(MasterBusFx, LimiterBypassedPassesThrough)
{
    const std::vector<float> limiterParams = { -12.0f, 80.0f };
    const float peak = renderPeak(44100.0, 44100, 200.0f, 0.5f, 1, true, limiterParams, "limiter");
    EXPECT_NEAR(peak, 0.5f, 0.02f);
}

// ---- P2-a (2026-09-30): post-gain ceiling clamp ---------------------------
//
// The master FX chain runs BEFORE the master gain, so the limiter's ceiling
// clamp could not protect the output from a gain above unity: measured on a
// real project, ceiling 0.97 + set_master_gain 1.6 -> peak 1.0 with 22.7% of
// frames at full scale (rms 0.46), while the same limiter at gain 1.0 held
// the peak at exactly 0.97 with zero ceiling hits. The fix re-applies the
// enabled limiter slot's effective ceiling to the post-gain buffer, using the
// SAME `ceilingRaw >= 0.5 ? ceilingRaw : 1.0` rule the chain already uses.

TEST(MasterBusFx, PostGainClampHoldsLimiterCeilingAboveUnityGain)
{
    const std::vector<float> params = { -12.0f, 80.0f, 0.8f };
    const float limitedUnity = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, params, "limiter", 1.0f);
    const float limitedHot   = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, params, "limiter", 1.6f);
    // Control: the same 1.6x gain with the limiter BYPASSED is what the pre-fix
    // code produced with the limiter ENABLED (chain output x gain, no clamp).
    const float unprotected  = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, true, params, "limiter", 1.6f);

    EXPECT_LE(limitedUnity, 0.81f) << "in-chain ceiling (gain 1.0) unchanged";
    EXPECT_GT(limitedUnity * 1.6f, 0.81f) << "control: pre-fix, ceiling x gain exceeded the ceiling";
    EXPECT_GT(unprotected, 1.0f) << "control: 1.5 amplitude x 1.6 is over full scale";

    EXPECT_LE(limitedHot, 0.81f) << "gain > 1 must not push the output past the limiter ceiling (P2-a)";
    EXPECT_GE(limitedHot, 0.5f) << "the post-gain clamp must clamp, not mute";

    // The exact configuration from the bug report (ceiling 0.97 + gain 1.6).
    const std::vector<float> measured = { -12.0f, 80.0f, 0.97f };
    const float measuredHot = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, measured, "limiter", 1.6f);
    EXPECT_LE(measuredHot, 0.971f) << "the measured 0.97-ceiling + 1.6-gain case must hold";
}

TEST(MasterBusFx, PostGainClampIsExactGainScalingAtOrBelowUnityGain)
{
    // gain <= 1 can only attenuate, so the post-gain clamp is a no-op there:
    // the output must stay bit-identical to the pre-fix `chainOut * gain`.
    const std::vector<float> params = { -12.0f, 80.0f, 0.8f };
    auto unity = renderMasterBuffer(makeSineBuffer(44100.0, 8192, 200.0f, 1.5f),
                                    44100.0, 1, false, params, "limiter", 1.0f);
    auto half  = renderMasterBuffer(makeSineBuffer(44100.0, 8192, 200.0f, 1.5f),
                                    44100.0, 1, false, params, "limiter", 0.5f);

    float unityPeak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < unity.getNumSamples(); ++s)
        {
            unityPeak = std::max(unityPeak, std::abs(unity.getSample(ch, s)));
            EXPECT_FLOAT_EQ(half.getSample(ch, s), unity.getSample(ch, s) * 0.5f)
                << "ch " << ch << " sample " << s;
        }

    EXPECT_LE(unityPeak, 0.81f) << "limiter + gain 1.0 unchanged from today";
    EXPECT_GT(unityPeak, 0.5f) << "limiter + gain 1.0 must not be muted";
}

TEST(MasterBusFx, NoEnabledLimiterLeavesGainUnclampedBitIdentical)
{
    // COMPATIBILITY GUARANTEE (P2-a): with no limiter slot enabled the
    // post-gain clamp must not run at all. The pre-fix — and still current —
    // behaviour is exactly out = in * gain, so a project that never engages
    // the master limiter renders bit-identically to before this change.
    const float gain = 1.6f;
    const std::vector<float> params = { -12.0f, 80.0f, 0.8f };
    auto src = makeSineBuffer(44100.0, 4096, 200.0f, 1.5f);

    // (a) limiter slot present but BYPASSED; (b) no slot kind at all.
    auto bypassed  = renderMasterBuffer(src, 44100.0, 1, true, params, "limiter", gain);
    auto emptySlot = renderMasterBuffer(src, 44100.0, 1, true, params, "", gain);

    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < src.getNumSamples(); ++s)
        {
            const float expected = src.getSample(ch, s) * gain;
            EXPECT_FLOAT_EQ(bypassed.getSample(ch, s), expected)
                << "bypassed limiter, ch " << ch << " sample " << s;
            EXPECT_FLOAT_EQ(emptySlot.getSample(ch, s), expected)
                << "no slot kind, ch " << ch << " sample " << s;
        }

    // Provably unclamped: 1.5 amplitude x 1.6 = 2.4 peak, far above full scale.
    EXPECT_GT(bypassed.getMagnitude(0, 0, bypassed.getNumSamples()), 2.3f);
}

TEST(MasterBusFx, PostGainClampKeepsUnityCeilingForLegacyUnsetCeiling)
{
    // ceilingRaw < 0.5 means the slot never stored a ceiling (legacy project /
    // direct construction — the command layer cannot write below the 0.5 def
    // minimum). The in-chain rule treats that as unity; the post-gain clamp
    // must reuse the SAME rule, so gain > 1 is clamped at 1.0 — never left to
    // clip past full scale, and never clamped to the raw sub-0.5 value (0.0
    // would mute the bus).
    const std::vector<float> params = { -12.0f, 80.0f };   // param_2 never stored
    const float peak = renderPeak(44100.0, 44100, 200.0f, 1.5f, 1, false, params, "limiter", 1.6f);

    EXPECT_LE(peak, 1.0f) << "legacy/unset ceiling keeps the unity-ceiling contract post-gain";
    EXPECT_GT(peak, 0.9f) << "the limiter's makeup still drives near full scale (not muted)";
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

// ---- EQ coefficients: array path == allocating wrapper ---------------------

TEST(MasterBusFx, EqCoefficientsMatchAllocatingWrapper)
{
    // The three EQ rebuild sites (FxBusProcessor, MasterBusProcessor,
    // TrackFXSlot) assign
    // juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(...) straight
    // into the persistent Coefficients object instead of dereferencing the
    // allocating Coefficients::makePeakFilter wrapper (one heap allocation per
    // call — juce_IIRFilter.cpp). This pins that the array path stores the SAME
    // coefficients the wrapper stored, so no behaviour can shift: the 0 dB
    // default (the 2026-08-27 silence pitfall) and both +/-24 dB gain extremes
    // included.
    //
    // Compared on `coefficients` (== getRawCoefficients()): the wrapper is
    // itself built from ArrayCoefficients::makePeakFilter and both paths run
    // Coefficients::assignImpl, so the a0-normalized storage must be
    // bit-identical — a biquad keeps 5 of the 6 array values.
    const double sr = 44100.0;
    const struct { float f, q, g; } cases[] = {
        {  1000.0f,  0.7f,   0.0f },   // default gain: 0 dB == unity
        {  4000.0f,  0.7f,  12.0f },
        {   120.0f,  4.0f, -24.0f },   // bottom of the gain def range
        { 12000.0f,  0.1f,  24.0f },   // top of the gain def range
        { 20000.0f, 10.0f, -24.0f },   // def-range edges for frequency/Q too
    };

    for (const auto& c : cases)
    {
        auto wrapper = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sr, c.f, c.q, juce::Decibels::decibelsToGain(c.g));

        // The production form: the array assigned into the persistent object.
        juce::dsp::IIR::Coefficients<float> viaArray;
        viaArray = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
            sr, c.f, c.q, juce::Decibels::decibelsToGain(c.g));

        ASSERT_EQ(viaArray.getFilterOrder(), wrapper->getFilterOrder());
        ASSERT_EQ(viaArray.coefficients.size(), wrapper->coefficients.size());
        for (int i = 0; i < viaArray.coefficients.size(); ++i)
            EXPECT_EQ(viaArray.coefficients[i], wrapper->coefficients[i])
                << "f=" << c.f << " q=" << c.q << " g=" << c.g << " coeff " << i;
    }
}

// ---- Gate 2: EQ RETURN BUS automation path is observable -------------------

TEST(MasterBusFx, EqBusAutomationRebuildRaisesRmsAtBandCenter)
{
    // FxBusProcessor rebuilds its EQ coefficients inside processBlock's
    // dirty-consume pass from the AUDIO-THREAD automation entry
    // (setAutomationValue) — the same rebuild the new allocation-free array
    // assignment serves. +12 dB at 4 kHz must boost a 4 kHz sine, and the 0 dB
    // default must stay unity rather than silence (2026-08-27 pitfall).
    const float unityRms   = renderEqBusRms(44100.0, 44100, 4000.0f, 0.5f, 0.0f);
    const float boostedRms = renderEqBusRms(44100.0, 44100, 4000.0f, 0.5f, 12.0f);

    EXPECT_NEAR(unityRms, 0.354f, 0.03f) << "0 dB peak filter must be unity, not silence";
    EXPECT_GT(boostedRms, unityRms * 1.6f) << "+12 dB at the band centre must boost";
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
