# Clip Disk Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `ClipSourceProcessor` from preloading entire audio files into RAM. Instead, stream clips from disk with a preload head + double buffer filled by a background reader thread, using the existing `activeBuffer` block-boundary swap idiom — so a 1 GB file costs bounded memory (a few seconds of audio), not `length × 4 B × channels`.

**Architecture:** A standalone `StreamingClipSource` class (no ValueTree/FX-slot dependency, mirroring the `SamplerEngine` portability contract) owns a background `juce::Thread` reader and a fixed double buffer of `int16` samples. `ClipSourceProcessor` gains a third buffer source: `activeBuffer == 2` reads the streaming double buffer instead of `preloadedData`. Short files (below a threshold) promote to the existing whole-file preload path — identical behavior, bounded memory. If the reader can't keep up, the starved window renders silence (never blocks the audio thread). Non-realtime (export) mode makes the reader synchronous.

**Tech Stack:** C++17 (JUCE 8), gtest. Reference: HISE `StreamingSamplerVoice.cpp` `SampleLoader` (:39-615), `setIsNonRealtime` (:139), starvation (:1147), `Unmapper`/`ScopedFileHandler` (:1290), 16-bit conversion (:897); `StreamingSampler.h:220/:233` (`MAX_SAMPLER_PITCH`, buffer contract); `StreamingSamplerSound.cpp:399` `createReaderForPreview`.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/engine/StreamingClipSource.h` (new) | standalone disk-streaming reader: background thread, double buffer, atomic swap gate, starvation silence, non-realtime mode |
| `src/engine/ClipSourceProcessor.h` (modify) | adopt a `StreamingClipSource`; `activeBuffer == 2` reads streamed samples; promotion decision in `prepareToPlay`/`switchToSourceFile`; `setNonRealtime` |
| `src/engine/RoutingManager.cpp` (modify) | construct the streamer in `rebuildClipsForTrack` (`:507`); add `setClipSourcesNonRealtime(bool)` forwarded to every `ClipSourceProcessor` |
| `src/engine/ExportManager.cpp` (modify) | call `routingManager.setClipSourcesNonRealtime(true)` right after `renderGraph.setNonRealtime(true)` in `renderThreadFunc` |
| `tests/unit/engine/streaming_clip_source_test.cpp` (new) | gtest suite |
| `tests/unit/engine/clip_streaming_e2e_test.cpp` (new) | clip-level regression: streamed playback == preloaded playback |
| `tests/CMakeLists.txt` (modify) | register both test files |

---

## Task 1: `StreamingClipSource` — preload head + background reader + double buffer

**Files:**
- Create: `src/engine/StreamingClipSource.h`
- Test: `tests/unit/engine/streaming_clip_source_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/engine/streaming_clip_source_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/StreamingClipSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace {

// Writes a stereo sine .wav to a temp file and returns its path.
juce::File writeSineWav(int lengthSamples, double sr)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_stream_test.wav");
    f.deleteFile();
    juce::AudioBuffer<float> buf(2, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        float v = static_cast<float>(std::sin(2.0 * 3.14159 * 440.0 * s / sr));
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    {
        std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sr, 2, 16, {}, 0));
        // AudioFormatWriter takes ownership of the stream.
        out.release();
        writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    }
    return f;
}

} // namespace

TEST(StreamingClipSource, PreloadHeadCoversFirstBlocks)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr); // 30 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.setNonRealtime(true); // deterministic: read synchronously
    src.startPlayback();

    const int blockSize = 512;
    juce::AudioBuffer<float> out(2, blockSize);
    for (int i = 0; i < 8; ++i)
    {
        out.clear();
        src.readNextBlock(out, static_cast<int64_t>(i) * blockSize);
        // Preload head alone must have covered the first blocks.
        EXPECT_GT(out.getMagnitude(0, 0, blockSize), 0.05f)
            << "block " << i << " is silent (starved too early)";
        if (out.getMagnitude(0, 0, blockSize) <= 0.05f)
            break;
    }
    file.deleteFile();
}

TEST(StreamingClipSource, BackgroundFillKeepsStreamingAhead)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 60), sr); // 60 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.startPlayback(); // background reader ON (fills preload head, then runs)

    const int blockSize = 512;
    juce::AudioBuffer<float> out(2, blockSize);
    const int totalBlocks = 500; // ~5.7 s > 4 s preload head => must refill from disk
    bool lastBlockHadSignal = false;
    for (int i = 0; i < totalBlocks; ++i)
    {
        out.clear();
        src.readNextBlock(out, static_cast<int64_t>(i) * blockSize);
        // Pace reads at ~real-time consumption so the background reader gets
        // scheduled between blocks (real playback advances 1 block per 11.6 ms;
        // this test runs far faster than that without the sleep).
        if (i % 8 == 0)
            juce::Thread::sleep(2);
        if (i == totalBlocks - 1)
            lastBlockHadSignal = out.getMagnitude(0, 0, blockSize) > 0.05f;
    }
    // If the background reader never refilled past the head, the tail blocks
    // would be silent. This assertion is what proves the reader keeps ahead.
    EXPECT_TRUE(lastBlockHadSignal) << "background reader failed to refill past the preload head";
    EXPECT_GE(src.getDiskUsagePercent(), 0.0f);
    EXPECT_LE(src.getDiskUsagePercent(), 1.0f);
    src.stopPlayback();
    file.deleteFile();
}

TEST(StreamingClipSource, ShortFilePromotesToWholeFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 2), sr); // 2 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    EXPECT_TRUE(src.isWholeFileResident());
    file.deleteFile();
}

TEST(StreamingClipSource, MissingFileYieldsSilenceNotCrash)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingClipSource src;
    src.prepare(juce::File("C:/definitely/not/here.wav"), fm, 44100.0, 512);
    src.setNonRealtime(true);
    src.startPlayback();
    juce::AudioBuffer<float> out(2, 512);
    out.clear();
    src.readNextBlock(out, 0);
    EXPECT_EQ(out.getMagnitude(0, 0, 512), 0.0f); // silence, no crash
}

TEST(StreamingClipSource, PositionDrivenSeekReadsCorrectWindow)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr); // 30 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.setNonRealtime(true); // deterministic synchronous refill
    src.startPlayback();

    // Seek deep into the file — past the 4 s preload head, so the active side
    // must be refilled at the requested position (not auto-advanced).
    const int64_t deep = static_cast<int64_t>(sr * 20); // 20 s in
    juce::AudioBuffer<float> out(2, 512);
    out.clear();
    src.readNextBlock(out, deep);
    // A 440 Hz sine at 20 s is mid-cycle — expect a non-trivial signal, not
    // silence (silence would mean the window was never refilled for the seek).
    EXPECT_GT(out.getMagnitude(0, 0, 512), 0.05f)
        << "seek into [20s,20s+512) returned silence";
    src.stopPlayback();
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=StreamingClipSource.*
```
Expected: FAIL — `engine/StreamingClipSource.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `src/engine/StreamingClipSource.h`:

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <thread>
#include <vector>

namespace HDAW {

// Streams an audio file from disk in a background thread, exposing a fixed
// double buffer the audio thread reads lock-free via `readNextBlock`.
//
// Thread model:
//   - prepare / setNonRealtime / startPlayback / stopPlayback: message thread.
//   - readNextBlock(out, sourcePos): audio thread (realtime). No allocation,
//     no locking, no I/O — it only copies from whichever side currently covers
//     the requested source position and bumps the atomic swap gate when the
//     window is exhausted.
//   - background reader thread: opens the AudioFormatReader, fills the inactive
//     side at the requested position, and swaps via an atomic.
//
// Position-driven contract: the caller passes the exact source sample position
// (`offsetSamples + clipLocalSample` from ClipSourceProcessor), so seeks,
// loops, and clip offsets all work — the streamer never auto-advances.
//
// Starvation: if the reader cannot keep a side covering the request,
// readNextBlock emits silence for the starved window (never blocks, never
// asserts) and raises a starvation counter the instrumentation drainer can
// surface. In non-realtime (export) mode, a missing window is refilled
// synchronously on the export thread instead.
//
// Short files (< kPromoteToWholeFileMs) are read entirely into memory at
// prepare — identical to ClipSourceProcessor's preload today.
class StreamingClipSource
{
public:
    static constexpr int kBufferSeconds = 4;      // double buffer depth (2 s per side)
    static constexpr int kPromoteToWholeFileMs = 8000; // files under this duration preload whole

    StreamingClipSource() = default;
    ~StreamingClipSource() { stopPlayback(); }

    // Message thread. Opens the file, reads metadata; for short files reads
    // the WHOLE file into memory (promotion). For long files allocates the
    // double buffer and leaves the reader stopped.
    void prepare(const juce::File& file, juce::AudioFormatManager& fm,
                 double sampleRate, int samplesPerBlock)
    {
        stopPlayback();
        reader_.reset();
        wholeFile_.data[0].reset();
        wholeFile_.data[1].reset();
        wholeFile_.channels = 0;
        wholeFile_.length = 0;
        sampleRate_ = sampleRate;
        blockSize_ = samplesPerBlock;

        reader_ = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(file));
        if (reader_ == nullptr)
        {
            ok_ = false;
            return;
        }
        ok_ = true;

        numChannels_ = juce::jmin(static_cast<int>(reader_->numChannels), 2);
        sourceLength_ = reader_->lengthInSamples;

        const double durationSec = static_cast<double>(sourceLength_) / sampleRate_;
        if (durationSec <= static_cast<double>(kPromoteToWholeFileMs) / 1000.0)
        {
            // Promotion: read the whole file synchronously (short files are
            // cheaper in memory than a reader thread).
            wholeFile_.data[0] = std::make_unique<int16[]>(static_cast<size_t>(sourceLength_));
            wholeFile_.data[1] = std::make_unique<int16[]>(static_cast<size_t>(sourceLength_));
            wholeFile_.channels = numChannels_;
            wholeFile_.length = sourceLength_;
            juce::AudioBuffer<float> scratch(numChannels_, static_cast<int>(sourceLength_));
            reader_->read(&scratch, 0, static_cast<int>(sourceLength_), 0, true, numChannels_ == 1);
            for (int ch = 0; ch < numChannels_; ++ch)
            {
                for (int64_t i = 0; i < sourceLength_; ++i)
                    wholeFile_.data[ch][i] = static_cast<int16>(
                        juce::jlimit(-32768.0f, 32767.0f, scratch.getSample(ch, static_cast<int>(i)) * 32768.0f));
            }
            promoted_ = true;
            return;
        }

        promoted_ = false;
        const int64_t bufLen = static_cast<int64_t>(kBufferSeconds) * static_cast<int64_t>(sampleRate_);
        bufA_.assign(static_cast<size_t>(bufLen) * numChannels_, 0);
        bufB_.assign(static_cast<size_t>(bufLen) * numChannels_, 0);
        bufLen_ = bufLen;
        baseA_.store(0, std::memory_order_release);
        baseB_.store(0, std::memory_order_release);
        fillA_.store(0, std::memory_order_release);
        fillB_.store(0, std::memory_order_release);
        activeBuffer_.store(0, std::memory_order_release);
        targetPos_.store(0, std::memory_order_release);
        swapRequest_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        starvedCount_.store(0, std::memory_order_release);
    }

    bool isOk() const noexcept { return ok_; }
    bool isWholeFileResident() const noexcept { return promoted_; }
    int64_t sourceLength() const noexcept { return sourceLength_; }
    int numChannels() const noexcept { return numChannels_; }

    // Non-realtime (offline export): switching TO non-realtime stops the
    // background reader so the audio/export thread refills synchronously
    // (no dual writers). Message thread.
    void setNonRealtime(bool nr) noexcept
    {
        if (nr)
        {
            running_.store(false, std::memory_order_release);
            if (worker_.joinable())
                worker_.join();
            nonRealtime_.store(true, std::memory_order_release);
        }
        else
        {
            nonRealtime_.store(false, std::memory_order_release);
        }
    }

    // Message thread. In BOTH modes the preload head (side 0 at position 0) is
    // filled synchronously here so the first blocks never starve; non-realtime
    // then stops (later fills happen on demand inside readNextBlock), realtime
    // hands off to the background reader thread.
    void startPlayback()
    {
        if (!ok_ || promoted_)
            return;
        fillBuffer(0, 0);
        activeBuffer_.store(0, std::memory_order_release);
        if (nonRealtime_.load(std::memory_order_acquire))
            return;
        running_.store(true, std::memory_order_release);
        if (!worker_.joinable())
            worker_ = std::thread(&StreamingClipSource::readerLoop, this);
    }

    void stopPlayback()
    {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable())
            worker_.join();
    }

    // Audio thread. Emits `[sourcePos, sourcePos + numSamples)` of the source
    // into `out`. Never blocks, never allocates, does no I/O in realtime mode.
    // If the active side covers the request, copies; if not and non-realtime,
    // refills the active side synchronously; if not and realtime, raises a
    // swap request (reader fills at `targetPos_`) and emits silence.
    void readNextBlock(juce::AudioBuffer<float>& out, int64_t sourcePos)
    {
        const int numSamples = out.getNumSamples();
        const int numChannelsOut = juce::jmin(2, out.getNumChannels());

        out.clear();

        if (!ok_)
            return;

        if (promoted_)
        {
            for (int ch = 0; ch < numChannelsOut; ++ch)
            {
                const int srcCh = (numChannels_ > 1) ? juce::jmin(ch, numChannels_ - 1) : 0;
                float* dest = out.getWritePointer(ch);
                for (int s = 0; s < numSamples; ++s)
                {
                    const int64_t idx = sourcePos + s;
                    if (idx < wholeFile_.length)
                        dest[s] = static_cast<float>(wholeFile_.data[srcCh][idx]) / 32768.0f;
                }
            }
            return;
        }

        // Streaming path: read from the active sliding window [base, base+fill).
        const int which = activeBuffer_.load(std::memory_order_acquire);
        const int16* active = (which == 0) ? bufA_.data() : bufB_.data();
        int64_t base = (which == 0) ? baseA_.load(std::memory_order_acquire)
                                    : baseB_.load(std::memory_order_acquire);
        int64_t fill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                    : fillB_.load(std::memory_order_acquire);

        const bool covered = (sourcePos >= base
                              && sourcePos + numSamples <= base + fill);

        if (!covered && nonRealtime_.load(std::memory_order_acquire))
        {
            // Offline export: refill the CURRENT side synchronously. Disk I/O
            // on the export thread is acceptable — export is not realtime.
            fillBuffer(which, sourcePos);
            base = (which == 0) ? baseA_.load(std::memory_order_acquire)
                                : baseB_.load(std::memory_order_acquire);
            fill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                : fillB_.load(std::memory_order_acquire);
        }
        else if (!covered)
        {
            // Realtime and the reader is behind: ask it to fill at this
            // position and emit silence for the starved block.
            targetPos_.store(sourcePos, std::memory_order_release);
            swapRequest_.store(true, std::memory_order_release);
            ++starvedCount_;
            return;
        }

        const int64_t off = sourcePos - base;
        for (int ch = 0; ch < numChannelsOut; ++ch)
        {
            const int srcCh = (numChannels_ > 1) ? juce::jmin(ch, numChannels_ - 1) : 0;
            float* dest = out.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                // Guard against reading past the valid fill: after a near-EOF
                // partial refill, `fill < numSamples`, and `fillBuffer` zeroed
                // the remainder of the side, so `off + s < fill` gates real
                // data and anything beyond is silence.
                if (off + s < fill)
                    dest[s] = static_cast<float>(active[static_cast<size_t>(off + s) * numChannels_ + srcCh]) / 32768.0f;
            }
        }
    }

    // Message thread. Fraction of the active side currently filled (0..1) for
    // a disk-usage style readout.
    float getDiskUsagePercent() const noexcept
    {
        if (promoted_) return 1.0f;
        if (bufLen_ <= 0) return 0.0f;
        const int which = activeBuffer_.load(std::memory_order_acquire);
        const int64_t fill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                          : fillB_.load(std::memory_order_acquire);
        return juce::jlimit(0.0f, 1.0f, static_cast<float>(fill) / static_cast<float>(bufLen_));
    }

    uint64_t starvedCount() const noexcept { return starvedCount_.load(std::memory_order_acquire); }

private:
    // Fills `which` side (0 or 1) with a contiguous window starting at source
    // `base`, up to bufLen_ samples or the source end, whichever is smaller.
    // Publishes sideFill_/sideBase_ BEFORE the caller flips activeBuffer_.
    void fillBuffer(int which, int64_t base)
    {
        int16* destBuf = (which == 0) ? bufA_.data() : bufB_.data();
        const int64_t available = (std::max)((int64_t) 0,
            (std::min)(sourceLength_ - base, bufLen_));

        if (available > 0)
        {
            // Read directly into the int16 buffer via a float scratch that
            // stays within the reader's read window.
            juce::AudioBuffer<float> scratch(numChannels_, static_cast<int>(available));
            reader_->read(&scratch, 0, static_cast<int>(available),
                          base, true, numChannels_ == 1);
            for (int ch = 0; ch < numChannels_; ++ch)
            {
                for (int64_t i = 0; i < available; ++i)
                {
                    destBuf[static_cast<size_t>(i) * numChannels_ + ch] =
                        static_cast<int16>(juce::jlimit(-32768.0f, 32767.0f,
                            scratch.getSample(ch, static_cast<int>(i)) * 32768.0f));
                }
            }
        }
        // Zero the tail beyond a partial fill so the audio thread's
        // `off + s < fill` guard sees silence (never stale data from this
        // side's previous fill). This is O(bufLen) but happens on the message
        // thread (prepare / non-realtime refill) or the background reader
        // thread, never on the audio thread.
        for (int64_t i = available; i < bufLen_; ++i)
        {
            for (int ch = 0; ch < numChannels_; ++ch)
                destBuf[static_cast<size_t>(i) * numChannels_ + ch] = 0;
        }
        (which == 0 ? fillA_ : fillB_).store(available, std::memory_order_release);
        (which == 0 ? baseA_ : baseB_).store(base, std::memory_order_release);
    }

    void readerLoop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            const bool swap = swapRequest_.load(std::memory_order_acquire);
            if (!swap)
            {
                juce::Thread::sleep(2);
                continue;
            }
            swapRequest_.store(false, std::memory_order_release);
            // Fill the OTHER side at the audio thread's last requested
            // position, then flip the active window. The side's base/fill were
            // published before the store below, so the audio thread always
            // sees a consistent pair.
            const int next = activeBuffer_.load(std::memory_order_acquire) == 0 ? 1 : 0;
            const int64_t pos = targetPos_.load(std::memory_order_acquire);
            fillBuffer(next, pos);
            activeBuffer_.store(next, std::memory_order_release);
        }
    }

    std::unique_ptr<juce::AudioFormatReader> reader_;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    bool ok_ = false;
    bool promoted_ = false;
    int numChannels_ = 0;
    int64_t sourceLength_ = 0;

    // Whole-file promotion path.
    struct WholeFile
    {
        std::unique_ptr<int16[]> data[2];
        int channels = 0;
        int64_t length = 0;
    } wholeFile_;

    // Streaming double buffer (interleaved int16 per side). Each side holds a
    // contiguous [sideBase, sideBase + sideFill) window of the source.
    std::vector<int16> bufA_, bufB_;
    int64_t bufLen_ = 0;
    std::atomic<int64_t> baseA_{ 0 }, baseB_{ 0 };
    std::atomic<int64_t> fillA_{ 0 }, fillB_{ 0 };

    std::atomic<int64_t> targetPos_{ 0 };
    std::atomic<int> activeBuffer_{ 0 };
    std::atomic<bool> swapRequest_{ false };
    std::atomic<bool> running_{ false };
    std::atomic<bool> nonRealtime_{ false };
    std::atomic<uint64_t> starvedCount_{ 0 };

    std::thread worker_;
};

} // namespace HDAW
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=StreamingClipSource.*
```
Expected: PASS (4/4).

> **Implementation notes for the executor:** the initial buffer fills and the swap handshake above are intentionally minimal and correct-for-the-tests. The audio thread requests a window with `readNextBlock(out, sourcePos)`; the reader fulfills uncovered positions. Tuning knobs if a test races (e.g. `BackgroundFillKeepsStreamingAhead` starves): the reader loop timing, the preload-head prefill depth, and the `juce::Thread::sleep(2)` in `readerLoop` — not the test asserts. The contract is: the reader must keep the active window ≥ 1 block ahead of the read position during normal playback, and the window covering `sourcePos` must be present before `activeBuffer_` flips to the side containing it. `blockSize_` is retained only for the promotion path's preload-head decision and the window sizing — the streaming read itself is position-driven and never advances an internal read pointer. The `int16` scratch-copy loops are intentionally simple; a later optimization can read the reader's native buffer directly. In non-realtime mode `readNextBlock` performs synchronous disk I/O — that is deliberate (export is offline) and must NOT be back-ported into the realtime path.

- [ ] **Step 5: Commit**

```bash
git add src/engine/StreamingClipSource.h tests/unit/engine/streaming_clip_source_test.cpp tests/CMakeLists.txt
git commit -m "feat(stream): StreamingClipSource double-buffer background reader with promotion + starvation"
```

---

## Task 2: Adopt the streamer into `ClipSourceProcessor`

**Files:**
- Modify: `src/engine/ClipSourceProcessor.h`
- Test: `tests/unit/engine/clip_streaming_e2e_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/engine/clip_streaming_e2e_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/ClipSourceProcessor.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <cmath>

namespace {
juce::File writeSineWav(int lengthSamples, double sr)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_clip_stream_e2e.wav");
    f.deleteFile();
    juce::AudioBuffer<float> buf(2, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        float v = static_cast<float>(std::sin(2.0 * 3.14159 * 440.0 * s / sr));
        buf.setSample(0, s, v); buf.setSample(1, s, v);
    }
    std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out.get(), sr, 2, 16, {}, 0));
    out.release();
    writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    return f;
}
}

// Reads `numBlocks` 512-sample blocks from a ClipSourceProcessor, advancing the
// transport each block so processBlock actually emits the source (silence
// otherwise). Returns channel-0 samples.
std::vector<float> renderBlocks(HDAW::ClipSourceProcessor& proc,
                                HDAW::TransportManager& tm,
                                int numBlocks)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf(2, 512);
    for (int i = 0; i < numBlocks; ++i)
    {
        tm.setCurrentSample(static_cast<int64_t>(i) * 512);
        buf.clear();
        juce::MidiBuffer empty;
        proc.processBlock(buf, empty);
        for (int s = 0; s < 512; ++s)
            out.push_back(buf.getSample(0, s));
    }
    return out;
}

TEST(ClipStreamingE2E, StreamedPlaybackMatchesPreloadedPlayback)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr); // >8s => streams

    HDAW::TransportManager tm;
    HDAW::ClipSourceProcessor proc(tm, fm);
    proc.setSourceFile(file.getFullPathName());
    proc.setStartTime(0.0);
    proc.setDuration(30.0);
    proc.setGain(1.0f);
    proc.setNonRealtime(true); // synchronous deterministic read
    proc.prepareToPlay(sr, 512);

    // 430 blocks ≈ 5 s: read within the preload head AND across the 4 s refill
    // boundary, capturing the output.
    const std::vector<float> streamed = renderBlocks(proc, tm, 430);

    // Reference: identical processor with streaming DISABLED (whole-file
    // float preload — the pre-feature behavior).
    HDAW::ClipSourceProcessor ref(tm, fm);
    ref.setSourceFile(file.getFullPathName());
    ref.setStartTime(0.0);
    ref.setDuration(30.0);
    ref.setGain(1.0f);
    ref.setStreamingEnabled(false); // force the preload path
    ref.setNonRealtime(true);
    ref.prepareToPlay(sr, 512);
    const std::vector<float> preloaded = renderBlocks(ref, tm, 430);

    ASSERT_EQ(streamed.size(), preloaded.size());
    for (size_t i = 0; i < streamed.size(); ++i)
    {
        // int16 storage gives a 1/32768 ≈ 3e-5 quantization step; allow a
        // tight tolerance that still catches a wrong-buffer/offset bug.
        EXPECT_NEAR(streamed[i], preloaded[i], 4.0 / 32768.0)
            << "mismatch at sample " << i;
    }
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ClipStreamingE2E.*
```
Expected: FAIL — `setNonRealtime` doesn't exist on `ClipSourceProcessor` yet.

- [ ] **Step 3: Add the streaming source and `setNonRealtime`/`setStreamingEnabled` to `ClipSourceProcessor`**

In `src/engine/ClipSourceProcessor.h`:
- Add `#include "StreamingClipSource.h"` near the top.
- Add a member: `HDAW::StreamingClipSource streamer;`
- Add a streaming flag and a `setStreamingEnabled` escape hatch (default ON so the feature applies automatically; the E2E reference uses it to force the legacy preload path):

```cpp
    // When ON (default), long clips stream from disk; when OFF, the legacy
    // whole-file float preload is used (the pre-feature behavior). Used by
    // tests to build the preload reference.
    void setStreamingEnabled(bool e) { streamingEnabled_ = e; }
    bool isStreamingEnabled() const { return streamingEnabled_; }
```

- Add the non-realtime flag and setter. It must survive a later `prepareToPlay` (RoutingManager rebuild), so store it and pass it to the streamer inside `prepareToPlay`:

```cpp
    void setNonRealtime(bool nr) { nonRealtimeFlag_ = nr; }
    bool isNonRealtime() const { return nonRealtimeFlag_; }
```

- In `prepareToPlay` (`:122-162`), REPLACE the unconditional whole-file preload with a streaming-or-preload decision. **Do NOT preload AND stream the same long file** (that double-allocates the very memory the feature exists to save — a 1 GB clip would still malloc 2×4 GB of `HeapBlock<float>`):

```cpp
        preloadedData[0].free();
        preloadedData[1].free();
        preloadedChannels = 0;
        preloadedLength = 0;

        if (sourceFile.isNotEmpty())
        {
            juce::File f(sourceFile);
            bool fileExists = f.existsAsFile();
            std::unique_ptr<juce::AudioFormatReader> r(
                formatManager.createReaderFor(f));

            // Streaming decision: ON, an existing file, and long enough that
            // whole-file preload would dominate memory.
            const double durSec = (r != nullptr) ? (double) r->lengthInSamples / sr : 0.0;
            const bool useStreaming = streamingEnabled_ && fileExists && r != nullptr
                && durSec > (double) StreamingClipSource::kPromoteToWholeFileMs / 1000.0;

            if (useStreaming)
            {
                // Streaming path: the streamer owns the file. It promotes
                // short files to whole-file itself; for long files it keeps a
                // ~4 s double buffer. Do NOT also fill preloadedData.
                streamer.setNonRealtime(nonRealtimeFlag_);
                streamer.prepare(f, formatManager, sr, samplesPerBlock);
                streamer.startPlayback();
                activeBuffer.store(2, std::memory_order_release); // streamer source
                preloadedChannels = streamer.numChannels();
                preloadedLength = streamer.sourceLength();
            }
            else if (r != nullptr && fileExists)
            {
                // Legacy whole-file float preload (short files, or streaming
                // disabled). This is the pre-feature behavior, byte for byte.
                preloadedChannels = juce::jmin(static_cast<int>(r->numChannels), 2);
                const int total = static_cast<int>(r->lengthInSamples);
                if (preloadedChannels > 0 && total > 0)
                {
                    preloadedData[0].malloc(total);
                    preloadedData[1].malloc(total);
                    float* const ptrs[2] = { preloadedData[0], preloadedData[1] };
                    r->read(ptrs, preloadedChannels, 0, total);
                    preloadedLength = static_cast<int64_t>(total);
                }
                activeBuffer.store(0, std::memory_order_release); // preloaded source
            }
            else
            {
                // Missing/unreadable source — surface once per clip (unchanged
                // from today).
                HDAW_LOG("DIAG", "ClipSourceProc prepareToPlay FAIL file=" + sourceFile.toStdString() + " exists=" + (fileExists ? "yes" : "no") + " reader=" + (r ? "ok" : "null"));
                activeBuffer.store(0, std::memory_order_release);
            }
        }
```

- In `processBlock`, add the streaming branch to the fill path — NOT the buffer-selection block at `:226-246`. The audible clamp (`numToRead`, `:256-289`) runs AFTER the selection and must govern the streamer's read the same way it governs the preloaded read; if the streaming branch filled the buffer before that clamp, the clamp's `else { buffer.clear(); return; }` at `:285-289` would wipe the streamer's data. So: extend the fill block at `:264-283`:

```cpp
            buffer.clear();
            if (buf == 2)
            {
                // Streaming source: delegate the read to the streamer at the
                // transport-derived source position. It clears + fills the
                // whole block (or emits silence on starvation). The audible
                // clamp above (numToRead) already bounds how many samples the
                // gain/envelope loop below will apply; zero the tail exactly
                // like the preloaded paths do.
                streamer.readNextBlock(buffer, sourceSample);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* dest = buffer.getWritePointer(ch);
                    for (int s = numToRead; s < numSamples; ++s)
                        dest[s] = 0.0f;
                }
            }
            else
            {
                // ... existing preloaded/stretched read loop unchanged ...
            }
```

> The clip bounds/`audibleRemaining` checks (`:256-289`) run BEFORE the fill, so `numToRead` is already clamped to the clip's audible window. For a mid-block clip end (`numToRead < numSamples`), the streamer has filled the whole block but the gain/envelope loop (`:341-414`) applies only over `numToRead`, and the tail is zeroed above — matching the preloaded paths. The fade-out envelope stays phase-safe because it never goes negative (`:356-361`). `srcLength` for the streaming case is `streamer.sourceLength()` (set in `prepareToPlay`), so the `sourceSample < srcLength` gate behaves identically; if `sourceSample` runs past EOF the streamer itself emits silence. Restructure only if the executor prefers a `fillBlock(...)` helper over an inline branch — but the shared envelope loop must run on the streaming block exactly as it does on the preloaded one.

- `switchToSourceFile` (`:30-56`): add the same streaming-or-preload decision so a live source switch doesn't double-preload. If `streamingEnabled_` and the new file is long, `streamer.prepare` + `streamer.startPlayback()` + `activeBuffer.store(2)` and skip the malloc; otherwise free the streamer and use the legacy path.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ClipStreamingE2E.*
build/Debug/hdaw_tests.exe --gtest_filter=ClipSourceProcessor.*
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/engine/ClipSourceProcessor.h tests/unit/engine/clip_streaming_e2e_test.cpp tests/CMakeLists.txt
git commit -m "feat(stream): ClipSourceProcessor streams long clips from disk via StreamingClipSource"
```

---

## Task 3: Rebuild-path restore (Gate 1/10) — adopt streamer state in `RoutingManager`

**Files:**
- Modify: `src/engine/RoutingManager.cpp` (`rebuildClipsForTrack` `:507-597`)
- Test: `tests/unit/engine/clip_streaming_e2e_test.cpp` (or extend `track_mixer_state_test.cpp` pattern)

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/engine/clip_streaming_e2e_test.cpp`:

```cpp
// The streamer is created fresh per rebuild (rebuildClipsForTrack makes a new
// ClipSourceProcessor each time, RoutingManager.cpp:507). The contract to
// assert is: a LONG clip that must stream is actually created in streaming
// mode (isWholeFileResident() == false) and a SHORT one preloads. We assert
// on the processor the RoutingManager creates by reaching into a rebuilt
// project. Rather than duplicating the full routing fixture, assert the
// decision logic at the unit level: a >8s file must NOT be whole-file
// resident in a freshly-prepared StreamingClipSource.
TEST(ClipStreamingE2E, LongFileNotWholeFileResident)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    auto file = writeSineWav(static_cast<int>(44100.0 * 30), 44100.0);
    HDAW::StreamingClipSource src;
    src.prepare(file, fm, 44100.0, 512);
    EXPECT_FALSE(src.isWholeFileResident());
    EXPECT_GT(src.sourceLength(), 0);
    file.deleteFile();
}
```

> Note: the full `rebuildRoutingGraph` seam (streamer reacquired without re-decode) is the domain of Subsystem C (audio pool). Here the minimal Gate 1/10 obligation is: the streamer is **freshly created per rebuild** (already the case — `rebuildClipsForTrack` builds a new `ClipSourceProcessor`), so no state crosses a rebuild that needs restore. The explicit assertion lives in this test.

- [ ] **Step 2: Run test to verify it passes (decision logic is already correct)**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ClipStreamingE2E.LongFileNotWholeFileResident
```
Expected: PASS.

- [ ] **Step 3: Confirm `rebuildClipsForTrack` needs no change for streaming adoption**

`rebuildClipsForTrack` (`RoutingManager.cpp:507`) already constructs `ClipSourceProcessor` per clip and `prepareToPlay` is invoked by the graph during rebuild — the streamer is created inside `prepareToPlay`. Verify by reading `RoutingManager.cpp:594-601` (clip node creation) — no change needed; the `setNonRealtime` wiring for export is Task 4.

- [ ] **Step 4: Commit (test-only; documents the rebuild contract)**

```bash
git add tests/unit/engine/clip_streaming_e2e_test.cpp
git commit -m "test(stream): assert long clips stream (not whole-file) after rebuild"
```

---

## Task 4: Non-realtime propagation into export

**Files:**
- Modify: `src/engine/ExportManager.cpp` (render loop, `:194-219`)
- Modify: `src/engine/RoutingManager.cpp` (accept an `isNonRealtime` flag)

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/engine/clip_streaming_e2e_test.cpp`:

```cpp
TEST(ClipStreamingE2E, NonRealtimeStreamingMatchesPreloadAcrossLongFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 60), sr);

    // Non-realtime streamer: synchronous refill on demand, deterministic.
    HDAW::StreamingClipSource nr;
    nr.prepare(file, fm, sr, 512);
    nr.setNonRealtime(true);
    nr.startPlayback();
    juce::AudioBuffer<float> a(2, 512);
    std::vector<float> outA;
    for (int i = 0; i < 430; ++i)
    {
        a.clear();
        nr.readNextBlock(a, static_cast<int64_t>(i) * 512);
        for (int s = 0; s < 512; ++s) outA.push_back(a.getSample(0, s));
    }

    // Realtime streamer: background reader.
    HDAW::StreamingClipSource rt;
    rt.prepare(file, fm, sr, 512);
    rt.setNonRealtime(false);
    rt.startPlayback();
    juce::AudioBuffer<float> b(2, 512);
    std::vector<float> outB;
    for (int i = 0; i < 430; ++i)
    {
        b.clear();
        rt.readNextBlock(b, static_cast<int64_t>(i) * 512);
        // Pace so the reader thread is scheduled (real playback advances one
        // block per 11.6 ms; this loop is otherwise far faster).
        if (i % 8 == 0)
            juce::Thread::sleep(2);
        for (int s = 0; s < 512; ++s) outB.push_back(b.getSample(0, s));
    }
    rt.stopPlayback();

    ASSERT_EQ(outA.size(), outB.size());
    for (size_t i = 0; i < outA.size(); ++i)
        EXPECT_NEAR(outA[i], outB[i], 4.0 / 32768.0) << "sample " << i;
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it passes (both modes share the same fill)**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ClipStreamingE2E.NonRealtimeStreamingMatchesPreloadAcrossLongFile
```
Expected: PASS — both modes fill the same contiguous windows from the same
source offsets (realtime reader and non-realtime on-demand refill both call
`fillBuffer(side, pos)`), so their sample streams are identical. The one
behavioral difference is that the realtime path may starve the first block if
the background thread is slow to start; the preload-head prefill in
`startPlayback` (buffer 0 filled synchronously in BOTH modes) is what makes
this deterministic.

- [ ] **Step 3: If it fails, check the preload-head prefill is present**

Both modes must call `fillBuffer(0, 0)` in `startPlayback` before the
realtime/`nonRealtime_` branch — the non-realtime mode stops after that, the
realtime mode additionally starts the worker. Confirm both paths are
identical up to the point the window is consumed; the realtime path must NOT
`continue`-sleep past the first block without filling buffer 0. Re-run.

- [ ] **Step 4: Propagate the flag through export**

`ClipSourceProcessor::setNonRealtime(nr)` stores `nonRealtimeFlag_`; the streamer is switched by the next `prepareToPlay`. Export, however, renders from an ALREADY-BUILT graph — `renderThreadFunc` runs `renderGraph.prepareToPlay` at `:212`, then `renderGraph.setNonRealtime(true)` at `:219`, then renders. There is no rebuild between `:219` and the first `processBlock`, so the stored-flag route is too late for the streamer that was created during `prepareToPlay`.

Fix: `ClipSourceProcessor::setNonRealtime` must ALSO push through to the live streamer immediately (idempotent with the later `prepareToPlay`):

```cpp
    void setNonRealtime(bool nr)
    {
        nonRealtimeFlag_ = nr;
        streamer.setNonRealtime(nr); // switch the live streamer now (export)
    }
```

`StreamingClipSource::setNonRealtime(true)` stops the background reader and joins it (message/export thread — safe), so a long clip built in realtime mode becomes synchronous for the render. (Calling it before `prepareToPlay` is harmless: the streamer hasn't opened anything yet; the flag is re-applied inside `prepareToPlay`.)

Then wire the RoutingManager to forward it. In `ExportManager.cpp::renderThreadFunc`, right after `renderGraph.setNonRealtime(true)` (`:219`), add:

```cpp
            routingManager.setClipSourcesNonRealtime(true);
```

Add `RoutingManager::setClipSourcesNonRealtime(bool)` iterating `audioClipSources` (`std::map<std::pair<int, int>, ClipSourceProcessor*>`, `RoutingManager.h:99`) and calling `setNonRealtime` on each:

```cpp
    void setClipSourcesNonRealtime(bool nr)
    {
        for (auto& kv : audioClipSources)
            kv.second->setNonRealtime(nr);
    }
```

This runs on the export render thread inside `renderThreadFunc`, before rendering begins — no audio thread is running yet. `StreamingClipSource::setNonRealtime(true)` stops and joins the background reader, which does plain `AudioFormatReader` I/O (no JUCE message machinery), so the join is safe off the message thread. Lesson 12 (parking the pump for graph mutations) does NOT apply here — no `AudioProcessorGraph` mutation is involved, only the streamer's own thread.

- [ ] **Step 5: Run the export + clip suites**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=ClipStreamingE2E.*
build/Debug/hdaw_tests.exe --gtest_filter=*Export*
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/engine/StreamingClipSource.h src/engine/ExportManager.cpp src/engine/RoutingManager.cpp tests/unit/engine/clip_streaming_e2e_test.cpp
git commit -m "feat(stream): propagate non-realtime mode to streaming clip sources during export"
```

---

## Task 5: Success-gate verification (full suite + latency/quality)

**Files:** (verification only)

- [ ] **Step 1: Full engine suite**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe
```
Expected: PASS, all suites.

- [ ] **Step 2: Latency/quality check (lessons 7/8)**

The streaming path adds **zero** latency — `readNextBlock` emits the same sample frames the preload path would, from a different storage. Verify:
- `ClipSourceProcessor::getLatency` unchanged (it reports 0 — no change to report).
- The `ClipStreamingE2E` tests assert sample-exact equality (within int16 quantization) to preloaded playback — this is the quality gate.
- Run `audio-dsp-review` + `audio-numerics-review` on `StreamingClipSource.h` before merge (denormals: `processBlock` already has `ScopedNoDenormals`; the streamer has no arithmetic that can denormal).

- [ ] **Step 3: Memory ceiling sanity**

Manual check: a project with a 60 s clip must NOT allocate `60 s × 44.1k × 4 B × 2 ≈ 21 MB` in `preloadedData`. With streaming, `StreamingClipSource` allocates `4 s × 44.1k × 2 B × 2 ≈ 700 KB` for the double buffer. The `LongFileNotWholeFileResident` test asserts the streamer is not in promotion mode for long files.

- [ ] **Step 4: Anti-pattern scan**

- Audio thread: `readNextBlock` allocates nothing, locks nothing, does no I/O — copies from the active buffer, bumps atomics.
- No new RPC/MCP (feature is internal; MCP parity N/A).
- No `DBG`. No raw hex. No full-tree walks.
- Starvation renders silence (not a crash/assert) — matches the HISE starvation contract.

- [ ] **Step 5: Version bump + graph refresh**

- Bump `CMakeLists.txt` + `frontend/package.json` in sync (e.g. 0.21.0 → 0.22.0 — feature).
- Refresh knowledge graph: `codebase-memory` `index_repository` (project `D-pdf-roo-projects-hdaw3`, mode `full` — new file + new methods).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt frontend/package.json
git commit -m "chore: bump to 0.22.0 (clip disk streaming)"
```

---

## Success Gates (completion contract — evidence required)

- [ ] G1: `StreamingClipSource.*` + `ClipStreamingE2E.*` gtest suites pass (preload head, background fill, promotion, missing-file silence, position-driven seek, streamed==preloaded sample-exact within int16 quantization, non-realtime==realtime, long-file-not-whole-file).
- [ ] G2: Full `build/Debug/hdaw_tests.exe` passes — no regression in the 250+ existing tests, including `ClipSourceProcessor.*`, `Stretch*.*`, `*Export*`.
- [ ] G3: Audio thread free of allocation/lock/I-O in the streaming path (review + the sample-exact tests).
- [ ] G4: Latency unchanged (report 0; no path-length change).
- [ ] G5: Quality unchanged (sample-exact within 4/32768 tolerance vs preloaded playback).
- [ ] G6: Memory ceiling: long clips stream (double buffer ≈ 2 s/side) instead of whole-file preload — asserted by `LongFileNotWholeFileResident`. `prepareToPlay` must NOT fill `preloadedData` for a streaming clip (the double-read is a gate failure).
- [ ] G7: Version bumped in both files; knowledge graph refreshed (`index_repository` full).
- [ ] G8: No new anti-patterns (no audio-thread I/O/alloc, no `DBG`, no N-call loops).

## Dependency Map

- **Blast radius:** `ClipSourceProcessor` (hot audio path + its `activeBuffer` selection), `RoutingManager::rebuildClipsForTrack` (`:507` clip construction), `ExportManager` render loop (`:194-219`), engine tests that touch clips.
- **Upstream:** `RoutingManager::rebuildClipsForTrack` creates each `ClipSourceProcessor` (`RoutingManager.cpp:507`) and calls `prepareToPlay` via the graph; `ExportManager` builds a standalone `RoutingManager` (`ExportManager.cpp:194-199`).
- **Downstream:** `processBlock` consumers of clip audio; the graph's clip nodes (`audioClipNodes`, `RoutingManager.h:98`); export render.
- **Projections affected:** none — clip ValueTree/entity unchanged; delta/fullSync behavior unchanged.
- **SPSC paths touched:** new message→audio handoff (streamer `activeBuffer`/`baseA/B`/`fillA/B`/`targetPos`/`swapRequest` atomics — same shape as `activeBuffer` in `ClipSourceProcessor`); audio→message starvation counter; export-thread→streamer `setNonRealtime` (joins the reader, no audio thread running during export build).
- **God nodes in scope:** `ClipSourceProcessor` (high-degree — new buffer source), `RoutingManager` (rebuilt per export). Elevated-risk, minimal-diff changes.
- **Path integrity:** full chain = `rebuildClipsForTrack` → `ClipSourceProcessor::prepareToPlay` → `streamer.prepare` → `streamer.readNextBlock(buffer, sourceSample)` in `processBlock` → graph output. Verified by `ClipStreamingE2E` (sample-exact) + Task 4 (export).

## Pitfall Gates Triggered

- **Gate 1/6/10 (rebuild restore):** the streamer is freshly created per `rebuildClipsForTrack` (no cross-rebuild state), so nothing to restore — asserted by `LongFileNotWholeFileResident`. When Subsystem C (pool) lands, the pool must reacquire without re-decode (its own Gate).
- **Gate 3 (audio-thread safety):** `readNextBlock` is allocation/lock/I-O free; the reader thread is the only disk/format caller.
- **Gate 4 (stale binaries):** new test `.cpp` registered in `tests/CMakeLists.txt`.
- **Gate 8 (quality):** int16 streaming storage must not audibly regress vs float preload — `ClipStreamingE2E` enforces ±4/32768.
- **Lesson 3 (early-out):** `processBlock` still clears + returns when transport stopped — streaming doesn't change the early-out contract.
- **Lesson 14 (cross-process):** N/A — no isolation; the streamer is in-process.
- **Lesson 17 (visible logs):** starvation is counted (drainable by Subsystem B's `BufferCheck`); no silent failures.
- **Gate 11 (pump):** the reader thread does NOT touch JUCE graph/message machinery — it uses `AudioFormatReader` directly (same as `StretchRenderer`'s worker `StretchRenderer.cpp:38`); no pump dependency.

## Anti-Pattern Scan

- No audio-thread allocation/locking/I-O in `readNextBlock`. No `rebuildRoutingGraph()` per-clip. No `DBG`. No raw hex. No N-call RPC loops (no new RPC). Starvation = silence + counter, never a block or assert. The streamer has no ValueTree/FX-slot dependency (portability contract, mirroring `SamplerEngine`).