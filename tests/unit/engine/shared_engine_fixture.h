#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// shared_engine_fixture.h — ONE AudioEngine per test SUITE, project reset per test.
//
// WHY
//   Measured 2026-09-25: per-test cost across tests/ is dominated by ENGINE
//   CONSTRUCTION, not test logic (a no-engine DSP test is ~1 ms; the Commands
//   suite averaged ~0.88 s/test, BusSendRpcTest ~0.55 s/test, and there are 584
//   `engine.initialize()` call sites in tests/). AudioEngine::initialize() opens
//   the audio device, loads the plugin cache, prepares the graph and constructs
//   ~15 subsystems — every one of those is repeated for every test that owns a
//   local engine.
//
// WHAT MAKES SHARING POSSIBLE
//   AudioEngineCommands::newProject() (= ProjectSerializer::createNew() + the
//   default tempo/time-signature from settings + rebuildRoutingGraph()) is a
//   COMPLETE project reset on a LIVE engine: ProjectModel::createDefaultProject()
//   drops every child and property of the project tree, clears the undo history
//   and resets the clip-id counter. The engine's own ValueTree listener then
//   re-pushes the new TRANSPORT node's playing/loop/arranger state. So a suite
//   can keep ONE engine and call newProject() once per test.
//
// INVARIANTS (a suite that opts in MUST hold all of them)
//   1. ONE engine for the whole process: sharedEngine() builds and initialize()s
//      the engine exactly once (function-local static) and never destroys it.
//      It is deliberately process-lifetime: the test binary's QCoreApplication
//      is a stack object in main(), so a static engine would be destroyed AFTER
//      Qt and after the JUCE MessageManager are gone. Existing tests dodge that
//      by destroying their engine inside the test body; a suite-level engine
//      cannot, so it lives until process exit (the OS reclaims it).
//      Consequence, intended: with --gtest_repeat=N the SAME engine is reused
//      run over run — that is the cross-run isolation proof, not a bug.
//   2. PROJECT RESET per test: SharedEngineSuite::SetUp() calls newProject()
//      and then restores the few pieces of state a FRESHLY CONSTRUCTED engine
//      holds that the project tree does not own — see resetSharedEngineProject().
//   3. NO CROSS-TEST STATE: a test converted onto this fixture must not depend
//      on anything the previous test left in the engine OUTSIDE the project
//      tree. Everything the fixture restores is listed in
//      resetSharedEngineProject(); anything NOT listed is a known bleed risk
//      and must be fixed in the fixture (or the test left unconverted) rather
//      than papered over in the test body.
//   4. NO VIRGIN-PROCESS DEPENDENCE: a test that asserts on something
//      initialize() does ONCE and newProject() cannot restore does NOT belong on
//      this fixture — keep it as a plain TEST with its own local engine.
//
// ROLLOUT SCOPE
//   sharedEngine() is ONE engine per PROCESS, so several converted suites in the
//   same binary share it deliberately (that is where the saving is largest) and
//   the per-test contract above is the only thing keeping them apart. Two kinds
//   of suite must therefore NOT use this fixture:
//     * a suite that needs a DIFFERENT initialize()-time configuration — e.g. the
//       incremental-routing tests, for which initialize() reads
//       HDAW_FORCE_INCREMENTAL_ROUTING once, and any suite that installs its own
//       audio device setup; it must keep a private engine;
//     * a suite whose assertions read engine state that newProject() cannot
//       restore (see resetSharedEngineProject() and the risk list in the header
//       of the converted suite) — convert it only after that state is added to
//       the reset, or leave it unconverted.
//
// USAGE (no CMake change: header-only)
//   // my_suite_test.cpp
//   #include "shared_engine_fixture.h"      // or "../engine/shared_engine_fixture.h"
//
//   class MySuite : public hdaw_test::SharedEngineSuite {};   // suite NAME = class name
//
//   TEST_F(MySuite, Something)
//   {
//       auto& cmds = engine.getProjectCommands();   // `engine` is the AudioEngine&
//       ...
//   }
//
//   The old per-test prologue (`AudioEngine engine; engine.initialize();`) is
//   deleted verbatim and nothing else in the body changes: `engine` resolves to
//   the shared engine through the fixture member. gtest derives the SUITE NAME
//   from the FIXTURE CLASS name, so naming the fixture class after the existing
//   suite keeps every --gtest_filter working unchanged.
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>

#include <juce_events/juce_events.h>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"

namespace hdaw_test {

// The suite/process-wide engine. Initialized exactly once; never destroyed
// (invariant 1). Safe to call from any test: after the first call it is a
// function-local-static read.
inline AudioEngine& sharedEngine()
{
    static AudioEngine& engine = *[]() -> AudioEngine* {
        auto* e = new AudioEngine();
        e->initialize();

        // Live-graph seam. On a machine with no usable audio endpoint (measured
        // on the dev box: `Error opening Primary Sound Driver: "No driver"`)
        // AudioEngine::initialize() never reaches
        // AudioProcessorPlayer::setProcessor()'s prepareToPlay (JUCE guards it
        // with `sampleRate > 0`), so
        // MainAudioProcessor::getRoutingManager() stays null and every assertion
        // that reads the LIVE graph would fail for ENVIRONMENTAL reasons. Mirror
        // the device-open path explicitly, ONCE per process — the same seam the
        // engine tests already use for a deviceless harness
        // (send_test.cpp:ensureRoutingGraph, incremental_routing_ab_test.cpp).
        // With a working device open, getRoutingManager() is already non-null and
        // this is a no-op.
        if (auto* proc = e->getMainProcessor())
        {
            if (proc->getRoutingManager() == nullptr)
            {
                const juce::MessageManagerLock pumpPark;
                proc->prepareToPlay(44100.0, 512);
            }
        }
        return e;
    }();
    return engine;
}

// Restore, on a LIVE engine, the observable state a freshly constructed +
// initialize()d engine starts a test with.
//
// Owned by newProject() (project tree + everything ProjectModel::createDefaultProject()
// rebuilds): every track/clip/note/marker/send/song-plan/automation object, the
// undo history, the clip-id counter, the root name/tempo/masterGain and the
// TRANSPORT node (whose re-creation pushes playing=false, loop and arranger
// state into TransportManager via AudioEngine's ValueTree listener).
//
// NOT owned by newProject() — engine-owned state restored here explicitly:
inline void resetSharedEngineProject (AudioEngine& engine)
{
    engine.getProjectCommands().newProject();

    // Transport clock. createDefaultProject() sets the new TRANSPORT node's
    // properties BEFORE addChild(), so valueTreePropertyChanged never fires for
    // `position` and the sample clock keeps whatever the previous test seeked
    // to (a fresh engine starts at sample 0). `playing` IS re-pushed by the
    // child-added path, but set it explicitly so the reset does not depend on
    // that internals detail, and clear the one-shot audio-thread auto-stop flag.
    auto& tm = engine.getTransportManager();
    tm.setCurrentSample(0);
    tm.setPlaying(false);
    tm.consumeAutoStopRequested();

    // Input-record arm flags are engine atomics, not tree state: a test that
    // armed CC/note recording and never disarmed would otherwise record into the
    // NEXT test. A fresh engine starts with both false.
    engine.setMidiCcRecordArmed(false);
    engine.setMidiNoteRecordArmed(false);

    // StretchCache entries are keyed by (clipID, ratio, sampleRate) and
    // createDefaultProject() restarts the clip-id counter at 1, so a clip that a
    // previous test stretched could hand its rendered buffer to a DIFFERENT clip
    // that happens to get the recycled id (the cache is only invalidated on a
    // tempo change, AudioEngine.cpp:1009 — not on clip removal). Empty it.
    engine.getStretchCache().clear();

    // Settle the coalesced async routing rebuild raised by newProject()'s
    // rebuildRoutingGraph() and the tree listeners, so the next test starts on a
    // settled projection (the same explicit-drain contract every existing test
    // uses; lessons 9/10/12 — no sleeps).
    engine.drainPendingRoutingRebuild();
}

// TEST_F base: gives a suite its single shared engine plus a per-test project
// reset. The converted suite names itself by deriving with the suite's name.
class SharedEngineSuite : public ::testing::Test
{
protected:
    void SetUp() override
    {
        resetSharedEngineProject(engine);
    }

    // The engine the old per-test prologue used to declare as a local:
    // `AudioEngine engine; engine.initialize();` becomes just `engine`.
    AudioEngine& engine{ sharedEngine() };
};

} // namespace hdaw_test
