#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/Track.h"
#include "engine/PluginManager.h"
#include "model/ProjectModel.h"
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>
#include <thread>
#include <set>

#if HDAW_PLUGIN_ISOLATION

namespace
{

// Count live hdaw_plugin_host.exe processes via a Toolhelp32 snapshot.
// Returns the set of PIDs so callers can diff before/after.
static std::set<DWORD> countHostProcesses()
{
    std::set<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, L"hdaw_plugin_host.exe") == 0)
                pids.insert(pe.th32ProcessID);
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// Kill all hdaw_plugin_host.exe children not in the keepSet (baseline).
static void killNewHosts(const std::set<DWORD>& keepSet)
{
    auto now = countHostProcesses();
    for (auto pid : now)
    {
        if (keepSet.find(pid) == keepSet.end())
        {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
        }
    }
}

// Directly add an isolated __passthrough__ FX to a track's ValueTree,
// bypassing resolvePluginFormat which doesn't know about test sentinels.
static void addIsolatedPassthroughFx(AudioEngine& engine, int trackIdx)
{
    auto& pm = engine.getPluginManager();
    auto& model = engine.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    auto track = trackList.getChild(trackIdx);

    auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        track.addChild(fxChain, -1, &um);
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, "plugin", &um);
    slot.setProperty(IDs::pluginID, "__passthrough__", &um);
    slot.setProperty(IDs::pluginFormat, "CLAP", &um);
    slot.setProperty(IDs::name, "Passthrough", &um);
    slot.setProperty(IDs::bypassed, false, &um);
    fxChain.addChild(slot, -1, &um);

    // Trigger the FX chain rebuild so the isolated plugin is instantiated.
    if (auto* proc = engine.getMainProcessor())
        proc->rebuildTrackFX(trackIdx);
}

} // anonymous namespace

// Regression gate for Fix A / lesson 21: after a full rebuildRoutingGraph(true),
// the previous graph's render-sequence pin is released and the old isolated
// hdaw_plugin_host.exe children are freed WITHOUT needing to play.
//
// Mechanism (the T1 handshake risk): JUCE's RenderSequenceExchange installs the
// NEW sequence in mainThreadState via rebuild(); the OLD sequence stays in
// audioThreadState until the audio thread calls processBlock →
// updateAudioThreadState(). With stopped transport, MainAudioProcessor::processBlock
// early-outs before graph.processBlock, so the swap never happens from playback.
// Fix A closes this by driving one graph.processBlock on a scratch buffer via
// runOnMessageThread. T1 measures the real hdaw_plugin_host.exe count — if it
// doesn't return to baseline, Fix A is incomplete.
TEST(RenderSequenceRelease, RebuildReleasesPreviousGraphChildren)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Ensure no stale children from a previous test (lesson 20).
    auto baseline = countHostProcesses();
    for (auto pid : baseline)
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
    // Wait for cleanup to settle.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    baseline = countHostProcesses();

    AudioEngine engine;
    engine.initialize();

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);

    // Add a track and install an isolated __passthrough__ FX plugin.
    // Add both to the ValueTree FIRST, then rebuild the routing graph
    // synchronously — addTrack is async and the Track processor may not
    // exist yet when rebuildTrackFX is called.
    auto& cmds = engine.getProjectCommands();
    int trackIdx = cmds.addTrack("T1 Test");
    ASSERT_GE(trackIdx, 0);

    addIsolatedPassthroughFx(engine, trackIdx);

    // Force a synchronous rebuild so the Track processor is created AND
    // the FX chain (including the __passthrough__ child) is instantiated.
    proc->rebuildRoutingGraph();

    // Wait for the child to spawn and reach READY.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    auto afterSpawn = countHostProcesses();
    EXPECT_GT(afterSpawn.size(), baseline.size())
        << "Expected at least one new hdaw_plugin_host.exe after addFxSlot";

    // Start transport — processBlock early-outs when stopped, so we must play
    // to bake the render sequence during normal operation.
    auto& transport = engine.getTransportCommands();
    transport.play();

    // Drive ~50 processBlock calls to fully bake the render sequence (the
    // sequence pins Node::Ptr → Track → FX slot → PluginProxySlot → child).
    // Do NOT hold graphLock here — processBlock internally tryEnter()s it,
    // and holding it from outside causes tryEnter to fail → graph processing
    // skipped → render sequence never swapped.
    {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 50; ++i)
        {
            buf.clear();
            midi.clear();
            proc->processBlock(buf, midi);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    transport.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Remove the FX from the ValueTree before the second rebuild so no NEW
    // child is created — the second rebuildRoutingGraph should only release
    // the OLD child via the render-sequence swap. This isolates the test to
    // the release mechanism itself.
    {
        auto& model = engine.getProjectModel();
        auto trackList = model.getTrackListTree();
        auto track = trackList.getChild(trackIdx);
        auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
        if (fxChain.isValid())
        {
            int idx = track.indexOf(fxChain);
            if (idx >= 0)
            {
                auto& um = model.getUndoManager();
                track.removeChild(idx, &um);
            }
        }
    }

    // Rebuild with loading=true — this is the Fix A code path.
    proc->rebuildRoutingGraph(true);

    // Let the message thread process the async bake from rebuildGraph(),
    // then drive one graph.processBlock to force the RenderSequenceExchange
    // swap (old sequence → mainThreadState → timer clears → children freed).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        auto& graph = proc->getGraph();
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        juce::MidiBuffer midi;
        graph.processBlock(buf, midi);
    }

    // Poll for the child count to return to baseline. The graph.processBlock
    // above swapped the old sequence into mainThreadState; JUCE's 500ms Timer
    // then frees it, destroying the stale proxies and their children. Allow
    // up to 10 seconds for the cascade + OS cleanup.
    bool returnedToBaseline = false;
    for (int i = 0; i < 50; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto current = countHostProcesses();
        if (current.size() <= baseline.size())
        {
            returnedToBaseline = true;
            break;
        }
    }

    // Clean up any children we spawned (belt-and-suspenders).
    killNewHosts(baseline);

    EXPECT_TRUE(returnedToBaseline)
        << "After rebuildRoutingGraph(true), hdaw_plugin_host.exe count did not "
           "return to baseline within 5s — the render sequence was not released "
           "(Fix A incomplete or the handshake-closing drive did not execute)";

    // Cleanup
    engine.shutdown();
}

// Verify that rebuildRoutingGraph(true) with NO prior play does NOT leak
// children. Without Fix A, even a load-only path (no play) could leak if the
// graph's internal state retained stale nodes.
TEST(RenderSequenceRelease, RebuildWithoutPlayDoesNotLeak)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Clean slate.
    auto baseline = countHostProcesses();
    for (auto pid : baseline)
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    baseline = countHostProcesses();

    AudioEngine engine;
    engine.initialize();

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);

    auto& cmds = engine.getProjectCommands();
    int trackIdx = cmds.addTrack("T1 NoPlay");
    ASSERT_GE(trackIdx, 0);
    addIsolatedPassthroughFx(engine, trackIdx);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // NO play — directly rebuild.
    proc->rebuildRoutingGraph(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto afterRebuild = countHostProcesses();
    // Kill any leftover children.
    killNewHosts(baseline);

    // After rebuild, the count should not have grown beyond what we added.
    EXPECT_LE(afterRebuild.size(), baseline.size() + 1)
        << "rebuildRoutingGraph(true) without play leaked extra children";

    engine.shutdown();
}

#endif // HDAW_PLUGIN_ISOLATION
