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

// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Seed exactly the track the test
// addresses and drain the coalesced routing rebuild so live-processor reads
// are deterministic (lessons 9/10/12; no sleeps).
int seedTrack(AudioEngine& engine)
{
    const int idx = engine.getProjectCommands().addTrack("Track 0");
    engine.drainPendingRoutingRebuild();
    return idx;
}

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

// paramID of the named automation lane on a track (-999 when no lane owns the
// name) — the tree-side contract of the send-removal remap tests below assert
// exact paramIDs on the AUTOMATION_LIST, never a ReadModel projection.
int lanePidOf(AudioEngine& engine, int trackIndex, const std::string& name)
{
    const auto autoList = engine.getProjectModel().getTrackListTree()
                              .getChild(trackIndex)
                              .getChildWithName(IDs::AUTOMATION_LIST);
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        const auto lane = autoList.getChild(i);
        if (lane.getProperty(IDs::name, "").toString().toStdString() == name)
            return static_cast<int>(lane.getProperty(IDs::paramID, 0));
    }
    return -999;
}

// targetParamID of the lfoIndex-th MODULATION child on a track (-999 when the
// index is out of range, i.e. no such LFO) — the tree-side contract of the
// send-removal LFO remap tests below, read from MODULATION_LIST directly,
// never a ReadModel projection.
int lfoTargetPidOf(AudioEngine& engine, int trackIndex, int lfoIndex)
{
    const auto modList = engine.getProjectModel().getTrackListTree()
                             .getChild(trackIndex)
                             .getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren())
        return -999;
    return static_cast<int>(modList.getChild(lfoIndex).getProperty(IDs::targetParamID, 0));
}

// Number of MODULATION children on a track. Proves a parked LFO was KEPT
// (targetParamID -1) rather than deleted.
int modulationChildCountOf(AudioEngine& engine, int trackIndex)
{
    const auto modList = engine.getProjectModel().getTrackListTree()
                             .getChild(trackIndex)
                             .getChildWithName(IDs::MODULATION_LIST);
    return modList.isValid() ? modList.getNumChildren() : 0;
}

// A non-target property of the lfoIndex-th MODULATION child (proves a parked
// LFO kept its own settings). 0.0 when the index is out of range.
double lfoPropOf(AudioEngine& engine, int trackIndex, int lfoIndex,
                 const juce::Identifier& prop)
{
    const auto modList = engine.getProjectModel().getTrackListTree()
                             .getChild(trackIndex)
                             .getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren())
        return 0.0;
    return static_cast<double>(modList.getChild(lfoIndex).getProperty(prop, 0));
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
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addFxSlot(0, "filter");          // slot 0 — wide open, transparent
    cmds.addFxSlot(0, "filter");          // slot 1 — the automated filter
    cmds.setFxSlotParam(0, 0, 1, 0.0f);   // slot 0 Mode = lowpass
    // REAL-unit contract (lesson 23 / handoff B5-B8): setFxSlotParam values
    // are in the param def's real range, not normalized 0..1 — 1.0 would clamp
    // to the 20 Hz minimum and kill the sweep contrast below.
    cmds.setFxSlotParam(0, 0, 0, 20000.0f); // slot 0 Cutoff max (~20 kHz)
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
    ASSERT_GE(seedTrack(engine), 0);

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
    ASSERT_GE(seedTrack(engine), 0);

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

// ── Send-level (2000+) and bus-FX (3000+) automatable pids ───────────────
// Same discipline as the tests above: decode is pinned on the LIVE processors
// (getMainProcessor()->getTrack / getRoutingManager()), through a full
// rebuildRoutingGraph — mutate -> rebuild -> apply -> LIVE assert, never
// ReadModel-only (Gates 1/2/9/10).

// A lane bound to pid 2000 + sendIndex drives the LIVE SendProcessor's level
// in the raw send-gain domain, and the registration survives a full rebuild:
// the post-rebuild apply must move the NEW live SendProcessor, asserted from
// BOTH ends of the registration contract (RoutingManager::getSend and the
// Track's registered handle are the same object).
TEST(AutomationSendBusPids, SendLaneDrivesLiveSendLevelAcrossRebuild)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    // The default project ships bus 1 = "Reverb" (fx) — route a send there.
    const auto send = cmds.createSend(0, 1, 0.0f, false);
    ASSERT_TRUE(send.ok) << send.error;
    ASSERT_EQ(send.sendIndex, 0);
    engine.drainPendingRoutingRebuild();

    // Lane bound to pid 2000 + sendIndex(0). Points are in BEATS: 0 -> 0.25,
    // 16 beats @120 BPM = 8 s -> 1.0, holding after that.
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendLevel", 2000));
    cmds.addAutomationPoint(0, "SendLevel", 0.0, 0.25f);
    cmds.addAutomationPoint(0, "SendLevel", 16.0, 1.0f);

    // Gate 10: the full rebuild must re-register the FRESH Track's handles.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);

    // Both ends of the registration contract point at the SAME live object.
    ASSERT_EQ(track->getNumRegisteredSends(), 1);
    auto* registered = track->getRegisteredSend(0);
    ASSERT_NE(registered, nullptr);
    auto* live = rm->getSend(0, send.sendIndex);
    ASSERT_NE(live, nullptr);
    ASSERT_EQ(registered, live);

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;

    ph.setTimeSeconds(0.0);
    track->processBlock(buf, midi);
    EXPECT_FLOAT_EQ(live->getSendLevel(), 0.25f)
        << "pid 2000 lane must reach the live SendProcessor (raw level domain)";

    ph.setTimeSeconds(10.0); // past the 8 s point -> the lane holds 1.0
    buf.clear();
    track->processBlock(buf, midi);
    EXPECT_FLOAT_EQ(live->getSendLevel(), 1.0f);
    EXPECT_FLOAT_EQ(registered->getSendLevel(), 1.0f)
        << "registered handle and RoutingManager state are one object";
}

// A lane bound to pid 3000 + busID*8 + paramIndex drives the LIVE
// FxBusProcessor param in the def's REAL units (Q 2.0 -> 5.0; a normalized
// decode would clamp to 1.0 and fail this), touches ONLY paramIndex 1, and
// the registration survives a full rebuild (Gate 1/10).
TEST(AutomationSendBusPids, BusLaneDrivesLiveBusParamAcrossRebuild)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    const auto bus = cmds.createBus("fx", "Auto EQ", "eq", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine.drainPendingRoutingRebuild();

    // pid = 3000 + busID*8 + 1 -> this bus's Q; index 0 (Frequency) must not move.
    ASSERT_TRUE(cmds.addAutomationLane(0, "BusQ", 3000 + bus.busID * 8 + 1));
    cmds.addAutomationPoint(0, "BusQ", 0.0, 2.0f);
    cmds.addAutomationPoint(0, "BusQ", 16.0, 5.0f);

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* fx = rm->getFxBus(bus.busID);
    ASSERT_NE(fx, nullptr);

    // Registration contract end 2: the Track's registry maps busID to the
    // SAME live processor.
    const auto* registry = track->getBusRegistry();
    ASSERT_NE(registry, nullptr);
    const auto regIt = registry->find(bus.busID);
    ASSERT_NE(regIt, registry->end());
    EXPECT_EQ(regIt->second, fx);

    // eq default restored by the rebuild's applyFromTree — also the
    // untouched-index baseline for the paramIndex precision check.
    EXPECT_FLOAT_EQ(fx->getParam(0), 1000.0f);

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;

    ph.setTimeSeconds(0.0);
    track->processBlock(buf, midi);
    EXPECT_FLOAT_EQ(fx->getParam(1), 2.0f)
        << "bus lane rides the def's REAL units — a normalized decode would be 1.0";
    EXPECT_FLOAT_EQ(fx->getParam(0), 1000.0f)
        << "paramIndex 1 must not leak into paramIndex 0";

    ph.setTimeSeconds(10.0);
    buf.clear();
    track->processBlock(buf, midi);
    EXPECT_FLOAT_EQ(fx->getParam(1), 5.0f);
}

// Range isolation + bounds (Gates 2/9): pid 2000+ moves ONLY sends, pid 3000+
// ONLY bus params — neither may fall into the >=1000 midiFx or >=100 audio-FX
// decode (the shadow hazard) — while out-of-range sends/params, a just-below
// pid and a negative pid are silent no-ops that leave everything untouched.
TEST(AutomationSendBusPids, SendAndBusPidsDoNotShadowFxAndOutOfRangeNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    const auto bus = cmds.createBus("fx", "Auto Rev", "reverb", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine.drainPendingRoutingRebuild();
    const auto send = cmds.createSend(0, bus.busID, 0.0f, false);
    ASSERT_TRUE(send.ok) << send.error;
    engine.drainPendingRoutingRebuild();

    // Coexisting FX chains whose params must NOT move (shadow probes).
    cmds.addFxSlot(0, "filter");          // audio FX slot 0
    cmds.setFxSlotParam(0, 0, 0, 440.0f); // Cutoff = 440 Hz (real units)
    cmds.addMidiFxSlot(0, "arpeggiator", 0); // midiFx slot 0
    engine.drainPendingRoutingRebuild();

    // In-range lanes: one send, one bus param.
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendLvl", 2000 + send.sendIndex));
    cmds.addAutomationPoint(0, "SendLvl", 0.0, 0.75f);
    cmds.addAutomationPoint(0, "SendLvl", 16.0, 0.75f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "BusRoom", 3000 + bus.busID * 8 + 0));
    cmds.addAutomationPoint(0, "BusRoom", 0.0, 0.9f);
    cmds.addAutomationPoint(0, "BusRoom", 16.0, 0.9f);

    // Boundary lanes (Gate 9), all silent no-ops:
    //  - 2999: sendIndex 999 > the one registered send — near 3000 it must
    //    NOT decode as a bus and must not touch send 0;
    //  - 3000+busID*8+6: paramIndex 6 >= reverb's defs count;
    //  - 1999: just below the send range -> the pre-existing midiFx decode
    //    (slot 9, absent here -> no-op), never the send;
    //  - -5: negative pid matches no branch at all.
    ASSERT_TRUE(cmds.addAutomationLane(0, "GhostSend", 2999));
    cmds.addAutomationPoint(0, "GhostSend", 0.0, 0.0f);
    cmds.addAutomationPoint(0, "GhostSend", 16.0, 0.0f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "GhostBus", 3000 + bus.busID * 8 + 6));
    cmds.addAutomationPoint(0, "GhostBus", 0.0, 0.0f);
    cmds.addAutomationPoint(0, "GhostBus", 16.0, 0.0f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "BelowSend", 1999));
    cmds.addAutomationPoint(0, "BelowSend", 0.0, 1.0f);
    cmds.addAutomationPoint(0, "BelowSend", 16.0, 1.0f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "NegPid", -5));
    cmds.addAutomationPoint(0, "NegPid", 0.0, 1.0f);
    cmds.addAutomationPoint(0, "NegPid", 16.0, 1.0f);

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* fx = rm->getFxBus(bus.busID);
    ASSERT_NE(fx, nullptr);
    auto* liveSend = rm->getSend(0, send.sendIndex);
    ASSERT_NE(liveSend, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);
    ASSERT_EQ(track->getMidiFxChain().size(), 1u);
    auto* audioSlot = track->getFXChain().at(0).get();
    auto* midiFx = track->getMidiFxChain().at(0).get();

    // Baselines AFTER the rebuild, BEFORE the lane run: the rebuild restores
    // the filter's 440 Hz from the tree; the arp carries its load default.
    const float audioBaseline = audioSlot->getAutomationParam(0);
    const float midiBaseline = midiFx->getAutomationParam(0);

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    ph.setTimeSeconds(5.0); // between both constant points -> steady values
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    track->processBlock(buf, midi);

    // In-range lanes moved their targets...
    EXPECT_FLOAT_EQ(liveSend->getSendLevel(), 0.75f);
    EXPECT_FLOAT_EQ(fx->getParam(0), 0.9f);
    // ...the FX chains and the boundary lanes' would-be targets did not.
    EXPECT_FLOAT_EQ(audioSlot->getAutomationParam(0), audioBaseline)
        << "pid 2000+/3000+ lanes must not decode into the audio fx range";
    EXPECT_FLOAT_EQ(midiFx->getAutomationParam(0), midiBaseline)
        << "pid 2000+/3000+ lanes must not decode into midiFx (>=1000 shadow)";
    EXPECT_FLOAT_EQ(fx->getParam(1), 0.5f)
        << "paramIndex 6 is out of the reverb defs: Damping default untouched";
}

// Encoder contract: getAutomatableParams advertises the new ranges with FULL
// pids (pass-through >= 1000, composePid-safe), names resolved from the send
// list and the BusFxDefs.h tables, and no non-fx bus leaking in.
TEST(AutomationSendBusPids, AutomatableParamsCarryFullSendAndBusPids)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    const auto bus = cmds.createBus("fx", "Auto EQ", "eq", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine.drainPendingRoutingRebuild();
    const auto send = cmds.createSend(0, bus.busID, 0.5f, false);
    ASSERT_TRUE(send.ok) << send.error;
    engine.drainPendingRoutingRebuild();

    const auto params = engine.getReadModel().getAutomatableParams(0);

    const AutomatableParamSnapshot* sendEntry = nullptr;
    const AutomatableParamSnapshot* freqEntry = nullptr;
    const AutomatableParamSnapshot* qEntry = nullptr;
    int sendEntries = 0, busEntries = 0;
    for (const auto& p : params)
    {
        if (p.paramIndex >= 2000 && p.paramIndex < 3000)
        {
            ++sendEntries;
            if (p.paramIndex == 2000 + send.sendIndex) sendEntry = &p;
        }
        else if (p.paramIndex >= 3000)
        {
            ++busEntries;
            if (p.paramIndex == 3000 + bus.busID * 8 + 0) freqEntry = &p;
            if (p.paramIndex == 3000 + bus.busID * 8 + 1) qEntry = &p;
        }
    }

    // Send entry: full pid, indexed by sendIndex, positionally named.
    ASSERT_NE(sendEntry, nullptr);
    EXPECT_EQ(sendEntry->slotIndex, send.sendIndex);
    EXPECT_TRUE(sendEntry->automatable);
    EXPECT_EQ(sendEntry->name, "Send 1");

    // Bus entries: full pid 3000 + busID*8 + p for every eq def, named
    // "<bus>.<param>" from the BusFxDefs table.
    ASSERT_NE(freqEntry, nullptr);
    ASSERT_NE(qEntry, nullptr);
    EXPECT_EQ(freqEntry->slotIndex, bus.busID);
    EXPECT_TRUE(freqEntry->automatable);
    EXPECT_EQ(freqEntry->name, "Auto EQ.Frequency");
    EXPECT_EQ(qEntry->name, "Auto EQ.Q");
    // The shipped default return (busID 1, reverb, 5 defs) is advertised too.
    bool hasReverbRoomSize = false;
    for (const auto& p : params)
        if (p.paramIndex == 3000 + 1 * 8 + 0) hasReverbRoomSize = true;
    EXPECT_TRUE(hasReverbRoomSize) << "the default Reverb return must be listed";

    // Exactly the track's one send, and bus params of exactly the two fx
    // buses (eq 3 defs + shipped reverb 5 defs) — the master/group buses
    // (busType != "fx") contribute nothing.
    EXPECT_EQ(sendEntries, 1);
    EXPECT_EQ(busEntries, 8);
}

// ── removeSend lane remap (item-7 inventory gap) ────────────────────────────
// removeSend must splice the SEND list AND the durable sendIndex encoded in
// lane paramIDs (2000 + sendIndex) — both surfaces reach this ONE command.
// Three sends A/B/C with lanes 2000/2001/2002 (held values 0.25/0.5/0.75):
// removing send 0 reindexes B->0, C->1, so B's and C's lanes must decrement
// and A's own lane must be DELETED (a re-created send at index 0 must not
// inherit A's automation). Gate 1/10: the shifted lanes are proved on the
// LIVE sends through a full extra rebuild + processBlock, never the ReadModel.

TEST(AutomationSendBusPids, RemoveSendShiftsSurvivorLanesOntoReindexedSends)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    // A/B/C on the default Reverb bus, tree baseline 0.0 so a stale lane
    // (wrong pid) and a correctly shifted one are distinguishable.
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 1);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 2);
    engine.drainPendingRoutingRebuild();

    // One lane per send, distinct HELD values (flat points at 0 and 16 beats).
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendA", 2000));
    cmds.addAutomationPoint(0, "SendA", 0.0, 0.25f);
    cmds.addAutomationPoint(0, "SendA", 16.0, 0.25f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendB", 2001));
    cmds.addAutomationPoint(0, "SendB", 0.0, 0.5f);
    cmds.addAutomationPoint(0, "SendB", 16.0, 0.5f);
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendC", 2002));
    cmds.addAutomationPoint(0, "SendC", 0.0, 0.75f);
    cmds.addAutomationPoint(0, "SendC", 16.0, 0.75f);
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;

    // Tree contract after the splice: survivors decremented, the removed
    // send's lane gone.
    EXPECT_EQ(lanePidOf(engine, 0, "SendB"), 2000) << "B's lane rides reindexed send 0";
    EXPECT_EQ(lanePidOf(engine, 0, "SendC"), 2001) << "C's lane rides reindexed send 1";
    EXPECT_EQ(lanePidOf(engine, 0, "SendA"), -999) << "the removed send's lane must not survive";

    // Gate 1/10: the remap is tree state — it must survive a full rebuild and
    // then drive the LIVE processors.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(engine.getReadModel().getTrackSends(0).size(), 2u);

    // Both ends of the registration contract on the two survivors; the third
    // send is gone.
    ASSERT_EQ(track->getNumRegisteredSends(), 2);
    auto* live0 = rm->getSend(0, 0);
    auto* live1 = rm->getSend(0, 1);
    ASSERT_NE(live0, nullptr);
    ASSERT_NE(live1, nullptr);
    EXPECT_EQ(track->getRegisteredSend(0), live0);
    EXPECT_EQ(track->getRegisteredSend(1), live1);
    EXPECT_EQ(rm->getSend(0, 2), nullptr) << "send index 2 no longer exists";

    FixedPlayHead ph;
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    ph.setTimeSeconds(0.0);
    track->processBlock(buf, midi);

    EXPECT_FLOAT_EQ(live0->getSendLevel(), 0.5f)
        << "the SHIFTED lane (2001 -> 2000) must drive the reindexed send 0 (old B)";
    EXPECT_FLOAT_EQ(live1->getSendLevel(), 0.75f)
        << "lane 2002 -> 2001 must still drive its target (old C, now send 1)";
}

// The remap touches ONLY the send range: the removed send's own lane and the
// survivor window [2000+R+1, 2999]. Every other pid range on the SAME track —
// mixer 1/2/3, track FX 100-999, MIDI FX 1000-1999, stable bus 3000+busID*8 —
// must come through a removal with its exact paramID (Gate 9), while the top
// of the send range (2999 = sendIndex 999) shifts WITH the survivor window
// and never gets deleted, never crosses into the 3000+ bus range.
TEST(AutomationSendBusPids, RemoveSendRemapsOnlySendRangeLeavingOtherPids)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    const auto send = cmds.createSend(0, 1, 0.0f, false);   // one send: index 0
    ASSERT_TRUE(send.ok) << send.error;
    ASSERT_EQ(send.sendIndex, 0);
    engine.drainPendingRoutingRebuild();

    // Lanes in every pid range on the owning track. Default Volume/Pan/Mute
    // (1/2/3) ship with the track (createTrackAutomationList); add the rest.
    ASSERT_TRUE(cmds.addAutomationLane(0, "RemovedSend", 2000));          // deleted with R=0
    ASSERT_TRUE(cmds.addAutomationLane(0, "Ghost999", 2999));             // top of send range
    ASSERT_TRUE(cmds.addAutomationLane(0, "FxTone", 300));                // track FX compound
    ASSERT_TRUE(cmds.addAutomationLane(0, "MidiFxLvl", 1000));            // midi FX compound
    ASSERT_TRUE(cmds.addAutomationLane(0, "BusRoom", 3000 + 1 * 8 + 0));  // bus 1 (Reverb) room
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    engine.drainPendingRoutingRebuild();

    // Send range: the removed send's lane is gone; 2999 rides the survivor
    // window down to 2998 (still a ghost — sendIndex 998 doesn't exist — and
    // never deleted, never pushed across the 3000+ bus boundary).
    EXPECT_EQ(lanePidOf(engine, 0, "RemovedSend"), -999);
    EXPECT_EQ(lanePidOf(engine, 0, "Ghost999"), 2998);
    // Every other range keeps its exact paramID.
    EXPECT_EQ(lanePidOf(engine, 0, "Volume"), 1);
    EXPECT_EQ(lanePidOf(engine, 0, "Pan"), 2);
    EXPECT_EQ(lanePidOf(engine, 0, "Mute"), 3);
    EXPECT_EQ(lanePidOf(engine, 0, "FxTone"), 300);
    EXPECT_EQ(lanePidOf(engine, 0, "MidiFxLvl"), 1000);
    EXPECT_EQ(lanePidOf(engine, 0, "BusRoom"), 3000 + 1 * 8 + 0)
        << "stable busID pids are NEVER remapped";
}

// The splice and the lane fixup share the ONE "Remove send" transaction
// (same UndoManager, zero new units): a SINGLE undo restores the send AND
// every lane paramID — including re-creating the removed send's deleted lane.
// If the remap had opened its own transaction (or written outside `um`), one
// undo would come back with sends restored but lanes still shifted.
TEST(AutomationSendBusPids, RemoveSendUndoRestoresSendAndLanePidsTogether)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 1);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 2);
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendA", 2000));
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendB", 2001));
    ASSERT_TRUE(cmds.addAutomationLane(0, "SendC", 2002));
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    EXPECT_EQ(engine.getReadModel().getTrackSends(0).size(), 2u);
    EXPECT_EQ(lanePidOf(engine, 0, "SendA"), -999);
    EXPECT_EQ(lanePidOf(engine, 0, "SendB"), 2000);
    EXPECT_EQ(lanePidOf(engine, 0, "SendC"), 2001);

    ASSERT_TRUE(cmds.canUndo());
    cmds.undo();   // ONE step, no second undo

    EXPECT_EQ(engine.getReadModel().getTrackSends(0).size(), 3u)
        << "the removed send must be back after the single undo";
    EXPECT_EQ(lanePidOf(engine, 0, "SendA"), 2000) << "deleted lane restored";
    EXPECT_EQ(lanePidOf(engine, 0, "SendB"), 2001);
    EXPECT_EQ(lanePidOf(engine, 0, "SendC"), 2002);
}

// ── removeSend LFO-target remap (the SAME positional pid space) ─────────────
// MODULATION children carry their destination in IDs::targetParamID, decoded by
// the SAME chain as lane paramIDs (Track.cpp: automation record, lane apply,
// per-sample modulation apply), so an LFO aimed at 2000+sendIndex goes stale on
// the SEND splice exactly like a lane would — and a send re-created at the freed
// index inherits it. The fixup deliberately DIFFERS from the lane rule: the
// removed send's LFO is KEPT and parked at targetParamID -1 (inert — Track.cpp
// skips pid <= 0 when collecting modulation sources), because its
// waveform/rate/depth are hand-configured state a delete would destroy, whereas
// a lane (whose identity IS its pid) is deleted because that is lossless.
// Gate 1/10: parked/shifted targets are proved on the LIVE ModulationManager
// through a full rebuild, never the ReadModel.

// Three sends with LFOs on 2000/2001/2002: removing send 0 must leave the
// removed send's LFO PRESENT but inert (-1, settings intact) and decrement the
// survivors' targets to 2000/2001 — on the tree AND on the live sources.
TEST(AutomationSendBusPids, RemoveSendInertsRemovedLfoAndShiftsSurvivors)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 1);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 2);
    engine.drainPendingRoutingRebuild();

    for (int i = 0; i < 3; ++i)
        cmds.addLfo(0);
    ASSERT_EQ(modulationChildCountOf(engine, 0), 3);
    cmds.setLfoParam(0, 0, "targetParamID", 2000.0);
    cmds.setLfoParam(0, 1, "targetParamID", 2001.0);
    cmds.setLfoParam(0, 2, "targetParamID", 2002.0);
    // Hand-configured settings on the removed send's LFO: parking it must keep
    // every one of them.
    cmds.setLfoParam(0, 0, "waveform", 2.0);
    cmds.setLfoParam(0, 0, "depth", 0.42);
    cmds.setLfoParam(0, 0, "rate", 0.5);
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;

    EXPECT_EQ(modulationChildCountOf(engine, 0), 3) << "no LFO may be deleted";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 0), -1)
        << "the removed send's LFO is parked, not deleted";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 1), 2000)
        << "survivor LFO rides reindexed send 0";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 2), 2001)
        << "survivor LFO rides reindexed send 1";
    EXPECT_DOUBLE_EQ(lfoPropOf(engine, 0, 0, IDs::waveform), 2.0);
    EXPECT_DOUBLE_EQ(lfoPropOf(engine, 0, 0, IDs::depth), 0.42);
    EXPECT_DOUBLE_EQ(lfoPropOf(engine, 0, 0, IDs::rate), 0.5);

    // Gate 1/10: the same targets must be on the LIVE ModulationManager after a
    // full rebuild — a tree-only fixup that never reached the processor would
    // leave the stale source running.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getNumModulations(), 3);
    EXPECT_EQ(track->getModulationSourceParamID(0), -1);
    EXPECT_EQ(track->getModulationSourceParamID(1), 2000);
    EXPECT_EQ(track->getModulationSourceParamID(2), 2001);
}

// The parked LFO must not hand the freed send slot to whoever takes it next:
// re-create a send at index 0 with a zero level and PROCESS THE BLOCK — the
// live SendProcessor's level must not move. Asserted on the LIVE processor (not
// merely targetParamID == -1) because that is the observable the bug produced:
// pre-fix the stale 2000 drove the NEW send's level on every sample. A control
// then re-arms the SAME LFO onto that pid and proves the harness does see the
// movement, so the inert assertion is not vacuous.
TEST(AutomationSendBusPids, RemoveSendParkedLfoDoesNotDriveRecreatedSend)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    cmds.addLfo(0);
    cmds.setLfoParam(0, 0, "targetParamID", 2000.0);
    cmds.setLfoParam(0, 0, "depth", 0.4);
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    ASSERT_EQ(lfoTargetPidOf(engine, 0, 0), -1);

    // The freed index 0 is refilled: a stale LFO would drive this new send.
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* live = rm->getSend(0, 0);
    ASSERT_NE(live, nullptr);

    FixedPlayHead ph;   // bpm 120 + rate 1 (1 cycle/beat), so the LFO advances
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    ph.setTimeSeconds(0.0);
    EXPECT_FLOAT_EQ(live->getSendLevel(), 0.0f);
    track->processBlock(buf, midi);
    EXPECT_FLOAT_EQ(live->getSendLevel(), 0.0f)
        << "a parked LFO (targetParamID -1) is skipped when modulation sources "
           "are collected, so it cannot move any send";

    // Control: the SAME LFO re-armed onto the re-created send's pid does move it
    // through the identical harness.
    cmds.setLfoParam(0, 0, "targetParamID", 2000.0);
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();
    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    live = rm->getSend(0, 0);
    ASSERT_NE(live, nullptr);
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> buf2(2, kBlock);
    juce::MidiBuffer midi2;
    track->processBlock(buf2, midi2);
    EXPECT_GT(live->getSendLevel(), 0.0f)
        << "control: an armed LFO moves the same send's level";
}

// The LFO fixup touches ONLY the send range, exactly like the lane fixup: an
// LFO on any other pid range — mixer 1/3, track FX, MIDI FX, stable
// 3000+busID*8 — comes through a removal with its EXACT targetParamID (Gate 9),
// while the send window shifts and its bottom is parked.
TEST(AutomationSendBusPids, RemoveSendRemapsOnlySendRangeForLfoTargets)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    const auto send = cmds.createSend(0, 1, 0.0f, false);   // one send: index 0
    ASSERT_TRUE(send.ok) << send.error;
    ASSERT_EQ(send.sendIndex, 0);
    engine.drainPendingRoutingRebuild();

    for (int i = 0; i < 7; ++i)
        cmds.addLfo(0);
    ASSERT_EQ(modulationChildCountOf(engine, 0), 7);
    cmds.setLfoParam(0, 0, "targetParamID", 2000.0);             // removed -> -1
    cmds.setLfoParam(0, 1, "targetParamID", 2999.0);             // top of range -> 2998
    cmds.setLfoParam(0, 2, "targetParamID", 1.0);                // volume
    cmds.setLfoParam(0, 3, "targetParamID", 3.0);                // mute
    cmds.setLfoParam(0, 4, "targetParamID", 300.0);              // track FX compound
    cmds.setLfoParam(0, 5, "targetParamID", 1000.0);             // MIDI FX compound
    cmds.setLfoParam(0, 6, "targetParamID", 3000.0 + 1 * 8 + 0); // bus 1 (Reverb) room
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    engine.drainPendingRoutingRebuild();

    EXPECT_EQ(modulationChildCountOf(engine, 0), 7) << "every LFO survives the removal";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 0), -1);
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 1), 2998)
        << "the top of the send range shifts WITH the survivor window, never "
           "parked, never pushed across 3000";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 2), 1) << "mixer pids are never remapped";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 3), 3);
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 4), 300) << "track FX pids are never remapped";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 5), 1000) << "MIDI FX pids are never remapped";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 6), 3000 + 1 * 8 + 0)
        << "stable busID pids are never remapped";
}

// The splice, the lane fixup and the LFO fixup share the ONE "Remove send"
// transaction: a SINGLE undo restores the send AND every LFO targetParamID —
// including the parked -1 going back to 2000. A second undo must not be needed
// (that would mean the LFO writes opened their own unit, or wrote outside `um`).
TEST(AutomationSendBusPids, RemoveSendUndoRestoresSendAndLfoTargetsTogether)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(0, 1, 0.0f, false).sendIndex, 1);
    for (int i = 0; i < 2; ++i)
        cmds.addLfo(0);
    cmds.setLfoParam(0, 0, "targetParamID", 2000.0);
    cmds.setLfoParam(0, 1, "targetParamID", 2001.0);
    engine.drainPendingRoutingRebuild();

    std::string error;
    ASSERT_TRUE(cmds.removeSend(0, 0, error)) << error;
    EXPECT_EQ(engine.getReadModel().getTrackSends(0).size(), 1u);
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 0), -1);
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 1), 2000);

    ASSERT_TRUE(cmds.canUndo());
    cmds.undo();   // ONE step, no second undo

    EXPECT_EQ(engine.getReadModel().getTrackSends(0).size(), 2u)
        << "the removed send must be back after the single undo";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 0), 2000) << "the parked LFO target is restored";
    EXPECT_EQ(lfoTargetPidOf(engine, 0, 1), 2001) << "the shifted LFO target is restored";
}

// ── Retired FM pid trap (300..308) ─────────────────────────────────────────
// Track.cpp used to carry a legacy "FM modulation" branch for pids 300..308
// (the deleted FM pid table in ModulationManager.h) that wrote a psy_fm
// slot's mod-source pool — feedbackOffset for 306, modWheelValue for 300..305.
// That branch was UNREACHABLE: the >=100 audio-FX compound is tested FIRST and
// claims 300..308 (si = (pid-100)/100, pi = (pid-100)%100), so 306 has always
// meant "audio fx slot 2, param 6" — never OP6Feedback. The branch is now
// deleted (2026-09-23, docs/adr-automation-model.md); this test pins the
// surviving decode on the LIVE processors: an LFO with targetParamID 306 must
// drive fxChain[2] param 6 — here a psy_fm slot's Feedback BASE param, the
// address the pid actually names — and must leave the mod-source pool's
// feedbackOffset/modWheelValue untouched (grep proves nothing else in src/
// writes either field, so a write here could only come from a revived trap).
TEST(AutomationPidRouting, LfoTarget306DrivesTrackFxSlot2Param6NotFmPool)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(seedTrack(engine), 0);

    cmds.addFxSlot(0, "psy_fm");  // slot 0 — the FIRST psy_fm slot: a revived
                                  // trap (which scanned the chain for the first
                                  // psy_fm) would write THIS slot's pool
    cmds.addFxSlot(0, "filter");  // slot 1 — must stay untouched
    cmds.addFxSlot(0, "psy_fm");  // slot 2 — the slot pid 306 names (param 6)
    cmds.addLfo(0);
    // Unipolar square, depth 0.5, targeting 306. Kept DISABLED until after the
    // rebuild: rebuildRoutingGraph's scratch processBlock drive (lesson 21)
    // runs the modulation loop and an enabled LFO would pre-saturate the target.
    cmds.setLfoParam(0, 0, "enabled", 0.0);
    cmds.setLfoParam(0, 0, "waveform", 3.0);
    cmds.setLfoParam(0, 0, "depth", 0.5);
    cmds.setLfoParam(0, 0, "targetParamID", 306.0);

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 3);
    auto* slot0 = track->getFXChain().at(0).get();
    auto* slot1 = track->getFXChain().at(1).get();
    auto* slot2 = track->getFXChain().at(2).get();
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);
    ASSERT_EQ(slot0->getType().toStdString(), "psy_fm");
    ASSERT_EQ(slot1->getType().toStdString(), "filter");
    ASSERT_EQ(slot2->getType().toStdString(), "psy_fm");
    auto* psyFm0 = slot0->psyFmEngine();
    auto* psyFm2 = slot2->psyFmEngine();
    ASSERT_NE(psyFm0, nullptr);
    ASSERT_NE(psyFm2, nullptr);

    // Live modulation source carries the pid verbatim (Track probe, not the
    // ValueTree) — the same decode the retired branch keyed off.
    ASSERT_EQ(track->getNumModulations(), 1);
    EXPECT_EQ(track->getModulationSourceParamID(0), 306)
        << "rebuilt LFO source must carry targetParamID 306 verbatim";

    // Baselines AFTER the rebuild, BEFORE the LFO runs: psy_fm param 6
    // (Feedback, def [0,1] default 0.0) and the other slots' param 0 (a
    // non-target address on each — slot 0 OP1 Ratio, slot 1 Cutoff).
    const float base = slot2->getAutomationParam(6);
    const float slot0Base = slot0->getAutomationParam(0);
    const float slot1Base = slot1->getAutomationParam(0);
    auto& pool0 = psyFm0->getModSourcePool();
    auto& pool2 = psyFm2->getModSourcePool();

    // Enable the LFO: tree write -> MODULATION listener -> rebuildModulation
    // re-reads enabled=true onto the live source.
    cmds.setLfoParam(0, 0, "enabled", 1.0);
    ASSERT_EQ(track->getModulationSourceParamID(0), 306);

    FixedPlayHead ph;
    ph.setTimeSeconds(0.0);
    track->setPlayHead(&ph);
    juce::AudioBuffer<float> silent(2, kBlock);
    juce::MidiBuffer midi;
    track->processBlock(silent, midi);

    const float after = slot2->getAutomationParam(6);

    // 306 = 100 + slot 2 * 100 + param 6: the track-FX compound claims the pid
    // first, so the LFO drives that slot's param 6 (the unipolar square pushes
    // the normalized [0,1] param up).
    EXPECT_GT(after, base) << "LFO target 306 must modulate live fxChain[2] param 6";
    EXPECT_NE(after, base);
    // Only the named address moved: no other slot's param did.
    EXPECT_FLOAT_EQ(slot0->getAutomationParam(0), slot0Base)
        << "pid 306 names slot 2, not slot 0";
    EXPECT_FLOAT_EQ(slot1->getAutomationParam(0), slot1Base)
        << "pid 306 names slot 2, not slot 1";
    // ... and it did NOT reach ANY psy_fm mod-source pool: the retired branch
    // wrote feedbackOffset (306) / modWheelValue (300..305) on the first psy_fm
    // slot it found, and neither field has a writer left in src/.
    EXPECT_FLOAT_EQ(pool0.feedbackOffset, 0.0f)
        << "the first psy_fm slot's pool must stay untouched by a track-level LFO";
    EXPECT_FLOAT_EQ(pool2.feedbackOffset, 0.0f)
        << "pid 306 is the track-FX compound address (slot 2 param 6), not the "
           "retired OP6Feedback target";
    EXPECT_FLOAT_EQ(pool2.modWheelValue, 0.0f)
        << "the retired ratio-target pool write (300..305) must stay gone";
}
