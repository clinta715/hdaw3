// Automation/LFO pid routing tests (plan 2026-09-02-automation-pid-routing-fix).
// Commit e917c1f1 routed every pid >= 200 into midiFxChain, shadowing the
// audio compound 100 + slot*100 + param for slot >= 1 (audio-FX lanes on slot
// 1 decoded as midiFx[0]). The midiFx compound moved to 1000 + slot*100 + param;
// these tests pin the decode on the LIVE track processors (rebuild-restore
// discipline: assert getMainProcessor()->getTrack(idx), never the ReadModel).
//
// Seam style copied from InternalFx.FilterCutoffAutomationSweeps
// (psytrance_composition_stress_test.cpp): FixedPlayHead + direct
// Track::processBlock + bandEnergyDb. Those helpers are static/anonymous-
// namespace in that file, so small local copies live here.

#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/MidiFx.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

// Band-limited (2k-8k) energy of channel 0 in dB (4096-point FFT) — proves a
// lowpass really attenuates the high band. Same math as the stress test.
double bandEnergyDb(const juce::AudioBuffer<float>& buf, double sr)
{
    constexpr int order = 12;
    const int n = 1 << order; // 4096
    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<size_t>(2 * n), 0.0f);
    const float* src = buf.getReadPointer(0);
    const int m = std::min(n, buf.getNumSamples());
    for (int i = 0; i < m; ++i)
        data[static_cast<size_t>(i)] = src[i];
    fft.performFrequencyOnlyForwardTransform(data.data(), true);
    const double binHz = sr / n;
    const int k0 = static_cast<int>(std::ceil(2000.0 / binHz));
    const int k1 = std::min(static_cast<int>(std::floor(8000.0 / binHz)), n / 2);
    double energy = 0.0;
    for (int k = k0; k <= k1; ++k)
    {
        const double mag = data[static_cast<size_t>(k)];
        energy += mag * mag;
    }
    return 10.0 * std::log10(energy + 1e-12);
}

// Fixed playhead so lane reads (and the per-block tempo feed) are
// deterministic during direct Track::processBlock calls.
class FixedPlayHead : public juce::AudioPlayHead
{
public:
    void setTimeSeconds(double s) { seconds_ = s; }
    void setBpm(double b) { bpm_ = b; }
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setTimeInSeconds(seconds_);
        info.setTimeInSamples(static_cast<juce::int64>(seconds_ * 48000.0));
        info.setBpm(bpm_);
        return info;
    }
private:
    double seconds_ = 0.0;
    double bpm_ = 120.0;
};

constexpr double kSr = 48000.0;
constexpr int kBlock = 4096;

juce::AudioBuffer<float> makeSineMix()
{
    // 200 Hz (below cutoff, passes) + 4 kHz (above, cut).
    juce::AudioBuffer<float> buf(2, kBlock);
    for (int i = 0; i < kBlock; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        const float v = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 200.0f * t)
                      + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 4000.0f * t);
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    return buf;
}

} // namespace

// An automation lane with the AUDIO compound pid 200 (= fx slot 1, param 0)
// must drive the live filter at fxChain[1], must NOT leak into the coexisting
// arpeggiator (pre-fix, pid 200 decoded as midiFx[0] param 0), and must
// audibly sweep the band.
TEST(AutomationPidRouting, AudioLaneDrivesLiveFilterCutoff)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "filter");          // slot 0 — wide open, transparent
    cmds.addFxSlot(0, "filter");          // slot 1 — the automated filter
    cmds.setFxSlotParam(0, 0, 1, 0.0f);   // slot 0 Mode = lowpass
    cmds.setFxSlotParam(0, 0, 0, 1.0f);   // slot 0 Cutoff max (~20 kHz)
    cmds.setFxSlotParam(0, 1, 1, 0.0f);   // slot 1 Mode = lowpass
    cmds.addMidiFxSlot(0, "arpeggiator", 0);

    // Lane bound to pid 200 = 100 + slot 1 * 100 + param 0 (Cutoff).
    ASSERT_TRUE(cmds.addAutomationLane(0, "FilterSlot1Cutoff", 200));
    cmds.addAutomationPoint(0, "FilterSlot1Cutoff", 0.0, 0.0f);  // 0 s -> 20 Hz
    cmds.addAutomationPoint(0, "FilterSlot1Cutoff", 16.0, 1.0f); // 8 s @120 BPM -> 20 kHz

    // Gate 1/10: lanes + slots survive a full routing rebuild (the rebuilt
    // managers must re-decode pid 200 as audio slot 1, not midiFx).
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 2);
    auto* filterSlot = track->getFXChain().at(1).get();
    ASSERT_EQ(filterSlot->getType().toStdString(), "filter");
    ASSERT_EQ(track->getMidiFxChain().size(), 1u);
    auto* midiFx = track->getMidiFxChain().at(0).get();

    // Untouched-arp baseline. Captured AFTER the rebuild: loadParamsFromTree
    // stores the NORMALIZED arp-rate default ((0.25-0.01)/(2.0-0.01)), and the
    // rebuild's scratch processBlock drive (lesson 21) runs the modulation
    // loop but never touches params (no enabled LFO here; lanes need a
    // playing playhead, not yet attached). The lane must leave this value
    // alone — pre-fix, pid 200 landed here and overwrote it with 0/1.
    const float midiFxBaseline = midiFx->getAutomationParam(0);
    EXPECT_NEAR(midiFxBaseline, (0.25f - 0.01f) / (2.0f - 0.01f), 1e-3f);

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    const juce::AudioBuffer<float> input = makeSineMix();
    juce::MidiBuffer midi;

    // Warm-up pass settles the IIR state; the measured pass uses a fresh copy.
    auto renderAt = [&](double seconds) {
        ph.setTimeSeconds(seconds);
        juce::AudioBuffer<float> warm(input);
        track->processBlock(warm, midi);
        juce::AudioBuffer<float> out(input);
        track->processBlock(out, midi);
        return bandEnergyDb(out, kSr);
    };

    const double lowDb = renderAt(0.0);   // lane value 0.0 -> cutoff 20 Hz
    const float cutoffAtOpen = filterSlot->getAutomationParam(0);
    const double highDb = renderAt(10.0); // lane holds 1.0 -> cutoff ~20 kHz
    const float cutoffAtClosed = filterSlot->getAutomationParam(0);

    std::cout << "AudioLanePid200: cutoff@0s=" << cutoffAtOpen
              << " cutoff@10s=" << cutoffAtClosed
              << " band low=" << lowDb << " high=" << highDb
              << " (diff=" << (highDb - lowDb) << " dB)" << std::endl;

    // Live audio slot follows the lane points (normalized cutoff 0 -> 1;
    // linear [20, 20000] Hz round-trips exactly at both ends).
    EXPECT_FLOAT_EQ(cutoffAtOpen, 0.0f);
    EXPECT_FLOAT_EQ(cutoffAtClosed, 1.0f);
    // Band contrast: the automated lowpass really sweeps the spectrum.
    EXPECT_GE(highDb - lowDb, 12.0) << "lane-driven cutoff must sweep >= 12 dB";

    // The audio lane must not leak into the coexisting arpeggiator: midiFx
    // param 0 (rate) keeps its post-load value. Pre-fix, pid 200 landed here.
    EXPECT_FLOAT_EQ(midiFx->getAutomationParam(0), midiFxBaseline)
        << "pid 200 (audio slot 1) must not touch midiFx[0] param 0";
}

// A lane with the NEW midiFx compound pid 1000 (= midiFx slot 0, param 0)
// must drive the live arpeggiator and leave the audio fxChain untouched.
TEST(AutomationPidRouting, MidiFxLanePid1000DrivesLiveArp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiFxSlot(0, "arpeggiator", 0);
    cmds.addFxSlot(0, "filter"); // audio counterpart that must stay untouched

    // Lane bound to pid 1000 = 1000 + slot 0 * 100 + param 0 (arp rate).
    ASSERT_TRUE(cmds.addAutomationLane(0, "ArpRate", 1000));
    cmds.addAutomationPoint(0, "ArpRate", 0.0, 0.0f);  // normalized 0
    cmds.addAutomationPoint(0, "ArpRate", 16.0, 1.0f); // normalized 1 (holds past 8 s)

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getMidiFxChain().size(), 1u);
    auto* midiFx = track->getMidiFxChain().at(0).get();
    ASSERT_GE(track->getNumFXSlots(), 1);
    auto* audioSlot = track->getFXChain().at(0).get();
    const float audioCutoffBaseline = audioSlot->getAutomationParam(0);

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> silent(2, kBlock);
    juce::MidiBuffer midi;

    auto renderAt = [&](double seconds) {
        ph.setTimeSeconds(seconds);
        track->processBlock(silent, midi);
    };

    // Pre-fix, pid 1000 decoded as midiFx slot 8 (out of range) and dropped.
    renderAt(0.0);
    EXPECT_FLOAT_EQ(midiFx->getAutomationParam(0), 0.0f)
        << "pid 1000 lane value 0 must reach live midiFx[0] param 0";
    renderAt(10.0);
    EXPECT_FLOAT_EQ(midiFx->getAutomationParam(0), 1.0f)
        << "pid 1000 lane value 1 must reach live midiFx[0] param 0";

    // The stored normalized value must actually denormalize onto the effect:
    // arp rate range [0.01, 2.0] -> 0.0 maps to 0.01 beats, 1.0 maps to 2.0.
    auto* arp = dynamic_cast<HDAW::Arpeggiator*>(midiFx->getEffect());
    ASSERT_NE(arp, nullptr);
    EXPECT_NEAR(arp->rate, 2.0, 1e-3);

    // Audio fxChain untouched by the midiFx lane.
    EXPECT_FLOAT_EQ(audioSlot->getAutomationParam(0), audioCutoffBaseline);
}

// An LFO whose targetParamID is the NEW midiFx compound 1000 must modulate
// the live arpeggiator param during processBlock (ModulationPanel's midiFx
// LFO targets keep working under the moved range).
TEST(AutomationPidRouting, LfoTarget1000ModulatesLiveArp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiFxSlot(0, "arpeggiator", 0);
    cmds.addLfo(0);
    // Unipolar square, depth 0.5, targeting pid 1000. The tree write triggers
    // the AudioEngine MODULATION listener -> rebuildModulation (the MCP path).
    // The LFO stays DISABLED until after the rebuild: rebuildRoutingGraph's
    // scratch processBlock drive (lesson 21) runs the modulation loop, and an
    // enabled LFO would pre-saturate the target param before the base read.
    cmds.setLfoParam(0, 0, "enabled", 0.0);
    cmds.setLfoParam(0, 0, "waveform", 3.0);
    cmds.setLfoParam(0, 0, "depth", 0.5);
    cmds.setLfoParam(0, 0, "targetParamID", 1000.0);

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getMidiFxChain().size(), 1u);
    auto* midiFx = track->getMidiFxChain().at(0).get();

    // Live modulation source carries the new-range target (Track probe, not
    // the ValueTree).
    ASSERT_EQ(track->getNumModulations(), 1);
    EXPECT_EQ(track->getModulationSourceParamID(0), 1000)
        << "rebuilt LFO source must decode targetParamID 1000";

    // Base AFTER the rebuild, BEFORE the LFO runs (normalized arp-rate
    // default from loadParamsFromTree, ~0.1206).
    const float base = midiFx->getAutomationParam(0);

    // Enable the LFO: tree write -> MODULATION listener -> rebuildModulation
    // re-reads enabled=true onto the live source.
    cmds.setLfoParam(0, 0, "enabled", 1.0);
    ASSERT_EQ(track->getModulationSourceParamID(0), 1000);

    FixedPlayHead ph;
    ph.setTimeSeconds(0.0);
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> silent(2, kBlock);
    juce::MidiBuffer midi;

    track->processBlock(silent, midi);
    const float after = midiFx->getAutomationParam(0);

    std::cout << "LfoTarget1000: base=" << base << " after=" << after << std::endl;
    // Unipolar square pushes every sample up (depth 0.5); the accumulated
    // base is clamped to [0,1], so the param must sit above its default.
    EXPECT_GT(after, base) << "LFO target 1000 must modulate live midiFx[0] param 0";
    // Pre-fix, pid 1000 decoded as midiFx slot 8 (out of range) -> no change.
    EXPECT_NE(after, base);
}
