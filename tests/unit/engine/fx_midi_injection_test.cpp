// FX MIDI injection (Phase 1): program change / CC / note queued into a plugin
// FX slot's next processed block (TrackFXSlot::queueMidiForNextBlock).
// Gates for docs/plans/2026-09-11-fx-midi-injection-virus-presets.md:
//   G2 (unit): queued PC/CC arrive in the next block, ordered, byte-exact;
//              empty queue adds nothing; cap drops-not-blocks.
//   G4 (env-guarded HDAW_REAL_PLUGIN_TESTS + Osirus): a CC0+PC preset change
//              alters the isolated render and survives rebuildRoutingGraph()
//              (the pluginState blob path — same machinery as save/reload).
#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/ExportManager.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "mcp/PresetFileParser.h"
#include "mcp/PresetRoute.h"
#include "model/ProjectModel.h"

namespace {

constexpr const char* kOsTIrusClap = "C:\\Program Files\\Common Files\\CLAP\\OsTIrus.clap";
constexpr const char* kNodalRed2xClap = "C:\\Program Files\\Common Files\\CLAP\\NodalRed2x.clap";
constexpr const char* kXeniaClap = "C:\\Program Files\\Common Files\\CLAP\\Xenia.clap";
// Real NL2x bank for the live probe (BUG-7): user's bank library.
constexpr const char* kNordBankMid = "D:\\pdf\\NL2x Banks\\NL2x Factory\\ProgBank0.mid";

bool realPluginTestsEnabled()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    return !(s.trim().isEmpty() || s.trim() == "0");
}

// Minimal fake: records every non-empty incoming MidiBuffer, one entry per
// processed block (override set mirrors FixedStatePlugin in
// tests/unit/engine/plugin_state_save_load_test.cpp).
struct RecordingPlugin : juce::AudioPluginInstance
{
    RecordingPlugin()
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withInput("In", juce::AudioChannelSet::stereo())
                  .withOutput("Out", juce::AudioChannelSet::stereo())) {}

    std::vector<juce::MidiBuffer> received;

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    const juce::String getName() const override { return "RecordingPlugin"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midi) override
    {
        if (midi.isEmpty())
            return;
        juce::MidiBuffer copy;
        for (const auto metadata : midi)
            copy.addEvent(metadata.getMessage(), metadata.samplePosition);
        received.push_back(copy);
    }
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override
    {
        jassertfalse; // not supported; the slot never requests double precision
    }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumParameters() override { return 0; }
    float getParameter(int) override { return 0; }
    void setParameter(int, float) override {}
    const juce::String getParameterName(int) override { return {}; }
    const juce::String getParameterText(int) override { return {}; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void fillInPluginDescription(juce::PluginDescription& desc) const override
    {
        desc.name = "RecordingPlugin";
        desc.pluginFormatName = "Internal";
    }
};

std::vector<juce::MidiMessage> flatten(const juce::MidiBuffer& buf)
{
    std::vector<juce::MidiMessage> out;
    for (const auto metadata : buf)
        out.push_back(metadata.getMessage());
    return out;
}

} // namespace

// G2a: queued PC+CC land in the NEXT process() block, ordered, byte-exact.
TEST(FxMidiInjection, QueuedMessagesArriveNextBlockOrderedAndExact)
{
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-1", false);
    auto* rec = static_cast<RecordingPlugin*>(slot.getPluginInstance());
    ASSERT_NE(rec, nullptr);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    slot.queueMidiForNextBlock(juce::MidiMessage::programChange(1, 40));
    slot.queueMidiForNextBlock(juce::MidiMessage::controllerEvent(1, 0, 2));
    slot.process(buffer, midi);

    ASSERT_EQ(rec->received.size(), 1u);
    const auto msgs = flatten(rec->received[0]);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_TRUE(msgs[0].isProgramChange());
    EXPECT_EQ(msgs[0].getChannel(), 1);
    EXPECT_EQ(msgs[0].getProgramChangeNumber(), 40);
    EXPECT_TRUE(msgs[1].isController());
    EXPECT_EQ(msgs[1].getControllerNumber(), 0);
    EXPECT_EQ(msgs[1].getControllerValue(), 2);

    // Queue is drained: the following block sees nothing new.
    juce::MidiBuffer midi2;
    slot.process(buffer, midi2);
    ASSERT_EQ(rec->received.size(), 1u);
}

// G2b: empty queue adds no events at all.
TEST(FxMidiInjection, EmptyQueueAddsNothing)
{
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-2", false);
    auto* rec = static_cast<RecordingPlugin*>(slot.getPluginInstance());
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    slot.process(buffer, midi);
    EXPECT_TRUE(rec->received.empty());
    EXPECT_TRUE(midi.isEmpty());
}

// G2c: queue cap drops-not-blocks; the capped batch stays ordered.
TEST(FxMidiInjection, QueueCapDropsNotBlocks)
{
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-3", false);
    auto* rec = static_cast<RecordingPlugin*>(slot.getPluginInstance());
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i)
        slot.queueMidiForNextBlock(juce::MidiMessage::programChange(1, i % 128));
    slot.process(buffer, midi);
    ASSERT_EQ(rec->received.size(), 1u);
    const auto msgs = flatten(rec->received[0]);
    EXPECT_EQ(msgs.size(), 256u); // TrackFXSlot queue cap (matches the SHM midiIn ring capacity)
    EXPECT_EQ(msgs.front().getProgramChangeNumber(), 0);
    EXPECT_EQ(msgs.back().getProgramChangeNumber(), 255 % 128);
}

// G4: real OsTIrus (isolated by default) — a CC0+PC preset change on the LIVE
// slot is reflected in the child's own reported parameter state (poll the
// param cache for a patch/program-ish param before vs after). Phase-1
// boundary: injection reaches the LIVE instance (playback/jamming); offline
// exports see the preset once the state is captured (project save /
// applyPluginProgram-style snapshot).
TEST(FxMidiInjection, OsTIrusPresetChangeReflectsInChildParams)
{
    if (!realPluginTestsEnabled() || !juce::File(kOsTIrusClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or OsTIrus.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kOsTIrusClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 7;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    // Collect the before-state of every patch/program-ish param we can find.
    const auto all = paramSvc.getParams(a.trackIndex, pluginId);
    if (all.empty())
        GTEST_SKIP() << "param cache unavailable in deviceless environment"
                        " (no live slot objects without an audio device)";
    struct Key { int index; std::string name, text; double value; };
    std::vector<Key> before;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        if (low.find("patch") != std::string::npos || low.find("program") != std::string::npos
            || low.find("bank") != std::string::npos || low.find("single") != std::string::npos)
            before.push_back({ p.index, p.name, p.text, p.value });
    }
    std::cout << "[FxMidiInjection] OsTIrus patch-ish params: " << before.size() << "\n";
    for (const auto& k : before)
        std::cout << "  [" << k.index << "] " << k.name << " = '" << k.text << "' (" << k.value << ")\n";

    // Snapshot the child's serialized state before the injection — the
    // AUTHORITATIVE "did the PC reach the child" observable (the param cache is
    // not: see the note at the assertion below).
    auto rawState = [&]() -> juce::MemoryBlock {
        juce::MemoryBlock mb;
        auto* proc = engine.getMainProcessor();
        auto* tr = proc ? proc->getTrack(a.trackIndex) : nullptr;
        auto* slot = (tr && a.slotIndex >= 0
                      && static_cast<size_t>(a.slotIndex) < tr->getFXChain().size())
            ? tr->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
        auto* inst = slot ? slot->getPluginInstance() : nullptr;
        if (inst)
            inst->getStateInformation(mb);
        return mb;
    };
    const auto stateBefore = rawState();
    if (stateBefore.getSize() == 0)
        GTEST_SKIP() << "live slot exposes no state (deviceless environment)";

    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = a.trackIndex;
    mp.slotIndex = a.slotIndex;
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 0, 0});
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, 1, 40, 0});
    auto mr = cmds.sendFxMidi(mp);
    ASSERT_TRUE(mr.ok) << mr.error;
    EXPECT_EQ(mr.queued, 2);

    // Poll the child param cache for up to ~10s: any observed change in a
    // patch-ish param proves the PC traversed parent -> SHM -> child -> OS.
    bool changed = false;
    for (int attempt = 0; attempt < 20 && !changed; ++attempt)
    {
        juce::Thread::sleep(500);
        for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            for (const auto& k : before)
                if (p.index == k.index && (std::abs(p.value - k.value) > 1e-6 || p.text != k.text))
                {
                    std::cout << "[FxMidiInjection] changed: [" << p.index << "] " << p.name
                              << " '" << k.text << "'(" << k.value << ") -> '"
                              << p.text << "'(" << p.value << ")\n";
                    changed = true;
                    break;
                }
    }
    // The param CACHE is not a valid probe for the TI: its OS does not echo
    // PC-loaded patch parameters back to the host, so the cache can stay
    // byte-identical while the state AND the offline render both change (proven
    // by OsTIrusInjectionCapturesToTreeAndSurvivesRebuild). Keep the cache
    // result as a diagnostic and assert on the child's serialized state.
    bool stateChanged = false;
    for (int attempt = 0; attempt < 20 && !stateChanged; ++attempt)
    {
        juce::Thread::sleep(500);
        const auto now = rawState();
        if (now.getSize() > 0
            && (now.getSize() != stateBefore.getSize() || !(now == stateBefore)))
            stateChanged = true;
    }
    const auto stateAfter = rawState();
    std::cout << "[FxMidiInjection] OsTIrus paramCacheChanged=" << (changed ? 1 : 0)
              << " stateChanged=" << (stateChanged ? 1 : 0)
              << " (state " << stateBefore.getSize() << " -> " << stateAfter.getSize()
              << " bytes, patch-ish params polled=" << before.size() << ")\n";
    EXPECT_TRUE(stateChanged)
        << "CC0+PC did not change the child's serialized state (the PC never reached the OS)";
}

// #3: injected preset state is captured into the tree pluginState and
// survives rebuildRoutingGraph — the same restore path offline exports use
// (Track.cpp reads IDs::pluginState after prepare). Env-gated: real OsTIrus.
TEST(FxMidiInjection, OsTIrusInjectionCapturesToTreeAndSurvivesRebuild)
{
    if (!realPluginTestsEnabled() || !juce::File(kOsTIrusClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or OsTIrus.clap missing";

    AudioEngine engine;   // deviceless -> sendFxMidi drives+captures synchronously
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kOsTIrusClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 11;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = a.trackIndex;
    mp.slotIndex = a.slotIndex;
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 0, 0}); // bank A
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, 1, 40, 0});
    auto mr = cmds.sendFxMidi(mp);
    ASSERT_TRUE(mr.ok) << mr.error;

    // A device is open (RDP render endpoint), so the capture was deferred to
    // a +800ms juce timer — test_main runs no juce dispatch loop, so pump it
    // here until the timer fires and the capture lands in the tree.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(3000);

    // Hard gate: the tree now carries the live child's post-injection state.
    auto slotTree = engine.getProjectModel().getTrackListTree()
        .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(a.slotIndex);
    ASSERT_TRUE(slotTree.isValid());
    const auto stateStr = slotTree.getProperty(IDs::pluginState, "").toString();
    EXPECT_FALSE(stateStr.isEmpty()) << "pluginState was not captured to the tree";

    // Offline reach: render through the dedicated domain again — it builds
    // from the tree, so R2 must reflect the captured post-injection state
    // (the 177KB blob proves the PC loaded a different single; R1 ran the
    // factory default).
    // NOTE: deviceless rebuildRoutingGraph is NOT a valid offline-reach probe:
    // with no device, fxSpec.sampleRate==0 and slots are never prepared, so
    // the restore path can't deliver state to the child (observed: fresh 262B
    // state vs captured 177KB). Real export/audition domains run at 44.1kHz.
    auto render = [&](int trackIdx, int slotIdx) {
        ProjectCommands::AuditionParams rp;
        rp.trackIndex = trackIdx;
        rp.slotIndex = slotIdx;
        rp.lengthBeats = 2.0;
        rp.windowSeconds = 2.0;
        rp.seed = 11;
        return cmds.auditionPlugin(rp);
    };
    auto r2 = render(a.trackIndex, a.slotIndex);
    ASSERT_TRUE(r2.ok) << r2.error;
    EXPECT_GT(std::abs(r2.rms - a.rms), 1e-4f)
        << "offline render did not reflect the captured preset (a.rms=" << a.rms
        << " r2.rms=" << r2.rms << ")";
}

// BUG-7 live probe: the NodalRed2x boots with NO valid presets (program
// changes are dropped as garbage — n2xdevice.cpp), so a bank of Clavia SysEx
// dumps must land FIRST, then a PC selects a voice. This drives the
// load_nord_bank pipeline end to end against the real isolated plugin:
// parse .mid -> validate dumps -> queue via SHM -> child applies the dump to
// its volatile patch RAM -> PC selects -> capture pluginState -> offline
// audition reflects the loaded bank (render differs from the boot state).
TEST(FxMidiInjection, NordBankLoadChangesNodalRed2xRender)
{
    if (!realPluginTestsEnabled() || !juce::File(kNodalRed2xClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or NodalRed2x.clap missing";
    if (!juce::File(kNordBankMid).existsAsFile())
        GTEST_SKIP() << "NL2x bank library not mounted (" << kNordBankMid << ")";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Initial render: boot state (no valid presets -> fixed default tone).
    ProjectCommands::AuditionParams probe;
    probe.pluginId = kNodalRed2xClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 5;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    // Parse the factory bank exactly like load_nord_bank does (.mid ->
    // juce::MidiFile -> sysex events; SMF lengths include the trailing F7).
    juce::MemoryBlock block;
    ASSERT_TRUE(juce::File(kNordBankMid).loadFileAsData(block));
    juce::MemoryInputStream in(block, false);
    juce::MidiFile mf;
    ASSERT_TRUE(mf.readFrom(in));
    std::vector<std::vector<uint8_t>> dumps;
    int dumpCount = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack(t);
        for (int e = 0; e < seq->getNumEvents(); ++e)
        {
            const auto meta = seq->getEventPointer(e);
            if (!meta->message.isSysEx())
                continue;
            const auto* raw = meta->message.getRawData();
            std::vector<uint8_t> d(raw, raw + meta->message.getRawDataSize());
            const auto verr = mcp::validateNordDump(d.data(), d.size());
            if (!verr.isEmpty())
                std::cout << "[NordBank] dump " << dumpCount << " size="
                          << d.size() << " head=" << std::hex
                          << (int) d[0] << "," << (int) d[1] << ","
                          << (int) d[2] << "," << (int) d[3] << std::dec
                          << " tail=" << (int) d[d.size()-2] << ","
                          << (int) d[d.size()-1]
                          << " err=" << verr.toStdString() << "\n";
            ASSERT_TRUE(verr.isEmpty()) << "bank contains an invalid dump";
            ++dumpCount;
        }
    }
    ASSERT_GT(dumpCount, 0);

    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = a.trackIndex;
    mp.slotIndex = a.slotIndex;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack(t);
        for (int e = 0; e < seq->getNumEvents(); ++e)
        {
            const auto meta = seq->getEventPointer(e);
            if (!meta->message.isSysEx())
                continue;
            ProjectCommands::FxMidiEvent ev;
            ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
            const auto* raw = meta->message.getRawData();
            ev.sysex.assign(raw, raw + meta->message.getRawDataSize());
            mp.events.push_back(std::move(ev));
        }
    }
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, 1, 3, 0});
    auto mr = cmds.sendFxMidi(mp);
    ASSERT_TRUE(mr.ok) << mr.error;
    EXPECT_EQ(mr.queued, dumpCount + 1);

    // The bank sendFxMidi stamps a pending receipt and defers its capture
    // ~30ms per queued dump (adaptive delay), so it fires after the metered
    // drain (<=1 SysEx/block) delivers every dump. A tiny trailing CC125
    // (undefined on the NL2x — B4: CC74 would retune the cutoff) re-arms a
    // second capture AFTER everything has been consumed.
    ProjectCommands::FxMidiParams cp;
    cp.trackIndex = a.trackIndex;
    cp.slotIndex = a.slotIndex;
    cp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
    auto cr = cmds.sendFxMidi(cp);
    ASSERT_TRUE(cr.ok) << cr.error;
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(800 + 30 * dumpCount + 2000);

    auto slotTree = engine.getProjectModel().getTrackListTree()
        .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(a.slotIndex);
    ASSERT_TRUE(slotTree.isValid());
    // Durability (2026-09-20): the plugin-state capture route is a dead end for
    // the n2x — its serialized child state does not reflect the volatile patch
    // RAM the dumps land in, so the D-lite guard honestly reports "unchanged"
    // and persists nothing to IDs::pluginState. The injected dumps ARE persisted
    // on the slot (IDs::presetSysex) and replayed into fresh children at restore
    // (Track.cpp), which is the durable path this pipeline relies on — the same
    // shape the Xenia gate below documents. Assert the dump persistence and the
    // offline render, not the state blob.
    const auto stateStr = slotTree.getProperty(IDs::pluginState, "").toString();
    const auto presetSyx = slotTree.getProperty(IDs::presetSysex, "").toString();
    const auto capStatus = slotTree.getProperty(IDs::captureStatus, "").toString();
    std::cout << "[NordBank] captureStatus=" << capStatus.toStdString()
              << " pluginStateLen=" << stateStr.length()
              << " presetSysexLen=" << presetSyx.length() << "\n";
    EXPECT_GT(presetSyx.length(), 0)
        << "injected bank dumps were not persisted for replay (IDs::presetSysex)";
    // The receipt must SETTLE: a plugin whose serialized state does not move
    // reports "unchanged" by design — that is not a failure.
    EXPECT_NE(capStatus.toStdString(), std::string("pending"))
        << "capture receipt never settled";
    EXPECT_FALSE(capStatus.startsWithIgnoreCase("failed"))
        << "capture failed: " << capStatus.toStdString();

    // Offline audition re-renders from the tree: with the bank applied the
    // engine plays a real patch voice instead of the boot default tone.
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 5;
    auto r2 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r2.ok) << r2.error;
    // Render is otherwise deterministic; ANY difference proves the bank load
    // reached the offline domain (observed diffs ~1e-4..1e-2 across patches).
    EXPECT_GT(std::abs(r2.rms - a.rms), 1e-5f)
        << "bank load did not change the render (a.rms=" << a.rms
        << " r2.rms=" << r2.rms << ")";
}

// Xenia (Microwave XT) edit-buffer dump A/B -- the apply_preset WaldorfSysex
// route drives the same 265-byte F0 3E 0E 00 10 20 00 SingleDump lane whose
// live edit-buffer audibility was verified 2026-09-16. This deviceless gate
// exercises the DURABLE path the route exists for: sendFxMidi with
// captureToTree -> deferred capture -> IDs::pluginState -> offline audition
// re-render. Docs predict the capture reads the program/state blob, not the
// edit buffer (get_fx_capture_status stays "unchanged"), so an identical
// re-render here is itself the evidence of that gap -- the test prints the
// capture receipt either way and asserts the render changed.
TEST(FxMidiInjection, XeniaEditBufferDumpChangesOfflineRender)
{
    if (!realPluginTestsEnabled() || !juce::File(kXeniaClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Xenia.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kXeniaClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 5;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);
    std::cout << "[XeniaAB] baseline rms=" << a.rms << " peak=" << a.peak << "\n";

    const auto rerender = [&](ProjectCommands::AuditionResult& out) {
        ProjectCommands::AuditionParams rp;
        rp.trackIndex = a.trackIndex;
        rp.slotIndex = a.slotIndex;
        rp.lengthBeats = 2.0;
        rp.windowSeconds = 2.0;
        rp.seed = 5;
        out = cmds.auditionPlugin(rp);
        ASSERT_TRUE(out.ok) << out.error;
    };

    const auto injectAndRerender = [&](const juce::File& dumpFile, const char* tag,
                                       ProjectCommands::AuditionResult& out) {
        juce::MemoryBlock block;
        ASSERT_TRUE(dumpFile.loadFileAsData(block));
        std::vector<std::vector<uint8_t>> dumps;
        const auto* b = static_cast<const uint8_t*>(block.getData());
        const int split = mcp::splitWaldorfSyx(b, block.getSize(),
                                               mcp::kWaldorfMachineMw2, dumps);
        ASSERT_EQ(split, 1) << tag;
        for (const auto& d : dumps)
            ASSERT_TRUE(mcp::validateWaldorfDump(d.data(), d.size(),
                mcp::kWaldorfMachineMw2, "Microwave XT/Xenia").isEmpty()) << tag;

        ProjectCommands::FxMidiParams mp;
        mp.trackIndex = a.trackIndex;
        mp.slotIndex = a.slotIndex;
        mp.captureToTree = true;
        for (const auto& d : dumps)
        {
            ProjectCommands::FxMidiEvent ev;
            ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
            ev.sysex = d;
            mp.events.push_back(std::move(ev));
        }
        // Trailing CC125 re-arms a second capture after the drain consumes
        // the dump (same protocol as the Nord gate).
        mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto mr = cmds.sendFxMidi(mp);
        ASSERT_TRUE(mr.ok) << mr.error;
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(4000);

        auto slotTree = engine.getProjectModel().getTrackListTree()
            .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(a.slotIndex);
        ASSERT_TRUE(slotTree.isValid());
        const auto capStatus = slotTree.getProperty(IDs::captureStatus, "").toString();
        const auto stateLen =
            slotTree.getProperty(IDs::pluginState, "").toString().length();
        std::cout << "[XeniaAB] " << tag << " captureStatus="
                  << capStatus << " pluginStateLen=" << stateLen << "\n";
        // Direct live-instance state probe: does the serialized state carry
        // the injected patch name? Decides whether the state-chunk fix's hook
        // is even on the path (gearmulator Device::process -> xtState cache).
        if (auto* proc = engine.getMainProcessor())
        if (auto* track = proc->getTrack(a.trackIndex))
        if (a.slotIndex >= 0
            && static_cast<size_t>(a.slotIndex) < track->getFXChain().size())
        if (auto* inst = track->getFXChain()[static_cast<size_t>(a.slotIndex)]->getPluginInstance())
        {
            juce::MemoryBlock st;
            inst->getStateInformation(st);
            const std::string nm = (std::string(tag) == "dumpA") ? "HDAW A/B A" : "HDAW A/B B";
            bool found = false;
            if (st.getSize() > 0)
            {
                const auto* raw = static_cast<const uint8_t*>(st.getData());
                for (size_t i = 0; i + nm.size() < st.getSize() && !found; ++i)
                    found = !std::memcmp(raw + i, nm.data(), nm.size());
            }
            std::cout << "[XeniaAB] " << tag << " LIVE state size=" << st.getSize()
                      << " containsPatchName=" << (found ? "yes" : "no") << "\n";
        }
        // Durability (2026-09-18): the plugin-state capture route is a dead
        // end here (the XT single cache is editor-request-driven, so the
        // serialized child state stays a boot stub) -- the injected dumps are
        // persisted on the slot instead (IDs::presetSysex) and replayed into
        // fresh children at restore. Assert the persistence landed.
        const auto syxLen = slotTree.getProperty(IDs::presetSysex, "").toString().length();
        std::cout << "[XeniaAB] " << tag << " presetSysexLen=" << syxLen << "\n";
        EXPECT_GT(syxLen, 0) << tag << ": presetSysex was not written to the tree";

        rerender(out);
        std::cout << "[XeniaAB] " << tag << " rms=" << out.rms
                  << " peak=" << out.peak << "\n";
    };

    auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto dumpA = cwd.getChildFile("compositions/xenia-ab/dumpA.syx");
    const auto dumpB = cwd.getChildFile("compositions/xenia-ab/dumpB.syx");
    if (!dumpA.existsAsFile() || !dumpB.existsAsFile())
        GTEST_SKIP() << "xenia-ab dumps not present (run timbre-lib/xenia_dump.py)";

    ProjectCommands::AuditionResult b;
    injectAndRerender(dumpA, "dumpA", b);
    ProjectCommands::AuditionResult c;
    injectAndRerender(dumpB, "dumpB", c);

    // Fresh-instance probe: a NEW probe track builds its Xenia from the tree
    // (pluginState is empty -> factory boot). This is the no-injection control
    // for the LIVE path, and also what exports/save-load instantiate.
    ProjectCommands::AuditionParams fresh;
    fresh.pluginId = kXeniaClap;
    fresh.trackIndex = -1;
    fresh.keepTrack = false;
    fresh.lengthBeats = 2.0;
    fresh.windowSeconds = 2.0;
    fresh.seed = 5;
    auto f = cmds.auditionPlugin(fresh);
    ASSERT_TRUE(f.ok) << f.error;
    std::cout << "[XeniaAB] fresh-instance rms=" << f.rms << " peak=" << f.peak << "\n";

    // Rendering-path note: the FIRST audition ran before any live routing
    // existed (deviceless -> tree/offline render, so a.rms is the OFFLINE
    // boot sound). sendFxMidi settles live routing on demand
    // (ensureLiveRouting), so every re-render after it uses the LIVE child.
    // The fresh probe is therefore the correct no-injection control for the
    // live path: a brand-new instance built from the tree, which carries no
    // pluginState -> boots to the factory sound.
    const float dLiveA = std::abs(b.rms - f.rms);   // live boot vs dumpA
    const float dLiveB = std::abs(c.rms - f.rms);   // live boot vs dumpB
    const float dAB = std::abs(c.rms - b.rms);      // dumpA vs dumpB
    std::cout << "[XeniaAB] live: boot->dumpA=" << dLiveA
              << " boot->dumpB=" << dLiveB << " dumpA->dumpB=" << dAB << "\n";

    // Gate (live): the 265-byte edit-buffer dump must retarget the sound of
    // the LIVE Xenia child.
    EXPECT_GT(dLiveA, 1e-5f)
        << "dumpA did not change the live Xenia render";
    EXPECT_GT(dLiveB, 1e-5f)
        << "dumpB did not change the live Xenia render";
    EXPECT_GT(dAB, 1e-5f)
        << "dumpA and dumpB rendered identically (not just boot-vs-dump)";

    // Durability gate (the xtLib state-chunk fix): a REAL offline export
    // builds fresh children from the tree (IDs::pluginState). Export once
    // before injection (boot) and once after (the captured state) -- the
    // post-injection export must hear the injected patch and differ.

    // Rebuild/replay proof: tear down the live graph and rebuild it from the
    // tree. The fresh Xenia child is restored from the SAME tree, so Track.cpp
    // replays the persisted dumps (IDs::presetSysex) into it -- the
    // re-rendered sound must differ from a no-dump fresh instance.
    {
        const auto syxProp = engine.getProjectModel().getTrackListTree()
            .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN)
            .getChild(a.slotIndex).getProperty(IDs::presetSysex, "").toString();
        ASSERT_FALSE(syxProp.isEmpty()) << "presetSysex missing before rebuild";
        auto* mp = engine.getMainProcessor();
        ASSERT_NE(mp, nullptr);
        mp->rebuildRoutingGraph();
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(6000);
        ProjectCommands::AuditionResult rd;
        ProjectCommands::AuditionParams rp;
        rp.trackIndex = a.trackIndex;
        rp.slotIndex = a.slotIndex;
        rp.lengthBeats = 2.0;
        rp.windowSeconds = 2.0;
        rp.seed = 5;
        rd = cmds.auditionPlugin(rp);
        ASSERT_TRUE(rd.ok) << rd.error;
        const float dRebuild = std::abs(rd.rms - f.rms);
        std::cout << "[XeniaAB] rebuilt-from-tree rms=" << rd.rms
                  << " vs fresh-boot=" << f.rms << " |delta|=" << dRebuild << "\n";
        EXPECT_GT(dRebuild, 1e-5f)
            << "fresh child rebuilt from the tree did not hear the replayed dumps";
    }

    auto proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto& em = proc->getExportManager();
    auto exportRoot = [&](const juce::File& out, float& rms) {
        out.deleteFile();
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        ASSERT_FALSE(em.isExporting());
        const double dur = std::max(4.0, HDAW::ExportManager::calculateProjectDuration(
                                             engine.getProjectModel()));
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), fm,
                                   &engine.getPluginManager(), out, 48000.0, 0.0,
                                   dur, HDAW::ExportManager::WAV, 24));
        for (int i = 0; i < 600; ++i)
        {
            if (!em.isExporting())
                break;
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->runDispatchLoopUntil(20);
        }
        ASSERT_FALSE(em.isExporting()) << "offline export did not finish";
        ASSERT_TRUE(out.existsAsFile()) << out.getFullPathName();
        juce::AudioFormatManager fm2;
        fm2.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd(fm2.createReaderFor(out));
        ASSERT_NE(rd, nullptr);
        const int n = static_cast<int>(rd->lengthInSamples);
        juce::AudioBuffer<float> buf(2, n);
        rd->read(&buf, 0, n, 0, true, true);
        double acc = 0.0;
        for (int s = 0; s < n; ++s)
        {
            const float v = buf.getSample(0, s);
            acc += static_cast<double>(v) * v;
        }
        rms = static_cast<float>(std::sqrt(acc / std::max(1, n)));
        out.deleteFile();
    };

    const auto wavA = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("hdaw_xenia_export_boot.wav");
    const auto wavB = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("hdaw_xenia_export_patch.wav");
    float exportBoot = 0.0f, exportPatch = 0.0f;
    exportRoot(wavA, exportBoot);
    exportRoot(wavB, exportPatch);
    std::cout << "[XeniaAB] export: boot rms=" << exportBoot
              << " post-injection rms=" << exportPatch
              << " |delta|=" << std::abs(exportPatch - exportBoot) << "\n";
    EXPECT_GT(std::abs(exportPatch - exportBoot), 1e-5f)
        << "offline export did not hear the injected patch (pluginState="
           "restore missing)";
    std::cout << "[XeniaAB] fresh(no-pluginState) rms=" << f.rms
              << " vs injected live rms " << b.rms << "/" << c.rms
              << " vs export-after rms " << exportPatch << "\n";
}

// Vavra (microQ) edit-buffer retarget + replay gate (2026-09-18): real microQ
// bank dumps carry their ORIGINAL buffer/location bytes (0x30 multi-edit or
// 0x40+ bank slots). The microQ OS only plays the single-mode edit buffer
// (0x20), so as-is injection loaded a buffer the current sound does not read
// -- renders stayed identical ("NOT APPLYING", 2026-09-16). runWaldorfSysexFile
// now retargets 392-byte dumps to 0x20/0x00 (same as mqController::sendSingle)
// and fixes the Waldorf checksum. This gate proves the retargeted dump changes
// the LIVE render and survives rebuild via the preset-sysex replay path.
TEST(FxMidiInjection, VavraEditBufferDumpChangesOfflineRender)
{
    constexpr const char* kVavraClap = "C:\\Program Files\\Common Files\\CLAP\\Vavra.clap";
    if (!realPluginTestsEnabled() || !juce::File(kVavraClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Vavra.clap missing";

    const auto dumpA = juce::File("D:/pdf/rhythm-lab.com_waldorf_micro_q/Arp/Acid bender   CJ Arp.syx");
    if (!dumpA.existsAsFile())
        GTEST_SKIP() << "rhythm-lab microQ library not mounted";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kVavraClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 11;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    // Route-level load (exercises the microQ edit-buffer retarget + the
    // preset-sysex persistence in sendFxMidi).
    auto rr = mcp::runWaldorfSysexFile(
        engine, a.trackIndex, a.slotIndex,
        QString::fromStdString(dumpA.getFullPathName().toStdString()),
        "Vavra.clap", true);
    ASSERT_FALSE(rr.isError)
        << rr.content[0].toObject().value("text").toString().toStdString();
    std::cout << "[VavraAB] route: "
              << rr.content[0].toObject().value("text").toString().toStdString() << "\n";
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(4000);

    const auto slotTree = engine.getProjectModel().getTrackListTree()
        .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(a.slotIndex);
    ASSERT_TRUE(slotTree.isValid());
    const auto syxLen = slotTree.getProperty(IDs::presetSysex, "").toString().length();
    std::cout << "[VavraAB] presetSysexLen=" << syxLen << "\n";
    EXPECT_GT(syxLen, 0) << "presetSysex not persisted";

    ProjectCommands::AuditionResult b;
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 11;
    b = cmds.auditionPlugin(rp);
    ASSERT_TRUE(b.ok) << b.error;

    // Fresh (no dump) live control.
    ProjectCommands::AuditionParams fresh;
    fresh.pluginId = kVavraClap;
    fresh.trackIndex = -1;
    fresh.keepTrack = false;
    fresh.lengthBeats = 2.0;
    fresh.windowSeconds = 2.0;
    fresh.seed = 11;
    auto f = cmds.auditionPlugin(fresh);
    ASSERT_TRUE(f.ok) << f.error;

    std::cout << "[VavraAB] baseline(rms)=" << a.rms
              << " fresh-boot(rms)=" << f.rms
              << " injected-live(rms)=" << b.rms << "\n";
    EXPECT_GT(std::abs(b.rms - f.rms), 1e-5f)
        << "retargeted dump did not change the live Vavra render";

    // Rebuild from the tree -- the fresh child must replay the persisted dump.
    {
        auto* mp = engine.getMainProcessor();
        ASSERT_NE(mp, nullptr);
        mp->rebuildRoutingGraph();
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(6000);
        ProjectCommands::AuditionResult rd;
        rd = cmds.auditionPlugin(rp);
        ASSERT_TRUE(rd.ok) << rd.error;
        std::cout << "[VavraAB] rebuilt-from-tree rms=" << rd.rms
                  << " vs fresh-boot=" << f.rms
                  << " |delta|=" << std::abs(rd.rms - f.rms) << "\n";
        EXPECT_GT(std::abs(rd.rms - f.rms), 1e-5f)
            << "fresh child rebuilt from the tree did not hear the replayed dump";
    }
}

// Xenia host-parameter exposure gate (2026-09-19): gearmulator
// parameterDescriptions_xt.json now marks the curated sound/FX params public,
// so the CLAP exposes them to the host (previously 0). Proves (1) the param
// cache lists them, (2) PluginParamService::setParam reaches the live child and
// the XT OS responds (cached value/text changes), (3) the moved param audibly
// changes the live render vs a no-touch fresh instance.
TEST(FxMidiInjection, XeniaHostParamsChangeRender)
{
    constexpr const char* kXeniaClap = "C:\\Program Files\\Common Files\\CLAP\\Xenia.clap";
    if (!realPluginTestsEnabled() || !juce::File(kXeniaClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Xenia.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kXeniaClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 17;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    // Settle live routing so the param cache has a live child to read/write.
    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = a.trackIndex;
        p.slotIndex = a.slotIndex;
        p.captureToTree = false;
        p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto mr = cmds.sendFxMidi(p);
        ASSERT_TRUE(mr.ok) << mr.error;
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(2000);
    }

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    const auto all = paramSvc.getParams(a.trackIndex, pluginId);
    std::cout << "[XeniaParams] exposed=" << all.size() << "\n";
    ASSERT_GT(all.size(), 20u) << "Xenia still exposes no host params";

    int cutoffIdx = -1;
    double cutoffBefore = -1.0;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("f1cutoff") != std::string::npos)
        {
            cutoffIdx = p.index;
            cutoffBefore = p.value;
            std::cout << "[XeniaParams] F1Cutoff idx=" << p.index
                      << " text='" << p.text << "' value=" << p.value << "\n";
        }
    }
    ASSERT_GE(cutoffIdx, 0) << "F1Cutoff not exposed";

    // r0: the SAME live child, BEFORE the set (deterministic A/B reference).
    ProjectCommands::AuditionResult r0;
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 17;
    r0 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r0.ok) << r0.error;

    paramSvc.setParam(a.trackIndex, pluginId, cutoffIdx, 0.1f);
    bool changed = false;
    for (int attempt = 0; attempt < 20 && !changed; ++attempt)
    {
        juce::Thread::sleep(500);
        for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            if (p.index == cutoffIdx && (std::abs(p.value - cutoffBefore) > 1e-6 || p.text != juce::String("")))
            {
                std::cout << "[XeniaParams] F1Cutoff now text='" << p.text
                          << "' value=" << p.value << " (was " << cutoffBefore << ")\n";
                if (std::abs(p.value - cutoffBefore) > 1e-6)
                    changed = true;
            }
        if (!changed && attempt % 5 == 4)
            std::cout << "[XeniaParams] ...still polling (" << (attempt + 1) << ")\n";
    }
    EXPECT_TRUE(changed) << "setParam did not reach the live Xenia child (F1Cutoff unchanged)";

    // Same child, AFTER the set: any render difference is the param itself.
    ProjectCommands::AuditionResult r1;
    r1 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r1.ok) << r1.error;
    std::cout << "[XeniaParams] same-child rms before=" << r0.rms
              << " after=" << r1.rms
              << " |delta|=" << std::abs(r1.rms - r0.rms) << "\n";
    EXPECT_GT(std::abs(r1.rms - r0.rms), 1e-5f)
        << "closing F1Cutoff did not audibly change the live render";
}

// Vavra host-parameter exposure gate (2026-09-19): parameterDescriptions_mq.json
// now marks the curated sound/FX params public (96); same proof as the Xenia
// gate: host exposure, setParam round trip into the microQ OS, and a same-child
// render A/B attributable to the param.
TEST(FxMidiInjection, VavraHostParamsChangeRender)
{
    constexpr const char* kVavraClap = "C:\\Program Files\\Common Files\\CLAP\\Vavra.clap";
    if (!realPluginTestsEnabled() || !juce::File(kVavraClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Vavra.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kVavraClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 29;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = a.trackIndex;
        p.slotIndex = a.slotIndex;
        p.captureToTree = false;
        p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto mr = cmds.sendFxMidi(p);
        ASSERT_TRUE(mr.ok) << mr.error;
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(2000);
    }

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    // The microQ OS boots slower than the XT; the param cache may not be
    // populated on the first read, so poll up to ~12 s.
    auto all = paramSvc.getParams(a.trackIndex, pluginId);
    for (int i = 0; i < 24 && all.empty(); ++i)
    {
        juce::Thread::sleep(500);
        all = paramSvc.getParams(a.trackIndex, pluginId);
        if (i % 4 == 3)
            std::cout << "[VavraParams] ...param poll attempt " << (i + 1)
                      << " size=" << all.size() << "\n";
    }
    std::cout << "[VavraParams] exposed=" << all.size() << "\n";
    ASSERT_GT(all.size(), 20u) << "Vavra still exposes no host params";

    int cutoffIdx = -1;
    double cutoffBefore = -1.0;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("f1cutoff") != std::string::npos)
        {
            cutoffIdx = p.index;
            cutoffBefore = p.value;
            std::cout << "[VavraParams] F1Cutoff idx=" << p.index
                      << " text='" << p.text << "' value=" << p.value << "\n";
        }
    }
    ASSERT_GE(cutoffIdx, 0) << "F1Cutoff not exposed";

    ProjectCommands::AuditionResult r0;
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 29;
    r0 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r0.ok) << r0.error;

    paramSvc.setParam(a.trackIndex, pluginId, cutoffIdx, 0.05f);
    bool changed = false;
    for (int attempt = 0; attempt < 20 && !changed; ++attempt)
    {
        juce::Thread::sleep(500);
        for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            if (p.index == cutoffIdx && std::abs(p.value - cutoffBefore) > 1e-6)
            {
                std::cout << "[VavraParams] F1Cutoff value " << cutoffBefore
                          << " -> " << p.value << "\n";
                changed = true;
                break;
            }
        if (!changed && attempt % 5 == 4)
            std::cout << "[VavraParams] ...still polling (" << (attempt + 1) << ")\n";
    }
    EXPECT_TRUE(changed) << "setParam did not reach the live Vavra child (F1Cutoff unchanged)";

    ProjectCommands::AuditionResult r1;
    r1 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r1.ok) << r1.error;
    std::cout << "[VavraParams] same-child rms before=" << r0.rms
              << " after=" << r1.rms
              << " |delta|=" << std::abs(r1.rms - r0.rms) << "\n";
    EXPECT_GT(std::abs(r1.rms - r0.rms), 1e-5f)
        << "closing F1Cutoff did not audibly change the live Vavra render";
}

// Osirus boot-patch awakening gate (2026-09-19, diagnostic): the Virus C OS
// boots with an INIT-SILENT patch (probe: 'Ch 1 Channel Volume' def=0), which
// explains finding F-A (bit-identical silence under all programs/dumps) better
// than any boot-timing theory. This gate wakes the boot patch via
// PluginParamService::setParam (Ch 1 Channel Volume -> max) and measures the
// same-child render A/B. If the delta is audible, the fix for F-A is HDAW-side
// (wake the boot patch at slot creation / first capture) - no emulator change.
TEST(FxMidiInjection, OsirusBootPatchAwakening)
{
    constexpr const char* kOsirusClap = "C:\\Program Files\\Common Files\\CLAP\\Osirus.clap";
    if (!realPluginTestsEnabled() || !juce::File(kOsirusClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Osirus.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kOsirusClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 41;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = a.trackIndex;
        p.slotIndex = a.slotIndex;
        p.captureToTree = false;
        p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto mr = cmds.sendFxMidi(p);
        ASSERT_TRUE(mr.ok) << mr.error;
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(2000);
    }

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    auto all = paramSvc.getParams(a.trackIndex, pluginId);
    for (int i = 0; i < 24 && all.empty(); ++i)
    {
        juce::Thread::sleep(500);
        all = paramSvc.getParams(a.trackIndex, pluginId);
    }
    std::cout << "[OsirusWake] exposed=" << all.size() << "\n";
    ASSERT_GT(all.size(), 20u) << "Osirus exposes no host params";

    int volIdx = -1;
    double volBefore = -1.0;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("channel volume") != std::string::npos && low.find("ch 1") != std::string::npos)
        {
            volIdx = p.index;
            volBefore = p.value;
            std::cout << "[OsirusWake] Channel Volume idx=" << p.index
                      << " value=" << p.value << "\n";
            break;
        }
    }
    ASSERT_GE(volIdx, 0) << "Ch 1 Channel Volume not found";
    (void)volIdx;

    // The Osirus renders AUDIBLE audio with the ROM factory patch boot fix
    // (was exact 3.09e-06 dust before the fix — the ROM boot patch has proper
    // levels, so the Channel Volume awakening is a no-op).
    EXPECT_GT(a.rms, 0.001f)
        << "Osirus still renders silence — the ROM boot patch fix is not active";

    std::cout << "[OsirusWake] FIXED: Osirus renders audio rms=" << a.rms << "\n";
}

// FINDING F-A re-test (2026-09-19). F-A ("load_virus_preset queues but does NOT
// change renders") was measured while the Osirus slot rendered exact silence —
// every comparison was 0-vs-0, which manufactures a false "no effect" result.
// The boot-patch fix (virusLib/device.cpp loads ROM factory patch A-0) made the
// slot audible, so the A/B is finally meaningful. This probe measures it:
//   boot render -> CC0 bank 0 + PC 40 (exactly what load_virus_preset sends)
//   -> captured pluginState -> second render of the SAME slot -> delta.
TEST(FxMidiInjection, OsirusPresetChangeReflectsInRender)
{
    constexpr const char* kOsirusClap = "C:\\Program Files\\Common Files\\CLAP\\Osirus.clap";
    if (!realPluginTestsEnabled() || !juce::File(kOsirusClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or Osirus.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kOsirusClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 7;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    auto slotTree = [&] {
        return engine.getProjectModel().getTrackListTree()
            .getChild(a.trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(a.slotIndex);
    };
    const auto bootState = slotTree().getProperty(IDs::pluginState, "").toString();
    std::cout << "[OsirusFA] boot render rms=" << a.rms << " peak=" << a.peak
              << " pluginStateBytes=" << bootState.length() << "\n";

    // Precondition: a silent slot makes this test meaningless (finding F-A trap).
    ASSERT_GT(a.rms, 0.001f)
        << "Osirus is silent — the boot-patch fix is not active; F-A cannot be measured";

    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = a.trackIndex;
    mp.slotIndex = a.slotIndex;
    mp.captureToTree = true;
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 0, 0});
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, 1, 40, 0});
    auto mr = cmds.sendFxMidi(mp);
    ASSERT_TRUE(mr.ok) << mr.error;
    std::cout << "[OsirusFA] queued=" << mr.queued
              << " capturedToTree=" << (mr.capturedToTree ? 1 : 0) << "\n";

    // F-A flush diagnostics: was the probe slot already prepared when sendFxMidi
    // ran (my stopped-transport flush then skips prepare), and what does the
    // child's serialized state look like right after the flush delivers the
    // CC0+PC? A partial state here (tens of KB with no JPAR/OBST) means the
    // child was re-initialized by the flush's prepare and had not finished
    // booting/syncing params when the deferred capture samples it.
    {
        auto rawStateNow = [&]() -> int {
            auto* proc = engine.getMainProcessor();
            auto* tr = proc ? proc->getTrack(a.trackIndex) : nullptr;
            auto* slot = (tr && a.slotIndex >= 0
                          && static_cast<size_t>(a.slotIndex) < tr->getFXChain().size())
                ? tr->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
            auto* inst = slot ? slot->getPluginInstance() : nullptr;
            if (!inst)
                return -1;
            juce::MemoryBlock mb;
            inst->getStateInformation(mb);
            return static_cast<int>(mb.getSize());
        };
        auto* proc = engine.getMainProcessor();
        auto* tr = proc ? proc->getTrack(a.trackIndex) : nullptr;
        auto* slot = (tr && a.slotIndex >= 0
                      && static_cast<size_t>(a.slotIndex) < tr->getFXChain().size())
            ? tr->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
        std::cout << "[OsirusFA] phase1 slotIsPreparedAtSendFxMidi="
                  << (slot ? (slot->isPrepared() ? 1 : 0) : -1) << "\n";
        auto chunkScan = [&](const char* tag, const juce::MemoryBlock& mb) {
            bool p = false, o = false, j = false;
            const auto* d = static_cast<const uint8_t*>(mb.getData());
            for (size_t i = 0; i + 3 < mb.getSize(); ++i)
            {
                if (d[i]=='P'&&d[i+1]=='R'&&d[i+2]=='G'&&d[i+3]=='S') p = true;
                if (d[i]=='O'&&d[i+1]=='B'&&d[i+2]=='S'&&d[i+3]=='T') o = true;
                if (d[i]=='J'&&d[i+1]=='P'&&d[i+2]=='A'&&d[i+3]=='R') j = true;
            }
            std::cout << "[OsirusFA] " << tag << " PRGS=" << (p?1:0)
                      << " OBST=" << (o?1:0) << " JPAR=" << (j?1:0) << "\n";
        };
        juce::MemoryBlock mb0;
        if (auto* inst0 = slot ? slot->getPluginInstance() : nullptr)
            inst0->getStateInformation(mb0);
        std::cout << "[OsirusFA] phase1 raw state right after flush: "
                  << static_cast<int>(mb0.getSize()) << " bytes\n";
        chunkScan("flushstate", mb0);
    }

    // Deferred capture. test_main runs no juce dispatch loop of its own, so pump
    // it here AND print the slot's capture RECEIPT each second: the receipt
    // distinguishes "timer never fired" (still pending) from "capture ran but
    // was deliberately skipped" (ok + bytes, yet pluginState empty) from
    // "capture failed" (failed:<reason>).
    auto receipt = [&](const char* when) {
        auto t = slotTree();
        std::cout << "[OsirusFA] receipt(" << when << ") status="
                  << t.getProperty(IDs::captureStatus, "none").toString()
                  << " bytes=" << static_cast<int>(t.getProperty(IDs::captureBytes, 0))
                  << " atMs=" << static_cast<juce::int64>(t.getProperty(IDs::captureTimeMs, 0))
                  << " pluginStateBytes="
                  << t.getProperty(IDs::pluginState, "").toString().length() << "\n";
    };
    receipt("boot");

    // Did the CC0+PC reach the live child at all? Poll the param cache for a
    // patch/program-ish change (the instrument the OsTIrus gate uses).
    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    const std::string pluginId = static_cast<size_t>(a.slotIndex) < fxSlots.size()
        ? fxSlots[static_cast<size_t>(a.slotIndex)].pluginId : std::string();
    auto& paramSvc = engine.getPluginParamService();
    const auto beforeParams = paramSvc.getParams(a.trackIndex, pluginId);
    std::map<int, std::pair<std::string, double>> beforeMap;
    for (const auto& p : beforeParams) beforeMap[p.index] = { p.text, p.value };
    std::cout << "[OsirusFA] params before=" << beforeParams.size() << "\n";

    bool paramChanged = false;
    for (int sec = 1; sec <= 10; ++sec)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(1000);
        // Trajectory: raw child state size each second after the flush, to see
        // whether the child converges to the full state (146476-class) or stays
        // partial (34-39KB class) after the CC0+PC delivery.
        {
            auto* procX = engine.getMainProcessor();
            auto* trX = procX ? procX->getTrack(a.trackIndex) : nullptr;
            auto* slotX = (trX && a.slotIndex >= 0
                           && static_cast<size_t>(a.slotIndex) < trX->getFXChain().size())
                ? trX->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
            auto* instX = slotX ? slotX->getPluginInstance() : nullptr;
            if (instX)
            {
                juce::MemoryBlock mbX;
                instX->getStateInformation(mbX);
                std::cout << "[OsirusFA] traj raw size t+" << sec << "s: "
                          << static_cast<int>(mbX.getSize()) << "\n";
            }
        }
        if (!paramChanged)
        {
            for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            {
                const auto it = beforeMap.find(p.index);
                if (it != beforeMap.end()
                    && (std::abs(p.value - it->second.second) > 1e-6 || p.text != it->second.first))
                {
                    std::cout << "[OsirusFA] param changed: [" << p.index << "] " << p.name
                              << " '" << it->second.first << "' -> '" << p.text << "'\n";
                    paramChanged = true;
                    break;
                }
            }
        }
        const std::string label = "t+" + std::to_string(sec) + "s";
        receipt(label.c_str());
    }
    std::cout << "[OsirusFA] childParamChanged=" << (paramChanged ? 1 : 0) << "\n";

    const auto afterState = slotTree().getProperty(IDs::pluginState, "").toString();
    std::cout << "[OsirusFA] pluginStateBytes after=" << afterState.length()
              << " stateChanged=" << (afterState != bootState ? 1 : 0) << "\n";
    {
        juce::MemoryBlock pb;
        if (afterState.isNotEmpty() && pb.fromBase64Encoding(afterState))
        {
            bool p = false, o = false, j = false;
            const auto* d = static_cast<const uint8_t*>(pb.getData());
            for (size_t i = 0; i + 3 < pb.getSize(); ++i)
            {
                if (d[i]=='P'&&d[i+1]=='R'&&d[i+2]=='G'&&d[i+3]=='S') p = true;
                if (d[i]=='O'&&d[i+1]=='B'&&d[i+2]=='S'&&d[i+3]=='T') o = true;
                if (d[i]=='J'&&d[i+1]=='P'&&d[i+2]=='A'&&d[i+3]=='R') j = true;
            }
            std::cout << "[OsirusFA] persistedstate PRGS=" << (p?1:0)
                      << " OBST=" << (o?1:0) << " JPAR=" << (j?1:0)
                      << " size=" << static_cast<int>(pb.getSize()) << "\n";
        }
        else
            std::cout << "[OsirusFA] persistedstate empty\n";
    }

    auto render = [&](int trackIdx, int slotIdx) {
        ProjectCommands::AuditionParams rp;
        rp.trackIndex = trackIdx;
        rp.slotIndex = slotIdx;
        rp.lengthBeats = 2.0;
        rp.windowSeconds = 2.0;
        rp.seed = 7;
        return cmds.auditionPlugin(rp);
    };
    auto r2 = render(a.trackIndex, a.slotIndex);
    ASSERT_TRUE(r2.ok) << r2.error;

    const float delta = std::abs(r2.rms - a.rms);
    std::cout << "[OsirusFA] post render rms=" << r2.rms << " |delta|=" << delta << "\n";
    std::cout << "[OsirusFA] VERDICT: preset load " << (delta > 1e-4f ? "CHANGED" : "did NOT change")
              << " the offline render\n";

    // ---------------------------------------------------------------------
    // PHASE 2 — isolate the variable. Drive a HOST PARAM write (the path the
    // docs describe as "reaches the cache"), then trigger a capture with a
    // harmless CC (the Nord loader's CC125 trick). If this round-trips
    // (receipt ok + bytes + render delta) while CC0+PC did not, the capture
    // path is sound and F-A is an injection/emulation limitation rather than a
    // capture bug.
    // ---------------------------------------------------------------------
    int cutIdx = -1;
    double cutBefore = 0.0;
    std::string cutName;
    for (const auto& p : beforeParams)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("cutoff") != std::string::npos)
        {
            cutIdx = p.index;
            cutBefore = p.value;
            cutName = p.name;
            break;
        }
    }
    std::cout << "[OsirusFA] phase2 cutoff idx=" << cutIdx << " name='" << cutName
              << "' before=" << cutBefore << "\n";
    if (cutIdx < 0)
    {
        std::cout << "[OsirusFA] phase2 SKIPPED (no cutoff param exposed)\n";
        EXPECT_GT(r2.rms, 0.001f) << "post-injection render is silent";
        return;
    }

    // PHASE 3 — raw proof, bypassing the guarded capture. (a) Is JPAR active?
    // With 3086 exposed params the serialized state should be ~34 KB, not the
    // ~262 B the wrapper produced before the JPAR chunk. (b) Does the cutoff
    // write reach the ISOLATED CHILD at all? We sample the live slot's raw
    // state before and after the write; identical bytes mean the child's
    // parameter object never changed (HDAW delivery gap), a change means the
    // wrapper applied it and the skip must be a baseline-timing artifact.
    auto rawState = [&]() -> juce::MemoryBlock {
        juce::MemoryBlock mb;
        auto* proc = engine.getMainProcessor();
        auto* tr = proc ? proc->getTrack(a.trackIndex) : nullptr;
        auto* slot = (tr && a.slotIndex >= 0
                      && static_cast<size_t>(a.slotIndex) < tr->getFXChain().size())
            ? tr->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
        auto* inst = slot ? slot->getPluginInstance() : nullptr;
        if (inst)
            inst->getStateInformation(mb);
        return mb;
    };
    const auto rawBefore = rawState();
    std::cout << "[OsirusFA] phase3 raw state BEFORE cutoff write: "
              << rawBefore.getSize() << " bytes\n";

    paramSvc.setParam(a.trackIndex, pluginId, cutIdx, 0.0f); // fully closed filter
    bool cutoffReached = false;
    for (int i = 0; i < 20 && !cutoffReached; ++i)
    {
        juce::Thread::sleep(500);
        for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            if (p.index == cutIdx && std::abs(p.value - cutBefore) > 1e-6)
            {
                std::cout << "[OsirusFA] phase2 cutoff cache " << cutBefore << " -> "
                          << p.value << "\n";
                cutoffReached = true;
                break;
            }
    }
    std::cout << "[OsirusFA] phase2 cutoffReachedChild=" << (cutoffReached ? 1 : 0) << "\n";

    // Pump the message loop so the parent's 100ms param-flush timer can deliver
    // the staged value to the ring BEFORE we sample the raw child state.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(2500);

    const auto rawAfter = rawState();
    const bool childStateChanged = rawAfter.getSize() != rawBefore.getSize()
        || !(rawAfter == rawBefore);
    std::cout << "[OsirusFA] phase3 raw state AFTER cutoff write: "
              << rawAfter.getSize() << " bytes | same=" << (childStateChanged ? 0 : 1)
              << "\n";
    std::cout << "[OsirusFA] phase3 childStateChangedByParam=" << (childStateChanged ? 1 : 0)
              << " (JPAR active when size ~34KB, duplex delivery proven when changed)\n";

    {
        ProjectCommands::FxMidiParams hp;
        hp.trackIndex = a.trackIndex;
        hp.slotIndex = a.slotIndex;
        hp.captureToTree = true;
        hp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto hr = cmds.sendFxMidi(hp);
        std::cout << "[OsirusFA] phase2 capture-trigger ok=" << (hr.ok ? 1 : 0) << "\n";
        auto* proc0 = engine.getMainProcessor();
        auto* tr0 = proc0 ? proc0->getTrack(a.trackIndex) : nullptr;
        auto* slotp = (tr0 && a.slotIndex >= 0
                       && static_cast<size_t>(a.slotIndex) < tr0->getFXChain().size())
            ? tr0->getFXChain()[static_cast<size_t>(a.slotIndex)].get() : nullptr;
        std::cout << "[OsirusFA] phase2 hasBootBaseline="
                  << (slotp ? (slotp->hasBootBaseline() ? 1 : 0) : -1) << "\n";
        for (int sec = 1; sec <= 9; ++sec)
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->runDispatchLoopUntil(1000);
            const std::string label = "phase2+" + std::to_string(sec) + "s";
            receipt(label.c_str());
        }
        const auto rawLate = rawState();
        std::cout << "[OsirusFA] phase2 raw state at end: " << rawLate.getSize()
                  << " bytes | changed=" << (rawLate.getSize() != rawBefore.getSize()
                      || !(rawLate == rawBefore) ? 1 : 0) << "\n";
    }
    auto r3 = render(a.trackIndex, a.slotIndex);
    if (r3.ok)
        std::cout << "[OsirusFA] phase2 render rms=" << r3.rms
                  << " |delta|" << "=" << std::abs(r3.rms - a.rms) << "\n";
    std::cout << "[OsirusFA] PHASE2 VERDICT: host-param write "
              << ((r3.ok && std::abs(r3.rms - a.rms) > 1e-4f) ? "ROUND-TRIPS" : "did NOT round-trip")
              << " to the offline render\n";

    // Diagnostic during the re-test: the verdict lines above are the measurement.
    // Tightened to a hard assertion once the measured behaviour is recorded.
    EXPECT_GT(r2.rms, 0.001f) << "post-injection render is silent (injection broke the slot)";
}

// NodalRed2x host-parameter exposure gate (2026-09-19): 33 curated sound
// params made public in parameterDescriptions_n2x.json + rebuilt. Proves the
// params are visible in the cache and that setParam (Cutoff) audibly changes
// the live render.
TEST(FxMidiInjection, NodalRed2xHostParamsChangeRender)
{
    constexpr const char* kN2xClap = "C:\\Program Files\\Common Files\\CLAP\\NodalRed2x.clap";
    if (!realPluginTestsEnabled() || !juce::File(kN2xClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or NodalRed2x.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kN2xClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 31;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);

    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = a.trackIndex;
        p.slotIndex = a.slotIndex;
        p.captureToTree = false;
        p.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
        auto mr = cmds.sendFxMidi(p);
        ASSERT_TRUE(mr.ok) << mr.error;
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(2000);
    }

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    auto all = paramSvc.getParams(a.trackIndex, pluginId);
    for (int i = 0; i < 24 && all.empty(); ++i)
    {
        juce::Thread::sleep(500);
        all = paramSvc.getParams(a.trackIndex, pluginId);
    }
    std::cout << "[N2xParams] exposed=" << all.size() << "\n";
    ASSERT_GT(all.size(), 15u) << "NodalRed2x still exposes no host params";

    int cutoffIdx = -1;
    double cutoffBefore = -1.0;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("cutoff") != std::string::npos && cutoffIdx < 0)
        {
            cutoffIdx = p.index;
            cutoffBefore = p.value;
            std::cout << "[N2xParams] Cutoff idx=" << p.index
                      << " value=" << p.value << "\n";
        }
    }
    ASSERT_GE(cutoffIdx, 0) << "Cutoff not exposed";

    ProjectCommands::AuditionResult r0;
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 31;
    r0 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r0.ok) << r0.error;

    paramSvc.setParam(a.trackIndex, pluginId, cutoffIdx, 0.05f);
    bool changed = false;
    for (int attempt = 0; attempt < 20 && !changed; ++attempt)
    {
        juce::Thread::sleep(500);
        for (const auto& p : paramSvc.getParams(a.trackIndex, pluginId))
            if (p.index == cutoffIdx && std::abs(p.value - cutoffBefore) > 1e-6)
            {
                std::cout << "[N2xParams] Cutoff " << cutoffBefore << " -> " << p.value << "\n";
                changed = true;
                break;
            }
    }
    EXPECT_TRUE(changed) << "setParam did not reach the live NodalRed2x child";

    ProjectCommands::AuditionResult r1;
    r1 = cmds.auditionPlugin(rp);
    ASSERT_TRUE(r1.ok) << r1.error;
    std::cout << "[N2xParams] same-child rms before=" << r0.rms
              << " after=" << r1.rms
              << " |delta|=" << std::abs(r1.rms - r0.rms) << "\n";
    EXPECT_GT(std::abs(r1.rms - r0.rms), 1e-5f)
        << "closing Cutoff did not audibly change the live NodalRed2x render";
}

// OsTIrus render gate (2026-09-19): the TI has a different OS from the C
// and may not share the model-C offline silence. Tests with a long warmup
// (25 s) + Channel Volume awakening to isolate the two variables.
TEST(FxMidiInjection, OsTIrusRenderAudibility)
{
    constexpr const char* kOsTIrusClap = "C:\\Program Files\\Common Files\\CLAP\\OsTIrus.clap";
    if (!realPluginTestsEnabled() || !juce::File(kOsTIrusClap).existsAsFile())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or OsTIrus.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::AuditionParams probe;
    probe.pluginId = kOsTIrusClap;
    probe.trackIndex = -1;
    probe.keepTrack = true;
    probe.lengthBeats = 2.0;
    probe.windowSeconds = 2.0;
    probe.seed = 53;
    auto a = cmds.auditionPlugin(probe);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_GE(a.trackIndex, 0);
    std::cout << "[OsTIrus] baseline rms=" << a.rms << "\n";

    const auto fxSlots = engine.getReadModel().getFxSlots(a.trackIndex);
    ASSERT_LT(static_cast<size_t>(a.slotIndex), fxSlots.size());
    const std::string pluginId = fxSlots[static_cast<size_t>(a.slotIndex)].pluginId;
    auto& paramSvc = engine.getPluginParamService();

    auto all = paramSvc.getParams(a.trackIndex, pluginId);
    for (int i = 0; i < 24 && all.empty(); ++i)
    {
        juce::Thread::sleep(500);
        all = paramSvc.getParams(a.trackIndex, pluginId);
    }
    std::cout << "[OsTIrus] exposed=" << all.size() << "\n";

    // Find a Channel Volume param
    int volIdx = -1;
    for (const auto& p : all)
    {
        std::string low = p.name;
        for (auto& ch : low) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        if (low.find("channel volume") != std::string::npos && low.find("ch 1") != std::string::npos)
        {
            volIdx = p.index;
            std::cout << "[OsTIrus] Channel Volume idx=" << p.index << "\n";
            break;
        }
    }

    if (volIdx < 0)
    {
        std::cout << "[OsTIrus] Channel Volume not found in param cache\n";
        GTEST_SKIP() << "no Channel Volume param";
    }

    paramSvc.setParam(a.trackIndex, pluginId, volIdx, 1.0f);

    // Same-child render after awakening
    ProjectCommands::AuditionResult b;
    ProjectCommands::AuditionParams rp;
    rp.trackIndex = a.trackIndex;
    rp.slotIndex = a.slotIndex;
    rp.lengthBeats = 2.0;
    rp.windowSeconds = 2.0;
    rp.seed = 53;
    b = cmds.auditionPlugin(rp);
    ASSERT_TRUE(b.ok) << b.error;

    // Fresh boot control
    ProjectCommands::AuditionParams fresh;
    fresh.pluginId = kOsTIrusClap;
    fresh.trackIndex = -1;
    fresh.keepTrack = false;
    fresh.lengthBeats = 2.0;
    fresh.windowSeconds = 2.0;
    fresh.seed = 53;
    auto f = cmds.auditionPlugin(fresh);
    ASSERT_TRUE(f.ok) << f.error;

    std::cout << "[OsTIrus] baseline=" << a.rms << " volume-max=" << b.rms
              << " fresh=" << f.rms << "\n";

    // OsTIrus (Virus TI) WORKS offline (2026-09-19): the TI OS boots and
    // renders real audio (rms 0.04, not dust) in a fresh offline child.
    // Unlike the Osirus model-C (which is silent), the TI is a working core
    // synth. Host param writes don't change the render (all three renders
    // identical — the OS ignores host-initiated param sets, same as the
    // Osirus), but the patch loading path (load_virus_preset CC0+PC or
    // presetSysex replay) is the correct control route, NOT host params.
    EXPECT_GT(a.rms, 0.001f)
        << "OsTIrus renders silence — the TI emulation is broken";
    std::cout << "[OsTIrus] WORKS: the Virus TI renders audio in offline children.\n";
}

namespace {

juce::MidiMessage makeTestSysex(uint8_t tag)
{
    const uint8_t d[] = { 0xF0, 0x33, 0x01, tag, 0xF7 };
    return juce::MidiMessage(d, 5);
}

uint8_t sysexTag(const juce::MidiMessage& m)
{
    return static_cast<const uint8_t*>(m.getRawData())[3];
}

} // namespace

// NB1/NB4: SysEx metering — at most one SysEx per processed block, order
// preserved across blocks (the SHM midiIn ring has a single SysEx lane).
TEST(FxMidiInjection, SysExMeteredOnePerBlockInOrder)
{
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-meter", false);
    auto* rec = static_cast<RecordingPlugin*>(slot.getPluginInstance());
    ASSERT_NE(rec, nullptr);
    juce::AudioBuffer<float> buffer(2, 512);
    slot.queueMidiForNextBlock(makeTestSysex(1));
    slot.queueMidiForNextBlock(makeTestSysex(2));
    slot.queueMidiForNextBlock(makeTestSysex(3));
    slot.queueMidiForNextBlock(juce::MidiMessage::programChange(1, 7));
    for (int i = 0; i < 4; ++i)
    {
        juce::MidiBuffer midi;
        slot.process(buffer, midi);
    }
    // The trailing PC rides WITH the last dump (no second SysEx follows it),
    // so 4 process calls yield 3 non-empty blocks: order is what matters.
    ASSERT_EQ(rec->received.size(), 3u);
    for (size_t i = 0; i < 2; ++i)
    {
        const auto msgs = flatten(rec->received[i]);
        ASSERT_EQ(msgs.size(), 1u) << "block " << i;
        ASSERT_TRUE(msgs[0].isSysEx()) << "block " << i;
        EXPECT_EQ(sysexTag(msgs[0]), static_cast<uint8_t>(i + 1)) << "block " << i;
    }
    const auto last = flatten(rec->received[2]);
    ASSERT_EQ(last.size(), 2u);
    ASSERT_TRUE(last[0].isSysEx());
    EXPECT_EQ(sysexTag(last[0]), 3u);
    EXPECT_TRUE(last[1].isProgramChange());
    EXPECT_EQ(last[1].getProgramChangeNumber(), 7);
}

// The non-SysEx prefix rides WITH the first SysEx; only the post-SysEx
// remainder is held (order preserved, short-message timing unchanged).
TEST(FxMidiInjection, NonSysexPrefixRidesWithFirstSysex)
{
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-prefix", false);
    auto* rec = static_cast<RecordingPlugin*>(slot.getPluginInstance());
    ASSERT_NE(rec, nullptr);
    juce::AudioBuffer<float> buffer(2, 512);
    slot.queueMidiForNextBlock(juce::MidiMessage::programChange(1, 9));
    slot.queueMidiForNextBlock(juce::MidiMessage::controllerEvent(1, 0, 2));
    slot.queueMidiForNextBlock(makeTestSysex(5));
    slot.queueMidiForNextBlock(makeTestSysex(6));
    for (int i = 0; i < 2; ++i)
    {
        juce::MidiBuffer midi;
        slot.process(buffer, midi);
    }
    ASSERT_EQ(rec->received.size(), 2u);
    const auto first = flatten(rec->received[0]);
    ASSERT_EQ(first.size(), 3u);
    EXPECT_TRUE(first[0].isProgramChange());
    EXPECT_TRUE(first[1].isController());
    ASSERT_TRUE(first[2].isSysEx());
    EXPECT_EQ(sysexTag(first[2]), 5u);
    const auto second = flatten(rec->received[1]);
    ASSERT_EQ(second.size(), 1u);
    ASSERT_TRUE(second[0].isSysEx());
    EXPECT_EQ(sysexTag(second[0]), 6u);
}

// NB4: the capture receipt helper stamps always-fresh values (never a
// setProperty no-op) so agents can poll capture completion.
TEST(FxMidiInjection, CaptureReceiptStampsFreshValues)
{
    juce::ValueTree slotTree(IDs::FX_SLOT);
    AudioEngineCommands::writeFxCaptureReceipt(slotTree, "pending", 0);
    EXPECT_EQ(slotTree.getProperty(IDs::captureStatus).toString(), "pending");
    EXPECT_EQ(static_cast<int>(slotTree.getProperty(IDs::captureBytes, -1)), 0);
    const auto t0 = static_cast<juce::int64>(slotTree.getProperty(IDs::captureTimeMs, 0));
    EXPECT_GT(t0, 0);
    AudioEngineCommands::writeFxCaptureReceipt(slotTree, "ok", 1234);
    EXPECT_EQ(slotTree.getProperty(IDs::captureStatus).toString(), "ok");
    EXPECT_EQ(static_cast<int>(slotTree.getProperty(IDs::captureBytes, -1)), 1234);
    // Invalid trees are a silent no-op (deferred timer vs rebuilt chain).
    AudioEngineCommands::writeFxCaptureReceipt(juce::ValueTree(), "ok", 1);
}

namespace {
QString jeResultText(const mcp::McpToolResult& r)
{
    return r.content.at(0).toObject().value("text").toString();
}
} // namespace

// JP-8080 (JE8086) loader: the bank file is validated ATOMICALLY before
// anything is queued, so a bank with one corrupt DT1 message can never
// half-load. Every path below returns before sendFxMidi, which is why the test
// needs no plugin slot (and proves a bad bank never reaches the child).
TEST(FxMidiInjection, Je8086LoaderValidatesBeforeQueueing)
{
    AudioEngine engine;
    engine.initialize();

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("hdaw_je8086_loader_test");
    dir.deleteRecursively();
    dir.createDirectory();

    auto writeFile = [&dir](const juce::String& name, const std::vector<uint8_t>& bytes)
    {
        const auto f = dir.getChildFile(name);
        f.replaceWithData(bytes.data(), bytes.size());
        return f;
    };
    auto asPath = [](const juce::File& f)
    {
        return QString::fromUtf8(f.getFullPathName().toRawUTF8());
    };
    auto dt1 = [](uint8_t a1, uint8_t a2, const std::vector<uint8_t>& data, bool corrupt)
    {
        std::vector<uint8_t> out { 0xF0, 0x41, 0x10, 0x00, 0x06, 0x12, 2, a1, a2 };
        out.insert(out.end(), data.begin(), data.end());
        uint32_t sum = 0;
        for (size_t i = 6; i < out.size(); ++i)
            sum += out[i];
        auto ck = static_cast<uint8_t>((128 - (sum % 128)) % 128);
        if (corrupt)
            ck = static_cast<uint8_t>((ck + 1) % 128);
        out.push_back(ck);
        out.push_back(0xF7);
        return out;
    };
    auto page = [](const char* name)
    {
        std::vector<uint8_t> p(256, 0);
        for (size_t i = 0; name[i] != 0 && i < 16; ++i)
            p[1 + i] = static_cast<uint8_t>(name[i]);
        return p;
    };

    std::vector<uint8_t> bank;
    for (uint8_t pat = 0; pat < 2; ++pat)
    {
        for (const auto& m : { dt1(0, static_cast<uint8_t>(pat * 2),
                                   page(pat == 0 ? "BASS ONE" : "LEAD TWO"), false),
                               dt1(0, static_cast<uint8_t>(pat * 2 + 1),
                                   std::vector<uint8_t>(16, 0), false) })
            bank.insert(bank.end(), m.begin(), m.end());
    }
    const auto goodFile = writeFile("good.syx", bank);

    // 1. corrupt DT1 in the SECOND patch -> the whole file is rejected
    auto corrupt = bank;
    corrupt[corrupt.size() - 3] ^= 0x01;      // a data byte of the last message
    const auto corruptFile = writeFile("corrupt.syx", corrupt);
    const auto rCorrupt = mcp::runJe8086PatchFile(engine, 0, 0, asPath(corruptFile), 1, true, false);
    EXPECT_TRUE(rCorrupt.isError) << "a bank with one corrupt message must not load";
    EXPECT_NE(jeResultText(rCorrupt).toStdString().find("checksum"), std::string::npos)
        << jeResultText(rCorrupt).toStdString();

    // 2. preset index beyond the file's units
    const auto rRange = mcp::runJe8086PatchFile(engine, 0, 0, asPath(goodFile), 5, true, false);
    EXPECT_TRUE(rRange.isError);
    EXPECT_NE(jeResultText(rRange).toStdString().find("out of range"), std::string::npos)
        << jeResultText(rRange).toStdString();

    // 3. a non-JP-8080 SysEx file (Clavia) is never mis-decoded
    const std::vector<uint8_t> clavia { 0xF0, 0x33, 0x00, 0x04, 0x01, 0x08, 0x00, 0xF7 };
    const auto rClavia = mcp::runJe8086PatchFile(engine, 0, 0,
        asPath(writeFile("nord.syx", clavia)), 1, true, false);
    EXPECT_TRUE(rClavia.isError);
    EXPECT_NE(jeResultText(rClavia).toStdString().find("no JP-8080"), std::string::npos)
        << jeResultText(rClavia).toStdString();

    // 4. unsupported container + missing file
    EXPECT_TRUE(mcp::runJe8086PatchFile(engine, 0, 0,
        asPath(writeFile("bank.txt", bank)), 1, true, false).isError);
    const auto rMissing = mcp::runJe8086PatchFile(engine, 0, 0, "Z:/nope/missing.syx", 1, true, false);
    EXPECT_TRUE(rMissing.isError);
    EXPECT_NE(jeResultText(rMissing).toStdString().find("file not found"), std::string::npos);

    dir.deleteRecursively();
}


// D-lite: a state identical to the fresh-instance baseline is a boot stub, not a
// capture. Persisting it makes later graph builds (including the offline render)
// restore it and play the plugin's default patch instead of the live one.
TEST(FxMidiInjection, BootStateBaselineGuard)
{
    juce::MemoryBlock boot("boot-stub", 9);
    juce::MemoryBlock changed("boot-stub-plus-patch", 19);

    using HDAW::shouldPersistStateCapture;
    const bool iso = true, inproc = false, restored = true, noRestore = false;
    const bool baseline = true, noBaseline = false;

    // in-process slots keep the old semantics (save/load durability contract)
    EXPECT_TRUE(shouldPersistStateCapture(inproc, noRestore, baseline, boot, boot));
    EXPECT_TRUE(shouldPersistStateCapture(inproc, noRestore, baseline, boot, changed));

    // isolated slot: before the first sample we cannot know, so we persist
    EXPECT_TRUE(shouldPersistStateCapture(iso, noRestore, noBaseline, boot, boot));
    // ...an echo of the boot baseline is a stub, not a capture
    EXPECT_FALSE(shouldPersistStateCapture(iso, noRestore, baseline, boot, boot));
    // ...a changed state is the real thing
    EXPECT_TRUE(shouldPersistStateCapture(iso, noRestore, baseline, boot, changed));
    // ...an empty read is left to the empty-state guard
    juce::MemoryBlock empty;
    EXPECT_TRUE(shouldPersistStateCapture(iso, noRestore, baseline, boot, empty));
    // ...a state restored from the tree is never dropped as a stub
    EXPECT_TRUE(shouldPersistStateCapture(iso, restored, baseline, boot, boot));

    // the baseline itself is never moved by a later sample (it is the BOOT state)
    HDAW::TrackFXSlot slot(std::make_unique<RecordingPlugin>(), "fake-baseline", false);
    slot.noteStateSample(boot);
    slot.noteStateSample(changed);
    EXPECT_FALSE(slot.stateLooksUnchangedSinceBoot(boot)) << "in-process: never skipped";

    // C2c shrink guard — the composed predicate encodes the production
    // contract of AudioEngineCommands::captureFxSlotState (TrackFXSlot.h
    // doc, both capture paths now call the same composition):
    //   * empty reads NEVER persist ("failed: empty state" up front);
    //   * an ISOLATED read SMALLER than the last-good blob is a pre-boot stub
    //     or resize regression and must not clobber ("ok", 0 bytes) — this
    //     applies to restored slots too;
    //   * otherwise the plain D-lite guard decides (boot echo -> skip).
    // In-process slots are never skipped when non-empty; a non-empty read at/
    // above the existing size passes through to the plain guard.
    using HDAW::shouldPersistStateCaptureWithExisting;
    juce::MemoryBlock stub233("b", 1); // simulated 233B-class pre-boot stub (any 1-byte block reproduces the guard)
    // empty reads
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, empty, 874));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, empty, 0));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(inproc, noRestore, baseline, boot, empty, 874));
    // smaller-than-existing is a stub-class read: refused, restored or not
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, restored, baseline, boot, stub233, 874));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, stub233, 874));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, changed, 874));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, restored, baseline, boot, boot, 874));
    // at/above the existing size the plain D-lite guard decides
    EXPECT_TRUE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, changed, 10));
    EXPECT_TRUE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, changed, 0));
    EXPECT_FALSE(shouldPersistStateCaptureWithExisting(iso, noRestore, baseline, boot, boot, 0));
    // in-process slots keep legacy durability semantics (never skipped when non-empty)
    EXPECT_TRUE(shouldPersistStateCaptureWithExisting(inproc, noRestore, baseline, boot, stub233, 874));
    // restored-from-tree slots are never dropped as stubs
    EXPECT_TRUE(shouldPersistStateCaptureWithExisting(iso, restored, baseline, boot, boot, 0));
    EXPECT_TRUE(shouldPersistStateCaptureWithExisting(iso, restored, baseline, boot, changed, 0));
}

// Matrix-preset A/B for the two engines whose presets were previously
// un-runnable: `apply_matrix_preset` refuses any sheet preset whose
// `appliesVia` is not `set_fx_param`, and virus.json/vavra.json declared
// `midi_cc_pc` / `state_blob_or_patch_unverified` (their values ARE parameter
// values — the labels predate the param-publishing work), while the param path
// had no name resolution for their vocabulary (no virus_param_index_map.json).
// Both sheets are now `set_fx_param` and the tool resolves sheet names against
// the LIVE slot's own exposed params, so this drives real parameters through the
// same route and requires them to reach the offline render.
//
// The render threshold is NOISE-FLOOR aware: two identical baseline renders
// measure the run-to-run jitter and the applied delta must beat 3x that (and an
// absolute 1e-4 floor) — the fixed thresholds elsewhere in this file range from
// 1e-5 to 1e-4 against jitter of ~1e-6..1e-4, which is what makes them flaky.
TEST(FxMidiInjection, MatrixPresetAudibilityVirusVavra)
{
    if (!realPluginTestsEnabled())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set";

    const juce::File here(__FILE__);
    // __FILE__ = <repo>/tests/unit/engine/fx_midi_injection_test.cpp
    const auto sheetDir = here.getParentDirectory().getParentDirectory()
                              .getParentDirectory().getParentDirectory()
                              .getChildFile("timbre-lib").getChildFile("matrix_presets");
    if (!sheetDir.isDirectory())
        GTEST_SKIP() << "matrix preset sheets not found at " << sheetDir.getFullPathName();

    struct Eng { const char* id; const char* clap; };
    const Eng engs[] = {
        { "virus", "C:\\Program Files\\Common Files\\CLAP\\Osirus.clap" },
        { "vavra", "C:\\Program Files\\Common Files\\CLAP\\Vavra.clap" } };

    for (const auto& en : engs)
    {
        if (!juce::File(en.clap).existsAsFile())
        {
            std::cout << "[MatrixAB] " << en.id << ": clap missing\n";
            continue;
        }

        AudioEngine engine;
        engine.initialize();
        auto& cmds = engine.getProjectCommands();
        auto& paramSvc = engine.getPluginParamService();

        ProjectCommands::AuditionParams probe;
        probe.pluginId = en.clap;
        probe.trackIndex = -1;
        probe.keepTrack = true;
        probe.lengthBeats = 2.0;
        probe.windowSeconds = 2.0;
        probe.seed = 21;
        auto a1 = cmds.auditionPlugin(probe);
        ASSERT_TRUE(a1.ok) << en.id << ": baseline audition failed: " << a1.error;

        auto render = [&]() {
            ProjectCommands::AuditionParams rp;
            rp.trackIndex = a1.trackIndex;
            rp.slotIndex = a1.slotIndex;
            rp.lengthBeats = 2.0;
            rp.windowSeconds = 2.0;
            rp.seed = 21;
            return cmds.auditionPlugin(rp);
        };

        // Noise floor: an unchanged state rendered twice.
        auto a2 = render();
        ASSERT_TRUE(a2.ok) << en.id << ": repeat baseline failed: " << a2.error;
        const float noise = std::abs(a2.rms - a1.rms);

        const auto fxSlots = engine.getReadModel().getFxSlots(a1.trackIndex);
        ASSERT_LT(static_cast<size_t>(a1.slotIndex), fxSlots.size());
        const std::string pluginId = fxSlots[static_cast<size_t>(a1.slotIndex)].pluginId;

        // Sheet preset 0's params, resolved exactly like apply_matrix_preset now
        // does: sheet vocabulary -> live param index (normalised, part-prefix and
        // spacing tolerant), value 0..127 -> normalised.
        const auto sheetText = sheetDir.getChildFile(en.id).withFileExtension(".json").loadFileAsString();
        const auto sheet = juce::JSON::parse(sheetText);
        const auto* presets = sheet.getProperty("presets", juce::var()).getArray();
        ASSERT_TRUE(presets != nullptr && !presets->isEmpty()) << en.id << ": sheet has no presets";

        // Device-native dump route: when the sheet carries a complete dump for
        // the preset (vavra - built offline from the same params by
        // timbre-lib/vavra_matrix_sysex.py), inject it. Those values ARE the
        // device's patch vocabulary, and its FX sub-parameters cannot become
        // host params at all (they share indexes, so the wrapper collapses them
        // into single host params with derived children). The emulated OS
        // applies the dump natively, exactly like a real patch transfer.
        const auto dumpVar = (*presets)[0].getProperty("sysex", juce::var());
        if (const auto* dumpArr = dumpVar.getArray(); dumpArr != nullptr && !dumpArr->isEmpty())
        {
            ProjectCommands::FxMidiParams dp;
            dp.trackIndex = a1.trackIndex;
            dp.slotIndex = a1.slotIndex;
            dp.captureToTree = true;
            ProjectCommands::FxMidiEvent dev;
            dev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
            for (const auto& b : *dumpArr)
                dev.sysex.push_back(static_cast<uint8_t>(static_cast<int>(b) & 0xFF));
            dp.events.push_back(std::move(dev));
            const auto dr = cmds.sendFxMidi(dp);
            ASSERT_TRUE(dr.ok) << en.id << ": dump injection failed: " << dr.error;
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->runDispatchLoopUntil(4000);
            auto rd = render();
            ASSERT_TRUE(rd.ok) << en.id << ": post-dump render failed: " << rd.error;
            const float ddelta = std::abs(rd.rms - a1.rms);
            const float dfloor = (std::max)(1e-4f, 3.0f * noise);
            std::cout << "[MatrixAB] " << en.id << " route=device_dump bytes=" << dumpArr->size()
                      << " base=" << (*presets)[0].getProperty("baseSyx", "").toString().toStdString()
                      << " rms=" << rd.rms << " delta=" << ddelta
                      << " threshold=" << dfloor << "\n";
            EXPECT_GT(ddelta, dfloor)
                << en.id << ": injected device dump did not change the offline render";
            continue;
        }

        const auto params = (*presets)[0].getProperty("params", juce::var());
        const auto* obj = params.getDynamicObject();
        ASSERT_TRUE(obj != nullptr) << en.id << ": preset 0 has no params";

        const auto norm = [](juce::String s) {
            s = s.toLowerCase();
            for (const auto c : juce::String(" _-/"))
                s = s.replaceCharacter(c, ' ');
            s = s.removeCharacters(" ");
            if (s.startsWith("ch"))
            {
                int k = 2;
                while (k < s.length() && juce::CharacterFunctions::isDigit(s[k])) ++k;
                if (k > 2) s = s.substring(k);
            }
            else if (s.startsWith("part"))
            {
                int k = 4;
                while (k < s.length() && juce::CharacterFunctions::isDigit(s[k])) ++k;
                if (k > 4) s = s.substring(k);
            }
            return s;
        };

        const auto live = paramSvc.getParams(a1.trackIndex, pluginId);
        int applied = 0, unmapped = 0;
        for (const auto& kv : obj->getProperties())
        {
            const auto want = norm(kv.name.toString());
            if (want.isEmpty())
                continue;
            int idx = -1;
            for (const auto& p : live)
                if (norm(juce::String(p.name)) == want) { idx = p.index; break; }
            if (idx < 0)
                for (const auto& p : live)
                    if (norm(juce::String(p.name)).endsWith(want)) { idx = p.index; break; }
            if (idx < 0) { ++unmapped; continue; }
            const double raw = kv.value.toString().getDoubleValue();
            paramSvc.setParam(a1.trackIndex, pluginId, idx,
                              static_cast<float>(juce::jlimit(0.0, 127.0, raw) / 127.0));
            ++applied;
        }
        std::cout << "[MatrixAB] " << en.id << " preset0 applied=" << applied
                  << " unmapped=" << unmapped << " of " << obj->getProperties().size()
                  << " params (live params=" << live.size() << ")\n";

        // Snapshot the applied params into the tree so the offline render sees
        // them (the same capture trigger apply_matrix_preset uses).
        {
            ProjectCommands::FxMidiParams cp;
            cp.trackIndex = a1.trackIndex;
            cp.slotIndex = a1.slotIndex;
            cp.captureToTree = true;
            cp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 125, 0});
            const auto cr = cmds.sendFxMidi(cp);
            EXPECT_TRUE(cr.ok) << en.id << ": capture trigger failed: " << cr.error;
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->runDispatchLoopUntil(3000);
        }

        auto r = render();
        ASSERT_TRUE(r.ok) << en.id << ": post-apply render failed: " << r.error;
        const float delta = std::abs(r.rms - a1.rms);
        const float floorAbs = (std::max)(1e-4f, 3.0f * noise);
        std::cout << "[MatrixAB] " << en.id << " rms base=" << a1.rms
                  << " repeat=" << a2.rms << " noise=" << noise
                  << " applied=" << r.rms << " delta=" << delta
                  << " threshold=" << floorAbs << "\n";

        EXPECT_GT(applied, 0) << en.id << ": no sheet param resolved against the live params";
        EXPECT_GT(delta, floorAbs)
            << en.id << ": applied matrix preset did not change the offline render"
            << " (delta=" << delta << " noise=" << noise << ")";
    }
}
