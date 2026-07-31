# Thorough DLL Loading Tests — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the plugin isolation test suite exercise real DLL loading end-to-end — load a real VST3 plugin in the child process, process audio through it, save/restore state, enumerate parameters, and verify crash isolation.

**Architecture:** The root cause of all DLL loading failures is that `PluginHost::loadPluginByPath` constructs a `PluginDescription` manually (just path + format name), but JUCE's VST3 hosting requires the description to be populated by `findAllTypesForFile` first. Fix that one call, then build a comprehensive test suite using the existing `PassthroughTest.vst3` test plugin and a new `__crash__` sentinel mode for crash testing.

**Tech Stack:** C++20, JUCE 8 (`AudioPluginFormatManager`, `VST3PluginFormat`), gtest, Windows named pipes + shared memory IPC.

**Root cause (diagnosed 2026-07-30):**
```
findAllTypesForFile → createPluginInstance: OK ✅
manual PluginDescription → createPluginInstance: FAIL ❌ ("Unable to load VST-3 plug-in file")
```
`PluginDescription` needs `uniqueId`, `category`, and other fields that only `findAllTypesForFile` populates. Both our test plugin and third-party plugins (Mono.vst3) load fine when the description comes from `findAllTypesForFile`.

---

## File Structure

**Modified files:**
- `src/proxy/host/PluginHost.cpp` — fix `loadPluginByPath` to use `findAllTypesForFile`; add `__crash__` sentinel mode
- `tests/integration/proxy/isolation_integration_test.cpp` — remove diagnostic test, add comprehensive DLL loading tests

**No new files needed.** The test plugin (`PassthroughTest.vst3`) already exists at `build/tests/test-plugin/HDAWTestPlugin_artefacts/Debug/VST3/`.

---

### Task 1: Fix `loadPluginByPath` to use `findAllTypesForFile`

**Files:** Modify `src/proxy/host/PluginHost.cpp`

This is the root cause fix. The current code constructs a `PluginDescription` with only `fileOrIdentifier` and `pluginFormatName`, which JUCE's VST3 hosting rejects. The fix: call `findAllTypesForFile` to get a fully-populated description, then use it.

- [ ] **Step 1:** Replace `loadPluginByPath` in `src/proxy/host/PluginHost.cpp`.

Replace the current implementation:
```cpp
bool PluginHost::loadPluginByPath(const juce::String& path) {
    if (path == "__passthrough__") {
        plugin = std::make_unique<PassthroughProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    juce::String error;

    for (auto* fmt : formatManager.getFormats()) {
        if (fmt->fileMightContainThisPluginType(path)) {
            juce::PluginDescription desc;
            desc.fileOrIdentifier = path;
            desc.pluginFormatName = fmt->getName();

            plugin = formatManager.createPluginInstance(desc, 44100.0, 512, error);
            if (plugin) {
                pluginLoaded.store(true);
                return true;
            }
        }
    }

    return false;
}
```

With:
```cpp
bool PluginHost::loadPluginByPath(const juce::String& path) {
    if (path == "__passthrough__") {
        plugin = std::make_unique<PassthroughProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    juce::String error;

    for (auto* fmt : formatManager.getFormats()) {
        if (!fmt->fileMightContainThisPluginType(path))
            continue;

        // JUCE's VST3 hosting requires the PluginDescription to be
        // populated by findAllTypesForFile — a manually-constructed
        // description (just path + format name) is missing required
        // fields (uniqueId, category, etc.) and will fail to load.
        juce::OwnedArray<juce::PluginDescription> types;
        fmt->findAllTypesForFile(types, path);

        for (auto* desc : types) {
            plugin = formatManager.createPluginInstance(*desc, 44100.0, 512, error);
            if (plugin) {
                pluginLoaded.store(true);
                return true;
            }
        }
    }

    return false;
}
```

- [ ] **Step 2:** Build.

```bash
cmake --build build --config Debug --target hdaw_plugin_host
```
Expected: clean compile.

- [ ] **Step 3:** Verify the test plugin now loads in the child. Run the existing passthrough test (it uses `__passthrough__` so it won't exercise this path, but confirms no regression):

```bash
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.AudioRoundTripWithPassthrough"
```
Expected: PASS.

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/host/PluginHost.cpp
git commit -m "proxy: fix loadPluginByPath to use findAllTypesForFile (root cause of VST3 load failure)"
```

---

### Task 2: Add `__crash__` sentinel mode to PluginHost

**Files:** Modify `src/proxy/host/PluginHost.cpp`

For crash isolation testing, we need a plugin that deliberately crashes during `processBlock`. Add a `__crash__` sentinel that creates a processor which calls `abort()` on the first `processBlock` call.

- [ ] **Step 1:** Add a `CrashingProcessor` class in the anonymous namespace at the top of `PluginHost.cpp`, after the existing `PassthroughProcessor`:

```cpp
class CrashingProcessor : public juce::AudioPluginInstance
{
public:
    CrashingProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "CrashTest"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        // Deliberately crash to test isolation
        std::abort();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "CrashTest";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__crash__";
    }
};
```

- [ ] **Step 2:** Add the `__crash__` sentinel to `loadPluginByPath`, right after the `__passthrough__` check:

```cpp
    if (path == "__crash__") {
        plugin = std::make_unique<CrashingProcessor>();
        pluginLoaded.store(true);
        return true;
    }
```

- [ ] **Step 3:** Build.

```bash
cmake --build build --config Debug --target hdaw_plugin_host
```
Expected: clean compile.

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/host/PluginHost.cpp
git commit -m "proxy: add __crash__ sentinel mode for crash isolation testing"
```

---

### Task 3: Add DLL loading test — real VST3 in child process

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

This test spawns the child with the real `PassthroughTest.vst3` DLL, sends PREPARE, pushes audio through, and verifies the output matches the input.

- [ ] **Step 1:** Add a helper to find the test plugin path. Add this function near the top of the file (after the `using namespace proxy;` line):

```cpp
static juce::File findBuiltTestPlugin() {
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    // build/Debug/hdaw_tests.exe → build/tests/test-plugin/.../PassthroughTest.vst3
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
```

- [ ] **Step 2:** Add the DLL loading audio round-trip test. Append after the existing `CrashAndRestartWithPassthrough` test:

```cpp
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

    // Send PREPARE
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

    // Write a 440 Hz sine
    std::vector<float> input(totalSamples);
    for (uint32_t i = 0; i < totalSamples; ++i)
        input[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f
                     * static_cast<float>(i) / 44100.0f);

    ASSERT_TRUE(shm->writeInput(input.data(), totalSamples));

    // Wait for output
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

    // PassthroughTest copies input to output
    for (uint32_t i = 0; i < totalSamples; ++i) {
        EXPECT_NEAR(output[i], input[i], 0.0001f)
            << "Sample " << i << " mismatch";
    }

    mgr.killPluginHost(9050);
}
```

- [ ] **Step 3:** Build and run.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.DLLLoadAndAudioRoundTrip"
```
Expected: PASS.

- [ ] **Step 4:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: add DLL loading audio round-trip test with real VST3 plugin"
```

---

### Task 4: Add parameter enumeration test

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

Verify that the child can enumerate plugin parameters via the IPC protocol.

- [ ] **Step 1:** Add the test after `DLLLoadAndAudioRoundTrip`:

```cpp
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

    // Request parameter count
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

    // PassthroughTest has 0 parameters
    EXPECT_EQ(paramCount, 0u) << "PassthroughTest should have 0 parameters";

    mgr.killPluginHost(9051);
}
```

- [ ] **Step 2:** Build and run.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.DLLParameterEnumeration"
```
Expected: PASS.

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: add DLL parameter enumeration test"
```

---

### Task 5: Add state save/restore test with real DLL

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

Verify that plugin state can be retrieved from the child and sent back.

- [ ] **Step 1:** Add the test:

```cpp
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

    // GET_STATE
    ProxyMessage getMsg{};
    getMsg.type = MessageType::GET_STATE;
    getMsg.slotId = 9052;
    pipe->sendMsg(getMsg);

    ProxyResponse getResp{};
    ASSERT_TRUE(pipe->receiveResp(getResp));
    EXPECT_EQ(getResp.result, 1u) << "GET_STATE should succeed";
    EXPECT_GT(getResp.dataSize, 0u) << "PassthroughTest should return state bytes";

    // Save the state bytes
    std::vector<uint8_t> savedState(getResp.data, getResp.data + getResp.dataSize);

    // SET_STATE — send the state back
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
```

- [ ] **Step 2:** Build and run.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.DLLStateSaveRestore"
```
Expected: PASS.

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: add DLL state save/restore test"
```

---

### Task 6: Add crash isolation test with `__crash__` mode

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

Verify that when a plugin crashes during `processBlock`, the DAW side detects it and the crash doesn't propagate.

- [ ] **Step 1:** Add the test:

```cpp
TEST(PluginIsolation, CrashIsolationDuringProcessBlock) {
    ProxyProcessManager mgr;

    std::atomic<bool> crashDetected{false};
    std::atomic<uint32_t> crashedSlot{0};
    mgr.setCrashCallback([&](uint32_t slotId) {
        crashDetected.store(true);
        crashedSlot.store(slotId);
    });

    // Spawn with the crashing processor
    bool spawned = mgr.spawnPluginHost("__crash__", 9053);
    ASSERT_TRUE(spawned);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(mgr.isAlive(9053));

    // Send PREPARE so the audio loop starts
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

    // Write audio to trigger processBlock → crash
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

    // Wait for the child to crash
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // The child should be dead
    EXPECT_FALSE(mgr.isAlive(9053)) << "Child should have crashed";

    // checkAllChildren should detect it
    mgr.checkAllChildren();
    EXPECT_TRUE(crashDetected.load()) << "Crash callback should have fired";
    EXPECT_EQ(crashedSlot.load(), 9053u);

    // DAW-side proxy should handle the crash gracefully
    // (processBlock returns silence, no crash propagation)
    mgr.killPluginHost(9053);
}
```

- [ ] **Step 2:** Build and run.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.CrashIsolationDuringProcessBlock"
```
Expected: PASS.

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: add crash isolation test (plugin crashes during processBlock)"
```

---

### Task 7: Add graceful shutdown test with real DLL

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

Verify that sending SHUTDOWN to a child with a real DLL causes a clean exit.

- [ ] **Step 1:** Add the test:

```cpp
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

    // Send SHUTDOWN
    auto* pipe = mgr.getPipe(9054);
    ASSERT_NE(pipe, nullptr);

    ProxyMessage shutdownMsg{};
    shutdownMsg.type = MessageType::SHUTDOWN;
    shutdownMsg.slotId = 9054;
    pipe->sendMsg(shutdownMsg);

    // Wait for the child to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Child should have exited
    EXPECT_FALSE(mgr.isAlive(9054)) << "Child should exit after SHUTDOWN";

    mgr.killPluginHost(9054);
}
```

- [ ] **Step 2:** Build and run.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.DLLGracefulShutdown"
```
Expected: PASS.

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: add graceful shutdown test with real DLL"
```

---

### Task 8: Remove diagnostic test, run full suite

**Files:** Modify `tests/integration/proxy/isolation_integration_test.cpp`

- [ ] **Step 1:** Remove the `DIAG_VST3LoadingTrace` test (the entire `TEST(PluginIsolation, DIAG_VST3LoadingTrace) { ... }` block and the `#include <iostream>` if no longer needed).

- [ ] **Step 2:** Build and run the full suite.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter="PluginIsolation.*"
```
Expected: all tests pass (no skips except when test plugin isn't built).

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/isolation_integration_test.cpp
git commit -m "test: remove VST3 diagnostic, finalize DLL loading test suite"
```

---

## Test coverage summary

| Test | What it verifies | DLL loaded? |
|------|-----------------|-------------|
| `AudioRoundTripWithPassthrough` | IPC audio pipeline (built-in) | No (internal) |
| `CrashAndRestartWithPassthrough` | Kill + respawn (built-in) | No (internal) |
| `DLLLoadAndAudioRoundTrip` | **Real VST3 DLL loads, processes audio** | **Yes** |
| `DLLParameterEnumeration` | **Parameter count via IPC** | **Yes** |
| `DLLStateSaveRestore` | **GET_STATE / SET_STATE round-trip** | **Yes** |
| `CrashIsolationDuringProcessBlock` | **Plugin crash doesn't kill DAW** | **Yes (crash mode)** |
| `DLLGracefulShutdown` | **SHUTDOWN message → clean exit** | **Yes** |
| + 10 existing lifecycle/IPC tests | Spawn, kill, shm, heartbeat, proxy slot | No |
