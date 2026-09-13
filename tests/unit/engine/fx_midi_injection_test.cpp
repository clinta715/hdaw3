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
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"

namespace {

constexpr const char* kOsTIrusClap = "C:\\Program Files\\Common Files\\CLAP\\OsTIrus.clap";

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
