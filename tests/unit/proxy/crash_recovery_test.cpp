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
