# Fix CLAP Plugin Export Hang

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the offline export hang when the project contains CLAP plugins, caused by three interacting issues: a global `s_renderMode` flag, missing message-thread pumping in the render loop, and cancel unable to interrupt stuck spin-waits.

**Architecture:** Three targeted fixes to `ExportManager::renderThreadFunc` and `PluginProxySlot`:
1. Scope `s_renderMode` to the render thread only (thread-local or per-export)
2. Pump the JUCE message loop between render blocks so CLAP's `on_main_thread` callbacks fire
3. Check `cancelFlag` inside the spin-waits so cancel can interrupt a stuck export

**Tech Stack:** C++17, JUCE 8, CLAP SDK, GTest

---

## Root Cause Summary

The export hang occurs through this chain:
1. `ExportManager::renderThreadFunc` sets a **global** `s_renderMode = true` (line 78)
2. The render loop calls `processBlock` at CPU speed with **no message pump** (lines 167-192)
3. CLAP plugins call `requestCallback()` → `triggerAsyncUpdate()` which posts to the JUCE message thread
4. `handleAsyncUpdate()` (which calls `on_main_thread`) **never fires** because nobody pumps the message queue
5. If the CLAP plugin blocks waiting for `on_main_thread`, `processBlock` never returns
6. `cancelFlag` is not checked inside the spin-waits, so cancel cannot interrupt

---

## Files to Modify

| File | Change |
|------|--------|
| `src/proxy/PluginProxySlot.h` | Change `s_renderMode` from global to thread-local; add `s_renderCancelFlag` |
| `src/proxy/PluginProxySlot.cpp` | Check cancel flag in spin-waits; use thread-local render mode |
| `src/engine/ExportManager.h` | Expose `cancelFlag` for spin-wait access |
| `src/engine/ExportManager.cpp` | Add message pump to render loop; set thread-local render mode; pass cancel flag |
| `tests/integration/mcp/mcp_server_test.cpp` | Add test: export with CLAP plugin doesn't hang |

## Pitfall Gates Triggered

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 3: Audio-Thread Safety** | Modifying `processBlock` spin-wait logic; adding message pump on render thread | The spin-wait changes only add an atomic load (cancel check). The message pump runs on the render thread (not the audio thread). No allocations, locks, or I/O in the audio path. |
| **Gate 2: Unimplemented Code Path** | Must verify the message pump actually fires `handleAsyncUpdate` | Test will use a CLAP plugin and verify export completes within a timeout. |

## Anti-Pattern Scan

- No new `rebuildRoutingGraph()` calls
- No new `setProperty` calls
- No new hex colors or CSS changes
- No changes to the frontend

---

## Task 1: Make `s_renderMode` Thread-Local

**Problem:** `s_renderMode` is a global `inline std::atomic<bool>` in `PluginProxySlot.h:18`. When the export thread sets it to `true`, it affects ALL `PluginProxySlot::processBlock` calls in the process — including the live app's audio callback. This causes the live audio thread to spin-wait (up to 200ms per ring access), which can block the message thread and starve CLAP's `on_main_thread`.

**Files:**
- Modify: `src/proxy/PluginProxySlot.h:18-19`
- Modify: `src/proxy/PluginProxySlot.cpp:346,504`
- Modify: `src/engine/ExportManager.cpp:78,231`

**Step 1:** Change `s_renderMode` from global inline to thread-local:

In `src/proxy/PluginProxySlot.h`, replace lines 14-19:
```cpp
// When true, PluginProxySlot::processBlock spin-waits for the child process
// to produce output instead of clearing the buffer on empty output ring.
// Thread-local: only the export render thread should be affected; the live
// audio callback must not spin-wait.
inline thread_local bool tls_renderMode{ false };
inline void setRenderMode(bool enabled) { tls_renderMode = enabled; }
inline bool isRenderMode() { return tls_renderMode; }
```

**Step 2:** Update `PluginProxySlot.cpp` to use the new API:

At line 346, replace:
```cpp
if (s_renderMode.load(std::memory_order_relaxed)) {
```
with:
```cpp
if (isRenderMode()) {
```

At line 504, replace:
```cpp
if (s_renderMode.load(std::memory_order_relaxed) && available < static_cast<uint32_t>(totalSamples)) {
```
with:
```cpp
if (isRenderMode() && available < static_cast<uint32_t>(totalSamples)) {
```

**Step 3:** No changes needed to `ExportManager.cpp` — it already calls `proxy::setRenderMode(true/false)` which now writes to the thread-local.

**Step 4:** Build and verify:
```
cmake --build build --config Debug
```

**Step 5:** Run existing export tests:
```
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*
```

---

## Task 2: Add Message Pump to Render Loop

**Problem:** The render loop at `ExportManager.cpp:167-192` runs at CPU speed with no message-thread pumping. CLAP plugins depend on `on_main_thread` callbacks (dispatched via `AsyncUpdater` on the JUCE message thread). Without pumping, `handleAsyncUpdate()` never fires, and CLAP plugins that block on `on_main_thread` hang forever.

**Files:**
- Modify: `src/engine/ExportManager.cpp:167-192`

**Step 1:** Add message pump inside the render loop, after `processBlock` and before the next iteration:

In `src/engine/ExportManager.cpp`, inside the while loop (after line 191, before line 192 `}`), add:

```cpp
                // Pump the JUCE message queue so CLAP plugins' on_main_thread
                // callbacks (dispatched via AsyncUpdater) can fire. Without
                // this, CLAP plugins that call requestCallback() during
                // process() will hang waiting for on_main_thread that never
                // arrives. runDispatchLoopUntil(0) is non-blocking — it
                // processes all pending messages and returns immediately.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
```

The full loop becomes:
```cpp
            while (samplesRendered < totalSamples && !cancelFlag.load())
            {
                int numThisBlock = static_cast<int>((std::min)(static_cast<int64_t>(blockSize), totalSamples - samplesRendered));
                buffer.clear();
                midiBuffer.clear();

                renderGraph.processBlock(buffer, midiBuffer);
                renderTransport.advance(numThisBlock);

                if (!writer->writeFromAudioSampleBuffer(buffer, 0, numThisBlock))
                {
                    success = false;
                    message = "Disk write failed during export.";
                    delete writer;
                    delete outStream;
                    goto finish;
                }

                samplesRendered += numThisBlock;
                ++blocksDone;
                if (onProgress)
                {
                    float prog = static_cast<float>(blocksDone) / static_cast<float>(totalBlocks);
                    onProgress(prog);
                }

                // Pump the JUCE message queue so CLAP plugins' on_main_thread
                // callbacks (dispatched via AsyncUpdater) can fire. Without
                // this, CLAP plugins that call requestCallback() during
                // process() will hang waiting for on_main_thread that never
                // arrives. runDispatchLoopUntil(0) is non-blocking — it
                // processes all pending messages and returns immediately.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
            }
```

**Step 2:** Build and verify:
```
cmake --build build --config Debug
```

**Step 3:** Run existing export tests:
```
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*
```

---

## Task 3: Add Cancel Check to Spin-Waits

**Problem:** The spin-waits in `PluginProxySlot::processBlock` (lines 346-358 and 504-515) only check `crashed` and a 200ms deadline. If the export is cancelled while a spin-wait is in progress, the cancel is not detected until the spin-wait times out (up to 200ms per block). For a fully hung plugin, this means cancel takes effect only after the 200ms deadline expires.

**Files:**
- Modify: `src/proxy/PluginProxySlot.h` — add cancel flag accessor
- Modify: `src/proxy/PluginProxySlot.cpp:346-358,504-515` — check cancel in spin-waits
- Modify: `src/engine/ExportManager.cpp` — set cancel flag before entering render

**Step 1:** Add a cancel flag mechanism to `PluginProxySlot.h`:

After the `s_renderMode` section (after the new `isRenderMode()` function), add:

```cpp
// Cancel flag for interrupting spin-waits during export. Set by
// ExportManager::cancel() so the spin-waits can bail out immediately
// instead of waiting for the 200ms deadline.
inline std::atomic<bool> s_renderCancelRequested{ false };
inline void setRenderCancelRequested(bool v) { s_renderCancelRequested.store(v, std::memory_order_relaxed); }
inline bool isRenderCancelRequested() { return s_renderCancelRequested.load(std::memory_order_relaxed); }
```

**Step 2:** Update spin-waits in `PluginProxySlot.cpp` to check cancel:

At line 346-358 (input ring spin-wait), replace:
```cpp
        if (isRenderMode()) {
            constexpr int kMaxSpinMs = 200;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
            while (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
                if (crashed.load()) return;
                if (std::chrono::steady_clock::now() >= deadline) return;
                std::this_thread::yield();
                w = hdr->inputWritePos.load(std::memory_order_relaxed);
                r = hdr->inputReadPos.load(std::memory_order_acquire);
            }
```
with:
```cpp
        if (isRenderMode()) {
            constexpr int kMaxSpinMs = 200;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
            while (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
                if (crashed.load()) return;
                if (isRenderCancelRequested()) return;
                if (std::chrono::steady_clock::now() >= deadline) return;
                std::this_thread::yield();
                w = hdr->inputWritePos.load(std::memory_order_relaxed);
                r = hdr->inputReadPos.load(std::memory_order_acquire);
            }
```

At line 504-515 (output ring spin-wait), replace:
```cpp
    if (isRenderMode() && available < static_cast<uint32_t>(totalSamples)) {
        constexpr int kMaxSpinMs = 200;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
        while (available < static_cast<uint32_t>(totalSamples)) {
            if (crashed.load()) break;
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::yield();
```
with:
```cpp
    if (isRenderMode() && available < static_cast<uint32_t>(totalSamples)) {
        constexpr int kMaxSpinMs = 200;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
        while (available < static_cast<uint32_t>(totalSamples)) {
            if (crashed.load()) break;
            if (isRenderCancelRequested()) break;
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::yield();
```

**Step 3:** Wire up the cancel flag in `ExportManager.cpp`:

In `renderThreadFunc`, after `proxy::setRenderMode(true)` (line 78), add:
```cpp
    proxy::setRenderCancelRequested(false);
```

In `renderThreadFunc`, at the `finish:` label (line 229), before `proxy::setRenderMode(false)` (line 231), add:
```cpp
    proxy::setRenderCancelRequested(false);
```

In `ExportManager::cancel()` (line 41-44), add:
```cpp
void ExportManager::cancel()
{
    cancelFlag = true;
    proxy::setRenderCancelRequested(true);
}
```

**Step 4:** Build and verify:
```
cmake --build build --config Debug
```

**Step 5:** Run existing export tests:
```
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*
```

---

## Task 4: Add Integration Test for CLAP Plugin Export

**Problem:** No test exists for exporting with CLAP plugins. The existing `ExportAudioRendersDefaultProject` test uses the default project (no plugins).

**Files:**
- Modify: `tests/integration/mcp/mcp_server_test.cpp`

**Step 1:** Add a test that exports with a CLAP plugin loaded. This test should:
1. Scan plugins
2. Load a CLAP instrument on a track
3. Generate MIDI
4. Export to WAV
5. Verify the export completes within a timeout (e.g., 30 seconds)
6. Verify the output file exists and has non-zero size

```cpp
TEST(McpServer, ExportAudioWithClapPluginDoesNotHang) {
    // This test verifies the fix for the CLAP plugin export hang.
    // Before the fix, exporting with a CLAP plugin would hang forever
    // because on_main_thread callbacks never fired during the tight
    // render loop.

    McpTransportLoopback tp;
    McpServer s;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto call = [&](const char* tool, const QJsonObject& args = {}) -> QJsonObject {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{{"name", tool}, {"arguments", args}};
        tp.drainOutgoing();
        tp.pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!tp.waitForOutgoing(5000, &out)) return {};
        auto r = McpServer::parseOneMessage(out);
        return r.value("result").toObject();
    };

    // Create a fresh project
    call("new_project");

    // Scan plugins
    call("scan_plugins");

    // List plugins and find a CLAP instrument
    auto pluginsResult = call("list_plugins");
    auto pluginsText = pluginsResult.value("content").toArray().first().toObject().value("text").toString();
    auto pluginsDoc = QJsonDocument::fromJson(pluginsText.toUtf8());
    auto plugins = pluginsDoc.object().value("plugins").toArray();

    QString clapPluginId;
    for (const auto& p : plugins) {
        auto obj = p.toObject();
        if (obj.value("format").toString() == "CLAP") {
            clapPluginId = obj.value("id").toString();
            break;
        }
    }

    if (clapPluginId.isEmpty()) {
        GTEST_SKIP() << "No CLAP plugins found — skipping export hang test";
    }

    // Add a track with the CLAP plugin
    auto addResult = call("add_track_with_fx", {{"name", "CLAP Track"}, {"pluginId", clapPluginId}});
    auto addText = addResult.value("content").toArray().first().toObject().value("text").toString();
    ASSERT_TRUE(addText.contains("trackId")) << "Failed to add track with CLAP plugin: " << addText.toStdString();

    // Extract track ID
    QRegularExpression re("trackId=(\\d+)");
    auto match = re.match(addText);
    ASSERT_TRUE(match.hasMatch()) << "Could not parse trackId from: " << addText.toStdString();
    int trackId = match.captured(1).toInt();

    // Generate a short phrase (4 bars = 16 beats)
    call("generate_phrase", {{"trackId", trackId},
                             {"style", "Lead"},
                             {"length", 16},
                             {"density", 20}});

    // Export to a temp file with a timeout
    auto tempDir = QDir::tempPath();
    auto outputPath = tempDir + "/hdaw_clap_export_test.wav";

    // Use a separate thread for the export so we can enforce a timeout
    std::atomic<bool> exportDone{false};
    std::atomic<bool> exportSuccess{false};
    std::thread exportThread([&]() {
        auto result = call("export_audio", {{"outputPath", outputPath},
                                             {"format", "wav"},
                                             {"sampleRate", 44100}});
        auto text = result.value("content").toArray().first().toObject().value("text").toString();
        exportSuccess = text.contains("Export complete");
        exportDone = true;
    });

    // Wait up to 30 seconds for the export to complete
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!exportDone.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!exportDone.load()) {
        // Export is hung — this is the bug we're testing for
        FAIL() << "Export with CLAP plugin hung for >30 seconds (the on_main_thread fix is not working)";
    }

    exportThread.join();

    // Verify the output file
    ASSERT_TRUE(exportSuccess.load()) << "Export reported failure";
    QFile outputFile(outputPath);
    ASSERT_TRUE(outputFile.exists()) << "Output file does not exist";
    ASSERT_GT(outputFile.size(), 0) << "Output file is empty";

    // Cleanup
    outputFile.remove();
    s.stop();
}
```

**Step 2:** Build and run:
```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudioWithClapPluginDoesNotHang
```

**Step 3:** Verify the test passes (export completes within 30 seconds).

---

## Task 5: Manual Verification with Real CLAP Plugins

**Step 1:** Run the generate-midi-to-wav script with CLAP plugins (the original failing case):

```
cd "D:\pdf\roo projects\hdaw3"
node generate-midi-to-wav.mjs
```

The script selects CLAP plugins (Altitude, Dexed, Gneiss, JC303, etc.) and exports. Before the fix, this hung forever. After the fix, it should complete within a few minutes.

**Step 2:** Verify the output WAV file:
- Exists at `output-32bars.wav`
- Size is proportional to the duration (≈17-18 MB for 64 seconds at 48kHz/24-bit stereo)
- Contains audible audio (not silence)

**Step 3:** Run all existing tests to verify no regressions:
```
build\Debug\hdaw_tests.exe
```

---

## Verification Checklist

- [ ] `cmake --build build --config Debug` succeeds
- [ ] `build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*` passes (existing tests)
- [ ] `build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudioWithClapPluginDoesNotHang` passes (new test)
- [ ] `build\Debug\hdaw_tests.exe` passes (full suite, no regressions)
- [ ] Manual test: `node generate-midi-to-wav.mjs` completes with CLAP plugins
- [ ] Output WAV file has correct size and contains audio
- [ ] No audio-thread safety violations (no allocations/locks in processBlock changes)
