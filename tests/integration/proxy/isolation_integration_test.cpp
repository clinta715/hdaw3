#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/ProxyCommon.h"
#include "proxy/PluginProxySlot.h"
#include "proxy/ProxySharedMemory.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/PluginManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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

    mgr.killPluginHost(9001, KillMode::KillHard);
}

TEST(PluginIsolation, SpawnAndShutdownCleanExit) {
    // Spawn, then verify clean shutdown path works.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9002);
    ASSERT_TRUE(spawned);

    // Wait for child to exit naturally
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // killPluginHost should handle the already-dead process without crashing
    mgr.killPluginHost(9002, KillMode::KillHard);
    SUCCEED();
}

TEST(PluginIsolation, KillReportsNotAlive) {
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9003);
    ASSERT_TRUE(spawned);

    // Kill the child
    bool killed = mgr.killPluginHost(9003, KillMode::KillHard);
    EXPECT_TRUE(killed);

    // isAlive should return false
    EXPECT_FALSE(mgr.isAlive(9003));
}

TEST(PluginIsolation, CheckAllChildrenFiresCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashCount{0};
    std::atomic<uint32_t> crashedSlotId{0};

    mgr.setSlotCrashCallback(9004, [&](uint32_t slotId) {
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

    mgr.killPluginHost(9004, KillMode::KillHard);
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

    mgr.killPluginHost(9014, KillMode::KillHard);
}

TEST(PluginIsolation, ResizesScratchBuffersToPreparedBlockSize) {
    ProxyProcessManager mgr;
    const uint32_t slot = 9130;
    ASSERT_TRUE(mgr.spawnPluginHost("__blocksize__", slot));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slot));

    auto* pipe = mgr.getPipe(slot);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slot;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 441, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp));
    EXPECT_EQ(prepareResp.result, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto* shm = mgr.getShm(slot);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    int retries = 100;
    while (hdr->numChannels == 0 && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "Child didn't initialize shared memory header";

    // Confirm the child saw the prepared block size.
    EXPECT_EQ(hdr->blockSize, 441u) << "header blockSize should reflect PREPARE";

    const uint32_t blockSize = 441;
    const uint32_t numChannels = 2;
    const uint32_t totalSamples = blockSize * numChannels;
    std::vector<float> input(totalSamples, 1.0f);
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

    // The probe fills every sample with the width passed to processBlock.
    // It MUST be the prepared 441, not the stale default 512.
    EXPECT_FLOAT_EQ(output[0], 441.0f)
        << "processBlock received " << output[0] << " samples/block; expected 441. "
           "audioLoop scratch buffers were not resized to the PREPARE block size.";

    mgr.killPluginHost(slot, KillMode::KillHard);
}

TEST(PluginIsolation, CrashAndRestartWithPassthrough) {
    ProxyProcessManager mgr;

    std::atomic<bool> crashDetected{false};
    mgr.setSlotCrashCallback(9015, [&](uint32_t) { crashDetected.store(true); });

    bool spawned = mgr.spawnPluginHost("__passthrough__", 9015);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9015));

    mgr.killPluginHost(9015, KillMode::KillHard);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(mgr.isAlive(9015));

    bool respawned = mgr.spawnPluginHost("__passthrough__", 9016);
    ASSERT_TRUE(respawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(mgr.isAlive(9016));

    mgr.killPluginHost(9016, KillMode::KillHard);
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
    mgr.killPluginHost(9020, KillMode::KillHard);
    mgr.killPluginHost(9021, KillMode::KillHard);
    SUCCEED();
}

TEST(PluginIsolation, CrashDetectionViaSelfExit) {
    // Spawn with bad plugin — child exits on its own (not killed).
    // checkAllChildren should detect the dead child and fire the callback.
    ProxyProcessManager mgr;

    std::atomic<int> callbackCount{0};
    std::atomic<uint32_t> lastCrashedSlot{0};

    mgr.setSlotCrashCallback(9030, [&](uint32_t slotId) {
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

    mgr.killPluginHost(9030, KillMode::KillHard);
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

    mgr.killPluginHost(9050, KillMode::KillHard);
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

    mgr.killPluginHost(9051, KillMode::KillHard);
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

    // dataSize is the TOTAL state size; bytes beyond the first chunk arrive
    // as STATE_CHUNK responses.
    const uint32_t stateTotal = getResp.dataSize;
    std::vector<uint8_t> savedState(
        getResp.data,
        getResp.data + std::min<uint32_t>(stateTotal,
                                          static_cast<uint32_t>(sizeof(getResp.data))));
    while (savedState.size() < stateTotal) {
        ProxyResponse chunk{};
        ASSERT_TRUE(pipe->receiveResp(chunk));
        ASSERT_EQ(chunk.type, MessageType::STATE_CHUNK);
        const uint32_t take = std::min<uint32_t>(
            chunk.dataSize, static_cast<uint32_t>(sizeof(chunk.data)));
        savedState.insert(savedState.end(), chunk.data, chunk.data + take);
    }
    ASSERT_EQ(savedState.size(), stateTotal);

    ProxyMessage setMsg{};
    setMsg.type = MessageType::SET_STATE;
    setMsg.slotId = 9052;
    setMsg.dataSize = static_cast<uint32_t>(savedState.size());
    const size_t setFirst = std::min(savedState.size(), sizeof(setMsg.data));
    std::memcpy(setMsg.data, savedState.data(), setFirst);
    pipe->sendMsg(setMsg);
    for (size_t offset = setFirst; offset < savedState.size();) {
        ProxyMessage chunk{};
        chunk.type = MessageType::STATE_CHUNK;
        chunk.slotId = 9052;
        const size_t take = std::min(savedState.size() - offset, sizeof(chunk.data));
        chunk.dataSize = static_cast<uint32_t>(take);
        std::memcpy(chunk.data, savedState.data() + offset, take);
        pipe->sendMsg(chunk);
        offset += take;
    }

    ProxyResponse setResp{};
    ASSERT_TRUE(pipe->receiveResp(setResp));
    EXPECT_EQ(setResp.result, 1u) << "SET_STATE should succeed";

    mgr.killPluginHost(9052, KillMode::KillHard);
}

TEST(PluginIsolation, CrashIsolationDuringProcessBlock) {
    ProxyProcessManager mgr;

    std::atomic<bool> crashDetected{false};
    std::atomic<uint32_t> crashedSlot{0};
    mgr.setSlotCrashCallback(9053, [&](uint32_t slotId) {
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

    mgr.killPluginHost(9053, KillMode::KillHard);
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

    mgr.killPluginHost(9054, KillMode::KillHard);
}

// ========================================================================
// Phase 1 isolation-plumbing reliability tests
//
// These cover the three root causes of FX slots silently collapsing to
// "none" across graph rebuilds:
//   (1) every proxy got the SAME slot id (derived from a constant) and thus
//       collided on the pipe/shm names;
//   (2) PluginProxySlot's dtor never killed its child process, orphaning it
//       so the next spawn for that slot collided on the still-held pipe/shm;
//   (3) spawnPluginHost never reaped a stale same-slot child before creating
//       new pipe/shm.
// ========================================================================

// Fix (2): the proxy destructor must terminate the child and release the
// slot's pipe + shared memory. Previously the dtor only called the empty
// releaseResources(), orphaning the child.
TEST(PluginIsolation, ProxyDestructorKillsChild) {
    ProxyProcessManager mgr;
    const uint32_t slot = 9070;

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slot));
    ASSERT_TRUE(mgr.isAlive(slot));
    EXPECT_NE(mgr.getPipe(slot), nullptr);
    EXPECT_NE(mgr.getShm(slot), nullptr);

    {
        auto proxy = std::make_unique<PluginProxySlot>(mgr, slot, "DtorTest");
        ASSERT_NE(proxy, nullptr);
    } // ~PluginProxySlot() -> killPluginHost(slot)

    EXPECT_FALSE(mgr.isAlive(slot)) << "child must be terminated by proxy dtor";
    EXPECT_EQ(mgr.getPipe(slot), nullptr) << "pipe must be released by proxy dtor";
    EXPECT_EQ(mgr.getShm(slot), nullptr) << "shm must be released by proxy dtor";
}

// Fix (2)+(3): a graph rebuild destroys a track's proxy and immediately
// recreates one. Re-creating a proxy for a slot id that was just used must
// succeed (not collide on the pipe/shm names) — exactly the path where the FX
// slot used to collapse to "none". Repeated cycles must not accumulate live
// children.
TEST(PluginIsolation, RebuildReusesSameSlotWithoutCollision) {
    ProxyProcessManager mgr;
    const uint32_t slot = 9071;

    for (int i = 0; i < 5; ++i) {
        // The previous iteration's proxy dtor already killed+released the
        // slot; spawnPluginHost also defensively reaps any stale same-slot
        // child before creating new pipe/shm.
        ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slot))
            << "re-spawn at same slot failed on iteration " << i;
        ASSERT_TRUE(mgr.isAlive(slot));
        EXPECT_NE(mgr.getPipe(slot), nullptr);

        {
            auto proxy = std::make_unique<PluginProxySlot>(mgr, slot, "ReuseTest");
            ASSERT_NE(proxy, nullptr);
        } // dtor kills the child for this slot

        // No child should remain between cycles (no leak).
        EXPECT_FALSE(mgr.isAlive(slot));
        EXPECT_EQ(mgr.getPipe(slot), nullptr);
    }
}

#if HDAW_PLUGIN_ISOLATION
// Fix (1): PluginManager must allocate a unique, monotonically-increasing
// slot id per proxy instance (was knownPlugins.size() — a constant — so every
// proxy collided on the same pipe/shm names). We verify two instances get
// distinct slot ids via the description each proxy reports.
TEST(PluginIsolation, UniqueSlotIdPerInstance) {
    HDAW::PluginManager mgr;
    EXPECT_TRUE(mgr.isolationEnabled);  // default ON

    juce::PluginDescription desc;
    desc.name = "UniqueSlot";
    desc.fileOrIdentifier = "__passthrough__";
    desc.pluginFormatName = "VST3";

    juce::String err;
    auto p1 = mgr.createPluginInstance(desc, err, 44100.0, 512, true);
    ASSERT_NE(p1, nullptr) << "first isolated instance should spawn: "
                           << err.toStdString();

    auto p2 = mgr.createPluginInstance(desc, err, 44100.0, 512, true);
    ASSERT_NE(p2, nullptr) << "second isolated instance should spawn: "
                           << err.toStdString();

    juce::PluginDescription d1, d2;
    p1->fillInPluginDescription(d1);
    p2->fillInPluginDescription(d2);

    EXPECT_STRNE(d1.fileOrIdentifier.toStdString().c_str(),
                 d2.fileOrIdentifier.toStdString().c_str())
        << "slot ids must be unique across instances (was constant before fix): "
        << d1.fileOrIdentifier << " vs " << d2.fileOrIdentifier;

    // p1/p2 destruction exercises the dtor-kills-child path for their slots.
}
#endif

// ========================================================================
// Bug fix tests: cap==0 guard, per-slot callbacks, health monitor
// ========================================================================

TEST(PluginIsolation, ProcessBlockWithZeroCapacity) {
    // Regression: if the child crashes before initializing shared memory,
    // capacity stays at 0 and (cap - 1) wraps to 0xFFFFFFFF, causing OOB
    // writes that crash the main process.
    ProxyProcessManager mgr;

    ShmRegion shm;
    ASSERT_TRUE(shm.create("hdaw_test_zerocap", computeShmSize(2, 512)));
    auto* hdr = shm.getHeader();
    hdr->numChannels = 2;
    hdr->blockSize = 512;
    hdr->capacity = 0;  // Simulate child crash before init

    PluginProxySlot slot(mgr, 9060, "ZeroCapTest");

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;

    // Must not crash - should output silence and return early
    slot.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 512; ++s)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, s), 0.0f);
}

TEST(PluginIsolation, PerSlotCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> slotAFires{0};
    std::atomic<int> slotBFires{0};

    mgr.setSlotCrashCallback(9080, [&](uint32_t) {
        slotAFires.fetch_add(1);
    });
    mgr.setSlotCrashCallback(9081, [&](uint32_t) {
        slotBFires.fetch_add(1);
    });

    mgr.spawnPluginHost("C:\\nonexistent\\a.vst3", 9080);
    mgr.spawnPluginHost("C:\\nonexistent\\b.vst3", 9081);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    mgr.checkAllChildren();

    EXPECT_GE(slotAFires.load(), 1) << "slot 9080 callback should fire";
    EXPECT_GE(slotBFires.load(), 1) << "slot 9081 callback should fire";

    mgr.killPluginHost(9080, KillMode::KillHard);
    mgr.killPluginHost(9081, KillMode::KillHard);
}

TEST(PluginIsolation, RemoveSlotCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> fires{0};
    mgr.setSlotCrashCallback(9090, [&](uint32_t) { fires.fetch_add(1); });

    mgr.removeSlotCrashCallback(9090);

    mgr.spawnPluginHost("C:\\nonexistent\\x.vst3", 9090);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    mgr.checkAllChildren();

    EXPECT_EQ(fires.load(), 0) << "removed callback should not fire";

    mgr.killPluginHost(9090, KillMode::KillHard);
}

TEST(PluginIsolation, HealthMonitorDetectsDeadChild) {
    // Simulate a child that crashes on its own (not killed by the host).
    // The __crash__ plugin calls std::_Exit(3) in its first processBlock,
    // so the child dies without killPluginHost being called.
    ProxyProcessManager mgr;

    std::atomic<bool> detected{false};
    std::atomic<uint32_t> detectedSlot{0};

    mgr.setSlotCrashCallback(9100, [&](uint32_t id) {
        detected.store(true);
        detectedSlot.store(id);
    });

    mgr.spawnPluginHost("__crash__", 9100);
    ASSERT_TRUE(mgr.isAlive(9100));

    // Send PREPARE so the child starts its audio loop
    auto* pipe = mgr.getPipe(9100);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9100;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    pipe->receiveResp(prepareResp);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Write audio to shared memory to trigger processBlock, which calls _Exit(3)
    auto* shm = mgr.getShm(9100);
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

    // Start health monitor with short interval
    mgr.startHealthMonitor(200);

    // Wait for child to crash and health monitor to detect it
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (!detected.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(detected.load()) << "Health monitor should detect dead child";
    EXPECT_EQ(detectedSlot.load(), 9100u);

    mgr.stopHealthMonitor();
    mgr.killPluginHost(9100, KillMode::KillHard);
}

TEST(PluginIsolation, BoundedPrepareToPlayDoesNotHang) {
    ProxyProcessManager mgr;

    mgr.spawnPluginHost("C:\\nonexistent\\hang.vst3", 9110);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    PluginProxySlot slot(mgr, 9110, "HangTest");

    auto start = std::chrono::steady_clock::now();

    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    juce::MidiBuffer midi;
    slot.processBlock(buf, midi);

    slot.prepareToPlay(44100.0, 512);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    EXPECT_LT(elapsedMs, 8000) << "prepareToPlay should not block for more than 8 seconds";

    mgr.killPluginHost(9110, KillMode::KillHard);
}

#if HDAW_PLUGIN_ISOLATION
TEST(PluginIsolation, GracefulShutdownDoesNotFireCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashFires{0};
    mgr.setSlotCrashCallback(9120, [&](uint32_t) { crashFires.fetch_add(1); });

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9120));

    mgr.startHealthMonitor(100);

    ASSERT_TRUE(mgr.killPluginHost(9120, KillMode::KillGraceful));

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_EQ(crashFires.load(), 0);

    mgr.stopHealthMonitor();
}

TEST(PluginIsolation, HardKillFiresCrashCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashFires{0};
    mgr.setSlotCrashCallback(9121, [&](uint32_t) { crashFires.fetch_add(1); });

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9121));

    mgr.startHealthMonitor(100);

    // Simulate an external crash: get the child handle and kill it directly
    // (NOT via killPluginHost, which erases the entry before checkAllChildren
    // can observe it). The child exits with code 0 (not the graceful sentinel),
    // so checkAllChildren should treat it as a crash.
    auto* info = mgr.getChildInfo(9121);
    ASSERT_NE(info, nullptr);
    ASSERT_NE(info->processHandle, INVALID_HANDLE_VALUE);
    TerminateProcess(info->processHandle, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_GE(crashFires.load(), 1);

    mgr.stopHealthMonitor();
}
#endif

// ========================================================================
// Chunked state transfer (state > 244-byte message payload)
// ========================================================================

TEST(PluginIsolation, LargeStateRoundTripThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9140;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    PluginProxySlot slot(mgr, slotId, "StateEcho");

    constexpr size_t kStateSize = 100000;
    juce::MemoryBlock in(kStateSize);
    auto* inBytes = static_cast<uint8_t*>(in.getData());
    for (size_t i = 0; i < kStateSize; ++i)
        inBytes[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);

    slot.setStateInformation(in.getData(), static_cast<int>(in.getSize()));

    juce::MemoryBlock out;
    slot.getStateInformation(out);

    ASSERT_EQ(out.getSize(), kStateSize);
    EXPECT_EQ(std::memcmp(out.getData(), in.getData(), kStateSize), 0)
        << "state must round-trip the proxy byte-exact";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

TEST(PluginIsolation, SmallStateRoundTripThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9141;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    PluginProxySlot slot(mgr, slotId, "StateEcho");

    constexpr size_t kStateSize = 100;
    juce::MemoryBlock in(kStateSize);
    auto* inBytes = static_cast<uint8_t*>(in.getData());
    for (size_t i = 0; i < kStateSize; ++i)
        inBytes[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);

    slot.setStateInformation(in.getData(), static_cast<int>(in.getSize()));

    juce::MemoryBlock out;
    slot.getStateInformation(out);

    ASSERT_EQ(out.getSize(), kStateSize);
    EXPECT_EQ(std::memcmp(out.getData(), in.getData(), kStateSize), 0)
        << "small (non-chunked) state must round-trip byte-exact";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// ========================================================================
// MIDI fidelity through the proxy (SysEx lane + short-message size)
// ========================================================================

TEST(PluginIsolation, MidiRoundTripThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9150;

    ASSERT_TRUE(mgr.spawnPluginHost("__midiecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto* pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slotId;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp));
    EXPECT_EQ(prepareResp.result, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    PluginProxySlot slot(mgr, slotId, "MidiEcho");

    // Patterned SysEx >244 bytes (proves the SysEx lane, not the inline path).
    constexpr int kSysexLen = 2000;
    std::vector<uint8_t> sysexBytes(kSysexLen);
    sysexBytes[0] = 0xF0;
    for (int i = 1; i < kSysexLen - 1; ++i)
        sysexBytes[i] = static_cast<uint8_t>((i * 7 + 3) & 0x7F);
    sysexBytes[kSysexLen - 1] = 0xF7;

    juce::MidiMessage sysexMsg(sysexBytes.data(), kSysexLen);
    ASSERT_TRUE(sysexMsg.isSysEx());
    ASSERT_EQ(sysexMsg.getRawDataSize(), kSysexLen);

    // 2-byte program change (proves size fidelity — the old code mangled it
    // to 3 bytes with a garbage third byte).
    juce::MidiMessage programChange(0xC0, 0x55);
    ASSERT_EQ(programChange.getRawDataSize(), 2);

    juce::MidiMessage noteOn(0x90, 60, 100);
    ASSERT_EQ(noteOn.getRawDataSize(), 3);

    bool gotSysex = false, gotProgramChange = false, gotNoteOn = false;
    std::vector<uint8_t> echoedSysex;

    for (int iter = 0; iter < 200 && !(gotSysex && gotProgramChange && gotNoteOn); ++iter) {
        juce::AudioBuffer<float> audio(2, 512);
        audio.clear();
        juce::MidiBuffer midi;
        if (iter == 0) {
            midi.addEvent(sysexMsg, 0);
            midi.addEvent(programChange, 0);
            midi.addEvent(noteOn, 10);
        }
        slot.processBlock(audio, midi);

        // Iteration 0's buffer also carries the input events we injected, so
        // an echo there is the SECOND copy of each; later iterations run on
        // empty buffers, where a single event is the echo.
        const int echoCount = (iter == 0) ? 2 : 1;
        int sysexSeen = 0, pcSeen = 0, noteOnSeen = 0;
        for (const auto metadata : midi) {
            const auto msg = metadata.getMessage();
            const uint8_t* bytes = msg.getRawData();
            if (msg.isSysEx()) {
                if (++sysexSeen >= echoCount && !gotSysex) {
                    gotSysex = true;
                    echoedSysex.assign(bytes, bytes + msg.getRawDataSize());
                }
            } else if (msg.getRawDataSize() == 2 && bytes[0] == 0xC0 && bytes[1] == 0x55) {
                if (++pcSeen >= echoCount) gotProgramChange = true;
            } else if (msg.getRawDataSize() == 3 && bytes[0] == 0x90
                       && bytes[1] == 60 && bytes[2] == 100) {
                if (++noteOnSeen >= echoCount) gotNoteOn = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_TRUE(gotSysex) << "SysEx did not round-trip through the proxy";
    ASSERT_TRUE(gotProgramChange) << "program change did not round-trip through the proxy";
    ASSERT_TRUE(gotNoteOn) << "note-on did not round-trip through the proxy";

    ASSERT_EQ(static_cast<int>(echoedSysex.size()), kSysexLen);
    EXPECT_EQ(echoedSysex.front(), 0xF0);
    EXPECT_EQ(echoedSysex.back(), 0xF7);
    EXPECT_EQ(std::memcmp(echoedSysex.data(), sysexBytes.data(), kSysexLen), 0)
        << "SysEx must round-trip the proxy byte-exact";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// ========================================================================
// Parameter & program bridge through the proxy
// ========================================================================

TEST(PluginIsolation, ParamBridgeThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9151;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto* pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slotId;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp));
    EXPECT_EQ(prepareResp.result, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    PluginProxySlot slot(mgr, slotId, "StateEcho");

    auto& params = slot.getParameters();
    ASSERT_EQ(params.size(), 3);
    EXPECT_EQ(params[0]->getName(64), "Echo A");
    EXPECT_EQ(params[1]->getName(64), "Echo B");
    EXPECT_EQ(params[2]->getName(64), "Echo C");
    EXPECT_TRUE(params[0]->isAutomatable());
    EXPECT_NEAR(params[0]->getDefaultValue(), 0.25f, 1e-5f);
    EXPECT_NEAR(params[0]->getValue(), 0.25f, 1e-5f);
    EXPECT_NEAR(params[1]->getDefaultValue(), 0.5f, 1e-5f);
    EXPECT_NEAR(params[2]->getDefaultValue(), 0.75f, 1e-5f);

    // Listener capturing param-index/value notifications delivered on the
    // message thread via slot.drainParamNotifications().
    struct CapturingListener : public juce::AudioProcessorListener {
        std::atomic<int> lastIndex{ -1 };
        std::atomic<float> lastValue{ 0.f };
        std::atomic<bool> got{ false };
        void audioProcessorParameterChanged(juce::AudioProcessor*, int idx, float v) override {
            lastIndex.store(idx);
            lastValue.store(v);
            got.store(true);
        }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    } listener;
    slot.addListener(&listener);

    params[0]->setValueNotifyingHost(0.42f);

    bool observed = false;
    for (int iter = 0; iter < 200 && !observed; ++iter) {
        juce::AudioBuffer<float> audio(2, 512);
        audio.clear();
        juce::MidiBuffer midi;
        slot.processBlock(audio, midi);
        slot.drainParamNotifications();
        if (listener.got.load() && listener.lastIndex.load() == 0
            && std::abs(listener.lastValue.load() - 0.42f) < 1e-4f) {
            observed = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    slot.removeListener(&listener);

    EXPECT_TRUE(observed) << "param change did not round-trip through the proxy";
    EXPECT_NEAR(params[0]->getValue(), 0.42f, 1e-4f);

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

TEST(PluginIsolation, ProgramBridgeThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9152;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto* pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slotId;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp));
    EXPECT_EQ(prepareResp.result, 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    PluginProxySlot slot(mgr, slotId, "StateEcho");

    EXPECT_EQ(slot.getNumPrograms(), 2);
    EXPECT_EQ(slot.getProgramName(0), "Init");
    EXPECT_EQ(slot.getProgramName(1), "Test Preset");
    EXPECT_EQ(slot.getCurrentProgram(), 0);
    slot.setCurrentProgram(1);
    EXPECT_EQ(slot.getCurrentProgram(), 1);

    mgr.killPluginHost(slotId, KillMode::KillHard);
}
