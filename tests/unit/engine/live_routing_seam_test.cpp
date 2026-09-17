// Live-routing seam (plan docs/plans/2026-09-16-matrix-preset-engine-fixes.md).
//
// Root cause proven live 2026-09-16: a deviceless session consumes the
// coalesced async rebuild while the live projection is still null
// (MainAudioProcessor::rebuildRoutingGraph no-ops without a RoutingManager),
// so every live-graph consumer fails right after add_track /
// add_instrument_part:
//   sendFxMidi             -> "track not found: N" (load_nord_bank /
//                                                     load_virus_preset via
//                                                     PresetRoute)
//   PluginParamServiceImpl -> {} params           (list_fx_params /
//                                                     set_fx_param)
// AudioEngine::ensureLiveRouting settles the seam on demand: immediate check
// -> drainPendingRoutingRebuild() -> ONE bounded full rebuild.
//
// Gates (plan G1/G2):
//   G1: after a deferred add (NO explicit drain), sendFxMidi resolves the
//       track and a real isolated plugin slot lists non-empty params — the
//       deviceless skips in fx_midi_injection_test ("param cache unavailable
//       in deviceless environment") become hard assertions here.
//   G2: ensureLiveRouting is a no-op when the graph is already settled, and
//       bounded to one full rebuild per call when it is not.
#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "common/ProjectCommands.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"

namespace {

constexpr const char* kTyrellN6Clap = "C:\\Program Files\\Common Files\\CLAP\\u-he\\TyrellN6.clap";

bool clapAvailable()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    if (s.trim().isEmpty() || s.trim() == "0")
        return false;
    return juce::File(kTyrellN6Clap).existsAsFile();
}

struct SlotRef {
    int trackIndex = -1;
    int slotIndex = 0;
    std::string pluginId;
};

// keepTrack audition probe leaves a live track + isolated plugin slot behind
// (the MCP add_instrument_part equivalent) — clap_param_metadata precedent.
SlotRef createClapSlot(AudioEngine& engine)
{
    SlotRef ref;
    ProjectCommands::AuditionParams p;
    p.pluginId = kTyrellN6Clap;
    p.programIndex = -1;
    p.lengthBeats = 4.0;
    p.windowSeconds = 2.0;
    p.seed = 42;
    p.keepTrack = true;
    auto res = engine.getProjectCommands().auditionPlugin(p);
    EXPECT_TRUE(res.error.empty()) << res.error;
    EXPECT_TRUE(res.ok);
    if (!res.ok)
        return ref;
    ref.trackIndex = res.trackIndex;
    ref.slotIndex = res.slotIndex;
    auto fxSlots = engine.getReadModel().getFxSlots(res.trackIndex);
    if (static_cast<size_t>(res.slotIndex) < fxSlots.size())
        ref.pluginId = fxSlots[static_cast<size_t>(res.slotIndex)].pluginId;
    return ref;
}

} // namespace

// The new seam API itself: a deferred (never drained) track add must resolve.
TEST(LiveRoutingSeam, EnsureLiveRoutingResolvesDeferredTrack)
{
    AudioEngine engine;
    engine.initialize();
    const int idx = engine.getProjectCommands().addTrack("SeamDeferred");
    ASSERT_GE(idx, 0);
    // Lesson-9 deferral repro: deliberately NO drainPendingRoutingRebuild().
    EXPECT_TRUE(engine.ensureLiveRouting(idx));
    ASSERT_NE(engine.getMainProcessor(), nullptr);
    EXPECT_NE(engine.getMainProcessor()->getTrack(idx), nullptr);
    engine.shutdown();
}

// No-op when already settled: the fallback must never fire (counter delta 0),
// and negative indices are rejected without any rebuild.
TEST(LiveRoutingSeam, EnsureLiveRoutingIsNoOpWhenSettled)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(engine.getProjectCommands().addTrack("SeamSettled"), 0);
    engine.drainPendingRoutingRebuild(); // conventional explicit settle
    const uint64_t before = engine.debugLiveRoutingRebuilds();
    EXPECT_TRUE(engine.ensureLiveRouting(0));
    EXPECT_EQ(engine.debugLiveRoutingRebuilds(), before);
    EXPECT_TRUE(engine.ensureLiveRouting(0));
    EXPECT_EQ(engine.debugLiveRoutingRebuilds(), before);
    EXPECT_FALSE(engine.ensureLiveRouting(-1));
    EXPECT_EQ(engine.debugLiveRoutingRebuilds(), before);
    engine.shutdown();
}

// Bounded: at most ONE full rebuild per call, even for a track that can
// never exist (no loops, no unbounded retry).
TEST(LiveRoutingSeam, EnsureLiveRoutingBoundedToOneRebuildPerCall)
{
    AudioEngine engine;
    engine.initialize();
    const uint64_t before = engine.debugLiveRoutingRebuilds();
    EXPECT_FALSE(engine.ensureLiveRouting(999));
    EXPECT_EQ(engine.debugLiveRoutingRebuilds(), before + 1);
    EXPECT_FALSE(engine.ensureLiveRouting(999));
    EXPECT_EQ(engine.debugLiveRoutingRebuilds(), before + 2);
    engine.shutdown();
}

// sendFxMidi end-to-end WITHOUT a plugin: the track must resolve (the
// pre-fix deviceless failure was "track not found: N"); the missing plugin
// slot is reported instead — error-text contract preserved.
TEST(LiveRoutingSeam, SendFxMidiSettlesDeferredTrackWithoutPlugin)
{
    AudioEngine engine;
    engine.initialize();
    const int idx = engine.getProjectCommands().addTrack("SeamNoPlugin");
    ASSERT_GE(idx, 0);
    // NO drain — the deferral repro.
    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = idx;
    mp.slotIndex = 0;
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 74, 64});
    const auto mr = engine.getProjectCommands().sendFxMidi(mp);
    EXPECT_FALSE(mr.ok) << "a fresh track has no plugin slot 0";
    EXPECT_EQ(mr.queued, 0);
    EXPECT_EQ(mr.error.find("track not found"), std::string::npos)
        << "track must resolve via ensureLiveRouting (error: " << mr.error << ")";
    engine.shutdown();
}

// G1 core: after a deferred add_track + isolated plugin (NO drain),
// sendFxMidi resolves AND the slot lists non-empty params. Pre-fix
// (deviceless) both failed — the exact live list_fx_params {} repro.
TEST(LiveRoutingSeam, DeferredAddThenSendFxMidiAndParamsResolve)
{
    if (!clapAvailable())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";
    AudioEngine engine;
    engine.initialize();
    SlotRef ref = createClapSlot(engine);
    ASSERT_GE(ref.trackIndex, 0);
    ASSERT_FALSE(ref.pluginId.empty());
    // NO drainPendingRoutingRebuild() here — that is the seam under test.
    ProjectCommands::FxMidiParams mp;
    mp.trackIndex = ref.trackIndex;
    mp.slotIndex = ref.slotIndex;
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ControlChange, 1, 0, 0});
    mp.events.push_back({ProjectCommands::FxMidiEvent::Kind::ProgramChange, 1, 40, 0});
    const auto mr = engine.getProjectCommands().sendFxMidi(mp);
    ASSERT_TRUE(mr.ok) << mr.error;
    EXPECT_EQ(mr.queued, 2);

    const auto params = engine.getPluginParamService().getParams(ref.trackIndex, ref.pluginId);
    ASSERT_FALSE(params.empty()) << "isolated plugin slot listed zero params after a deferred add";
    engine.shutdown();
}
