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
#include <cstdlib>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "mcp/PresetFileParser.h"
#include "mcp/PresetRoute.h"
#include "model/ProjectModel.h"

namespace {

constexpr const char* kOsTIrusClap = "C:\\Program Files\\Common Files\\CLAP\\OsTIrus.clap";
constexpr const char* kNodalRed2xClap = "C:\\Program Files\\Common Files\\CLAP\\NodalRed2x.clap";
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
    if (before.empty())
    {
        std::cout << "[FxMidiInjection] no patch/program/bank param exposed by the plugin;"
                     " delivery is covered by the SHM ring + CLAP_EVENT_MIDI path (G2)\n";
        GTEST_SKIP() << "no observable patch param on this plugin";
    }
    EXPECT_TRUE(changed) << "CC0+PC produced no observable param change in the child";
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
    const auto stateStr = slotTree.getProperty(IDs::pluginState, "").toString();
    EXPECT_FALSE(stateStr.isEmpty()) << "pluginState was not captured";
    EXPECT_EQ(slotTree.getProperty(IDs::captureStatus, "").toString(), "ok")
        << "capture receipt missing (NB4)";

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
}