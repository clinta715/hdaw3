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

// Regression gate for the export-path proxy-lifetime UAF. The fix:
// ~PluginProxySlot fires a destruction callback that erases the slot from
// PluginManager::liveProxySlots and cancels its CrashRecovery entry. Without
// this, destroying an export-path proxy leaves a dangling liveProxySlots
// entry, and a pending respawn then dereferences the freed proxy -> 0xC0000005.
//
// This test: creates an isolated proxy via PluginManager, then releases it
// (simulating export renderGraph teardown), then attempts a respawn. The
// respawn must return false (map entry gone) and must not crash.
TEST(CrashRecovery, DestroyedProxyIsDeregistered) {
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
    uint32_t slotId = proxy->getSlotId();

    // Enqueue a crash-recovery entry so we can verify it gets canceled on
    // destruction.
    pm.recovery().onSlotCrashed(slotId, "TestPlugin", "__passthrough__");

    // Destroy the proxy (simulates renderGraph teardown at export end).
    // ~PluginProxySlot fires the destruction callback -> erases from
    // liveProxySlots + cancels the recovery entry.
    instance.reset();
    proxy = nullptr;

    // Respawn must return false: the slot is gone from liveProxySlots.
    // Without the fix, this would find a dangling pointer and dereference it.
    bool ok = pm.respawnIsolatedSlot(slotId, "__passthrough__");
    EXPECT_FALSE(ok) << "respawnIsolatedSlot should return false after the "
                        "proxy was destroyed and deregistered";

    // Also exercise the Timer-driven path: tick() → attemptRespawn →
    // respawnFn(slotId, path). The entry was canceled by the destruction
    // callback, so tick() must find nothing for this slot and not crash.
    pm.recovery().tick();

    // No crash reaching here proves the dangling-entry UAF is closed.
}

// Regression gate for respawn suppression during export. CrashRecoveryManager
// must NOT attempt respawn while respawnEnabled is false (export duration).
// The entry stays pending and is cancelable by the destruction callback.
TEST(CrashRecovery, RespawnSuppressedWhenDisabled) {
    HDAW::CrashRecoveryManager crm;

    int respawnCalls = 0;
    crm.setRespawnFn([&](uint32_t, const juce::String&) {
        ++respawnCalls;
        return true;
    });

    crm.onSlotCrashed(8888, "Suppressed", "/fake/path");

    // Suppress and tick multiple times — respawn must NOT fire.
    crm.respawnEnabled.store(false, std::memory_order_relaxed);
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        crm.tick();
    }
    EXPECT_EQ(respawnCalls, 0)
        << "respawn must not fire while respawnEnabled is false";

    // Re-enable and tick — respawn should now proceed.
    crm.respawnEnabled.store(true, std::memory_order_relaxed);
    bool recovered = false;
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        crm.tick();
        if (respawnCalls > 0) { recovered = true; break; }
    }
    EXPECT_TRUE(recovered) << "respawn should fire after re-enabling";

    // Cancel must remove a pending entry so tick never fires respawnFn for it.
    crm.onSlotCrashed(7777, "Canceled", "/fake/path");
    crm.cancel(7777);
    int before = respawnCalls;
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        crm.tick();
    }
    EXPECT_EQ(respawnCalls, before)
        << "cancel() must prevent any respawn attempt for that slot";
}

// Isolation-of-the-export gate: an offline (export) PluginManager must live in
// its OWN plugin domain — its own ProxyProcessManager (children, crash
// callbacks, health monitor), its own slot counter (which restarts at 1 while
// live slots 1..N exist), its own pipe/shm names, and its own crash-state
// files. Before the fix the export reused the LIVE PluginManager's machinery,
// so live FX rebuilds during export could kill the export's children (slot-id
// collision) and wedge the render. This test drives two domains concurrently
// and asserts every seam is disjoint:
//   - the offline domain spawns children under the live domain's nose (same
//     slot ids — the collision the fix prevents);
//   - the live PM must NOT know the offline slots (respawnIsolatedSlot must
//     return false for them — a shared registry would find and respawn them);
//   - crash-state files are keyed per domain (a save from the offline domain
//     must not overwrite the live slot's file with the same id);
//   - destroying the offline domain must not disturb the live child.
TEST(CrashRecovery, OfflinePluginDomainIsolatedFromLive) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    HDAW::PluginManager livePm;

    juce::PluginDescription desc;
    desc.name = "Passthrough";
    desc.fileOrIdentifier = "__passthrough__";

    // Live domain: one isolated instance (slot id 1 — the counter starts at 1).
    juce::String liveError;
    auto liveInstance = livePm.createPluginInstance(desc, liveError, 44100.0, 512, true);
    ASSERT_NE(liveInstance, nullptr) << liveError.toStdString();
    auto* liveProxy = dynamic_cast<proxy::PluginProxySlot*>(liveInstance.get());
    ASSERT_NE(liveProxy, nullptr);
    const uint32_t liveSlotId = liveProxy->getSlotId();
    liveProxy->prepareToPlay(44100.0, 512);

    // Offline (export) domain: seeded from the live PM, own namespace.
    auto offlinePm = HDAW::PluginManager::createOfflineCopy(livePm);
    ASSERT_NE(offlinePm, nullptr);
    offlinePm->setProxyNamespacePrefix("export_");
    // Copy must carry the plugin list so resolveIdentifierToPath works offline.
    EXPECT_EQ(offlinePm->getPlugins().size(), livePm.getPlugins().size());

    // The offline counter restarts at 1 — the exact id space collision the
    // shared-domain bug hit (live slot 1 and export slot 1 coexist now).
    juce::String err1, err2;
    auto offlineInst1 = offlinePm->createPluginInstance(desc, err1, 44100.0, 512, true);
    ASSERT_NE(offlineInst1, nullptr) << err1.toStdString();
    auto* offlineProxy1 = dynamic_cast<proxy::PluginProxySlot*>(offlineInst1.get());
    ASSERT_NE(offlineProxy1, nullptr);
    EXPECT_EQ(offlineProxy1->getSlotId(), liveSlotId)
        << "offline slot id should collide numerically with the live slot id; "
           "the domains must still be disjoint";
    offlineProxy1->prepareToPlay(44100.0, 512);

    auto offlineInst2 = offlinePm->createPluginInstance(desc, err2, 44100.0, 512, true);
    ASSERT_NE(offlineInst2, nullptr) << err2.toStdString();
    auto* offlineProxy2 = dynamic_cast<proxy::PluginProxySlot*>(offlineInst2.get());
    ASSERT_NE(offlineProxy2, nullptr);
    offlineInst2->prepareToPlay(44100.0, 512);

    // Crash-state files are per-domain: a save from the offline slot with the
    // SAME id must not clobber the live slot's state file.
    {
        auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const auto liveStateFile = tempDir.getChildFile(
            "hdaw_proxy_state_" + juce::String((int)liveSlotId) + ".bin");
        const auto exportStateFile = tempDir.getChildFile(
            "hdaw_proxy_state_export_" + juce::String((int)offlineProxy1->getSlotId()) + ".bin");
        liveStateFile.deleteFile();
        exportStateFile.deleteFile();

        liveProxy->saveStateToTemp();
        ASSERT_TRUE(liveStateFile.existsAsFile())
            << "live slot state file should exist after saveStateToTemp";
        const auto liveContentBefore = liveStateFile.loadFileAsString();

        offlineProxy1->saveStateToTemp();
        ASSERT_TRUE(exportStateFile.existsAsFile())
            << "offline slot state file should exist after saveStateToTemp";
        EXPECT_EQ(liveStateFile.loadFileAsString(), liveContentBefore)
            << "offline slot save must not overwrite the live slot's state file "
               "(same numeric slot id, different domains)";

        // Domain-scoped cleanup: clearing the offline slot leaves the live
        // slot's file untouched.
        offlineProxy1->clearStateForSlot(offlineProxy1->getSlotId());
        EXPECT_FALSE(exportStateFile.existsAsFile());
        EXPECT_TRUE(liveStateFile.existsAsFile());

        liveProxy->clearStateForSlot(liveSlotId);
        liveStateFile.deleteFile();
        exportStateFile.deleteFile();
    }

    // The live PM must not know the offline domain's slots: a respawn lookup
    // of a slot that exists ONLY in the offline domain (the second offline
    // instance — the first one numerically collides with the live slot, which
    // legitimately belongs to the live PM) must fail. With a shared registry
    // it would succeed and spawn a host — the cross-domain leak the fix
    // removes. It must also return WITHOUT spawning or migrating anything.
    const uint32_t offlineOnlySlotId = offlineProxy2->getSlotId();
    ASSERT_NE(offlineOnlySlotId, liveSlotId) << "test premise broken";
    EXPECT_FALSE(livePm.respawnIsolatedSlot(offlineOnlySlotId, "__passthrough__"))
        << "live PM must not have the offline domain's slots in its registry";
    // The failed lookup must not have disturbed either domain's children.
    EXPECT_FALSE(offlineProxy2->isCrashed());
    EXPECT_FALSE(liveProxy->isCrashed());

    // Destroy the offline domain (render graph teardown at export end):
    // instances first (they reference the offline PM), then the PM, which
    // terminates any remaining children of ITS OWN domain.
    offlineInst2.reset();
    offlineInst1.reset();
    offlinePm.reset();

    // The live child must be untouched: not crashed, and still producing
    // audio through the live slot.
    EXPECT_FALSE(liveProxy->isCrashed())
        << "offline domain teardown must not kill the live child";

    proxy::setRenderMode(true);
    bool liveStillProducesAudio = false;
    juce::AudioBuffer<float> verify(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 25; ++i) {
        for (int ch = 0; ch < verify.getNumChannels(); ++ch)
            for (int s = 0; s < verify.getNumSamples(); ++s)
                verify.setSample(ch, s,
                    0.4f * std::sin(2.0 * 3.14159265 * 440.0 * s / 44100.0));
        liveProxy->processBlock(verify, midi);
        float peak = 0.0f;
        for (int ch = 0; ch < verify.getNumChannels(); ++ch)
            for (int s = 0; s < verify.getNumSamples(); ++s)
                peak = (std::max)(peak, std::abs(verify.getSample(ch, s)));
        if (peak > 0.01f) { liveStillProducesAudio = true; break; }
    }
    proxy::setRenderMode(false);
    EXPECT_TRUE(liveStillProducesAudio)
        << "live slot must keep processing audio after the offline domain "
           "was created and destroyed alongside it";
}

#endif
