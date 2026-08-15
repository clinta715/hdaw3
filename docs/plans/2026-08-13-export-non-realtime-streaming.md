# Export Non-Realtime Streaming (Subsystem D) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the `hdaw-guard` skill before any code change.

**Goal:** Close Subsystem D's gates — prove that offline export renders streamed (> 8 s) audio clips from synchronously-refilled windows with no background-reader race, no dropouts, and sample-accurate output versus the resident-decode path.

**Architecture:** `StreamingClipSource::setNonRealtime(true)` stops the background reader thread; `readNextBlock` then refills the window synchronously on the render thread when the requested position is not covered (`src/engine/StreamingClipSource.h:109-122,180-205`). `ExportManager::renderThreadFunc` propagates the flag to every live `ClipSourceProcessor` after `prepareToPlay` and before the first `processBlock` (`src/engine/ExportManager.cpp:221-230`) via `RoutingManager::setClipSourcesNonRealtime` (`src/engine/RoutingManager.cpp:685`). **All functional code already landed in 0.22.0 (commit `76bc275`)** — this plan is the verification/gate-closure half: dedicated tests for the two master-plan gates plus this status record.

**Tech Stack:** C++17 (JUCE 8), gtest, Qt JSON-RPC test loopback (`mcp::TransportLoopback`).

---

## Status & deviations from the master plan

- Master plan `docs/plans/2026-08-13-hise-derived-features-master-plan.md` §D blocked on Subsystem A; A landed in 0.22.0 and carried D's functional wiring (`76bc275 fix(stream): propagate non-realtime mode to clip sources during export`).
- **Deviation 1 (G1 "bit-identical to whole-file preload"):** streaming stores `int16` (Subsystem A's decision, AGENTS.md lesson 8), so equality with the float `DecodedSound` preload holds *within int16 requantization* (≤ 1 LSB). For 16-bit source material the requantization is idempotent, so samples match in practice; tests assert with a 2 LSB guard band.
- **Deviation 2 (G3 "version bump"):** no version bump — D adds no new functional code (it shipped in 0.22.0). Version stays **0.22.1**. Knowledge graph is refreshed instead.

## Success Gates (all must pass)

- [x] **G1:** unit tests in `tests/unit/engine/clip_streaming_e2e_test.cpp` — **PASS** (commit `9bba444`):
  - (a) `NonRealtimeJumpRefillsSynchronouslyWithoutStarvation` — 20 s jump beyond the filled window returns correct samples, `starvedCount() == 0`;
  - (b) `NonRealtimeStreamingMatchesWholeFileDecode` — NR streaming matches `DecodedSound::decode` within 2 LSB across 9 s (two window refills).
  - Suite evidence: `ClipStreamingE2E.*:StreamingClipSource.*` 9/9 PASS.
- [x] **G2:** integration test `McpServer.ExportAudioStreamsLongClipWithoutDropouts` in `tests/integration/mcp/mcp_server_test.cpp` — **PASS** (commit `2a2cefe`): 12 s clip (source > 8 s → streams) exports 0→12 s via `export_audio`; every 100 ms slice RMS > 0.1. Suite evidence: `McpServer.Export*` 7/7 PASS (29.3 s), incl. the CLAP export tests.
- [x] **G3:** build succeeds; both filtered suites PASS; new test names confirmed in the built binary via `--gtest_list_tests` after each build (lesson 15).
- [x] **G4:** this plan doc committed with gate results recorded.
- [x] **G5:** knowledge graph refreshed (`codebase-memory` `index_repository`, mode `fast`).

## Dependency Map (verified by reading code 2026-08-14)

- **Path integrity (Gate 2):** MCP `export_audio` (`src/mcp/McpExportTool.cpp:29`) → `ExportManager::renderThreadFunc` → `renderGraph.setNonRealtime(true)` (`ExportManager.cpp:221`) + `routingManager.setClipSourcesNonRealtime(true)` (`:230`) → `ClipSourceProcessor::setNonRealtimeFlag` (`ClipSourceProcessor.h:138`) → `StreamingClipSource::setNonRealtime` (`StreamingClipSource.h:109`). Render loop `renderGraph.processBlock` (`ExportManager.cpp:338`) → `ClipSourceProcessor::processBlock` (`activeBuffer==2`) → `StreamingClipSource::readNextBlock` NR synchronous refill (`StreamingClipSource.h:180-205`). Complete chain — no gaps.
- **Upstream:** `ExportManager` is the only consumer of `setClipSourcesNonRealtime` (grep-verified).
- **Downstream:** rendered WAV file only.
- **Blast radius:** tests + this doc. **No production code changes.**
- **God nodes in scope:** none modified (`ClipSourceProcessor`/`RoutingManager` read-only here).
- **Projections affected:** none. **SPSC paths touched:** none new.
- **Ordering note (documented, not a bug):** export `prepareToPlay` runs while `nonRealtimeFlag` is still false, so the streamer briefly starts its background reader; `setClipSourcesNonRealtime(true)` then joins it before any `processBlock`. Safe: no audio thread is running and the reader does plain `AudioFormatReader` I/O (comment at `ExportManager.cpp:223-229`).

## Pitfall Gates Triggered

- **Gate 2 (path integrity):** verified above; the integration test exercises the full MCP → WAV chain.
- **Gate 11 (message pump):** tests use the existing harness — `test_main` starts `MessagePumpThread` first; no new entry points.
- **Gate 15 (stale binary):** after the build, confirm the new test names appear via `--gtest_list_tests` before trusting results.
- **Gate 3:** N/A — no audio-thread code changes.
- **Lesson 9:** default project ships 3 tracks with EMPTY clip lists; the test adds its own clip and scopes all assertions to it.
- **Lesson 20:** if `McpServer.Export*` tests fail/hang on spawn or READY waits, check for stale `HDAW_headless_mcp.exe`/`HDAW.exe`/`hdaw_plugin_host.exe` processes BEFORE blaming the code.
- **Anti-pattern scan:** no new RPC, no loops of RPC calls (3 sequential one-shot tool calls, each a distinct setup step — not a batchable mutation), no DBG, no CSS.

---

### Task 1: Unit gates — NR synchronous refill + parity with whole-file decode

**Files:**
- Modify: `tests/unit/engine/clip_streaming_e2e_test.cpp` (append two tests + one include)

- [ ] **Step 1: Add the include**

At the top of `tests/unit/engine/clip_streaming_e2e_test.cpp`, after the existing `#include "engine/StreamingClipSource.h"` line:

```cpp
#include "engine/DecodedSoundPool.h"
```

- [ ] **Step 2: Add the synchronous-refill jump test**

Append to `tests/unit/engine/clip_streaming_e2e_test.cpp`:

```cpp
// Subsystem D gate G1a: with non-realtime set, the loader reads
// synchronously — a position jump far beyond the filled window returns
// correct samples immediately (there is no background reader to wait on)
// and never counts a starvation. In realtime mode the same jump starves
// (silence) until the reader catches up.
TEST(ClipStreamingE2E, NonRealtimeJumpRefillsSynchronouslyWithoutStarvation)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr);

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    ASSERT_FALSE(src.isWholeFileResident());
    src.setNonRealtime(true);
    src.startPlayback();

    juce::AudioBuffer<float> out(2, 512);

    // First block from the preloaded head (sine phase 0).
    out.clear();
    src.readNextBlock(out, 0);
    EXPECT_NEAR(out.getSample(0, 0), 0.0f, 2.0f / 32768.0f);

    // Jump to 20 s — ~5 windows past the prefill. Must refill synchronously.
    const int64_t jumpPos = static_cast<int64_t>(20.0 * sr);
    out.clear();
    src.readNextBlock(out, jumpPos);
    for (int s = 0; s < 512; ++s)
    {
        const float expected = static_cast<float>(
            std::sin(2.0 * 3.14159 * 440.0 * (jumpPos + s) / sr));
        EXPECT_NEAR(out.getSample(0, s), expected, 2.0f / 32768.0f)
            << "sample " << s;
    }
    EXPECT_EQ(src.starvedCount(), 0u);

    src.stopPlayback();
    file.deleteFile();
}
```

Note: the `std::sin(2.0 * 3.14159 * ...)` constant MUST match `writeSineWav`'s generator exactly (it uses `3.14159`, not `juce::MathConstants`).

- [ ] **Step 3: Add the whole-file-decode parity test**

Append to the same file:

```cpp
// Subsystem D gate G1b: non-realtime streaming matches the whole-file
// preload path (DecodedSound float decode) within int16 requantization.
// Streaming stores int16 (Subsystem A decision, lesson 8), so equality
// holds within 1 LSB; 16-bit source material requantizes idempotently, so
// samples match in practice — the 2 LSB band is the guard.
TEST(ClipStreamingE2E, NonRealtimeStreamingMatchesWholeFileDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    // 12 s: long enough to stream (> 8 s threshold) and to cross two
    // ~4 s window boundaries in the first 9 s.
    auto file = writeSineWav(static_cast<int>(sr * 12), sr);

    auto whole = HDAW::DecodedSound::decode(fm, file.getFullPathName());
    ASSERT_NE(whole, nullptr);

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    ASSERT_FALSE(src.isWholeFileResident());
    src.setNonRealtime(true);
    src.startPlayback();

    juce::AudioBuffer<float> out(2, 512);
    const int blocks = static_cast<int>((sr * 9.0) / 512.0);
    for (int i = 0; i < blocks; ++i)
    {
        out.clear();
        src.readNextBlock(out, static_cast<int64_t>(i) * 512);
        for (int s = 0; s < 512; ++s)
        {
            const int64_t idx = static_cast<int64_t>(i) * 512 + s;
            EXPECT_NEAR(out.getSample(0, s), whole->data[0][idx],
                        2.0f / 32768.0f) << "sample " << idx;
        }
    }
    EXPECT_EQ(src.starvedCount(), 0u);

    src.stopPlayback();
    file.deleteFile();
}
```

- [ ] **Step 4: Build and run**

```powershell
cmake --build build --config Debug --target hdaw_tests
& "build\Debug\hdaw_tests.exe" --gtest_filter=ClipStreamingE2E.*:StreamingClipSource.*
```

Expected: all PASS (these are verification tests for landed code — if one fails, treat it as a real regression in `StreamingClipSource`, not a test bug, and stop for analysis).

- [ ] **Step 5: Commit**

```powershell
git add tests/unit/engine/clip_streaming_e2e_test.cpp
git commit -m "test(stream): non-realtime sync-refill + decode-parity gates (Subsystem D G1)"
```

---

### Task 2: Integration gate — exported long clip streams without dropouts

**Files:**
- Modify: `tests/integration/mcp/mcp_server_test.cpp` (one helper + one test, placed after `ExportAudioRendersDefaultProject`)

- [ ] **Step 1: Add the long-sine helper**

In the anonymous namespace next to the existing `writeSineWav(const char*)` helper (`mcp_server_test.cpp:383-413`), add:

```cpp
// 12 s stereo 440 Hz sine (0.5 amplitude) — longer than the 8 s streaming
// promotion threshold (StreamingClipSource::kPromoteToWholeFileMs), for the
// streamed-clip export test (Subsystem D gate G2).
QString writeLongSineWav() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir = QDir::fromNativeSeparators(dir);
    QString path = QString("%1/hdaw_stream_long_%2.wav")
                       .arg(dir)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);

    constexpr int sampleRate = 44100;
    constexpr int numChannels = 2;
    constexpr int numSeconds = 12;
    constexpr double amplitude = 0.5;
    constexpr double freqHz = 440.0;

    juce::AudioBuffer<float> buf(numChannels, sampleRate * numSeconds);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            buf.setSample(ch, s, static_cast<float>(
                amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * s / sampleRate)));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(juce::File(path.toStdString())),
                            sampleRate, numChannels, 16, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    writer->flush();
    return path;
}
```

- [ ] **Step 2: Add the export test**

After `TEST(McpServer, ExportAudioRendersDefaultProject)` (`mcp_server_test.cpp:605-659`):

```cpp
// Subsystem D gate G2: a clip whose source file is long enough to STREAM
// (> 8 s) must export via the non-realtime synchronous-refill path with no
// dropouts. Before 76bc275 the realtime reader starved ~74 blocks at every
// ~4 s window boundary; this asserts every 100 ms slice of the render
// carries signal (a 100 ms slice straddling a boundary would dip below the
// threshold if whole blocks rendered silence).
TEST(McpServer, ExportAudioStreamsLongClipWithoutDropouts) {
    AudioEngine engine;
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const QString srcPath = writeLongSineWav();
    ASSERT_FALSE(srcPath.isEmpty());

    // 120 BPM so 24 beats == exactly 12 s (add_audio_clip takes beats).
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"set_tempo","arguments":{"bpm":120}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(5000, &out));
    EXPECT_FALSE(parseOne(out).value("error").isObject());

    tp.drainOutgoing();
    QString addArgs = QString(R"({"trackId":0,"start":0.0,"length":24.0,"sourceFile":"%1"})")
                          .arg(srcPath);
    QString addReq = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_audio_clip","arguments":%1}})")
                         .arg(addArgs);
    tp.pumpIncoming(addReq.toUtf8());
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(5000, &out));
    auto addR = parseOne(out);
    EXPECT_FALSE(addR.value("error").isObject());
    EXPECT_TRUE(textOf(addR).contains("clipId="))
        << "got: [" << textOf(addR).toStdString() << "]";

    QString path = makeTempWavPath("stream-long");
    tp.drainOutgoing();
    QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":12.0,"sampleRate":44100.0,"bitDepth":16})")
                       .arg(path);
    QString req = QString(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                      .arg(args);
    tp.pumpIncoming(req.toUtf8());
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseResponse(out);
    EXPECT_FALSE(r.value("error").isObject());
    if (r.value("result").toObject().value("isError").toBool(false)) {
        FAIL() << "export failed: " << textOf(r).toStdString();
    }

    auto complete = waitExportComplete(tp, 30000);
    ASSERT_FALSE(complete.isEmpty());
    EXPECT_TRUE(complete.value("success").toBool());
    ASSERT_TRUE(QFile::exists(path));

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        fm.createReaderFor(juce::File(path.toStdString())));
    ASSERT_NE(reader, nullptr);
    ASSERT_GT(reader->lengthInSamples, 44100 * 11); // ~12 s rendered

    const int slice = 4410; // 100 ms at 44.1 kHz
    juce::AudioBuffer<float> buf(static_cast<int>(reader->numChannels), slice);
    for (int64_t pos = 0; pos + slice <= reader->lengthInSamples; pos += slice) {
        buf.clear();
        float* ptrs[2] = { buf.getWritePointer(0),
                           buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr };
        reader->read(ptrs, buf.getNumChannels(), pos, slice);
        double rms = 0.0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < slice; ++i)
                rms += buf.getSample(ch, i) * buf.getSample(ch, i);
        rms = std::sqrt(rms / (slice * buf.getNumChannels()));
        EXPECT_GT(rms, 0.1) << "silent slice at sample " << pos
                            << " (t=" << (pos / 44100.0) << " s)";
    }

    QFile::remove(path);
    QFile::remove(srcPath);
    s.stop();
    s.setTransport(nullptr);
}
```

Notes for the implementer:
- `add_audio_clip` converts beats→seconds at the project BPM (`src/mcp/McpTools_Project.cpp:340-341`), hence the explicit `set_tempo` first.
- Track 0 of the default project is the audio track "Track 1" (AGENTS.md lesson 9). The other two tracks hold no clips, so the render contains only the sine.
- Follow the file's teardown discipline: `transport declared BEFORE McpServer` (comment at `mcp_server_test.cpp:107-109`).

- [ ] **Step 3: Build and run**

```powershell
cmake --build build --config Debug --target hdaw_tests
& "build\Debug\hdaw_tests.exe" --gtest_list_tests | Select-String "StreamsLongClip"
& "build\Debug\hdaw_tests.exe" --gtest_filter=McpServer.Export*
```

Expected: the new test name is listed (Gate 15 binary freshness) and all `McpServer.Export*` tests PASS. If spawn/READY failures appear, check for stale engine processes first (lesson 20).

- [ ] **Step 4: Commit**

```powershell
git add tests/integration/mcp/mcp_server_test.cpp
git commit -m "test(mcp): export streamed long clip without dropouts (Subsystem D G2)"
```

---

### Task 3: Gate record + graph refresh

- [ ] **Step 1:** Update this file's Success Gates checkboxes with the run evidence (test counts, suite results).
- [ ] **Step 2:** Commit the plan doc:

```powershell
git add docs/plans/2026-08-13-export-non-realtime-streaming.md
git commit -m "docs: Subsystem D plan + gate results (export non-realtime streaming)"
```

- [ ] **Step 3:** Refresh the knowledge graph: `codebase-memory` `index_repository` (repo_path `D:\pdf\roo projects\hdaw3`, mode `fast`).
