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

#endif
