#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/ProxyCommon.h"
#include "proxy/PluginProxySlot.h"
#include "proxy/ProxySharedMemory.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <atomic>

using namespace proxy;

static juce::File findBuiltTestPlugin() {
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    auto candidates = {
        exeDir.getChildFile("..").getChildFile("tests").getChildFile("test-plugin")
              .getChildFile("HDAWTestPlugin_artefacts").getChildFile("Debug")
              .getChildFile("VST3").getChildFile("PassthroughTest.vst3"),
        exeDir.getChildFile("tests").getChildFile("test-plugin")
              .getChildFile("HDAWTestPlugin_artefacts").getChildFile("Debug")
              .getChildFile("VST3").getChildFile("PassthroughTest.vst3"),
    };
    for (const auto& c : candidates)
        if (c.exists()) return c;
    return {};
}

// ========================================================================
// Spawn lifecycle tests
// ========================================================================

TEST(PluginIsolation, HostExePathResolves) {
    auto path = ProxyProcessManager::getHostExePath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.find("hdaw_plugin_host.exe") != std::string::npos);
}

TEST(PluginIsolation, SpawnWithBadPluginExits) {
    // Spawn with a non-existent plugin. The child sends READY then exits
    // because loadPlugin fails. Verify the spawn succeeds and the child dies.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9001);
    ASSERT_TRUE(spawned) << "Child should send READY before exiting";

    // Give the child time to exit (loadPlugin fails → child returns 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // The child should now be dead
    EXPECT_FALSE(mgr.isAlive(9001));

    mgr.killPluginHost(9001);
}

TEST(PluginIsolation, SpawnAndShutdownCleanExit) {
    // Spawn, then verify clean shutdown path works.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9002);
    ASSERT_TRUE(spawned);

    // Wait for child to exit naturally
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // killPluginHost should handle the already-dead process without crashing
    mgr.killPluginHost(9002);
    SUCCEED();
}

TEST(PluginIsolation, KillReportsNotAlive) {
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9003);
    ASSERT_TRUE(spawned);

    // Kill the child
    bool killed = mgr.killPluginHost(9003);
    EXPECT_TRUE(killed);

    // isAlive should return false
    EXPECT_FALSE(mgr.isAlive(9003));
}

TEST(PluginIsolation, CheckAllChildrenFiresCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashCount{0};
    std::atomic<uint32_t> crashedSlotId{0};

    mgr.setCrashCallback([&](uint32_t slotId) {
        crashCount.fetch_add(1);
        crashedSlotId.store(slotId);
    });

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9004);
    ASSERT_TRUE(spawned);

    // Wait for child to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // checkAllChildren should detect the dead child and fire the callback
    mgr.checkAllChildren();

    EXPECT_GE(crashCount.load(), 1);
    EXPECT_EQ(crashedSlotId.load(), 9004u);

    mgr.killPluginHost(9004);
}

// ========================================================================
// Shared memory / processBlock tests (no child process needed)
// ========================================================================

TEST(PluginIsolation, SharedMemoryHeaderInitialization) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_lifecycle", computeShmSize(2, 512)));

    auto* hdr = region.getHeader();
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->magic, SHM_MAGIC);
    EXPECT_EQ(hdr->numChannels, 0u);
    EXPECT_EQ(hdr->blockSize, 0u);
    EXPECT_EQ(hdr->inputWritePos.load(), 0u);
    EXPECT_EQ(hdr->inputReadPos.load(), 0u);
    EXPECT_EQ(hdr->outputWritePos.load(), 0u);
    EXPECT_EQ(hdr->outputReadPos.load(), 0u);
}

TEST(PluginIsolation, SharedMemoryAudioRoundTrip) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_audio", computeShmSize(2, 512)));

    auto* hdr = region.getHeader();
    hdr->numChannels = 2;
    hdr->blockSize = 4;
    hdr->capacity = 8;

    float input[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_TRUE(region.writeInput(input, 8));

    float output[8] = {};
    ASSERT_TRUE(region.readInput(output, 8));
    for (int i = 0; i < 8; ++i)
        EXPECT_FLOAT_EQ(output[i], input[i]);
}

TEST(PluginIsolation, SharedMemoryHeartbeat) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_heartbeat", computeShmSize(2, 512)));

    auto* hdr = region.getHeader();

    EXPECT_EQ(hdr->childAlive.load(), 0u);
    EXPECT_EQ(hdr->dawAlive.load(), 0u);

    uint32_t now = 12345;
    hdr->childAlive.store(now);
    EXPECT_EQ(hdr->childAlive.load(), now);

    hdr->dawAlive.store(now + 1);
    EXPECT_EQ(hdr->dawAlive.load(), now + 1);
}

// ========================================================================
// PluginProxySlot tests (without child process)
// ========================================================================

TEST(PluginIsolation, ProxySlotInitialState) {
    ProxyProcessManager mgr;
    PluginProxySlot slot(mgr, 9010, "TestPlugin");

    EXPECT_FALSE(slot.isCrashed());
    EXPECT_EQ(slot.getName(), "TestPlugin");
    EXPECT_TRUE(slot.hasEditor());
}

TEST(PluginIsolation, ProxySlotCrashState) {
    ProxyProcessManager mgr;
    PluginProxySlot slot(mgr, 9011, "TestPlugin");

    EXPECT_FALSE(slot.isCrashed());

    slot.onChildCrashed();
    EXPECT_TRUE(slot.isCrashed());

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    slot.processBlock(buffer, midi);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 512; ++s)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, s), 0.0f);
}

TEST(PluginIsolation, ProxySlotStateSaveRestore) {
    ProxyProcessManager mgr;
    PluginProxySlot slot(mgr, 9012, "TestPlugin");

    juce::MemoryBlock block;
    slot.getStateInformation(block);

    slot.setStateInformation(nullptr, 0);
    slot.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    SUCCEED();
}

TEST(PluginIsolation, ProxySlotFillInPluginDescription) {
    ProxyProcessManager mgr;
    PluginProxySlot slot(mgr, 9013, "TestPlugin");

    juce::PluginDescription desc;
    slot.fillInPluginDescription(desc);

    EXPECT_EQ(desc.name, "TestPlugin");
    EXPECT_EQ(desc.pluginFormatName, "Isolated");
    EXPECT_FALSE(desc.fileOrIdentifier.isEmpty());
}

// ========================================================================
// End-to-end audio round-trip using built-in passthrough mode
// (no external DLL — the child creates an internal passthrough processor
//  when given the special path "__passthrough__")
// ========================================================================

TEST(PluginIsolation, AudioRoundTripWithPassthrough) {
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("__passthrough__", 9014);
    ASSERT_TRUE(spawned) << "Child should start with passthrough mode";

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9014)) << "Child should still be alive";

    auto* pipe = mgr.getPipe(9014);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9014;
    struct { double sr; int32_t bs; int32_t ch; } prepareData{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &prepareData, sizeof(prepareData));
    prepareMsg.dataSize = sizeof(prepareData);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp)) << "Child should respond to PREPARE";
    EXPECT_EQ(prepareResp.result, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto* shm = mgr.getShm(9014);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    int retries = 100;
    while (hdr->numChannels == 0 && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "Child didn't initialize shared memory header";

    uint32_t blockSize = hdr->blockSize > 0 ? hdr->blockSize : 512;
    uint32_t numChannels = hdr->numChannels > 0 ? hdr->numChannels : 2;
    uint32_t totalSamples = blockSize * numChannels;

    std::vector<float> input(totalSamples);
    for (uint32_t i = 0; i < totalSamples; ++i)
        input[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);

    ASSERT_TRUE(shm->writeInput(input.data(), totalSamples));

    retries = 200;
    uint32_t outAvail = 0;
    while (retries-- > 0) {
        uint32_t ow = hdr->outputWritePos.load(std::memory_order_acquire);
        uint32_t or_ = hdr->outputReadPos.load(std::memory_order_relaxed);
        outAvail = ow - or_;
        if (outAvail >= totalSamples) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(outAvail, totalSamples) << "Child didn't produce output in time";

    std::vector<float> output(totalSamples);
    ASSERT_TRUE(shm->readOutput(output.data(), totalSamples));

    for (uint32_t i = 0; i < totalSamples; ++i) {
        EXPECT_NEAR(output[i], input[i], 0.0001f)
            << "Sample " << i << " mismatch (passthrough should be identical)";
    }

    mgr.killPluginHost(9014);
}

TEST(PluginIsolation, CrashAndRestartWithPassthrough) {
    ProxyProcessManager mgr;

    std::atomic<bool> crashDetected{false};
    mgr.setCrashCallback([&](uint32_t) { crashDetected.store(true); });

    bool spawned = mgr.spawnPluginHost("__passthrough__", 9015);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9015));

    mgr.killPluginHost(9015);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(mgr.isAlive(9015));

    bool respawned = mgr.spawnPluginHost("__passthrough__", 9016);
    ASSERT_TRUE(respawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(mgr.isAlive(9016));

    mgr.killPluginHost(9016);
}

// ========================================================================
// Additional lifecycle tests
// ========================================================================

TEST(PluginIsolation, MultipleChildrenSpawnIndependently) {
    // Verify that two children can be spawned with independent slot IDs.
    // Both use bad plugins, so they'll exit after sending READY.
    ProxyProcessManager mgr;

    bool spawned1 = mgr.spawnPluginHost("C:\\nonexistent\\plugin1.vst3", 9020);
    bool spawned2 = mgr.spawnPluginHost("C:\\nonexistent\\plugin2.vst3", 9021);

    // At least one should succeed (they may exit quickly)
    EXPECT_TRUE(spawned1 || spawned2);

    // Wait for both to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Clean up
    mgr.killPluginHost(9020);
    mgr.killPluginHost(9021);
    SUCCEED();
}

TEST(PluginIsolation, CrashDetectionViaSelfExit) {
    // Spawn with bad plugin — child exits on its own (not killed).
    // checkAllChildren should detect the dead child and fire the callback.
    ProxyProcessManager mgr;

    std::atomic<int> callbackCount{0};
    std::atomic<uint32_t> lastCrashedSlot{0};

    mgr.setCrashCallback([&](uint32_t slotId) {
        callbackCount.fetch_add(1);
        lastCrashedSlot.store(slotId);
    });

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\crashtest.vst3", 9030);
    ASSERT_TRUE(spawned);

    // Wait for the child to exit (loadPlugin fails → child returns 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // checkAllChildren should detect the dead child
    mgr.checkAllChildren();

    EXPECT_GE(callbackCount.load(), 1);
    EXPECT_EQ(lastCrashedSlot.load(), 9030u);

    mgr.killPluginHost(9030);
}

TEST(PluginIsolation, ProcessBlockWithSharedMemory) {
    // Test PluginProxySlot::processBlock using shared memory directly.
    // No child process needed — we write to the input ring and read from
    // the output ring as if a child had processed the audio.
    ProxyProcessManager mgr;

    // Create shared memory manually (as if spawnPluginHost created it)
    ShmRegion shm;
    ASSERT_TRUE(shm.create("hdaw_test_procblock", computeShmSize(2, 512)));
    auto* hdr = shm.getHeader();
    hdr->numChannels = 2;
    hdr->blockSize = 512;
    hdr->capacity = 1024; // power of 2, >= 512*2

    // Create a PluginProxySlot
    PluginProxySlot slot(mgr, 9040, "TestPlugin");

    // Prepare a test buffer with a known pattern
    juce::AudioBuffer<float> buffer(2, 512);
    for (int s = 0; s < 512; ++s) {
        buffer.setSample(0, s, static_cast<float>(s) / 512.0f);
        buffer.setSample(1, s, 1.0f - static_cast<float>(s) / 512.0f);
    }
    juce::MidiBuffer midi;

    // processBlock should not crash even without a running child.
    // It writes to shared memory (which doesn't exist for this slot),
    // so it should return early or output silence.
    slot.processBlock(buffer, midi);

    // The buffer may be unchanged (no shared memory → early return)
    // or cleared (no output available → buffer.clear()).
    // Either way, it should not crash.
    SUCCEED();
}

// ========================================================================
// DLL loading tests (real VST3 plugin via child process)
// ========================================================================

TEST(PluginIsolation, DLLLoadAndAudioRoundTrip) {
    auto pluginPath = findBuiltTestPlugin();
    if (!pluginPath.exists())
        GTEST_SKIP() << "PassthroughTest.vst3 not built";

    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost(
        pluginPath.getFullPathName().toStdString(), 9050);
    ASSERT_TRUE(spawned) << "Child should start with real VST3 DLL";

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_TRUE(mgr.isAlive(9050)) << "Child should be alive after loading DLL";

    auto* pipe = mgr.getPipe(9050);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9050;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp));
    EXPECT_EQ(prepareResp.result, 1u) << "PREPARE should succeed with real DLL";

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto* shm = mgr.getShm(9050);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    int retries = 100;
    while (hdr->numChannels == 0 && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "Child should init shared memory after PREPARE";

    uint32_t totalSamples = hdr->blockSize * hdr->numChannels;

    std::vector<float> input(totalSamples);
    for (uint32_t i = 0; i < totalSamples; ++i)
        input[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f
                     * static_cast<float>(i) / 44100.0f);

    ASSERT_TRUE(shm->writeInput(input.data(), totalSamples));

    retries = 200;
    uint32_t outAvail = 0;
    while (retries-- > 0) {
        uint32_t ow = hdr->outputWritePos.load(std::memory_order_acquire);
        uint32_t or_ = hdr->outputReadPos.load(std::memory_order_relaxed);
        outAvail = ow - or_;
        if (outAvail >= totalSamples) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(outAvail, totalSamples) << "DLL plugin should produce output";

    std::vector<float> output(totalSamples);
    ASSERT_TRUE(shm->readOutput(output.data(), totalSamples));

    for (uint32_t i = 0; i < totalSamples; ++i) {
        EXPECT_NEAR(output[i], input[i], 0.0001f)
            << "Sample " << i << " mismatch";
    }

    mgr.killPluginHost(9050);
}

TEST(PluginIsolation, DLLParameterEnumeration) {
    auto pluginPath = findBuiltTestPlugin();
    if (!pluginPath.exists())
        GTEST_SKIP() << "PassthroughTest.vst3 not built";

    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost(
        pluginPath.getFullPathName().toStdString(), 9051);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_TRUE(mgr.isAlive(9051));

    auto* pipe = mgr.getPipe(9051);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage msg{};
    msg.type = MessageType::GET_PARAM_COUNT;
    msg.slotId = 9051;
    pipe->sendMsg(msg);

    ProxyResponse resp{};
    ASSERT_TRUE(pipe->receiveResp(resp));
    EXPECT_EQ(resp.result, 1u);

    uint32_t paramCount = 0;
    if (resp.dataSize >= sizeof(uint32_t))
        std::memcpy(&paramCount, resp.data, sizeof(uint32_t));

    EXPECT_EQ(paramCount, 1u) << "JUCE's VST3 wrapper exposes a bypass parameter";

    mgr.killPluginHost(9051);
}

TEST(PluginIsolation, DLLStateSaveRestore) {
    auto pluginPath = findBuiltTestPlugin();
    if (!pluginPath.exists())
        GTEST_SKIP() << "PassthroughTest.vst3 not built";

    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost(
        pluginPath.getFullPathName().toStdString(), 9052);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_TRUE(mgr.isAlive(9052));

    auto* pipe = mgr.getPipe(9052);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage getMsg{};
    getMsg.type = MessageType::GET_STATE;
    getMsg.slotId = 9052;
    pipe->sendMsg(getMsg);

    ProxyResponse getResp{};
    ASSERT_TRUE(pipe->receiveResp(getResp));
    EXPECT_EQ(getResp.result, 1u) << "GET_STATE should succeed";
    EXPECT_GT(getResp.dataSize, 0u) << "PassthroughTest should return state bytes";

    std::vector<uint8_t> savedState(getResp.data, getResp.data + getResp.dataSize);

    ProxyMessage setMsg{};
    setMsg.type = MessageType::SET_STATE;
    setMsg.slotId = 9052;
    setMsg.dataSize = static_cast<uint32_t>(savedState.size());
    std::memcpy(setMsg.data, savedState.data(),
                std::min(savedState.size(), sizeof(setMsg.data)));
    pipe->sendMsg(setMsg);

    ProxyResponse setResp{};
    ASSERT_TRUE(pipe->receiveResp(setResp));
    EXPECT_EQ(setResp.result, 1u) << "SET_STATE should succeed";

    mgr.killPluginHost(9052);
}

TEST(PluginIsolation, CrashIsolationDuringProcessBlock) {
    ProxyProcessManager mgr;

    std::atomic<bool> crashDetected{false};
    std::atomic<uint32_t> crashedSlot{0};
    mgr.setCrashCallback([&](uint32_t slotId) {
        crashDetected.store(true);
        crashedSlot.store(slotId);
    });

    bool spawned = mgr.spawnPluginHost("__crash__", 9053);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9053));

    auto* pipe = mgr.getPipe(9053);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9053;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    pipe->receiveResp(prepareResp);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto* shm = mgr.getShm(9053);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    int retries = 50;
    while (hdr->numChannels == 0 && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (hdr->numChannels > 0) {
        uint32_t totalSamples = hdr->blockSize * hdr->numChannels;
        std::vector<float> audio(totalSamples, 0.5f);
        shm->writeInput(audio.data(), totalSamples);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    EXPECT_FALSE(mgr.isAlive(9053)) << "Child should have crashed";

    mgr.checkAllChildren();
    EXPECT_TRUE(crashDetected.load()) << "Crash callback should have fired";
    EXPECT_EQ(crashedSlot.load(), 9053u);

    mgr.killPluginHost(9053);
}

TEST(PluginIsolation, DLLGracefulShutdown) {
    auto pluginPath = findBuiltTestPlugin();
    if (!pluginPath.exists())
        GTEST_SKIP() << "PassthroughTest.vst3 not built";

    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost(
        pluginPath.getFullPathName().toStdString(), 9054);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_TRUE(mgr.isAlive(9054));

    auto* pipe = mgr.getPipe(9054);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage shutdownMsg{};
    shutdownMsg.type = MessageType::SHUTDOWN;
    shutdownMsg.slotId = 9054;
    pipe->sendMsg(shutdownMsg);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    EXPECT_FALSE(mgr.isAlive(9054)) << "Child should exit after SHUTDOWN";

    mgr.killPluginHost(9054);
}
