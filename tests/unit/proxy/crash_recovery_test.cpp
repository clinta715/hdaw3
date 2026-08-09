#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/PluginManager.h"
#include "engine/CrashRecoveryManager.h"
#include "proxy/PluginProxySlot.h"
#include "proxy/ProxyProcessManager.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>

#if HDAW_PLUGIN_ISOLATION

TEST(CrashRecovery, AutoRespawnAfterCrash) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    HDAW::PluginManager pm;

    juce::PluginDescription desc;
    desc.name = "Passthrough";
    desc.fileOrIdentifier = "__passthrough__";

    juce::String error;
    auto instance = pm.createPluginInstance(desc, error, 44100.0, 512, true);
    ASSERT_NE(instance, nullptr) << error.toStdString();

    auto* proxy = dynamic_cast<proxy::PluginProxySlot*>(instance.get());
    ASSERT_NE(proxy, nullptr);
    uint32_t originalSlotId = proxy->getSlotId();

    proxy->prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    proxy->processBlock(buffer, midi);

    proxy->saveStateToTemp();

    pm.killProxyForTesting(originalSlotId);

    bool recovered = false;
    for (int i = 0; i < 120; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        pm.recovery().tick();
        if (!proxy->isCrashed()) {
            recovered = true;
            break;
        }
    }
    EXPECT_TRUE(recovered) << "Plugin did not auto-respawn within 30s";

    if (recovered) {
        juce::AudioBuffer<float> buffer2(2, 512);
        buffer2.clear();
        proxy->processBlock(buffer2, midi);
    }

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (i % 10 == 5) pm.recovery().tick();
    }
    EXPECT_FALSE(proxy->isCrashed())
        << "Respawned child died shortly after respawn (empty plugin path?)";
}

// Regression gate for the respawn->migrate use-after-free. The fix holds
// graphLock across killPluginHost (which frees the old ShmRegion via the
// child-map erase) AND migrateToNewSlot (which swaps shmHandle). The
// "audio thread" here mirrors MainAudioProcessor::processBlock exactly: it
// tryEnter()s graphLock and skips processBlock on contention (silence). With
// the fix, a respawn happening concurrently cannot free the region out from
// under an in-flight processBlock; without the fix (lock taken only around
// migrate), the kill races the audio thread's shmHandle dereference -> UAF.
//
// The respawn is triggered directly on a LIVE slot (not a crashed one) so the
// proxy's `crashed` flag stays false and the background processBlock keeps
// actively reading/writing the shm rings, maximising overlap with the freed
// window. KillHard erases the child from the map before TerminateProcess, so
// the health monitor never fires onChildCrashed for this kill.
//
// Determinism note: the UAF is a narrow timing race, so this is a *gate*
// (overlapping load + direct respawn, repeated), not a guaranteed reproducer.
// A regression typically surfaces here as a process crash (gtest reports it as
// a death/crash); the test also asserts that audio resumes non-silent on the
// new region afterward.
TEST(CrashRecovery, RespawnDuringActiveProcessing) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    HDAW::PluginManager pm;
    // Wire graphLock so the fix's critical section is exercised and the test
    // mirrors MainAudioProcessor::processBlock's tryEnter discipline. Without
    // this, graphLockPtr is null and the lock would have no effect here.
    juce::SpinLock graphLock;
    pm.setGraphLock(&graphLock);

    juce::PluginDescription desc;
    desc.name = "Passthrough";
    desc.fileOrIdentifier = "__passthrough__";

    juce::String error;
    auto instance = pm.createPluginInstance(desc, error, 44100.0, 512, true);
    ASSERT_NE(instance, nullptr) << error.toStdString();

    auto* proxy = dynamic_cast<proxy::PluginProxySlot*>(instance.get());
    ASSERT_NE(proxy, nullptr);

    proxy->prepareToPlay(44100.0, 512);

    // Pre-warm the child pipeline so it is actively producing output before
    // the concurrent phase begins.
    {
        proxy::setRenderMode(true);
        juce::AudioBuffer<float> warm(2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 8; ++i) {
            for (int ch = 0; ch < warm.getNumChannels(); ++ch)
                for (int s = 0; s < warm.getNumSamples(); ++s)
                    warm.setSample(ch, s, 0.4f);
            proxy->processBlock(warm, midi);
        }
        proxy::setRenderMode(false);
    }

    // Background "audio thread": sustained processBlock load that respects
    // graphLock exactly like MainAudioProcessor::processBlock (tryEnter; skip
    // to silence on contention). It is the SOLE processBlock caller; respawn
    // runs on the main thread, so the only concurrency is processBlock
    // (reading shm) vs respawn's kill+migrate (freeing+swapping shm).
    std::atomic<bool> stop{ false };
    std::thread audio([&] {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        while (!stop.load(std::memory_order_relaxed)) {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int s = 0; s < buf.getNumSamples(); ++s)
                    buf.setSample(ch, s,
                        0.4f * std::sin(2.0 * 3.14159265 * 220.0 * s / 44100.0));
            // Mirrors MainAudioProcessor::processBlock: bail to silence while
            // respawn holds the lock across kill+migrate.
            if (!graphLock.tryEnter()) continue;
            proxy->processBlock(buf, midi);
            graphLock.exit();
        }
    });

    // Let the audio thread ramp up, then repeatedly respawn the LIVE slot
    // directly -- the faithful UAF trigger. Each respawn kills the current
    // child (freeing its ShmRegion) and migrates to a fresh one.
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(pm.respawnIsolatedSlot(proxy->getSlotId(), "__passthrough__"))
            << "respawnIsolatedSlot failed on iteration " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    stop.store(true, std::memory_order_relaxed);
    audio.join();
    // Reaching here means no access violation occurred during the concurrent
    // respawn+processBlock window. (A UAF regression would typically crash the
    // process before this point; gtest would report it as a test death.)

    // After respawn: audio must resume, non-silent, from the new region.
    proxy::setRenderMode(true);
    bool nonSilent = false;
    juce::AudioBuffer<float> verify(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 25; ++i) {
        for (int ch = 0; ch < verify.getNumChannels(); ++ch)
            for (int s = 0; s < verify.getNumSamples(); ++s)
                verify.setSample(ch, s,
                    0.4f * std::sin(2.0 * 3.14159265 * 440.0 * s / 44100.0));
        proxy->processBlock(verify, midi);
        float peak = 0.0f;
        for (int ch = 0; ch < verify.getNumChannels(); ++ch)
            for (int s = 0; s < verify.getNumSamples(); ++s)
                peak = (std::max)(peak, std::abs(verify.getSample(ch, s)));
        if (peak > 0.01f) { nonSilent = true; break; }
    }
    proxy::setRenderMode(false);
    EXPECT_TRUE(nonSilent)
        << "audio did not resume non-silent on the new region after respawn";
}

TEST(CrashRecovery, GivesUpAfterThreeFailures) {
    HDAW::CrashRecoveryManager crm;

    crm.setRespawnFn([](uint32_t, const juce::String&) { return false; });

    std::atomic<bool> gaveUp{false};
    crm.setGiveUpFn([&](uint32_t, const juce::String&) { gaveUp.store(true); });

    crm.onSlotCrashed(9999, "FailingPlugin", "/bad/path");

    for (int i = 0; i < 100 && !gaveUp.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        crm.tick();
    }

    EXPECT_TRUE(gaveUp.load()) << "CrashRecovery should give up after 3 failed attempts";
}

TEST(ProxyHealth, IdleChildNotKilledByStallDetector) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    proxy::ProxyProcessManager ppm;
    uint32_t slotId = 7777;
    ASSERT_TRUE(ppm.spawnPluginHost("__passthrough__", slotId));

    std::atomic<bool> crashFired{false};
    ppm.setSlotCrashCallback(slotId, [&](uint32_t) { crashFired.store(true); });

    // Let the child idle (no blocks fed) for longer than the stall threshold.
    // A healthy idle child — e.g. while the transport is stopped — must NOT
    // be flagged as hung.
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ppm.checkAllChildren(100); // aggressive 100ms threshold
    }
    EXPECT_FALSE(crashFired.load())
        << "Idle child with no pending input was falsely flagged as stalled";

    ppm.killPluginHost(slotId, proxy::KillMode::KillHard);
}

#endif
