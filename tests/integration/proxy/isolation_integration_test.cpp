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
#include <optional>

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

namespace {
// Set a process env var (inherited by the child: CreateProcessA runs with a
// null environment block) and return its previous value so the test restores
// it. Used to point the child's %TEMP% at a scratch dir and to arm the
// test-only hang hook / warmup override.
std::string setChildEnv(const char* name, const std::string& value)
{
    char buf[4096]{};
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    std::string old = (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();
    SetEnvironmentVariableA(name, value.c_str());
    return old;
}

void restoreChildEnv(const char* name, const std::string& old)
{
    SetEnvironmentVariableA(name, old.empty() ? nullptr : old.c_str());
}

// The child watchdog writes "hdaw_plugin_host_processBlock hung for 1s.dmp"
// into its %TEMP%. List that pattern in `dir`.
int countHungDumps(const juce::File& dir, juce::StringArray& names)
{
    names.clear();
    for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*hung*.dmp"))
    {
        auto f = entry.getFile();
        names.add(f.getFileName() + " (" + juce::String(f.getSize()) + " bytes)");
    }
    return names.size();
}

juce::File freshScratchDir(const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("hdaw_watchdog_" + tag);
    dir.deleteRecursively();
    return dir;
}
} // namespace

TEST(PluginIsolation, HostExePathResolves) {
    auto path = ProxyProcessManager::getHostExePath();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.find("hdaw_plugin_host.exe") != std::string::npos);
}

// Current contract (post-e917c1f): a failed plugin load no longer exits the
// child. PluginHost::loadPlugin() falls back to an internal passthrough
// processor, so the child STAYS ALIVE, its control/audio loops keep running,
// and the parent's proxy can still communicate — the pluginFailed /
// crash-recovery design depends on this. This test pins the new contract:
// spawn succeeds, the child survives, and the pipe still answers PREPARE
// (result=1 — the fallback is not a plugin failure).
TEST(PluginIsolation, SpawnWithBadPluginStaysAlive) {
    // Spawn with a non-existent plugin: loadPlugin fails and swaps in the
    // passthrough fallback instead of exiting the child.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("C:\\nonexistent\\fake.vst3", 9001);
    ASSERT_TRUE(spawned) << "Child should send READY";

    // Give any failure path time to play out; the passthrough fallback must
    // keep the child alive (the pre-e917c1f contract exited the child here).
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    EXPECT_TRUE(mgr.isAlive(9001))
        << "child must stay alive after a failed plugin load (passthrough fallback)";

    // The proxy must remain communicable: the control loop still answers
    // PREPARE and the fallback processor makes the audio loop go live.
    auto pipe = mgr.getPipe(9001);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9001;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    ASSERT_TRUE(pipe->receiveResp(prepareResp))
        << "child should answer PREPARE after the failed load";
    EXPECT_EQ(prepareResp.result, 1u)
        << "PREPARE succeeds on the passthrough fallback";

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

// Current contract (post-e917c1f): a bad plugin path no longer kills the
// child, so the check sweep is exercised with a deterministic external kill
// instead — the same mechanism as HardKillFiresCrashCallback:
// TerminateProcess directly on the child handle (NOT killPluginHost, which
// erases the entry before checkAllChildren can observe it). The sweep must
// detect the dead child and fire the per-slot crash callback with its id.
#if HDAW_PLUGIN_ISOLATION
TEST(PluginIsolation, CheckAllChildrenFiresCallback) {
    ProxyProcessManager mgr;

    std::atomic<int> crashCount{0};
    std::atomic<uint32_t> crashedSlotId{0};

    mgr.setSlotCrashCallback(9004, [&](uint32_t slotId) {
        crashCount.fetch_add(1);
        crashedSlotId.store(slotId);
    });

    bool spawned = mgr.spawnPluginHost("__passthrough__", 9004);
    ASSERT_TRUE(spawned);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9004));

    // Deterministic external kill on the child handle.
    auto* info = mgr.getChildInfo(9004);
    ASSERT_NE(info, nullptr);
    ASSERT_NE(info->processHandle, INVALID_HANDLE_VALUE);
    TerminateProcess(info->processHandle, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // checkAllChildren detects the dead child and fires the callback.
    mgr.checkAllChildren();

    EXPECT_GE(crashCount.load(), 1);
    EXPECT_EQ(crashedSlotId.load(), 9004u);
}
#endif

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

    auto pipe = mgr.getPipe(9014);
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

    auto shm = mgr.getShm(9014);
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

    auto pipe = mgr.getPipe(slot);
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

    auto shm = mgr.getShm(slot);
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

// Current contract (post-e917c1f): a bad plugin path no longer exits the
// child, so self-exit is exercised with the __crash__ sentinel — the plugin
// calls std::_Exit(3) in its first processBlock, a deterministic SELF-exit
// (no killPluginHost involved). The death must surface through the check
// sweep as a crash callback carrying the right slot id.
TEST(PluginIsolation, CrashDetectionViaSelfExit) {
    ProxyProcessManager mgr;

    std::atomic<int> callbackCount{0};
    std::atomic<uint32_t> lastCrashedSlot{0};

    mgr.setSlotCrashCallback(9030, [&](uint32_t slotId) {
        callbackCount.fetch_add(1);
        lastCrashedSlot.store(slotId);
    });

    ASSERT_TRUE(mgr.spawnPluginHost("__crash__", 9030));
    ASSERT_TRUE(mgr.isAlive(9030));

    // PREPARE starts the audio loop.
    auto pipe = mgr.getPipe(9030);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = 9030;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse prepareResp{};
    pipe->receiveResp(prepareResp);

    // Write audio so the child processes a block and self-exits (_Exit(3)).
    auto shm = mgr.getShm(9030);
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

    // Wait for the SELF-exit (not an external kill).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (mgr.isAlive(9030) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(mgr.isAlive(9030))
        << "__crash__ should self-exit in its first processBlock";

    mgr.checkAllChildren();

    EXPECT_GE(callbackCount.load(), 1);
    EXPECT_EQ(lastCrashedSlot.load(), 9030u);
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
// Transport playhead handoff (parent PluginProxySlot → child PluginHost)
// ========================================================================

namespace {
// Local playhead mimicking InternalPlayHead's field set, so the test observes
// exactly what PluginProxySlot::processBlock packs (production code path).
struct TestPlayHead : public juce::AudioPlayHead {
    bool playing = false;
    double bpm = 120.0;
    double seconds = 0.0;
    double ppq = 0.0;
    double sampleRate = 44100.0;

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
        info.setIsPlaying(playing);
        info.setTimeInSeconds(seconds);
        info.setTimeInSamples(static_cast<int64_t>(seconds * sampleRate));
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        return info;
    }
};

// Must match TransportProbeProcessor::ProbePayload in PluginHost.cpp.
struct ProbePayload {
    uint32_t isPlaying;
    float bpm;
    double seconds;
    double ppq;
};

// Pushes one block through the slot and decodes the transport-probe payload
// the child echoes in channel 0 of its output.
bool pushBlockAndReadProbe(PluginProxySlot& slot, const std::shared_ptr<ShmRegion>& shm,
                           ProbePayload& out) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    slot.processBlock(buffer, midi);

    auto* hdr = shm->getHeader();
    uint32_t totalSamples = hdr->blockSize * hdr->numChannels;
    int retries = 200;
    while (retries-- > 0) {
        uint32_t ow = hdr->outputWritePos.load(std::memory_order_acquire);
        uint32_t or_ = hdr->outputReadPos.load(std::memory_order_relaxed);
        if (ow - or_ >= totalSamples) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (retries < 0) return false;
    std::vector<float> output(totalSamples, 0.0f);
    if (!shm->readOutput(output.data(), totalSamples)) return false;
    std::memcpy(&out, output.data(), sizeof(ProbePayload));
    return true;
}
} // namespace

TEST(PluginIsolation, TransportClockHandoff) {
    // Full production pipeline: PluginProxySlot::processBlock packs the
    // playhead into the shm header, the isolated child snapshots it into its
    // ChildPlayHead, and the hosted TransportProbeProcessor echoes what
    // getPlayHead() reported back through the audio ring.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("__transportprobe__", 9061);
    ASSERT_TRUE(spawned) << "child host should start with the transport probe";

    for (int i = 0; i < 100 && !mgr.isAlive(9061); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(9061)) << "child should be alive after spawn";

    auto shm = mgr.getShm(9061);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    // Constructing the slot binds its shmHandle from the manager's registry.
    PluginProxySlot slot(mgr, 9061, "TestPlugin");
    slot.prepareToPlay(44100.0, 512);

    TestPlayHead ph;
    slot.setPlayHead(&ph);

    // Wait for the child to announce its initialized header (PREPARE handled).
    int retries = 100;
    while ((hdr->numChannels == 0 || hdr->capacity == 0) && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "child should init shm after PREPARE";

    // Phase 1 — transport stopped: the probe must report isPlaying=0 with the
    // forwarded (default) tempo — not a missing playhead.
    ProbePayload p1{};
    ASSERT_TRUE(pushBlockAndReadProbe(slot, shm, p1))
        << "child should process the block and echo the probe payload";
    EXPECT_EQ(p1.isPlaying, 0u);
    EXPECT_FLOAT_EQ(p1.bpm, 120.0f);
    EXPECT_DOUBLE_EQ(p1.seconds, 0.0);
    EXPECT_DOUBLE_EQ(p1.ppq, 0.0);

    // Phase 2 — transport playing with distinctive state: all fields must
    // arrive bit-exact (IEEE patterns travel the shm header unmodified).
    ph.playing = true;
    ph.bpm = 137.5;
    ph.seconds = 3.25;
    ph.ppq = 6.5;
    ProbePayload p2{};
    ASSERT_TRUE(pushBlockAndReadProbe(slot, shm, p2))
        << "child should process the block and echo the probe payload";
    EXPECT_EQ(p2.isPlaying, 1u);
    EXPECT_FLOAT_EQ(p2.bpm, 137.5f);
    EXPECT_DOUBLE_EQ(p2.seconds, 3.25);
    EXPECT_DOUBLE_EQ(p2.ppq, 6.5);

    mgr.killPluginHost(9061, KillMode::KillHard);
}

// G3 for docs/plans/2026-09-11-fx-midi-injection-virus-presets.md: a short
// MIDI message (the exact bytes send_fx_midi queues) pushed through
// PluginProxySlot::processBlock must traverse the SHM midiIn ring, reach the
// hosted child processor, and its echo must come back through midiOut —
// byte-exact and in order. Uses the child's __midiecho__ diagnostic
// (MidiEchoProcessor), which swaps incoming MIDI to the output.
TEST(PluginIsolation, MidiInjectionProxyRoundTrip) {
    ProxyProcessManager mgr;

    ASSERT_TRUE(mgr.spawnPluginHost("__midiecho__", 9461));
    for (int i = 0; i < 100 && !mgr.isAlive(9461); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(9461)) << "child should be alive after spawn";

    auto shm = mgr.getShm(9461);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    PluginProxySlot slot(mgr, 9461, "TestPlugin");
    slot.prepareToPlay(44100.0, 512);

    int retries = 100;
    while ((hdr->numChannels == 0 || hdr->capacity == 0) && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "child should init shm after PREPARE";

    juce::AudioBuffer<float> buffer(2, 512);
    std::vector<juce::MidiMessage> echoed;
    auto push = [&](const std::vector<juce::MidiMessage>& in) {
        juce::MidiBuffer midi;
        int sample = 0;
        for (const auto& m : in) {
            midi.addEvent(m, sample);
            sample += 16;
        }
        slot.processBlock(buffer, midi);
        // The child's response to the PREVIOUS block is read during this call
        // (same one-block latency as the audio ring) — accumulate everything.
        for (const auto& md : midi)
            echoed.push_back(md.getMessage());
    };

    // Warm-up blocks: establish the render loop; echoes lag one block.
    push({});
    push({});

    // Program change round trip: 0xC0 0x28 on channel 1.
    push({ juce::MidiMessage::programChange(1, 40) });
    push({});
    push({});

    bool pcSeen = false;
    for (const auto& m : echoed)
        if (m.isProgramChange() && m.getChannel() == 1 && m.getProgramChangeNumber() == 40)
            pcSeen = true;
    EXPECT_TRUE(pcSeen) << "program change did not round-trip parent -> child -> parent";

    // CC + note round trip, order preserved (FIFO ring).
    echoed.clear();
    push({ juce::MidiMessage::controllerEvent(1, 0, 2), juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)) });
    push({});
    push({});

    bool ccSeen = false, noteSeen = false, ccBeforeNote = false;
    int ccAt = -1, noteAt = -1, i = 0;
    for (const auto& m : echoed) {
        if (m.isController() && m.getControllerNumber() == 0 && m.getControllerValue() == 2) { ccSeen = true; ccAt = i; }
        if (m.isNoteOn() && m.getNoteNumber() == 60 && m.getVelocity() == 100) { noteSeen = true; noteAt = i; }
        ++i;
    }
    EXPECT_TRUE(ccSeen) << "control change did not round-trip";
    EXPECT_TRUE(noteSeen) << "note on did not round-trip";
    ccBeforeNote = (ccAt >= 0 && noteAt >= 0 && ccAt < noteAt);
    EXPECT_TRUE(ccBeforeNote) << "events must arrive in queue order (FIFO)";

    // SysEx round trip: the SHM midiIn/midiOut rings carry sysex via their
    // dedicated buffers (flags bit 0x80, 128KB cap) — byte-exact both ways.
    echoed.clear();
    const std::vector<uint8_t> dump = {0xF0, 0x43, 0x00, 0x7A, 0xF7};
    push({ juce::MidiMessage(dump.data(), static_cast<int>(dump.size())) });
    push({});
    push({});
    bool sysexSeen = false;
    for (const auto& m : echoed) {
        if (!m.isSysEx() || m.getRawDataSize() != static_cast<int>(dump.size()))
            continue;
        const auto* raw = m.getRawData();
        bool equal = true;
        for (size_t b = 0; b < dump.size(); ++b)
            if (raw[b] != dump[b]) { equal = false; break; }
        if (equal)
            sysexSeen = true;
    }
    EXPECT_TRUE(sysexSeen) << "sysex did not round-trip parent -> child -> parent";

    mgr.killPluginHost(9461, KillMode::KillHard);
}

TEST(PluginIsolation, MultiPortWidthHandoff) {
    // The __multiport__ sentinel declares two stereo output ports (4 channels)
    // — the NodalRed2x layout. The child must PREPARE 4 channels, report 4 in
    // the shm header, the parent must report 4, and channel-2 data must
    // survive the ring. A 2-channel-prepared child (main-port-only width bug,
    // pre-738a65c) fails every one of these.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("__multiport__", 9160);
    ASSERT_TRUE(spawned) << "child host should start with the multi-port probe";

    for (int i = 0; i < 100 && !mgr.isAlive(9160); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(9160)) << "child should be alive after spawn";

    auto shm = mgr.getShm(9160);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    // The child writes the hosted plugin's channel layout into the header in
    // loadPlugin(), which runs AFTER READY — so the parent must poll for the
    // width before prepareToPlay reads it (otherwise a fast prepare sees 0
    // and falls back to the 2-channel default).
    int retries = 100;
    while (hdr->pluginNumOutputChannels != 4u && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(hdr->pluginNumOutputChannels, 4u)
        << "child should report the 4-channel width at load";

    PluginProxySlot slot(mgr, 9160, "TestPlugin");
    slot.prepareToPlay(44100.0, 512);

    // Parent-side regression target: pre-738a65c reports 2 (main-port-only).
    EXPECT_EQ(slot.getReportedNumOutputChannels(), 4);
    EXPECT_EQ(hdr->pluginNumOutputChannels, 4u);

    // Push one block; the parent writes 4x512 samples channel-major.
    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    slot.processBlock(buffer, midi);

    // Read the output ring at the PREPARED width (4) — NOT hdr->numChannels,
    // which the child only sets at audioLoop start and never re-syncs.
    constexpr uint32_t kBlockSize = 512;
    constexpr uint32_t kNumChannels = 4;
    const uint32_t totalSamples = kBlockSize * kNumChannels;
    retries = 200;
    uint32_t outAvail = 0;
    while (retries-- > 0) {
        uint32_t ow = hdr->outputWritePos.load(std::memory_order_acquire);
        uint32_t or_ = hdr->outputReadPos.load(std::memory_order_relaxed);
        outAvail = ow - or_;
        if (outAvail >= totalSamples) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(outAvail, totalSamples) << "child should produce 4 channels of output";

    std::vector<float> output(totalSamples, 0.0f);
    ASSERT_TRUE(shm->readOutput(output.data(), totalSamples));

    // Channel 0 carries the probe payload (magic + prepared width).
    struct MultiPortPayload { uint32_t magic; uint32_t width; };
    MultiPortPayload p{};
    std::memcpy(&p, output.data(), sizeof(MultiPortPayload));
    EXPECT_EQ(p.magic, 0x4D504F52u);
    EXPECT_EQ(p.width, 4u);

    // Channel 2 (channel-major ring layout: offset 2*blockSize) carries the
    // marker — a 2-channel-prepared child would never touch it.
    EXPECT_FLOAT_EQ(output[2 * kBlockSize], -1.25e3f);

    mgr.killPluginHost(9160, KillMode::KillHard);
}

// ========================================================================
// Output resync — a lagging child must never deliver stale (misaligned)
// audio. __slowslot__ sleeps ~250ms per block (Sleep granularity ~15.6ms vs
// the parent's 10ms blocks), so the child is always one or more blocks behind.
// ========================================================================

TEST(PluginIsolation, SlowChildNeverStale) {
    // While the child lags, the proxy must output silence (cleared buffer),
    // never a previous block's samples; the child's eventual output must be
    // the aligned copy of the block it consumed (its input values).
    ProxyProcessManager mgr;
    const uint32_t slot = 9170;

    ASSERT_TRUE(mgr.spawnPluginHost("__slowslot__", slot));
    for (int i = 0; i < 100 && !mgr.isAlive(slot); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(slot)) << "child should be alive after spawn";

    auto shm = mgr.getShm(slot);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    PluginProxySlot slotProc(mgr, slot, "SlowSlot");
    slotProc.prepareToPlay(44100.0, 128);

    constexpr int kBlock = 128;
    constexpr int kChannels = 2;
    const uint32_t totalSamples = static_cast<uint32_t>(kBlock * kChannels);

    auto fill = [](juce::AudioBuffer<float>& b, float v) {
        for (int ch = 0; ch < kChannels; ++ch)
            for (int s = 0; s < kBlock; ++s)
                b.setSample(ch, s, v);
    };
    auto expectAll = [](const juce::AudioBuffer<float>& b, float v) {
        for (int ch = 0; ch < kChannels; ++ch)
            for (int s = 0; s < kBlock; ++s)
                EXPECT_FLOAT_EQ(b.getSample(ch, s), v);
    };
    juce::MidiBuffer midi;

    // Block 0 (0.05f): the child has not consumed it yet — the output cannot
    // be current; the proxy must deliver silence, not ring garbage.
    juce::AudioBuffer<float> b0(2, kBlock);
    fill(b0, 0.05f);
    slotProc.processBlock(b0, midi);
    expectAll(b0, 0.0f);

    // Let the child finish block 0: the ring now holds block 0's output
    // (0.05f). In a streaming ring buffer, output for block N arrives during
    // block N+1 — this is correct 1-block latency, not stale data.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    juce::AudioBuffer<float> b1(2, kBlock);
    fill(b1, 0.1f);
    slotProc.processBlock(b1, midi);
    // The child consumed block 0 and wrote its output. The proxy reads it
    // (correct streaming behavior: output for the previous block).
    expectAll(b1, 0.05f);

    // Let the child finish block 1; block 2 gets block 1's output (0.1f).
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    juce::AudioBuffer<float> b2(2, kBlock);
    fill(b2, 0.2f);
    slotProc.processBlock(b2, midi);
    expectAll(b2, 0.1f);

    // Drain remaining output — the child's response to block 2 (0.2f).
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::vector<float> out(totalSamples, -1.0f);
    ASSERT_TRUE(shm->readOutput(out.data(), totalSamples))
        << "child should produce aligned output after catching up";
    for (uint32_t i = 0; i < totalSamples; ++i)
        EXPECT_FLOAT_EQ(out[i], 0.2f)
            << "child output must be the aligned copy of the input block";

    mgr.killPluginHost(slot, KillMode::KillHard);
}

TEST(PluginIsolation, LiveDropDrainsStaleOutput) {
    // Fill the input ring faster than the slow child consumes → the live
    // drop path fires (ring full). The drop must drain the output ring so no
    // stale output survives for a future read, and must leave the caller's
    // buffer untouched (dry-audio passthrough contract preserved).
    ProxyProcessManager mgr;
    const uint32_t slot = 9171;

    ASSERT_TRUE(mgr.spawnPluginHost("__slowslot__", slot));
    for (int i = 0; i < 100 && !mgr.isAlive(slot); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(slot)) << "child should be alive after spawn";

    auto shm = mgr.getShm(slot);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    PluginProxySlot slotProc(mgr, slot, "SlowSlot");
    slotProc.prepareToPlay(44100.0, 128);

    constexpr int kBlock = 128;
    constexpr int kChannels = 2;

    auto fill = [](juce::AudioBuffer<float>& b, float v) {
        for (int ch = 0; ch < kChannels; ++ch)
            for (int s = 0; s < kBlock; ++s)
                b.setSample(ch, s, v);
    };
    juce::MidiBuffer midi;

    // Blocks 0 and 1 both fit while the child sleeps on block 0.
    juce::AudioBuffer<float> b0(2, kBlock);
    fill(b0, 0.05f);
    slotProc.processBlock(b0, midi);
    juce::AudioBuffer<float> b1(2, kBlock);
    fill(b1, 0.1f);
    slotProc.processBlock(b1, midi);

    // Child wakes, writes block 0's output (stale), consumes block 1.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_GT(hdr->outputWritePos.load(std::memory_order_acquire), 0u)
        << "child should have produced output by now";

    // Block 2: ring has room again (child consumed block 1) → written.
    juce::AudioBuffer<float> b2(2, kBlock);
    fill(b2, 0.2f);
    slotProc.processBlock(b2, midi);

    // Block 3: input ring full (child asleep on block 2) → live drop path.
    juce::AudioBuffer<float> b3(2, kBlock);
    fill(b3, 0.3f);
    slotProc.processBlock(b3, midi);

    // The drop leaves the caller's buffer untouched (dry audio passthrough) —
    // it must not inject stale output into the buffer.
    for (int ch = 0; ch < kChannels; ++ch)
        for (int s = 0; s < kBlock; ++s)
            EXPECT_FLOAT_EQ(b3.getSample(ch, s), 0.3f)
                << "drop path must pass dry audio through, not stale output";

    // ...and drains the output ring: no stale output survives for a future
    // read (pre-fix outputReadPos lagged behind outputWritePos here).
    EXPECT_EQ(hdr->outputReadPos.load(std::memory_order_relaxed),
              hdr->outputWritePos.load(std::memory_order_acquire))
        << "live drop must drain the output ring";
    EXPECT_GT(hdr->outputWritePos.load(std::memory_order_acquire), 0u)
        << "drain must be non-vacuous (child produced output before the drop)";

    mgr.killPluginHost(slot, KillMode::KillHard);
}

TEST(PluginIsolation, ControlThreadPluginExceptionContained) {
    // A plugin throwing a C++ exception from prepareToPlay (control-thread
    // lifecycle call — the exact path where Odin2 aborts the child via
    // std::terminate) must NOT kill the child: the control loop catches it,
    // marks the plugin failed, and the audio loop keeps running on silence.
    ProxyProcessManager mgr;

    bool spawned = mgr.spawnPluginHost("__throwprepare__", 9070);
    ASSERT_TRUE(spawned) << "child host should start with the throwing-prepare probe";

    for (int i = 0; i < 100 && !mgr.isAlive(9070); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(9070)) << "child should be alive after spawn";

    auto shm = mgr.getShm(9070);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);

    // Sends PREPARE → the child's plugin->prepareToPlay throws → the control
    // loop must catch it and reply instead of aborting the process.
    PluginProxySlot slot(mgr, 9070, "TestPlugin");
    slot.prepareToPlay(44100.0, 512);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(mgr.isAlive(9070))
        << "child must survive a throwing prepareToPlay (was aborting pre-fix)";

    // The audio loop must still run and output silence for the failed plugin.
    uint32_t blocksBefore =
        hdr->audioBlocksProcessed.load(std::memory_order_relaxed);
    juce::AudioBuffer<float> buffer(2, 512);
    for (int s = 0; s < 512; ++s)
    {
        buffer.setSample(0, s, 0.5f);
        buffer.setSample(1, s, -0.5f);
    }
    juce::MidiBuffer midi;
    slot.processBlock(buffer, midi);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_GT(hdr->audioBlocksProcessed.load(std::memory_order_relaxed), blocksBefore)
        << "audio loop should keep running after the plugin failed";
    EXPECT_TRUE(mgr.isAlive(9070)) << "child should still be alive after processing";

    mgr.killPluginHost(9070, KillMode::KillHard);
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

    auto pipe = mgr.getPipe(9050);
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

    auto shm = mgr.getShm(9050);
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

    auto pipe = mgr.getPipe(9051);
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

    auto pipe = mgr.getPipe(9052);
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

    auto pipe = mgr.getPipe(9053);
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

    auto shm = mgr.getShm(9053);
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

    auto pipe = mgr.getPipe(9054);
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

// Current contract (post-e917c1f): a bad plugin path no longer kills the
// child, so per-slot crash dispatch is exercised with deterministic external
// kills — the same mechanism as HardKillFiresCrashCallback. Each slot's
// callback must fire for its OWN death only, an already-reported death must
// not re-fire on a later sweep, and the second death must dispatch only the
// second slot.
#if HDAW_PLUGIN_ISOLATION
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

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9080));
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", 9081));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9080));
    ASSERT_TRUE(mgr.isAlive(9081));

    // Kill slot A only (external kill on the child handle).
    auto* infoA = mgr.getChildInfo(9080);
    ASSERT_NE(infoA, nullptr);
    ASSERT_NE(infoA->processHandle, INVALID_HANDLE_VALUE);
    TerminateProcess(infoA->processHandle, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_GE(slotAFires.load(), 1) << "slot 9080 callback should fire";
    EXPECT_EQ(slotBFires.load(), 0)
        << "slot 9081 callback must NOT fire for slot A's death";

    // Then kill slot B: its own callback fires; A's must not re-fire.
    auto* infoB = mgr.getChildInfo(9081);
    ASSERT_NE(infoB, nullptr);
    ASSERT_NE(infoB->processHandle, INVALID_HANDLE_VALUE);
    TerminateProcess(infoB->processHandle, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    mgr.checkAllChildren();

    EXPECT_GE(slotBFires.load(), 1) << "slot 9081 callback should fire";
    EXPECT_EQ(slotAFires.load(), 1)
        << "an already-reported death must not re-fire on a later sweep";

    mgr.killPluginHost(9080, KillMode::KillHard);
    mgr.killPluginHost(9081, KillMode::KillHard);
}
#endif

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
    auto pipe = mgr.getPipe(9100);
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
    auto shm = mgr.getShm(9100);
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
// Chunked state transfer (state > the fixed 240-byte message payload)
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
// Deterministic failure-path signaling (root-cause fixture for the
// state-round-trip flake). The __slowstate__ internal plugin blocks inside
// getStateInformation longer than the child's 3 s marshal timeout, forcing
// the runLifecycleOnMessageThread timeout branch on every attempt. The
// child must answer result=0 (failure) — never result=1 with 0 bytes, which
// the parent cannot distinguish from a genuinely empty plugin state — and
// the parent must retry the whole handshake a bounded number of times
// before surfacing the failure as an empty buffer.
// ========================================================================

TEST(PluginIsolation, SlowStateTimeoutSignalsFailure) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9142;

    ASSERT_TRUE(mgr.spawnPluginHost("__slowstate__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    PluginProxySlot slot(mgr, slotId, "SlowState");

    // No prior SET_STATE here on purpose: a slot SET would arm the parent's
    // background state-retry worker, whose own verify GET would race this
    // test's GET on the same pipe and skew the timing bounds below.
    const auto t0 = std::chrono::steady_clock::now();
    juce::MemoryBlock out;
    slot.getStateInformation(out);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    EXPECT_EQ(out.getSize(), 0u)
        << "marshal timeout must surface as failure (empty), never as a "
           "false 'success, 0 bytes' snapshot of stale plugin state";
    // Every attempt needs >= 3 s (the child cannot answer before its marshal
    // wait expires), so a fast success/return would mean the retry loop or
    // the failure signaling regressed.
    EXPECT_GE(elapsedMs, 2800)
        << "expected the bounded 3-attempt handshake, got a fast return";
    EXPECT_LE(elapsedMs, 20000)
        << "retry loop must stay bounded (3 attempts x 3 s + backoff)";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// Parent-side retry, wrong-response variant: a response with the wrong type
// (here a GET_PARAM_RESULT/failure injected ahead of the real answer) must
// not be delivered to the GET_STATE handshake, and the round trip must still
// complete with the child's real state. The injected reply carries the id of
// the raw GET_PARAM (0), so the correlation-id match discards it — it cannot
// be mistaken for the GET_STATE response even though it arrives first.
TEST(PluginIsolation, GetStateRetriesAfterWrongTypeResponse) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9143;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    PluginProxySlot slot(mgr, slotId, "StateEcho");

    constexpr size_t kStateSize = 100; // inline: no chunk interleaving
    juce::MemoryBlock in(kStateSize);
    auto* inBytes = static_cast<uint8_t*>(in.getData());
    for (size_t i = 0; i < kStateSize; ++i)
        inBytes[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    slot.setStateInformation(in.getData(), static_cast<int>(in.getSize()));

    // Confirm the child actually applied the state before injecting noise.
    {
        juce::MemoryBlock probe;
        bool applied = false;
        for (int i = 0; i < 50 && !applied; ++i) {
            slot.getStateInformation(probe);
            applied = probe.getSize() == kStateSize
                && std::memcmp(probe.getData(), in.getData(), kStateSize) == 0;
            if (!applied)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(applied) << "child never applied the seeded state";
    }

    // Inject exactly one stale response into the pipe: a bare GET_PARAM is
    // answered by the child with GET_PARAM_RESULT/result=0 — well-formed,
    // but the wrong type and result for a GET_STATE handshake.
    auto pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);
    ProxyMessage seed{};
    seed.type = MessageType::GET_PARAM;
    seed.slotId = slotId;
    ASSERT_TRUE(pipe->sendMsgBounded(seed, 3000));

    juce::MemoryBlock out;
    slot.getStateInformation(out);

    ASSERT_EQ(out.getSize(), kStateSize)
        << "retry loop must not surface the wrong-type failure response";
    EXPECT_EQ(std::memcmp(out.getData(), in.getData(), kStateSize), 0)
        << "round trip must complete with the child's real state after retry";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// Parent-side retry, busy-child variant: while the child's message thread is
// parked inside a slow setStateInformation (3.2 s hold) it cannot service
// GET_STATE. The raw SET's own marshal exceeds the 3 s wait, so the child's
// control thread relays result=0 at ~3 s (attempt 1 of the GET loop consumes
// it as a wrong-type/failure response and retries), and the blob is stored
// the moment the hold ends (~3.2 s) — before attempt 2's GET. The final read
// must therefore return exactly the stored blob. Uses __slowstateset__ whose
// SET-side hold makes the busy window deterministic (stateecho's SET marshal
// is instant and leaves no observable busy window).
TEST(PluginIsolation, GetStateRetriesWhileChildBusyInSetStateMarshal) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9144;

    ASSERT_TRUE(mgr.spawnPluginHost("__slowstateset__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    PluginProxySlot slot(mgr, slotId, "SlowStateSet");

    constexpr size_t kStateSize = 60; // inline small-SET payload
    juce::MemoryBlock blob(kStateSize);
    auto* blobBytes = static_cast<uint8_t*>(blob.getData());
    for (size_t i = 0; i < kStateSize; ++i)
        blobBytes[i] = static_cast<uint8_t>(0xAB ^ i);

    // Raw-pipe small SET_STATE: the child's message thread parks 3.2 s in
    // this store and the SET marshal times out at 3 s (the control thread
    // relays a result=0). Sent raw (not via the slot) so the parent's
    // background retry worker stays out of the picture.
    auto pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);
    ProxyMessage hold{};
    hold.type = MessageType::SET_STATE;
    hold.slotId = slotId;
    hold.dataSize = static_cast<uint32_t>(kStateSize);
    std::memcpy(hold.data, blob.getData(), kStateSize);
    ASSERT_TRUE(pipe->sendMsgBounded(hold, 3000));

    // Immediately read state: attempt 1 cannot be served until the child's
    // SET hold ends (>= 3 s), so the loop must retry.
    const auto t0 = std::chrono::steady_clock::now();
    juce::MemoryBlock out;
    slot.getStateInformation(out);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    ASSERT_EQ(out.getSize(), kStateSize)
        << "retry loop must complete the read once the child is free";
    EXPECT_EQ(std::memcmp(out.getData(), blob.getData(), kStateSize), 0)
        << "completed read must return the child's current state byte-exact";
    EXPECT_GE(elapsedMs, 2800)
        << "first attempt must have hit the busy child (>= 3 s marshal wait)";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// ========================================================================
// Deterministic teardown-vs-async-work regressions.
//
// Root-cause evidence: a CDB second-chance access violation inside
// hdaw_tests!proxy::PluginProxySlot::getStateInformation+0x9c
// ("mov edx, dword ptr [r15+1A0h]" — a re-read of this->slotId where
// r15 == this). The slot's background state-retry worker (std::jthread) and
// its editor watcher (std::thread) both capture `this` and previously
// outlived the destructor body: the jthread only joins during MEMBER
// destruction (after the dtor had already killed the child / released shm)
// and the watcher was detached outright. ~PluginProxySlot now stops and
// joins both before any other teardown.
// ========================================================================

// Destroying a slot while its background state-retry worker is armed mid-
// backoff must (a) return promptly — the dtor joins the worker before any
// member is torn down — and (b) never leave a `this`-capturing thread running
// past the object (which was the measured UAF). A SET on a __slowstateset__
// child takes the shm ring path in setStateInformation, which arms the worker
// synchronously, so the worker is guaranteed to exist by the time we destroy.
TEST(PluginIsolation, DestroyWhileStateRetryWorkerRuns) {
    ProxyProcessManager mgr;

    constexpr size_t kStateSize = 60; // inline small-SET payload
    juce::MemoryBlock blob(kStateSize);
    auto* blobBytes = static_cast<uint8_t*>(blob.getData());
    for (size_t i = 0; i < kStateSize; ++i)
        blobBytes[i] = static_cast<uint8_t>(0x5A ^ i);

    for (int iter = 0; iter < 5; ++iter) {
        const uint32_t slotId = 9145u + static_cast<uint32_t>(iter);
        ASSERT_TRUE(mgr.spawnPluginHost("__slowstateset__", slotId));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ASSERT_TRUE(mgr.isAlive(slotId));

        std::optional<PluginProxySlot> slot;
        slot.emplace(mgr, slotId, "SlowStateSet");

        // Arm the background retry worker (ring publish path), then let it
        // enter its first backoff before we destroy mid-retry.
        slot->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        const auto t0 = std::chrono::steady_clock::now();
        slot.reset(); // <- the destructor under test
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        EXPECT_LT(elapsedMs, 2000)
            << "slot destruction took " << elapsedMs
            << " ms while the state-retry worker was running — the dtor is not "
               "joining its async worker before teardown (cdb AV regression: "
               "getStateInformation re-reading this->slotId after free)";

        // The destructor kills the child; let the process exit be observed.
        for (int w = 0; w < 60 && mgr.isChildAlive(slotId); ++w)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_FALSE(mgr.isChildAlive(slotId))
            << "iter " << iter << ": child survived slot destruction";
    }
}

// Destroying a slot while the editor watcher thread is parked in its bounded
// 500 ms pipe read must join it (never detach it) — the watcher captured
// `this` and, pre-fix, kept calling getPipe/receiveRespBounded after the slot
// had been freed. Bounded destruction proves the join happened.
TEST(PluginIsolation, DestroyWhileEditorWatcherRuns) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9160;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    std::optional<PluginProxySlot> slot;
    slot.emplace(mgr, slotId, "StateEcho");

    // Start the watcher (its loop is now parked in receiveRespBounded 500 ms).
    slot->startEditorWatcher();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    const auto t0 = std::chrono::steady_clock::now();
    slot.reset(); // <- the destructor under test
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    EXPECT_LT(elapsedMs, 1500)
        << "slot destruction took " << elapsedMs
        << " ms with the editor watcher running — the watcher must be JOINED, "
           "never detached (cdb AV regression class: a `this`-capturing thread "
           "outliving the slot)";

    for (int w = 0; w < 60 && mgr.isChildAlive(slotId); ++w)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(mgr.isChildAlive(slotId));
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

    auto pipe = mgr.getPipe(slotId);
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

    // Patterned SysEx >240 bytes (proves the SysEx lane, not the inline path).
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

    auto pipe = mgr.getPipe(slotId);
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

// ========================================================================
// C2b regression: staged params reach the child WITHOUT processBlock
// ========================================================================
//
// Root cause (Phase C2b): the ONLY flush site for staged params was inside
// PluginProxySlot::processBlock, but MainAudioProcessor's transport-stopped
// buzz-guard (transport stopped && !recording && !countIn -> clear + return)
// early-outs the whole audio graph while the transport is stopped, so the
// flush never ran: staged params stayed dirty forever and the child kept its
// boot state. The flush now runs on the slot's 100ms message-thread timer
// (flushStagedParams - the sole paramSet-ring writer, transport-independent),
// and the child drains its own audioLoop regardless of audio input. This
// test proves delivery with NO processBlock call whatsoever.

TEST(PluginIsolation, StagedParamsReachChildWithoutProcessBlock) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9140;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto pipe = mgr.getPipe(slotId);
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

    // Stage a param value via the message path. NEVER call slot.processBlock:
    // the C2b fix must deliver this through the 100ms message-thread timer
    // flush (the JUCE message pump started by test_main dispatches timerCallback).
    params[1]->setValue(0.85f);

    auto* hdr = mgr.getShm(slotId)->getHeader();
    ASSERT_NE(hdr, nullptr);

    bool ringWritten = false;
    bool roundTrip = false;
    float echoed = 0.f;
    const int kMaxPolls = 300;
    for (int i = 0; i < kMaxPolls && !(ringWritten && roundTrip); ++i) {
        if (hdr->paramSetWritePos.load(std::memory_order_relaxed) > 0)
            ringWritten = true;

        // GET_PARAM pipe round-trip: the child's GET_PARAM handler reports the
        // value currently applied to its (previously drained) param, so a
        // result of ~0.85 proves the flushed ring entry reached the child.
        ProxyMessage getMsg{};
        getMsg.type = MessageType::GET_PARAM;
        getMsg.slotId = slotId;
        uint32_t idx = 1;
        std::memcpy(getMsg.data, &idx, sizeof(uint32_t));
        getMsg.dataSize = sizeof(uint32_t);
        static constexpr DWORD kPollTimeoutMs = 200;
        if (pipe->sendMsgBounded(getMsg, kPollTimeoutMs)) {
            ProxyResponse resp{};
            if (pipe->receiveRespBounded(resp, kPollTimeoutMs)
                && resp.type == MessageType::GET_PARAM_RESULT
                && resp.result == 1
                && resp.dataSize >= sizeof(float)) {
                std::memcpy(&echoed, resp.data, sizeof(float));
                if (std::abs(echoed - 0.85f) < 1e-3f)
                    roundTrip = true;
            }
        }

        if (!(ringWritten && roundTrip))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(ringWritten)
        << "paramSet ring was never written: the message-thread timer flush did not run";
    EXPECT_TRUE(roundTrip)
        << "GET_PARAM never returned the flushed 0.85 (child still reports "
        << echoed << ")";
    EXPECT_NEAR(params[1]->getValue(), 0.85f, 1e-4f);

    mgr.killPluginHost(slotId, KillMode::KillHard);
}


TEST(PluginIsolation, StagedParamsBakeIntoChildStateWithoutParentProcessBlock) {
    // C2b-rev: transport-stopped params must not only REACH the child (that is
    // StagedParamsReachChildWithoutProcessBlock) -- they must BAKE into the
    // plugin's state once the child audio loop clocks processBlock (the idle
    // clock / ring drain, exactly like the gearmulator wrapper's param
    // pipeline). __paramstate__ bakes its current parameter values into
    // getStateInformation() at audio time, so the persisted state readback
    // must DIFFER from boot after staged params, with the staged values, and
    // no parent-side processBlock was ever called.
    ProxyProcessManager mgr;
    const uint32_t slotId = 9150;

    ASSERT_TRUE(mgr.spawnPluginHost("__paramstate__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto pipe = mgr.getPipe(slotId);
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

    PluginProxySlot slot(mgr, slotId, "ParamState");
    auto& params = slot.getParameters();
    ASSERT_EQ(params.size(), 3);

    // Boot state readback (before any param staging; nothing has clocked
    // processBlock yet, so the baked values are the zero-initialised defaults).
    juce::MemoryBlock boot;
    slot.getStateInformation(boot);
    ASSERT_GE(boot.getSize(), 4u);

    // Stage params via the parent proxy (=> ring drain => child param
    // application). NEVER call slot.processBlock: delivery + bake must be
    // transport-stopped via the message-thread flush and the child idle clock.
    params[0]->setValue(0.2f);
    params[1]->setValue(0.85f);

    bool sawBaked = false;
    float v0 = 0.f, v1 = 0.f;
    const int kMaxPolls = 400;
    for (int i = 0; i < kMaxPolls && !sawBaked; ++i)
    {
        juce::MemoryBlock blob;
        slot.getStateInformation(blob);
        if (blob.getSize() == boot.getSize())
        {
            if (blob != boot)
            {
                // PStateBlob magic + 3 floats
                if (blob.getSize() >= sizeof(uint32_t) + 3 * sizeof(float))
                {
                    const auto* b = static_cast<const uint8_t*>(blob.getData());
                    uint32_t magic = 0;
                    std::memcpy(&magic, b, sizeof(magic));
                    std::memcpy(&v0, b + sizeof(uint32_t), sizeof(float));
                    std::memcpy(&v1, b + sizeof(uint32_t) + sizeof(float), sizeof(float));
                    if (magic == 0x50535a41u
                        && std::abs(v0 - 0.2f) < 1e-3f
                        && std::abs(v1 - 0.85f) < 1e-3f)
                        sawBaked = true;
                }
            }
        }
        if (!sawBaked)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(sawBaked)
        << "child state never baked the staged params (boot vs post: v0="
        << v0 << " v1=" << v1 << ") — ring/drain/idle-clock chain broken";

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

TEST(PluginIsolation, ProgramBridgeThroughProxy) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9152;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto pipe = mgr.getPipe(slotId);
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

// ========================================================================
// Change A — the hang watchdog must not mistake the INTENTIONAL Virus warmup
// (which blocks the control thread inside processBlock for its whole
// real-time-paced duration) for a hang. Before the fix every virus-family
// spawn wrote a 330-670 MB "processBlock hung for 1s" minidump into the
// child's %TEMP% (measured 4.5 GB across 15 spawns).
// ========================================================================
TEST(PluginIsolation, VirusWarmupWritesNoHangDump) {
    auto scratch = freshScratchDir("warmup");
    ASSERT_TRUE(scratch.createDirectory());

    const std::string oldTmp  = setChildEnv("TMP",  scratch.getFullPathName().toStdString());
    const std::string oldTemp = setChildEnv("TEMP", scratch.getFullPathName().toStdString());
    const std::string oldSec  = setChildEnv("HDAW_CHILD_WARMUP_SECONDS", "2");

    ProxyProcessManager mgr;
    const uint32_t slot = 9480;
    // A Virus-named path that does not exist: loadPlugin falls back to the
    // internal passthrough processor, and the name-based family gate still runs
    // the intentional warmup pump.
    ASSERT_TRUE(mgr.spawnPluginHost("C:\\fake\\Osirus.vst3", slot));
    auto pipe = mgr.getPipe(slot);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slot;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);

    ProxyResponse resp{};
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(pipe->receiveResp(resp)) << "child should answer PREPARE";
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(resp.result, 1u);
    // Real-time-paced warmup: >1 s proves the control thread held
    // processBlockActive long enough for the old 1 s watchdog to fire.
    EXPECT_GT(elapsedMs, 1000) << "warmup did not run long enough to exercise the watchdog";

    // Let any (pre-fix) dump finish writing before measuring, while the child
    // is still alive.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    juce::StringArray dumps;
    countHungDumps(scratch, dumps);
    std::fprintf(stderr, "MARK warmup scratch=%s dumps=%s\n",
                 scratch.getFullPathName().toRawUTF8(),
                 dumps.isEmpty() ? "(none)" : dumps.joinIntoString("; ").toRawUTF8());
    EXPECT_EQ(dumps.size(), 0)
        << "watchdog dump(s) produced by the intentional warmup: " << dumps.joinIntoString(", ");

    mgr.killPluginHost(slot, KillMode::KillHard);
    restoreChildEnv("HDAW_CHILD_WARMUP_SECONDS", oldSec);
    restoreChildEnv("TEMP", oldTemp);
    restoreChildEnv("TMP",  oldTmp);
    scratch.deleteRecursively();
}

// ========================================================================
// P2-b (2026-09-30) — the warmup-EPOCH carryover. The watchdog samples every
// 250 ms, but two warmups on ONE child can be ~1 ms apart: warmup #1 clears
// processBlockActive and the control loop immediately starts warmup #2, so the
// `justFinishedWarmup` edge is never observed and the hang counter carries
// warmup #1's elapsed time into #2 — which then trips its own
// `warmupExpectedMs + 1 s` threshold after only ~1 s and writes a dump during a
// perfectly HEALTHY warmup (measured live: warmups 26 ms apart, dump 1.87 s into
// #2). Fixed by a per-warmup epoch counter that resets the budget when a new
// warmup starts. This test is the regression pin: with the carryover, warmup #2
// dumps; with the fix, neither does. (A genuine hang still dumps —
// RealHangWritesHangDump.)
// ========================================================================
TEST(PluginIsolation, BackToBackWarmupsWriteNoHangDump) {
    auto scratch = freshScratchDir("warmup2");
    ASSERT_TRUE(scratch.createDirectory());

    const std::string oldTmp  = setChildEnv("TMP",  scratch.getFullPathName().toStdString());
    const std::string oldTemp = setChildEnv("TEMP", scratch.getFullPathName().toStdString());
    const std::string oldSec  = setChildEnv("HDAW_CHILD_WARMUP_SECONDS", "2");

    ProxyProcessManager mgr;
    const uint32_t slot = 9482;
    ASSERT_TRUE(mgr.spawnPluginHost("C:\\fake\\Osirus.vst3", slot));
    auto pipe = mgr.getPipe(slot);
    ASSERT_NE(pipe, nullptr);

    // Two PREPAREs with NOTHING between them: no gap the 250 ms sampler can see.
    auto prepareOnce = [&](const char* which) {
        ProxyMessage prepareMsg{};
        prepareMsg.type = MessageType::PREPARE;
        prepareMsg.slotId = slot;
        struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
        std::memcpy(prepareMsg.data, &pd, sizeof(pd));
        prepareMsg.dataSize = sizeof(pd);
        pipe->sendMsg(prepareMsg);

        ProxyResponse resp{};
        const auto t0 = std::chrono::steady_clock::now();
        EXPECT_TRUE(pipe->receiveResp(resp)) << which << ": child should answer PREPARE";
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        EXPECT_EQ(resp.result, 1u);
        EXPECT_GT(elapsedMs, 1000) << which
            << ": warmup did not run long enough to exercise the watchdog";
    };

    prepareOnce("warmup #1");
    prepareOnce("warmup #2");

    // Let any (pre-fix) dump finish writing before measuring.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    juce::StringArray dumps;
    countHungDumps(scratch, dumps);
    std::fprintf(stderr, "MARK warmup2 scratch=%s dumps=%s\n",
                 scratch.getFullPathName().toRawUTF8(),
                 dumps.isEmpty() ? "(none)" : dumps.joinIntoString("; ").toRawUTF8());
    EXPECT_EQ(dumps.size(), 0)
        << "watchdog dump(s) produced by two HEALTHY back-to-back warmups (epoch "
           "carryover): " << dumps.joinIntoString(", ");

    mgr.killPluginHost(slot, KillMode::KillHard);
    restoreChildEnv("HDAW_CHILD_WARMUP_SECONDS", oldSec);
    restoreChildEnv("TEMP", oldTemp);
    restoreChildEnv("TMP",  oldTmp);
    scratch.deleteRecursively();
}

// ========================================================================
// Change A — a GENUINE hang must still dump. The test-only HDAW_TEST_HANG_MS
// hook (PassthroughProcessor::processBlock) holds one processBlock call for
// 2500 ms; with no warmup running the 1 s watchdog must write the minidump.
// ========================================================================
TEST(PluginIsolation, RealHangWritesHangDump) {
    auto scratch = freshScratchDir("realhang");
    ASSERT_TRUE(scratch.createDirectory());

    const std::string oldTmp  = setChildEnv("TMP",  scratch.getFullPathName().toStdString());
    const std::string oldTemp = setChildEnv("TEMP", scratch.getFullPathName().toStdString());
    const std::string oldHang = setChildEnv("HDAW_TEST_HANG_MS", "2500");

    ProxyProcessManager mgr;
    const uint32_t slot = 9481;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slot));
    auto pipe = mgr.getPipe(slot);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage prepareMsg{};
    prepareMsg.type = MessageType::PREPARE;
    prepareMsg.slotId = slot;
    struct { double sr; int32_t bs; int32_t ch; } pd{44100.0, 512, 2};
    std::memcpy(prepareMsg.data, &pd, sizeof(pd));
    prepareMsg.dataSize = sizeof(pd);
    pipe->sendMsg(prepareMsg);
    ProxyResponse resp{};
    ASSERT_TRUE(pipe->receiveResp(resp));
    EXPECT_EQ(resp.result, 1u);

    auto shm = mgr.getShm(slot);
    ASSERT_NE(shm, nullptr);
    auto* hdr = shm->getHeader();
    ASSERT_NE(hdr, nullptr);
    int retries = 100;
    while (hdr->numChannels == 0 && retries-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GT(hdr->numChannels, 0u) << "child didn't initialize shared memory header";

    const uint32_t blockSize = hdr->blockSize > 0 ? hdr->blockSize : 512;
    const uint32_t numChannels = hdr->numChannels > 0 ? hdr->numChannels : 2;
    std::vector<float> input(blockSize * numChannels, 0.0f);
    ASSERT_TRUE(shm->writeInput(input.data(), static_cast<uint32_t>(input.size())));

    juce::StringArray dumps;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        if (countHungDumps(scratch, dumps) > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    EXPECT_GE(dumps.size(), 1)
        << "a genuine processBlock hang produced no minidump";
    // Let MiniDumpWriteDump finish before measuring/killing so the reported
    // size is the real dump, not a zero-byte placeholder.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    countHungDumps(scratch, dumps);
    EXPECT_GT(scratch.getChildFile("hdaw_plugin_host_processBlock hung for 1s.dmp").getSize(), 0)
        << "minidump was created but is empty";
    std::fprintf(stderr, "MARK realhang scratch=%s dumps=%s\n",
                 scratch.getFullPathName().toRawUTF8(),
                 dumps.isEmpty() ? "(none)" : dumps.joinIntoString("; ").toRawUTF8());

    mgr.killPluginHost(slot, KillMode::KillHard);
    restoreChildEnv("HDAW_TEST_HANG_MS", oldHang);
    restoreChildEnv("TEMP", oldTemp);
    restoreChildEnv("TMP",  oldTmp);
    scratch.deleteRecursively();
}

// ========================================================================
// Pipe/shm LEASE lifetime (root-cause fix for the raw-pointer lifetime
// hazards). getPipe/getShm hand out a shared_ptr lease taken under the
// manager mutex; a concurrent killPluginHost now only signals + cancels the
// pipe and drops the MAP's reference, so the object (and its OS handle) stays
// valid for the whole exchange and is closed by ~PipeServer at the last lease
// release. Before the fix the map owned the object and killPluginHost/erase
// freed it under a caller that still held the raw pointer.
// ========================================================================

// A lease taken before a kill must stay usable afterwards — and the killed
// slot must disappear from the map. The bounded op on the dead lease must
// FAIL cleanly (no crash, no unbounded wait).
TEST(PluginIsolation, PipeLeaseSurvivesKill) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9501;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto lease = mgr.getPipe(slotId);
    ASSERT_NE(lease, nullptr);

    mgr.killPluginHost(slotId, KillMode::KillHard);

    // The lease still owns a live PipeServer: a bounded op on it must return
    // cleanly (false) instead of touching a freed object.
    ProxyResponse resp{};
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = lease->receiveRespBounded(resp, 500);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_FALSE(ok) << "a bounded op on a lease whose child was killed must fail";
    EXPECT_LT(elapsedMs, 3000) << "the bounded op must stay bounded after the kill";

    EXPECT_FALSE(mgr.getPipe(slotId)) << "the killed slot must leave the map";
    EXPECT_FALSE(mgr.getShm(slotId));
}

// The actual hazard: killPluginHost landing while a bounded read is IN FLIGHT
// on a slot worker. stop() must cancel (not close) the handle, so the reader
// returns false promptly and the process never touches a closed handle.
// __slowstate__ parks the child inside GET_STATE for >3 s, so the read cannot
// complete on its own before the kill lands.
TEST(PluginIsolation, StopRacesInFlightBoundedRead) {
    ProxyProcessManager mgr;

    for (int iter = 0; iter < 5; ++iter) {
        const uint32_t slotId = 9510u + static_cast<uint32_t>(iter);
        ASSERT_TRUE(mgr.spawnPluginHost("__slowstate__", slotId));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ASSERT_TRUE(mgr.isAlive(slotId));

        auto lease = mgr.getPipe(slotId);
        ASSERT_NE(lease, nullptr);

        std::atomic<bool> done{false};
        std::atomic<bool> readOk{false};
        std::thread reader([&] {
            ProxyMessage msg{};
            msg.type = MessageType::GET_STATE;
            msg.slotId = slotId;
            if (lease->sendMsgBounded(msg, 3000)) {
                ProxyResponse resp{};
                readOk.store(lease->receiveRespBounded(resp, 5000));
            }
            done.store(true);
        });

        // Let the read actually enter ReadFile; the child cannot answer.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        ASSERT_FALSE(done.load())
            << "iter " << iter << ": the read must still be in flight when the kill lands";

        const auto t0 = std::chrono::steady_clock::now();
        mgr.killPluginHost(slotId, KillMode::KillHard);
        reader.join();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        EXPECT_TRUE(done.load());
        EXPECT_FALSE(readOk.load())
            << "iter " << iter << ": the cancelled in-flight read must fail cleanly";
        EXPECT_LT(elapsedMs, 3000)
            << "iter " << iter << ": kill must unblock the in-flight read promptly";
        EXPECT_FALSE(mgr.getPipe(slotId))
            << "iter " << iter << ": killed slot must leave the map";
    }
}

// The slot's editor watcher (a `this`-capturing thread parked in a bounded
// pipe read) against a kill from another thread: the watcher must observe the
// cancelled pipe, keep looping, and be joined by the destructor — bounded, no
// crash, no read against a closed handle.
TEST(PluginIsolation, EditorWatcherVsKill) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9520;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    std::optional<PluginProxySlot> slot;
    slot.emplace(mgr, slotId, "StateEcho");

    slot->startEditorWatcher();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Kill the child out from under the watcher's in-flight 500 ms read.
    mgr.killPluginHost(slotId, KillMode::KillHard);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto t0 = std::chrono::steady_clock::now();
    slot.reset(); // <- the destructor under test (joins the watcher)
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    EXPECT_LT(elapsedMs, 1500)
        << "destruction with the watcher running against a killed child took "
        << elapsedMs << " ms — the watcher must be joined, never detached";
}

// The shm region must be kept alive by the SLOT'S lease, not by the manager
// map: a kill erases the entry, and the region must still be readable.
TEST(PluginIsolation, ShmLeaseSurvivesKill) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9530;

    ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto shmLease = mgr.getShm(slotId);
    ASSERT_NE(shmLease, nullptr);
    auto* hdr = shmLease->getHeader();
    ASSERT_NE(hdr, nullptr);
    // Parent-written field: proves the mapping is live and unmodified.
    EXPECT_EQ(hdr->capacity, 1024u);

    mgr.killPluginHost(slotId, KillMode::KillHard);

    EXPECT_NE(shmLease->getHeader(), nullptr)
        << "the lease must keep the ShmRegion alive after the map entry is erased";
    EXPECT_EQ(shmLease->getHeader()->capacity, 1024u)
        << "the leased region must still be readable after the kill";
    EXPECT_FALSE(mgr.getShm(slotId)) << "the killed slot must leave the map";
}

// LEAK GATE — the single-closer rule. stop() must NOT close the handle (a
// stop() that mirrored the handle away from the dtor would leak it; a stop()
// that closed it would double-close), and ~PipeServer must close it exactly
// once, at the last lease release. Cycles: spawn -> take BOTH leases ->
// KillHard -> drop the leases (PipeServer + ShmRegion destroyed) and measure
// the process handle count. A leaked handle shows as a steady climb (~1/cycle).
TEST(PluginIsolation, PipeHandleNotLeakedAcrossKillCycles) {
    ProxyProcessManager mgr;
    constexpr int kCycles = 20;
    std::vector<DWORD> counts;

    DWORD n0 = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &n0));
    counts.push_back(n0);

    for (int i = 0; i < kCycles; ++i) {
        const uint32_t slotId = 9560u + static_cast<uint32_t>(i);
        ASSERT_TRUE(mgr.spawnPluginHost("__stateecho__", slotId));

        auto pipeLease = mgr.getPipe(slotId);
        auto shmLease = mgr.getShm(slotId);
        ASSERT_NE(pipeLease, nullptr);
        ASSERT_NE(shmLease, nullptr);

        mgr.killPluginHost(slotId, KillMode::KillHard);
        EXPECT_FALSE(mgr.getPipe(slotId)) << "iter " << i << ": slot must leave the map";

        // Destroy the leased objects -> ~PipeServer closes the pipe handle,
        // ~ShmRegion unmaps + closes the mapping handle.
        pipeLease.reset();
        shmLease.reset();

        DWORD n = 0;
        ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &n));
        counts.push_back(n);
    }

    std::string trace;
    for (size_t i = 0; i < counts.size(); ++i) {
        trace += std::to_string(counts[i]);
        if (i + 1 < counts.size()) trace += ",";
    }
    std::fprintf(stderr, "MARK handlecount baseline=%lu final=%lu series=%s\n",
                 static_cast<unsigned long>(counts.front()),
                 static_cast<unsigned long>(counts.back()), trace.c_str());

    const long growth = static_cast<long>(counts.back()) - static_cast<long>(counts.front());
    EXPECT_LE(growth, 6)
        << "process handle count grew by " << growth << " over " << kCycles
        << " kill cycles (series " << trace << ") — a leaked pipe/shm HANDLE "
           "would grow ~1 per cycle (the single-closer rule is broken)";
}

// STEERING 3 gate: the OPTIMISTIC program-count poll runs on the JUCE message
// thread (100 ms timer) and must NEVER wait for the exchange lock. While another
// thread holds the lock (here a real multi-second-scale exchange window is
// simulated by holding the guard), the poll must return promptly AND send
// nothing — a blocked poll would stall the message pump (timers/UI).
//
// Asserted: (a) the poll sequence returns well within the bound while the lock
// is held by another thread, (b) the cached program count is unchanged (it
// could only change if a GET_PROGRAM_COUNT had actually reached the child).
TEST(PluginIsolation, PollProgramCountDoesNotWaitForTheExchangeLock) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9170;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    PluginProxySlot slot(mgr, slotId, "Passthrough");
    const int countBefore = slot.getNumPrograms();

    auto pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    std::atomic<bool> lockHeld{false};
    std::atomic<bool> release{false};
    std::thread holder([&] {
        // A blocking exchange owning the lock for the whole window — exactly
        // what a slow GET_STATE / PREPARE handshake does on this pipe.
        PipeServer::Exchange ex(*pipe);
        lockHeld.store(true);
        while (!release.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    for (int i = 0; i < 1000 && !lockHeld.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(lockHeld.load()) << "the helper exchange never acquired the lock";

    // The poll gates on a ~1 s cadence (every 10th tick), so drive enough ticks
    // that the guarded path is certainly reached.
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 12; ++i)
        slot.pollProgramCount();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    EXPECT_LT(elapsedMs, 50)
        << "pollProgramCount waited " << elapsedMs
        << " ms for the exchange lock — the message-thread timer must try_lock "
           "and skip when the pipe is busy";
    EXPECT_EQ(slot.getNumPrograms(), countBefore)
        << "a skipped poll must not have reached the child";

    release.store(true);
    holder.join();
    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// ========================================================================
// Exchange serialization (defect A): one lock spans every request→response
// transaction on a slot's PipeServer, so two users of the SAME pipe can never
// interleave `A-send, B-send, A-receive` and consume each other's reply.
// ========================================================================

namespace {
// Unique OS pipe name per test invocation (the manager's own namespace helper
// is not reachable here and a fixed name would collide across parallel runs).
std::string uniqueExchangeTestPipeName(const char* tag) {
    static std::atomic<uint32_t> counter{0};
    return std::string("\\\\.\\pipe\\hdaw_test_exchange_") + tag + "_"
        + std::to_string(static_cast<unsigned>(::GetCurrentProcessId())) + "_"
        + std::to_string(counter.fetch_add(1));
}

// The server connects LAZILY on its first read (overlappedConnect), so a
// sendRequest before any read would fail the connected check. Prime the
// connection with a short requestless receive: nothing is queued yet, so it
// just connects and times out. (Production does the same via the READY await.)
void primePipeConnection(PipeServer& srv) {
    PipeServer::Exchange ex(srv);
    ProxyResponse p{};
    ex.receiveReply(p, MessageType::READY, 100);
}
} // namespace

// Two threads issue DISTINGUISHABLE request→response transactions on the SAME
// slot's pipe concurrently and must each receive THEIR OWN reply.
//
// Pre-fix (no exchange lock) this test fails in one of two ways, both
// probabilistic: a thread reads the other's response type (its own type check
// then rejects a reply it did receive), or — with the run long enough — a
// reply is consumed by the wrong caller. Post-fix the transactions are
// serialized, so the assertion below is deterministic: every reply carries the
// type the issuing thread asked for and NOTHING was ever observed stale.
TEST(PluginIsolation, ConcurrentExchangesDoNotMisattribute) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9700;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    auto pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    constexpr int kIterations = 50;
    std::atomic<int> countWrong{0}, currentWrong{0};
    std::atomic<int> countOk{0}, currentOk{0};

    auto runProgramCount = [&] {
        for (int i = 0; i < kIterations; ++i) {
            PipeServer::Exchange ex(*pipe);
            ProxyMessage msg{};
            msg.type = MessageType::GET_PROGRAM_COUNT;
            msg.slotId = slotId;
            if (!ex.sendRequest(msg, 3000)) { ++countWrong; continue; }
            ProxyResponse resp{};
            // The expectation is THIS thread's reply type: an interleaved
            // transaction's reply is a different type and would be counted.
            if (!ex.receiveReply(resp, MessageType::GET_PROGRAM_COUNT_RESULT, 3000)
                || resp.type != MessageType::GET_PROGRAM_COUNT_RESULT
                || resp.result != 1)
                ++countWrong;
            else
                ++countOk;
        }
    };
    auto runCurrentProgram = [&] {
        for (int i = 0; i < kIterations; ++i) {
            PipeServer::Exchange ex(*pipe);
            ProxyMessage msg{};
            msg.type = MessageType::GET_CURRENT_PROGRAM;
            msg.slotId = slotId;
            if (!ex.sendRequest(msg, 3000)) { ++currentWrong; continue; }
            ProxyResponse resp{};
            if (!ex.receiveReply(resp, MessageType::GET_CURRENT_PROGRAM_RESULT, 3000)
                || resp.type != MessageType::GET_CURRENT_PROGRAM_RESULT
                || resp.result != 1)
                ++currentWrong;
            else
                ++currentOk;
        }
    };

    std::thread a(runProgramCount);
    std::thread b(runCurrentProgram);
    a.join();
    b.join();

    EXPECT_EQ(countWrong.load(), 0)
        << "GET_PROGRAM_COUNT received a reply that was not its own "
        << countWrong.load() << "/" << kIterations << " times";
    EXPECT_EQ(currentWrong.load(), 0)
        << "GET_CURRENT_PROGRAM received a reply that was not its own "
        << currentWrong.load() << "/" << kIterations << " times";
    EXPECT_EQ(countOk.load(), kIterations);
    EXPECT_EQ(currentOk.load(), kIterations);

    // Serialization means no reply can ever be observed out of order.
    EXPECT_EQ(pipe->staleRepliesDiscarded(), 0u)
        << "a reply arrived out of order — the exchange lock did not span a "
           "complete transaction";
    EXPECT_FALSE(pipe->isDesynced());

    mgr.killPluginHost(slotId, KillMode::KillHard);
}

// An UNSOLICITED EDITOR_CLOSED that lands while another exchange owns the pipe
// must reach the editor-closed callback, NOT the in-flight exchange as its
// reply. In-process pipe pair (no child process) so the ordering is exact.
// The exchange's own reply carries the correlation id it stamped into its
// request: only a reply bearing THAT id is its own.
TEST(PluginIsolation, UnsolicitedEditorClosedIsRoutedNotConsumed) {
    const std::string pipeName = uniqueExchangeTestPipeName("editorclosed");
    PipeServer srv(pipeName);
    DWORD err = 0;
    ASSERT_TRUE(srv.start(&err));

    std::atomic<int> editorClosedCalls{0};
    srv.setEditorClosedHandler([&] { editorClosedCalls.fetch_add(1); });

    std::atomic<bool> clientUp{false};
    std::thread client([&] {
        PipeClient cli(pipeName);
        if (!cli.connect()) return;
        clientUp.store(true);

        // The parent's GET_PROGRAM_COUNT carries the correlation id we must
        // echo back on its reply. Read the request to learn it.
        ProxyMessage req{};
        if (!cli.receiveMsg(req)) return;

        // 1) the unsolicited editor-close notification (id 0 by design),
        // 2) the real reply for the exchange the parent is running.
        ProxyResponse closed{};
        closed.type = MessageType::EDITOR_CLOSED;
        closed.requestId = kUnsolicitedRequestId;
        closed.result = 1;
        cli.sendResp(closed);

        ProxyResponse ok{};
        ok.type = MessageType::GET_PROGRAM_COUNT_RESULT;
        ok.requestId = req.requestId;
        ok.result = 1;
        const uint32_t count = 3;
        ok.dataSize = sizeof(count);
        std::memcpy(ok.data, &count, sizeof(count));
        cli.sendResp(ok);
    });
    for (int i = 0; i < 1000 && !clientUp.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(clientUp.load()) << "in-process PipeClient failed to connect";

    primePipeConnection(srv);

    PipeServer::Exchange ex(srv);
    ProxyMessage req{};
    req.type = MessageType::GET_PROGRAM_COUNT;
    ASSERT_TRUE(ex.sendRequest(req, 5000));
    const uint32_t sentId = ex.currentRequestId();
    ProxyResponse resp{};
    ASSERT_TRUE(ex.receiveReply(resp, MessageType::GET_PROGRAM_COUNT_RESULT, 5000))
        << "the exchange must still receive its OWN reply behind the "
           "unsolicited EDITOR_CLOSED";

    EXPECT_EQ(editorClosedCalls.load(), 1)
        << "an unsolicited EDITOR_CLOSED must be routed to the editor-closed "
           "callback exactly once";
    EXPECT_EQ(resp.type, MessageType::GET_PROGRAM_COUNT_RESULT);
    EXPECT_NE(sentId, 0u) << "a request must carry a non-zero correlation id";
    EXPECT_EQ(resp.requestId, sentId)
        << "the delivered reply must carry the request's correlation id";
    uint32_t got = 0;
    std::memcpy(&got, resp.data, sizeof(got));
    EXPECT_EQ(got, 3u);
    // A routed EDITOR_CLOSED is NOT a stale reply, and the exchange that
    // completed behind it leaves the pipe in sync.
    EXPECT_EQ(srv.staleRepliesDiscarded(), 0u);
    EXPECT_FALSE(srv.isDesynced());

    client.join();
}

// THE DEFECT (lesson 41, now closed): a bounded receive that times out leaves
// its reply queued, and the protocol carries a correlation id so a late reply
// of the SAME type as the next request is still provably NOT that request's.
//
// Two SEQUENTIAL exchanges on one pipe: exchange 1 times out, its late reply
// lands, exchange 2 (a SAME-TYPE request) must discard it (wrong id) and
// deliver its own.
TEST(PluginIsolation, DesyncedPipeDiscardsUnexpectedReply) {
    const std::string pipeName = uniqueExchangeTestPipeName("desync");
    PipeServer srv(pipeName);
    DWORD err = 0;
    ASSERT_TRUE(srv.start(&err));

    std::atomic<bool> clientUp{false};
    std::atomic<bool> staleWritten{false};
    // Set after the connection-priming receive (which itself records a desync
    // timeout) so the client waits for the NEXT desync — exchange 1's timeout.
    std::atomic<uint64_t> desyncBaseline{0};
    std::thread client([&] {
        PipeClient cli(pipeName);
        if (!cli.connect()) return;
        clientUp.store(true);

        // Request #1 (the one that will time out) — learn its id.
        ProxyMessage m1{};
        if (!cli.receiveMsg(m1)) return;
        for (int i = 0; i < 2000
             && srv.desyncEvents() <= desyncBaseline.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

        // The late reply for request #1: SAME type as the next exchange expects,
        // but carrying request #1's (stale) id — indistinguishable from fresh
        // WITHOUT the correlation id.
        ProxyResponse stale{};
        stale.type = MessageType::GET_PROGRAM_COUNT_RESULT;
        stale.requestId = m1.requestId;
        stale.result = 1;
        const uint32_t staleCount = 111;
        stale.dataSize = sizeof(staleCount);
        std::memcpy(stale.data, &staleCount, sizeof(staleCount));
        if (cli.sendResp(stale))
            staleWritten.store(true);

        // Request #2 — echo ITS id on the reply.
        ProxyMessage m2{};
        if (!cli.receiveMsg(m2)) return;
        ProxyResponse ok{};
        ok.type = MessageType::GET_PROGRAM_COUNT_RESULT;
        ok.requestId = m2.requestId;
        ok.result = 1;
        const uint32_t count = 7;
        ok.dataSize = sizeof(count);
        std::memcpy(ok.data, &count, sizeof(count));
        cli.sendResp(ok);
    });
    for (int i = 0; i < 1000 && !clientUp.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(clientUp.load()) << "in-process PipeClient failed to connect";

    primePipeConnection(srv);
    desyncBaseline.store(srv.desyncEvents());

    // Exchange 1: nothing is queued for this expectation, so the bounded
    // receive times out and the pipe records the desync.
    {
        PipeServer::Exchange ex(srv);
        ProxyMessage req{};
        req.type = MessageType::GET_PROGRAM_COUNT;
        ASSERT_TRUE(ex.sendRequest(req, 5000));
        ProxyResponse resp{};
        EXPECT_FALSE(ex.receiveReply(resp, MessageType::GET_PROGRAM_COUNT_RESULT, 200))
            << "the first exchange must time out (no reply is queued yet)";
    }
    EXPECT_TRUE(srv.isDesynced()) << "a bounded-receive timeout must mark the pipe desynced";
    EXPECT_GE(srv.desyncEvents(), 1u);

    for (int i = 0; i < 2000 && !staleWritten.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(staleWritten.load());

    // Exchange 2: a SAME-TYPE request. It reads the stale reply first (its id
    // is not this exchange's), discards it, then returns its own.
    {
        PipeServer::Exchange ex(srv);
        ProxyMessage req{};
        req.type = MessageType::GET_PROGRAM_COUNT;
        ASSERT_TRUE(ex.sendRequest(req, 5000));
        const uint32_t sentId = ex.currentRequestId();
        ProxyResponse resp{};
        ASSERT_TRUE(ex.receiveReply(resp, MessageType::GET_PROGRAM_COUNT_RESULT, 5000))
            << "the exchange must skip the stale SAME-TYPE reply and still get its own";
        EXPECT_EQ(resp.type, MessageType::GET_PROGRAM_COUNT_RESULT);
        EXPECT_EQ(resp.requestId, sentId)
            << "the delivered reply must carry THIS request's id, not the stale one";
        uint32_t got = 0;
        std::memcpy(&got, resp.data, sizeof(got));
        EXPECT_EQ(got, 7u) << "the STALE count must never be delivered";
    }
    EXPECT_EQ(srv.staleRepliesDiscarded(), 1u)
        << "the stale SAME-TYPE reply must be logged/counted as discarded";
    EXPECT_FALSE(srv.isDesynced()) << "a completed exchange clears the desync hint";

    client.join();
}

// The steering upgrade of the above: SEVERAL sequential requests inside ONE
// Exchange guard (exactly how fetchParamMetadata / the GET_STATE retry loop
// issue theirs). Request #1 times out, so its late reply carries a stale id
// even though it is the SAME type as request #2 — the guard must deliver
// request #2's own reply only, and count the stale one. A DIFFERENT-type
// request (#3) in the same guard proves matching is by id, not by type.
TEST(PluginIsolation, LateSameTypeReplyIsDiscarded) {
    const std::string pipeName = uniqueExchangeTestPipeName("lateSameType");
    PipeServer srv(pipeName);
    DWORD err = 0;
    ASSERT_TRUE(srv.start(&err));

    std::atomic<bool> clientUp{false};
    std::thread client([&] {
        PipeClient cli(pipeName);
        if (!cli.connect()) return;
        clientUp.store(true);

        ProxyMessage m1{}, m2{};
        if (!cli.receiveMsg(m1)) return;   // request #1 (will time out)
        if (!cli.receiveMsg(m2)) return;   // request #2 (same type)

        // The stale reply for request #1 (SAME type as #2, stale id)…
        ProxyResponse stale{};
        stale.type = MessageType::GET_PROGRAM_COUNT_RESULT;
        stale.requestId = m1.requestId;
        stale.result = 1;
        const uint32_t staleCount = 111;
        stale.dataSize = sizeof(staleCount);
        std::memcpy(stale.data, &staleCount, sizeof(staleCount));
        cli.sendResp(stale);

        // …followed by request #2's own reply.
        ProxyResponse ok{};
        ok.type = MessageType::GET_PROGRAM_COUNT_RESULT;
        ok.requestId = m2.requestId;
        ok.result = 1;
        const uint32_t count = 222;
        ok.dataSize = sizeof(count);
        std::memcpy(ok.data, &count, sizeof(count));
        cli.sendResp(ok);

        // Request #3: a DIFFERENT type in the same guard.
        ProxyMessage m3{};
        if (!cli.receiveMsg(m3)) return;
        ProxyResponse cur{};
        cur.type = MessageType::GET_CURRENT_PROGRAM_RESULT;
        cur.requestId = m3.requestId;
        cur.result = 1;
        const uint32_t prog = 5;
        cur.dataSize = sizeof(prog);
        std::memcpy(cur.data, &prog, sizeof(prog));
        cli.sendResp(cur);
    });
    for (int i = 0; i < 1000 && !clientUp.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(clientUp.load()) << "in-process PipeClient failed to connect";

    primePipeConnection(srv);

    PipeServer::Exchange ex(srv);

    // Request #1: times out (the client is waiting for request #2 first).
    ProxyMessage req{};
    req.type = MessageType::GET_PROGRAM_COUNT;
    ASSERT_TRUE(ex.sendRequest(req, 5000));
    const uint32_t id1 = ex.currentRequestId();
    ProxyResponse r1{};
    EXPECT_FALSE(ex.receiveReply(r1, MessageType::GET_PROGRAM_COUNT_RESULT, 150))
        << "request #1 must time out inside the guard";

    // Request #2: SAME type, SAME guard, fresh id. The stale #1 reply arrives
    // first and must be discarded; #2 gets its own reply.
    ProxyMessage req2{};
    req2.type = MessageType::GET_PROGRAM_COUNT;
    ASSERT_TRUE(ex.sendRequest(req2, 5000));
    const uint32_t id2 = ex.currentRequestId();
    ASSERT_NE(id2, id1) << "each request must allocate a fresh id";
    ProxyResponse r2{};
    ASSERT_TRUE(ex.receiveReply(r2, MessageType::GET_PROGRAM_COUNT_RESULT, 5000))
        << "request #2 must receive its own reply despite the stale SAME-TYPE one";
    EXPECT_EQ(r2.requestId, id2) << "the reply must carry request #2's id, not #1's";
    uint32_t gotCount = 0;
    std::memcpy(&gotCount, r2.data, sizeof(gotCount));
    EXPECT_EQ(gotCount, 222u);
    EXPECT_EQ(srv.staleRepliesDiscarded(), 1u)
        << "the stale same-type reply (id #1) must be counted as discarded";

    // Request #3: DIFFERENT type, same guard — id matching is type-independent.
    ProxyMessage req3{};
    req3.type = MessageType::GET_CURRENT_PROGRAM;
    ASSERT_TRUE(ex.sendRequest(req3, 5000));
    const uint32_t id3 = ex.currentRequestId();
    ProxyResponse r3{};
    ASSERT_TRUE(ex.receiveReply(r3, MessageType::GET_CURRENT_PROGRAM_RESULT, 5000));
    EXPECT_EQ(r3.type, MessageType::GET_CURRENT_PROGRAM_RESULT);
    EXPECT_EQ(r3.requestId, id3);
    uint32_t gotProg = 0;
    std::memcpy(&gotProg, r3.data, sizeof(gotProg));
    EXPECT_EQ(gotProg, 5u);
    EXPECT_EQ(srv.staleRepliesDiscarded(), 1u)
        << "no further stale replies — request #3's reply was correctly matched";
    EXPECT_FALSE(srv.isDesynced());

    client.join();
}

// Process-level companion to LateSameTypeReplyIsDiscarded: a REAL child holds a
// reply past the parent's budget. __slowstate__ blocks getStateInformation past
// the child's 3 s marshal timeout, so the parent's bounded receive times out
// and the reply lands late, carrying the timed-out request's id.
TEST(PluginIsolation, SlowChildLateReplyIsDiscardedById) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9180;
    ASSERT_TRUE(mgr.spawnPluginHost("__slowstate__", slotId));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(slotId));

    auto pipe = mgr.getPipe(slotId);
    ASSERT_NE(pipe, nullptr);

    // Request #1: GET_STATE. The child cannot answer within the short budget,
    // so its reply is left queued (id #1).
    const uint64_t staleBefore = pipe->staleRepliesDiscarded();
    {
        PipeServer::Exchange ex(*pipe);
        ProxyMessage msg{};
        msg.type = MessageType::GET_STATE;
        msg.slotId = slotId;
        ASSERT_TRUE(ex.sendRequest(msg, 3000));
        ProxyResponse resp{};
        EXPECT_FALSE(ex.receiveReply(resp, MessageType::GET_STATE_RESULT, 400))
            << "the child must not answer within the parent's short budget";
    }

    // Request #2: a DIFFERENT type. The child's late GET_STATE_RESULT (id #1)
    // arrives while we wait; it must be discarded by id, and the
    // GET_PROGRAM_COUNT_RESULT (id #2) delivered.
    {
        PipeServer::Exchange ex(*pipe);
        ProxyMessage msg{};
        msg.type = MessageType::GET_PROGRAM_COUNT;
        msg.slotId = slotId;
        ASSERT_TRUE(ex.sendRequest(msg, 10000));
        const uint32_t sentId = ex.currentRequestId();
        ProxyResponse resp{};
        ASSERT_TRUE(ex.receiveReply(resp, MessageType::GET_PROGRAM_COUNT_RESULT, 10000))
            << "the exchange must discard the late id-#1 reply and get its own";
        EXPECT_EQ(resp.type, MessageType::GET_PROGRAM_COUNT_RESULT);
        EXPECT_EQ(resp.requestId, sentId);
        EXPECT_EQ(resp.result, 1u);
    }
    EXPECT_GT(pipe->staleRepliesDiscarded(), staleBefore)
        << "the late reply bearing request #1's id must be counted as discarded";

    // Request #3: SAME type as request #1 (GET_STATE). It must receive ITS OWN
    // reply (result=0 — __slowstate__'s marshal times out) rather than any
    // leftover late reply.
    {
        PipeServer::Exchange ex(*pipe);
        ProxyMessage msg{};
        msg.type = MessageType::GET_STATE;
        msg.slotId = slotId;
        ASSERT_TRUE(ex.sendRequest(msg, 10000));
        const uint32_t sentId = ex.currentRequestId();
        ProxyResponse resp{};
        ASSERT_TRUE(ex.receiveReply(resp, MessageType::GET_STATE_RESULT, 10000))
            << "the same-type retry must receive its own id-#3 reply";
        EXPECT_EQ(resp.type, MessageType::GET_STATE_RESULT);
        EXPECT_EQ(resp.requestId, sentId);
    }

    mgr.killPluginHost(slotId, KillMode::KillHard);
}



// ========================================================================
// LEGACY (v1) LAYOUT DETECTION at the READY handshake (Gate 4/15)
//
// A child built BEFORE the correlation-id framing does not know about
// requestId, so it answers READY with the OLD 256-byte layout
// {type, result=1, dataSize=0, data[244]}. A v2 parent decodes that frame as
// {type=READY, requestId=1, result=0, dataSize=0} — a requestId no requestless
// handshake owns. Correlation-id matching would DISCARD it, and the spawn
// would then die by BARE TIMEOUT (the exact failure mode that makes a stale
// hdaw_plugin_host.exe so confusing). The handshake must instead recognise the
// v1 signature and report the mismatch IMMEDIATELY.
//
// The fixture writes those RAW v1 bytes on an in-process pipe, so the check is
// on the parent's framing decode, not on any child binary.
// ========================================================================
namespace {
// The v1 (pre-correlation-id) ProxyResponse layout, byte for byte:
//   [0..3] type   [4..7] result   [8..11] dataSize   [12..255] data
void writeRawV1Ready(PipeClient& cli, int32_t type, int32_t result, int32_t dataSize) {
    alignas(256) uint8_t v1Raw[256] = {};
    std::memcpy(v1Raw + 0, &type, sizeof(type));
    std::memcpy(v1Raw + 4, &result, sizeof(result));
    std::memcpy(v1Raw + 8, &dataSize, sizeof(dataSize));
    ProxyResponse frame{};   // same fixed 256-byte message-mode frame
    static_assert(sizeof(frame) == 256, "the pipe frame is fixed at 256 bytes");
    std::memcpy(&frame, v1Raw, sizeof(frame));
    cli.sendResp(frame);
}
} // namespace

TEST(PluginIsolation, LegacyV1ReadyIsReportedNotTimedOut) {
    const std::string pipeName = uniqueExchangeTestPipeName("legacyv1");
    PipeServer srv(pipeName);
    DWORD err = 0;
    ASSERT_TRUE(srv.start(&err));

    std::atomic<bool> clientUp{false};
    std::atomic<bool> goWrite{false};
    std::thread client([&] {
        PipeClient cli(pipeName);
        if (!cli.connect()) return;
        clientUp.store(true);
        // Hold the frame until the server has connected (primePipeConnection),
        // so this test exercises the READY DECODE, not the connect path.
        for (int i = 0; i < 2000 && !goWrite.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        // The v1 child's READY: result=1 (its version), dataSize=0.
        writeRawV1Ready(cli, static_cast<int32_t>(MessageType::READY), 1, 0);
    });
    for (int i = 0; i < 1000 && !clientUp.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(clientUp.load()) << "in-process PipeClient failed to connect";

    primePipeConnection(srv);
    goWrite.store(true);

    const auto t0 = std::chrono::steady_clock::now();
    PipeServer::Exchange ex(srv);
    ProxyResponse resp{};
    const bool got = ex.receiveReplyReady(resp, MessageType::READY);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_TRUE(got)
        << "the handshake must RETURN the legacy reply for diagnosis instead of "
           "discarding it and timing out";
    EXPECT_TRUE(srv.sawLegacyProtocolReady())
        << "the v1 READY must be reported as a protocol mismatch";
    EXPECT_EQ(resp.type, MessageType::READY);
    EXPECT_LT(elapsedMs, 2000)
        << "detection must be immediate, not a budget expiry (elapsed "
        << elapsedMs << " ms)";
    EXPECT_NE(std::string(proxy::kLegacyV1Diagnosis).find("stale v1 hdaw_plugin_host.exe"),
              std::string::npos)
        << "the operator-facing diagnosis must name the stale binary";

    client.join();
}

// Negative control: a CURRENT (v2) child's READY — unsolicited id 0, reporting
// kProtocolVersion — must be delivered as before and must NOT be flagged as a
// legacy binary. The frame is written in the v2 layout, so only the signature
// distinguishes the two cases.
TEST(PluginIsolation, CurrentV2ReadyIsNotFlaggedAsLegacy) {
    const std::string pipeName = uniqueExchangeTestPipeName("readyv2");
    PipeServer srv(pipeName);
    DWORD err = 0;
    ASSERT_TRUE(srv.start(&err));

    std::atomic<bool> clientUp{false};
    std::atomic<bool> goWrite{false};
    std::thread client([&] {
        PipeClient cli(pipeName);
        if (!cli.connect()) return;
        clientUp.store(true);
        for (int i = 0; i < 2000 && !goWrite.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ProxyResponse ready{};
        ready.type = MessageType::READY;
        ready.requestId = kUnsolicitedRequestId;
        ready.result = proxy::kProtocolVersion;
        cli.sendResp(ready);
    });
    for (int i = 0; i < 1000 && !clientUp.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_TRUE(clientUp.load()) << "in-process PipeClient failed to connect";

    primePipeConnection(srv);
    goWrite.store(true);

    PipeServer::Exchange ex(srv);
    ProxyResponse resp{};
    EXPECT_TRUE(ex.receiveReplyReady(resp, MessageType::READY));
    EXPECT_FALSE(srv.sawLegacyProtocolReady())
        << "a current-framing READY must never be reported as a stale v1 child";
    EXPECT_EQ(resp.requestId, kUnsolicitedRequestId);
    EXPECT_TRUE(proxy::protocolVersionAccepted(resp.result))
        << "the v2 handshake must accept the child's advertised version";

    client.join();
}
