# Plugin Process Isolation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run VST3/CLAP plugins in a separate child process so crashes don't take down the DAW, with automatic recovery and state preservation.

**Architecture:** Per-plugin child process (`hdaw_plugin_host.exe`) communicates with the DAW via named pipes (control) and shared-memory SPSC ring buffers (audio). The DAW side presents a `PluginProxySlot` that implements `juce::AudioPluginInstance` — the rest of the engine sees a normal plugin. On crash, the DAW detects pipe disconnect, shows a dialog, re-spawns the child, and restores state from auto-saved snapshots.

**Tech Stack:** C++20, Windows API (`CreateFileMapping`, `CreateNamedPipe`, `CreateProcess`), JUCE 8 (AudioPluginInstance interface), gtest. Build: `cmake --build build --config Debug`.

**Spec:** `docs/superpowers/specs/2026-06-30-plugin-process-isolation-design.md`

---

## File Structure

**New files (DAW side, compiled into HDAW_lib):**
- `src/proxy/ProxyCommon.h` — shared types (ProxyMessage, ProxyResponse, MessageType, ShmHeader)
- `src/proxy/ProxySharedMemory.h/.cpp` — shared memory setup, ring buffer access (DAW side)
- `src/proxy/ProxyPipe.h/.cpp` — named pipe client (DAW side)
- `src/proxy/ProxyProcessManager.h/.cpp` — spawn/monitor/kill child processes
- `src/proxy/PluginProxySlot.h/.cpp` — `juce::AudioPluginInstance` wrapper
- `src/proxy/ProxyEditor.h/.cpp` — lightweight proxy editor UI card

**New files (child process, separate binary):**
- `src/proxy/host/main.cpp` — entry point, parses args, runs PluginHost
- `src/proxy/host/PluginHost.h/.cpp` — loads plugin, dispatches IPC messages
- `src/proxy/host/HostAudioThread.h/.cpp` — audio processing thread
- `src/proxy/host/HostControlThread.h/.cpp` — pipe listener
- `src/proxy/host/HostGuiThread.h/.cpp` — plugin editor window

**New test files:**
- `tests/unit/proxy/ring_buffer_test.cpp`
- `tests/unit/proxy/pipe_test.cpp`
- `tests/unit/proxy/proxy_slot_test.cpp`
- `tests/integration/proxy/end_to_end_test.cpp`

---

## Phase 1 — Foundation (Types, Ring Buffer, Pipe)

### Task 1: Shared types header

**Files:** Create `src/proxy/ProxyCommon.h`

- [ ] **Step 1:** Create `src/proxy/` directory.

- [ ] **Step 2:** Create `src/proxy/ProxyCommon.h` with all shared types:

```cpp
#pragma once
#include <cstdint>
#include <atomic>

namespace proxy {

// Magic number for shared memory validation
constexpr uint32_t SHM_MAGIC = 0x48444157; // "HDAW"

// Message types for control pipe
enum class MessageType : uint32_t {
    // Lifecycle
    READY = 0,
    PREPARE,
    PREPARE_RESULT,
    SHUTDOWN,

    // Audio
    PROCESS_BLOCK,

    // State
    SET_STATE,
    GET_STATE,
    GET_STATE_RESULT,

    // Parameters
    SET_PARAM,
    GET_PARAM,
    GET_PARAM_RESULT,
    GET_PARAM_COUNT,
    GET_PARAM_COUNT_RESULT,
    GET_PARAM_INFO,
    GET_PARAM_INFO_RESULT,

    // GUI
    SHOW_EDITOR,
    CLOSE_EDITOR,
    EDITOR_CLOSED,
    PARAM_CHANGED,

    // Health
    HEARTBEAT,
};

// Fixed-size message on the control pipe (256 bytes)
struct alignas(256) ProxyMessage {
    MessageType type;
    uint32_t slotId;
    uint32_t dataSize;   // bytes of payload following this struct
    uint8_t  data[244];  // inline payload
};

struct alignas(256) ProxyResponse {
    MessageType type;
    uint32_t result;     // 0 = error, 1 = success
    uint32_t dataSize;   // bytes of response payload
    uint8_t  data[244];  // inline payload
};

// Shared memory layout (one region per plugin slot)
struct ShmHeader {
    uint32_t magic;
    uint32_t numChannels;
    uint32_t blockSize;
    uint32_t sampleRate;
    uint32_t capacity;     // ring buffer capacity (power of 2)

    // Audio input ring (DAW writes, child reads)
    std::atomic<uint32_t> inputWritePos;
    std::atomic<uint32_t> inputReadPos;

    // Audio output ring (child writes, DAW reads)
    std::atomic<uint32_t> outputWritePos;
    std::atomic<uint32_t> outputReadPos;

    // MIDI input ring (DAW writes, child reads)
    std::atomic<uint32_t> midiInWritePos;
    std::atomic<uint32_t> midiInReadPos;

    // MIDI output ring (child writes, DAW reads)
    std::atomic<uint32_t> midiOutWritePos;
    std::atomic<uint32_t> midiOutReadPos;

    // Health check
    std::atomic<uint32_t> childAlive;
    std::atomic<uint32_t> dawAlive;
};

// MIDI event in the ring buffer
struct MidiEvent {
    uint32_t sampleOffset; // position within the block
    uint8_t  data[3];      // MIDI message bytes
    uint8_t  _pad;         // alignment
};

// Compute shared memory size for a given configuration
inline uint32_t computeShmSize(uint32_t numChannels, uint32_t blockSize) {
    // Round up to power of 2 for ring buffer
    uint32_t cap = 1;
    while (cap < blockSize * numChannels) cap <<= 1;

    uint32_t headerSize = sizeof(ShmHeader);
    uint32_t inputRing  = cap * sizeof(float);
    uint32_t outputRing = cap * sizeof(float);
    uint32_t midiInRing  = 256 * sizeof(MidiEvent);
    uint32_t midiOutRing = 256 * sizeof(MidiEvent);

    return headerSize + inputRing + outputRing + midiInRing + midiOutRing;
}

} // namespace proxy
```

- [ ] **Step 3:** Verify header compiles: `cmake --build build --config Debug`

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/ProxyCommon.h
git commit -m "proxy: add shared types header (ProxyCommon.h)"
```

---

### Task 2: SPSC ring buffer — implementation + tests

**Files:** Create `src/proxy/ProxyRingBuffer.h`, `tests/unit/proxy/ring_buffer_test.cpp`

- [ ] **Step 1:** Write the failing test.

Create `tests/unit/proxy/ring_buffer_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include "proxy/ProxyRingBuffer.h"
#include <thread>
#include <vector>

using namespace proxy;

TEST(RingBuffer, WriteAndReadSingleSample) {
    RingBuffer<float> rb(64); // capacity must be power of 2
    float val = 3.14f;
    ASSERT_TRUE(rb.write(&val, 1));
    float out = 0;
    ASSERT_TRUE(rb.read(&out, 1));
    EXPECT_FLOAT_EQ(out, 3.14f);
}

TEST(RingBuffer, WriteBeyondCapacityFails) {
    RingBuffer<float> rb(4);
    float vals[4] = {1, 2, 3, 4};
    ASSERT_TRUE(rb.write(vals, 4));
    float extra = 5;
    EXPECT_FALSE(rb.write(&extra, 1)); // full, should fail
}

TEST(RingBuffer, ReadFromEmptyFails) {
    RingBuffer<float> rb(4);
    float out = 0;
    EXPECT_FALSE(rb.read(&out, 1));
}

TEST(RingBuffer, WrapAround) {
    RingBuffer<float> rb(4);
    float vals[4] = {1, 2, 3, 4};
    ASSERT_TRUE(rb.write(vals, 4));
    float out[4];
    ASSERT_TRUE(rb.read(out, 4));
    // Write again (wraps around)
    float vals2[4] = {5, 6, 7, 8};
    ASSERT_TRUE(rb.write(vals2, 4));
    float out2[4];
    ASSERT_TRUE(rb.read(out2, 4));
    EXPECT_FLOAT_EQ(out2[0], 5.0f);
}

TEST(RingBuffer, SPSCConcurrent) {
    RingBuffer<float> rb(1024);
    const int N = 10000;

    std::thread writer([&]() {
        for (int i = 0; i < N; ++i) {
            float val = static_cast<float>(i);
            while (!rb.write(&val, 1)) { /* spin */ }
        }
    });

    std::thread reader([&]() {
        int count = 0;
        while (count < N) {
            float val;
            if (rb.read(&val, 1)) {
                EXPECT_FLOAT_EQ(val, static_cast<float>(count));
                count++;
            }
        }
    });

    writer.join();
    reader.join();
}

TEST(RingBuffer, WriteMultipleSamples) {
    RingBuffer<float> rb(8);
    float input[3] = {1.0f, 2.0f, 3.0f};
    ASSERT_TRUE(rb.write(input, 3));
    float output[3] = {};
    ASSERT_TRUE(rb.read(output, 3));
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 2.0f);
    EXPECT_FLOAT_EQ(output[2], 3.0f);
}
```

- [ ] **Step 2:** Run test to verify it fails.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter=RingBuffer.*
```
Expected: compile error — `proxy/ProxyRingBuffer.h` not found.

- [ ] **Step 3:** Create `src/proxy/ProxyRingBuffer.h`:

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace proxy {

// Single-producer single-consumer lock-free ring buffer.
// Capacity is rounded up to the nearest power of 2.
template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(uint32_t capacity)
        : cap(nextPow2(capacity), mask(capacity - 1)
    {
        buffer = new T[cap];
    }

    ~RingBuffer() { delete[] buffer; }

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Write `count` samples. Returns true if all written, false if not enough space.
    bool write(const T* data, uint32_t count) {
        uint32_t w = writePos.load(std::memory_order_relaxed);
        uint32_t r = readPos.load(std::memory_order_acquire);
        uint32_t available = cap - (w - r);
        if (count > available) return false;

        for (uint32_t i = 0; i < count; ++i)
            buffer[(w + i) & mask] = data[i];

        writePos.store(w + count, std::memory_order_release);
        return true;
    }

    // Read `count` samples. Returns true if all read, false if not enough data.
    bool read(T* data, uint32_t count) {
        uint32_t r = readPos.load(std::memory_order_relaxed);
        uint32_t w = writePos.load(std::memory_order_acquire);
        uint32_t available = w - r;
        if (count > available) return false;

        for (uint32_t i = 0; i < count; ++i)
            data[i] = buffer[(r + i) & mask];

        readPos.store(r + count, std::memory_order_release);
        return true;
    }

    uint32_t availableToRead() const {
        return writePos.load(std::memory_order_acquire) -
               readPos.load(std::memory_order_relaxed);
    }

    uint32_t availableToWrite() const {
        return cap - availableToRead();
    }

private:
    static uint32_t nextPow2(uint32_t v) {
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4;
        v |= v >> 8; v |= v >> 16;
        return v + 1;
    }

    T* buffer;
    uint32_t cap;
    uint32_t mask;
    alignas(64) std::atomic<uint32_t> writePos{0};
    alignas(64) std::atomic<uint32_t> readPos{0};
};

} // namespace proxy
```

- [ ] **Step 4:** Add ring buffer test to `tests/CMakeLists.txt`:
```cmake
add_executable(hdaw_tests
    test_main.cpp
    unit/mcp/json_rpc_test.cpp
    # ... existing ...
    unit/proxy/ring_buffer_test.cpp
)
```

- [ ] **Step 5:** Run tests — all should pass.

```bash
cmake --build build --config Debug --target hdaw_tests
build\Debug\hdaw_tests.exe --gtest_filter=RingBuffer.*
```
Expected: 6/6 PASS.

- [ ] **Step 6:** Commit.

```bash
git add src/proxy/ProxyRingBuffer.h tests/unit/proxy/ring_buffer_test.cpp tests/CMakeLists.txt
git commit -m "proxy: add SPSC ring buffer with tests"
```

---

### Task 3: Named pipe client/server — implementation + tests

**Files:** Create `src/proxy/ProxyPipe.h`, `src/proxy/ProxyPipe.cpp`, `tests/unit/proxy/pipe_test.cpp`

- [ ] **Step 1:** Write failing tests.

Create `tests/unit/proxy/pipe_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include "proxy/ProxyPipe.h"
#include <thread>

using namespace proxy;

TEST(Pipe, ServerClientRoundTrip) {
    PipeServer server("\\\\.\\pipe\\hdaw_test_pipe_1");
    ASSERT_TRUE(server.start());

    std::thread clientThread([]() {
        PipeClient client("\\\\.\\pipe\\hdaw_test_pipe_1");
        ASSERT_TRUE(client.connect());

        ProxyMessage msg{};
        msg.type = MessageType::READY;
        msg.slotId = 42;
        ASSERT_TRUE(client.send(msg));

        ProxyResponse resp{};
        ASSERT_TRUE(client.receive(resp));
        EXPECT_EQ(resp.type, MessageType::READY);
        EXPECT_EQ(resp.result, 1u);
    });

    ProxyMessage received{};
    ASSERT_TRUE(server.receive(received));
    EXPECT_EQ(received.type, MessageType::READY);
    EXPECT_EQ(received.slotId, 42u);

    ProxyResponse resp{};
    resp.type = MessageType::READY;
    resp.result = 1;
    ASSERT_TRUE(server.send(resp));

    clientThread.join();
    server.stop();
}

TEST(Pipe, SendReceiveLargePayload) {
    PipeServer server("\\\\.\\pipe\\hdaw_test_pipe_2");
    ASSERT_TRUE(server.start());

    std::thread clientThread([]() {
        PipeClient client("\\\\.\\pipe\\hdaw_test_pipe_2");
        ASSERT_TRUE(client.connect());

        ProxyMessage msg{};
        msg.type = MessageType::SET_STATE;
        msg.dataSize = 1024;
        // Fill payload with pattern
        for (int i = 0; i < 1024 && i < 244; ++i)
            msg.data[i] = static_cast<uint8_t>(i & 0xFF);
        ASSERT_TRUE(client.send(msg));
    });

    ProxyMessage received{};
    ASSERT_TRUE(server.receive(received));
    EXPECT_EQ(received.type, MessageType::SET_STATE);

    clientThread.join();
    server.stop();
}
```

- [ ] **Step 2:** Add to `tests/CMakeLists.txt` and verify tests fail (pipe not implemented).

- [ ] **Step 3:** Create `src/proxy/ProxyPipe.h`:

```cpp
#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <string>

namespace proxy {

class PipeServer {
public:
    explicit PipeServer(const std::string& pipeName);
    ~PipeServer();

    bool start();
    void stop();
    bool receive(ProxyMessage& msg);
    bool send(const ProxyResponse& resp);

private:
    std::string name;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    bool running = false;
};

class PipeClient {
public:
    explicit PipeClient(const std::string& pipeName);
    ~PipeClient();

    bool connect();
    void disconnect();
    bool send(const ProxyMessage& msg);
    bool receive(ProxyResponse& resp);

private:
    std::string name;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
};

} // namespace proxy
```

- [ ] **Step 4:** Create `src/proxy/ProxyPipe.cpp`:

```cpp
#include "ProxyPipe.h"
#include <cstring>

namespace proxy {

// --- PipeServer ---

PipeServer::PipeServer(const std::string& pipeName) : name(pipeName) {}

PipeServer::~PipeServer() { stop(); }

bool PipeServer::start() {
    hPipe = CreateNamedPipeA(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(ProxyResponse),
        sizeof(ProxyMessage),
        0,
        nullptr);
    return hPipe != INVALID_HANDLE_VALUE;
}

void PipeServer::stop() {
    running = false;
    if (hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeServer::receive(ProxyMessage& msg) {
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hPipe, &msg, sizeof(ProxyMessage), &bytesRead, nullptr);
    return ok && bytesRead >= sizeof(ProxyMessage) - sizeof(msg.data);
}

bool PipeServer::send(const ProxyResponse& resp) {
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &resp, sizeof(ProxyResponse), &bytesWritten, nullptr);
}

// --- PipeClient --PipeClient ---

PipeClient::PipeClient(const std::string& pipeName) : name(pipeName) {}

PipeClient::~PipeClient() { disconnect(); }

bool PipeClient::connect() {
    hPipe = CreateFileA(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);
    return true;
}

void PipeClient::disconnect() {
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::send(const ProxyMessage& msg) {
    DWORD bytesWritten = 0;
    return WriteFile(hPipe, &msg, sizeof(ProxyMessage), &bytesWritten, nullptr);
}

bool PipeClient::receive(ProxyResponse& resp) {
    DWORD bytesRead = 0;
    return ReadFile(hPipe, &resp, sizeof(ProxyResponse), &bytesRead, nullptr);
}

} // namespace proxy
```

- [ ] **Step 5:** Add `ProxyPipe.cpp` to HDAW_lib in `CMakeLists.txt`. Add pipe test to `tests/CMakeLists.txt`.

- [ ] **Step 6:** Build + run pipe tests. Expected: 2/2 PASS.

- [ ] **Step 7:** Commit.

```bash
git add src/proxy/ProxyPipe.h src/proxy/ProxyPipe.cpp tests/unit/proxy/pipe_test.cpp
git commit -m "proxy: add named pipe client/server with tests"
```

---

### Task 4: Shared memory helper — implementation + tests

**Files:** Create `src/proxy/ProxySharedMemory.h`, `src/proxy/ProxySharedMemory.cpp`, `tests/unit/proxy/shm_test.cpp`

- [ ] **Step 1:** Write failing tests.

```cpp
#include <gtest/gtest.h>
#include "proxy/ProxySharedMemory.h"

using namespace proxy;

TEST(SharedMemory, CreateAndMap) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_1", 4096));
    EXPECT_NE(region.getHeader(), nullptr);
    EXPECT_EQ(region.getHeader()->magic, SHM_MAGIC);
}

TEST(SharedMemory, WriteAndReadSamples) {
    ShmRegion region;
    ASSERT_TRUE(region.create("hdaw_test_shm_2", 4096));

    auto* hdr = region.getHeader();
    hdr->numChannels = 2;
    hdr->blockSize = 4;
    hdr->capacity = 8; // power of 2

    float input[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_TRUE(region.writeInput(input, 8));

    float output[8] = {};
    ASSERT_TRUE(region.readInput(output, 8));
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[7], 8.0f);
}
```

- [ ] **Step 2:** Create `src/proxy/ProxySharedMemory.h`:

```cpp
#pragma once
#include "ProxyCommon.h"
#include <windows.h>
#include <string>

namespace proxy {

class ShmRegion {
public:
    ShmRegion() = default;
    ~ShmRegion();

    bool create(const std::string& name, uint32_t totalSize);
    bool open(const std::string& name);
    void close();

    ShmHeader* getHeader() const;
    float* getInputRing() const;
    float* getOutputRing() const;
    MidiEvent* getMidiInRing() const;
    MidiEvent* getMidiOutRing() const;

    bool writeInput(const float* data, uint32_t count);
    bool readInput(float* data, uint32_t count);
    bool writeOutput(const float* data, uint32_t count);
    bool readOutput(float* data, uint32_t count);

private:
    HANDLE hMap = INVALID_HANDLE_VALUE;
    void* basePtr = nullptr;
    uint32_t size = 0;
};

} // namespace proxy
```

- [ ] **Step 3:** Create `src/proxy/ProxySharedMemory.cpp` with the implementation using `CreateFileMapping`/`MapViewOfFile`. Add to HDAW_lib in CMakeLists.txt.

- [ ] **Step 4:** Add shm test to `tests/CMakeLists.txt`, build, run.

- [ ] **Step 5:** Commit.

```bash
git add src/proxy/ProxySharedMemory.h src/proxy/ProxySharedMemory.cpp tests/unit/proxy/shm_test.cpp
git commit -m "proxy: add shared memory helper with tests"
```

---

## Phase 2 — Child Process (`hdaw_plugin_host.exe`)

### Task 5: Child process entry point + PluginHost skeleton

**Files:** Create `src/proxy/host/main.cpp`, `src/proxy/host/PluginHost.h`, `src/proxy/host/PluginHost.cpp`

- [ ] **Step 1:** Create `src/proxy/host/` directory.

- [ ] **Step 2:** Create `src/proxy/host/main.cpp`:

```cpp
#include "PluginHost.h"
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uint32_t slotId = 0;
    std::string pipeName, shmName, pluginPath;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--slot=", 7) == 0)
            slotId = static_cast<uint32_t>(atoi(argv[i] + 7));
        else if (strncmp(argv[i], "--pipe=", 7) == 0)
            pipeName = argv[i] + 7;
        else if (strncmp(argv[i], "--shm=", 6) == 0)
            shmName = argv[i] + 6;
        else if (strncmp(argv[i], "--plugin=", 9) == 0)
            pluginPath = argv[i] + 9;
    }

    if (pipeName.empty() || shmName.empty() || pluginPath.empty())
        return 1;

    PluginHost host(slotId, pipeName, shmName, pluginPath);
    return host.run();
}
```

- [ ] **Step 3:** Create `src/proxy/host/PluginHost.h`:

```cpp
#pragma once
#include "proxy/ProxyCommon.h"
#include "proxy/ProxyPipe.h"
#include "proxy/ProxySharedMemory.h"
#include <string>
#include <atomic>
#include <memory>

class PluginHost {
public:
    PluginHost(uint32_t slotId, const std::string& pipeName,
               const std::string& shmName, const std::string& pluginPath);
    ~PluginHost();

    int run();

private:
    void controlLoop();
    void audioLoop();
    bool loadPlugin();

    uint32_t slotId;
    std::string pipeName, shmName, pluginPath;

    proxy::PipeServer pipe;
    proxy::ShmRegion shm;

    std::atomic<bool> running{true};
    std::atomic<bool> pluginLoaded{false};

    // Plugin state (loaded via JUCE format manager)
    // Will be filled in Task 6
    // std::unique_ptr<juce::AudioPluginInstance> plugin;
};
```

- [ ] **Step 4:** Create `src/proxy/host/PluginHost.cpp` skeleton (constructor, destructor, run stubs).

- [ ] **Step 5:** Add `src/proxy/host/*.cpp` to a new CMake target `hdaw_plugin_host`. Link against `HDAW_lib`, `Qt6::Core`, `juce::juce_audio_processors`.

- [ ] **Step 6:** Build both `HDAW` and `hdaw_plugin_host`. Verify `hdaw_plugin_host.exe` is produced.

- [ ] **Step 7:** Commit.

```bash
git add src/proxy/host/main.cpp src/proxy/host/PluginHost.h src/proxy/host/PluginHost.cpp
git commit -m "proxy: add child process entry point and PluginHost skeleton"
```

---

### Task 6: PluginHost — load plugin via JUCE format manager

**Files:** Modify `src/proxy/host/PluginHost.h`, `src/proxy/host/PluginHost.cpp`

- [ ] **Step 1:** In `PluginHost.cpp`, implement `loadPlugin()` using `juce::AudioPluginFormatManager`:

```cpp
bool PluginHost::loadPlugin() {
    juce::AudioPluginFormatManager fmtMgr;
    // Register VST3 and CLAP formats (same as HDAW's PluginManager)
    fmtMgr.addFormat(new juce::VST3PluginFormat());
    // CLAPPluginFormat registration would go here

    juce::String error;
    plugin = fmtMgr.createPluginInstance(
        *fmtMgr.createPluginInstance(...), // from pluginPath
        44100.0, 512, error);

    if (plugin == nullptr) return false;
    pluginLoaded.store(true);
    return true;
}
```

- [ ] **Step 2:** Implement `controlLoop()` — reads `ProxyMessage` from pipe, dispatches based on `MessageType::PREPARE`, `SET_STATE`, `GET_STATE`, `SHUTDOWN`, etc.

- [ ] **Step 3:** Implement `audioLoop()` — waits for input in shared memory ring buffer, calls `plugin->processBlock()`, writes output to shared memory ring buffer.

- [ ] **Step 4:** Implement `run()` — sends `READY` on pipe, starts control thread and audio thread, waits for `SHUTDOWN` or `running == false`.

- [ ] **Step 5:** Build `hdaw_plugin_host`. Verify it starts and sends `READY` on the pipe (manual test or integration test).

- [ ] **Step 6:** Commit.

```bash
git add src/proxy/host/PluginHost.h src/proxy/host/PluginHost.cpp
git commit -m "proxy: implement PluginHost plugin loading and control/audio loops"
```

---

### Task 7: Host control + audio threads

**Files:** Create `src/proxy/host/HostControlThread.h/.cpp`, `src/proxy/host/HostAudioThread.h/.cpp`

- [ ] **Step 1:** Extract control loop from PluginHost into `HostControlThread` class.

- [ ] **Step 2:** Extract audio loop from PluginHost into `HostAudioThread` class.

- [ ] **Step 3:** Update PluginHost to own and start these threads.

- [ ] **Step 4:** Build + verify.

- [ ] **Step 5:** Commit.

```bash
git add src/proxy/host/HostControlThread.h src/proxy/host/HostControlThread.cpp
git add src/proxy/host/HostAudioThread.h src/proxy/host/HostAudioThread.cpp
git commit -m "proxy: extract control and audio threads from PluginHost"
```

---

### Task 8: Host GUI thread + floating window

**Files:** Create `src/proxy/host/HostGuiThread.h/.cpp`

- [ ] **Step 1:** Implement `HostGuiThread` — receives `SHOW_EDITOR` message, calls `plugin->createEditor()`, creates Win32 window, runs message loop.

- [ ] **Step 2:** Implement window close detection — sends `EDITOR_CLOSED` back to pipe.

- [ ] **Step 3:** Build + manual test (spawn child, send SHOW_EDITOR, verify window appears).

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/host/HostGuiThread.h src/proxy/host/HostGuiThread.cpp
git commit -m "proxy: add HostGuiThread for plugin editor floating window"
```

---

## Phase 3 — DAW Proxy Side

### Task 9: ProxyProcessManager — spawn + monitor child

**Files:** Create `src/proxy/ProxyProcessManager.h`, `src/proxy/ProxyProcessManager.cpp`

- [ ] **Step 1:** Create `ProxyProcessManager` with:
  - `spawnPluginHost(pluginPath, slotId)` — creates pipe server, shared memory, launches child process
  - `killPluginHost(slotId)` — terminates child, cleans up handles
  - `isAlive(slotId)` — checks `WaitForSingleObject` with 0 timeout
  - Internal: `std::unordered_map<uint32_t, ChildInfo>` tracking per-slot state

- [ ] **Step 2:** Implement `spawnPluginHost` — `CreateProcessA` with `--slot --pipe --shm --plugin` args. Store process handle, pipe handle, shm handle.

- [ ] **Step 3:** Build. No tests yet (requires actual process spawning).

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/ProxyProcessManager.h src/proxy/ProxyProcessManager.cpp
git commit -m "proxy: add ProxyProcessManager for child process lifecycle"
```

---

### Task 10: PluginProxySlot — juce::AudioPluginInstance wrapper

**Files:** Create `src/proxy/PluginProxySlot.h`, `src/proxy/PluginProxySlot.cpp`, `tests/unit/proxy/proxy_slot_test.cpp`

- [ ] **Step 1:** Write unit tests for message serialization:

```cpp
#include <gtest/gtest.h>
#include "proxy/ProxyCommon.h"

using namespace proxy;

TEST(ProxySlot, MessageFitsInBuffer) {
    ProxyMessage msg{};
    msg.type = MessageType::SET_STATE;
    msg.dataSize = 244; // max inline
    EXPECT_LE(sizeof(msg), 256u);
}

TEST(ProxySlot, ResponseFitsInBuffer) {
    ProxyResponse resp{};
    resp.type = MessageType::GET_STATE_RESULT;
    resp.dataSize = 244;
    EXPECT_LE(sizeof(resp), 256u);
}
```

- [ ] **Step 2:** Create `src/proxy/PluginProxySlot.h`:

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProxyCommon.h"
#include "ProxyPipe.h"
#include "ProxySharedMemory.h"
#include <atomic>
#include <memory>

namespace proxy {

class ProxyProcessManager;

class PluginProxySlot : public juce::AudioPluginInstance {
public:
    PluginProxySlot(ProxyProcessManager& mgr, uint32_t slotId,
                    const juce::String& pluginPath);
    ~PluginProxySlot() override;

    // AudioPluginInstance
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;
    void reset() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    const juce::String getName() const override;
    void fillInPluginDescription(juce::PluginDescription& desc) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() const override { return 1; }
    int getCurrentProgram() const override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }

    // Crash state
    bool isCrashed() const { return crashed.load(); }
    void onChildCrashed();

private:
    ProxyProcessManager& processManager;
    uint32_t slotId;
    juce::String pluginPath;

    PipeClient pipe;
    ShmRegion shm;
    std::atomic<bool> crashed{false};

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int numChannels = 2;
};

} // namespace proxy
```

- [ ] **Step 3:** Implement `PluginProxySlot.cpp` — each method serializes its args into `ProxyMessage`, sends over pipe, waits for `ProxyResponse`. `processBlock` uses shared memory ring buffers instead of pipe.

- [ ] **Step 4:** Add proxy_slot_test to tests/CMakeLists.txt. Build + run.

- [ ] **Step 5:** Commit.

```bash
git add src/proxy/PluginProxySlot.h src/proxy/PluginProxySlot.cpp tests/unit/proxy/proxy_slot_test.cpp
git commit -m "proxy: add PluginProxySlot AudioPluginInstance wrapper"
```

---

### Task 11: ProxyEditor — lightweight UI card

**Files:** Create `src/proxy/ProxyEditor.h`, `src/proxy/ProxyEditor.cpp`

- [ ] **Step 1:** Create `ProxyEditor` — a `juce::AudioProcessorEditor` that shows:
  - Plugin name label
  - Bypass toggle button
  - "Open Editor" button (sends `SHOW_EDITOR` to child)
  - "Crashed — Restart?" button (shown when `isCrashed()`)

- [ ] **Step 2:** Wire button callbacks to `PluginProxySlot` methods.

- [ ] **Step 3:** Build + visual verification (add a proxy slot, verify UI renders).

- [ ] **Step 4:** Commit.

```bash
git add src/proxy/ProxyEditor.h src/proxy/ProxyEditor.cpp
git commit -m "proxy: add ProxyEditor UI card for isolated plugins"
```

---

## Phase 4 — Integration + Build Flag

### Task 12: Hook into TrackFXSlot + PluginManager

**Files:** Modify `src/engine/TrackFXSlot.h`, `src/engine/PluginManager.h`, `src/engine/PluginManager.cpp`

- [ ] **Step 1:** Add `bool isolated = false` parameter to `PluginManager::createPluginInstance`. When `true`, return a `PluginProxySlot` instead of loading the plugin directly.

- [ ] **Step 2:** Add `isIsolated()` flag to `TrackFXSlot`. When true, the slot uses a `PluginProxySlot` instead of a regular `AudioPluginInstance`.

- [ ] **Step 3:** Add build flag `-DHDAW_PLUGIN_ISOLATION=ON` to CMakeLists.txt. Guard all proxy includes with `#if HDAW_PLUGIN_ISOLATION`.

- [ ] **Step 4:** Build with flag ON. Verify existing tests still pass.

- [ ] **Step 5:** Commit.

```bash
git add src/engine/TrackFXSlot.h src/engine/PluginManager.h src/engine/PluginManager.cpp CMakeLists.txt
git commit -m "proxy: integrate PluginProxySlot into TrackFXSlot and PluginManager"
```

---

### Task 13: Integration test — spawn child, load plugin, process audio

**Files:** Create `tests/integration/proxy/end_to_end_test.cpp`

- [ ] **Step 1:** Write integration test that:
  1. Spawns `hdaw_plugin_host.exe` with a test plugin
  2. Sends `PREPARE` over pipe
  3. Writes input to shared memory ring
  4. Sends `PROCESS_BLOCK` over pipe
  5. Reads output from shared memory ring
  6. Verifies output is not silence (plugin processed)

- [ ] **Step 2:** Add to `tests/CMakeLists.txt`. Build + run.

- [ ] **Step 3:** Commit.

```bash
git add tests/integration/proxy/end_to_end_test.cpp
git commit -m "proxy: add end-to-end integration test"
```

---

## Phase 5 — Crash Recovery

### Task 14: Crash detection + auto-save

**Files:** Modify `src/proxy/PluginProxySlot.cpp`, `src/proxy/ProxyProcessManager.cpp`

- [ ] **Step 1:** Add heartbeat check to `ProxyProcessManager` — timer every 500ms, check `childAlive` atomic. If stale for 2s, call `onChildCrashed()`.

- [ ] **Step 2:** Add auto-save timer to `PluginProxySlot` — every 5s, call `getStateInformation` and write to `%TEMP%/hdaw_proxy_state_{slotId}.bin`.

- [ ] **Step 3:** On crash, set `crashed = true`, emit signal for UI update.

- [ ] **Step 4:** Build + test (manually kill child process, verify DAW detects).

- [ ] **Step 5:** Commit.

```bash
git commit -m "proxy: add crash detection, heartbeat monitoring, and state auto-save"
```

---

### Task 15: Crash recovery dialog + restart flow

**Files:** Modify `src/proxy/PluginProxySlot.cpp`, create `src/proxy/CrashDialog.h`

- [ ] **Step 1:** Create `CrashDialog` — simple `QMessageBox` with "Plugin crashed — Restart?" and OK/Cancel buttons.

- [ ] **Step 2:** On crash, show dialog on GUI thread. If user clicks Restart:
  1. Kill zombie child
  2. Re-spawn child via `ProxyProcessManager`
  3. Read last auto-save from temp file
  4. Send `SET_STATE` to new child
  5. Resume audio

- [ ] **Step 3:** Build + smoke test (kill child, verify dialog appears, click restart, verify audio resumes).

- [ ] **Step 4:** Commit.

```bash
git commit -m "proxy: add crash recovery dialog and restart flow"
```

---

## Phase 6 — Final Verification

### Task 16: Build + test + smoke

- [ ] **Step 1:** Clean build: `cmake --build build --config Debug`
- [ ] **Step 2:** Run all tests: `build\Debug\hdaw_tests.exe`
- [ ] **Step 3:** Smoke: load an isolated VST3 plugin, play audio, verify output. Kill child, verify crash dialog, restart.
- [ ] **Step 4:** Update `AGENTS.md` with proxy architecture notes.
- [ ] **Step 5:** Final commit.

```bash
git commit -m "proxy: final verification and documentation"
```
