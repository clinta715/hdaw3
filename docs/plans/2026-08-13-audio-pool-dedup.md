# Audio Pool Dedup / Shared Decodes (Subsystem C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `DecodedSoundPool` that decodes each audio file once and hands out `shared_ptr<const DecodedSound>` to every consumer (clip preload path + sampler), so two clips (or a clip + sampler slot) referencing the same file share one decode instead of decoding per consumer per rebuild.

**Architecture:** A message-thread-only `DecodedSoundPool` owned by `ProjectPool`, keyed by full path, holding strong cache entries with `use_count()==1` pruning (an entry dies only when no consumer references it). `ClipSourceProcessor` borrows the pooled float buffers directly (no copy); the sampler path reuses the pooled decode when a pool is available and falls back to its current direct decode otherwise. The streaming path (long files > 8 s) is unchanged — per-clip readers remain; shared streaming handles are a documented follow-up. When a `RoutingManager` has no pool (export render path), consumers decode directly as today.

**Tech Stack:** C++17 (JUCE 8), gtest. Reference: HISE `ModulatorSamplerSoundPool` (`ModulatorSamplerSound.h:757/:766`).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/engine/DecodedSoundPool.h` (new) | `DecodedSound` immutable float decode + `DecodedSoundPool` cache (header-only, no `.cpp`, no CMake change) |
| `src/engine/ClipSourceProcessor.h` (modify) | borrow pooled decode in `preloadWholeFile`/`prepareToPlay`; `preloadedData` HeapBlocks → `decoded_` shared_ptr |
| `src/engine/TrackFXSlot.h` (modify) | `loadSamplerState` gains `DecodedSoundPool*` param; pooled path when present |
| `src/engine/Track.h` (modify) | `setDecodedSoundPool` + member; pass pool to `loadSamplerState` in `rebuildFXChain` |
| `src/engine/Track.cpp` (modify) | `rebuildFXChain` sampler branch passes the pool |
| `src/engine/RoutingManager.h` (modify) | ctor gains `DecodedSoundPool* = nullptr`; member |
| `src/engine/RoutingManager.cpp` (modify) | pass pool to `ClipSourceProcessor` ctor + `buildTrackProcessor` |
| `src/engine/MainAudioProcessor.h` (modify) | `setDecodedSoundPool` + member |
| `src/engine/MainAudioProcessor.cpp` (modify) | pass pool at both `RoutingManager` constructions (:65, :502) |
| `src/engine/ProjectPool.h` (modify) | own `DecodedSoundPool` member + getter |
| `src/engine/AudioEngine.cpp` (modify) | `initialize()` wires `projectPool.getDecodedSoundPool()` into `mainProcessor` |
| `tests/unit/engine/audio_pool_dedup_test.cpp` (new) | gtest suite (pool semantics + engine-level sharing/rebuild) |
| `tests/CMakeLists.txt` (modify) | register the new test |

---

## Task 1: `DecodedSound` + `DecodedSoundPool` (header-only)

**Files:**
- Create: `src/engine/DecodedSoundPool.h`
- Test: `tests/unit/engine/audio_pool_dedup_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/engine/audio_pool_dedup_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/DecodedSoundPool.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>

namespace {

juce::File writeSineWav(const char* tag, int lengthSamples, double sr = 44100.0)
{
    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int dataSize = lengthSamples * numChannels * bytesPerSample;

    juce::File file = juce::File::getCurrentWorkingDirectory()
        .getChildFile(juce::String("pool_test_") + tag + ".wav");
    file.deleteFile();
    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);

    auto writeChunk = [&](const char* id, const void* data, int size)
    {
        out.write(id, 4);
        out.write(reinterpret_cast<const char*>(&size), 4);
        out.write(static_cast<const char*>(data), size);
    };

    int sampleRate = static_cast<int>(sr);
    int byteRate = sampleRate * numChannels * bytesPerSample;
    int blockAlign = numChannels * bytesPerSample;
    out.write("RIFF", 4);
    int riffSize = 36 + dataSize;
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int fmtSize = 16;
    short audioFormat = 1;
    short channels = static_cast<short>(numChannels);
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    short bits = bitsPerSample;
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (int i = 0; i < lengthSamples; ++i)
    {
        short v = static_cast<short>(std::sin(2.0 * 3.14159 * 440.0 * i / sampleRate) * 32000.0);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    out.close();
    return file;
}

} // namespace

TEST(AudioPoolDedup, SameFileReturnsSameSoundAndDecodesOnce)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("share", 44100);
    auto a = pool.acquire(file.getFullPathName());
    auto b = pool.acquire(file.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a.get(), b.get());            // shared_ptr equality — one DecodedSound
    EXPECT_EQ(pool.getDecodeCount(), 1);    // decode-count == 1
    EXPECT_EQ(pool.getEntryCount(), 1);
    file.deleteFile();
}

TEST(AudioPoolDedup, DifferentFilesDecodeSeparately)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto fileA = writeSineWav("diff_a", 44100);
    auto fileB = writeSineWav("diff_b", 44100);
    auto a = pool.acquire(fileA.getFullPathName());
    auto b = pool.acquire(fileB.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(pool.getDecodeCount(), 2);
    fileA.deleteFile();
    fileB.deleteFile();
}

TEST(AudioPoolDedup, MissingFileReturnsNull)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto sound = pool.acquire("C:/definitely/not/here.wav");
    EXPECT_EQ(sound, nullptr);
    EXPECT_EQ(pool.getDecodeCount(), 0);
}

TEST(AudioPoolDedup, RefcountDropsAndEntryEvictsWhenUnreferenced)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("refcount", 44100);
    {
        auto a = pool.acquire(file.getFullPathName());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(pool.getEntryCount(), 1);
    } // last consumer released
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 0);     // evicted — nothing references it

    // Re-acquire after eviction re-decodes (genuinely unused in between).
    auto b = pool.acquire(file.getFullPathName());
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.getDecodeCount(), 2);
    file.deleteFile();
}

TEST(AudioPoolDedup, ReferencedEntrySurvivesPrune)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("keep", 44100);
    auto a = pool.acquire(file.getFullPathName());
    ASSERT_NE(a, nullptr);
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 1);     // still referenced → not evicted
    EXPECT_EQ(pool.getDecodeCount(), 1);
    file.deleteFile();
}

TEST(AudioPoolDedup, MonoDataMatchesDecodedSamples)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("data", 1024);
    auto sound = pool.acquire(file.getFullPathName());
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->numChannels, 1);
    EXPECT_EQ(sound->length, 1024);
    EXPECT_GT(std::abs(sound->data[0][0]), 0.0f); // non-silent start sample
    file.deleteFile();
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `tests/CMakeLists.txt` (after line 48, `unit/engine/realtime_safety_test.cpp`):

```cmake
    unit/engine/audio_pool_dedup_test.cpp
```

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.*
```

Expected: FAIL to compile — `engine/DecodedSoundPool.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `src/engine/DecodedSoundPool.h`:

```cpp
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace HDAW {

// Immutable decoded PCM. Built on the message thread; the audio thread only
// reads it. Held by shared_ptr so the refcount IS the consumer count.
struct DecodedSound
{
    std::unique_ptr<float[]> data[2] = { nullptr, nullptr }; // [0]=L, [1]=R (null when mono)
    int numChannels = 0;
    int64_t length = 0;          // per-channel sample count
    double sampleRate = 44100.0;

    // Message thread. Decodes `path` via `fm` into a fresh DecodedSound.
    // Returns nullptr when the file cannot be read (same contract as
    // AudioFormatManager::createReaderFor returning null).
    static std::shared_ptr<const DecodedSound> decode(juce::AudioFormatManager& fm,
                                                      const juce::String& path)
    {
        auto sound = std::make_shared<DecodedSound>();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(path)));
        if (r == nullptr)
            return nullptr;
        sound->numChannels = juce::jmin(static_cast<int>(r->numChannels), 2);
        sound->length = r->lengthInSamples;
        sound->sampleRate = r->sampleRate;
        if (sound->numChannels <= 0 || sound->length <= 0)
            return nullptr;
        const int total = static_cast<int>(sound->length);
        sound->data[0] = std::make_unique<float[]>(static_cast<size_t>(total));
        if (sound->numChannels > 1)
            sound->data[1] = std::make_unique<float[]>(static_cast<size_t>(total));
        float* const ptrs[2] = { sound->data[0].get(),
                                 sound->numChannels > 1 ? sound->data[1].get() : nullptr };
        r->read(ptrs, sound->numChannels, 0, total);
        return sound;
    }
};

// Message-thread-only decode cache. Keyed by full path; decodes once per file
// and hands out shared_ptr<const DecodedSound>. Entries are strong cache refs;
// pruneUnreferenced() drops any entry whose use_count()==1 (no consumer holds
// it). The audio thread never touches this class — consumers copy the pointer
// into their own member at prepare time, mirroring the SamplerSound pattern.
class DecodedSoundPool
{
public:
    explicit DecodedSoundPool(juce::AudioFormatManager& fm) : formatManager(fm) {}

    // Message thread. Returns the shared decode for `path`, decoding on first
    // request. Never decodes a file already in the cache (decode-count is
    // monotonic per genuinely-missed file). Returns nullptr on unreadable file.
    std::shared_ptr<const DecodedSound> acquire(const juce::String& path)
    {
        const std::string key = juce::File(path).getFullPathName().toStdString();
        auto it = cache.find(key);
        if (it != cache.end())
        {
            auto sound = it->second.lock();
            if (sound)
                return sound;
            cache.erase(it); // expired — re-decode below
        }
        auto sound = DecodedSound::decode(formatManager, path);
        if (sound)
        {
            cache[key] = sound; // strong ref: entry survives consumer swaps
            decodeCount++;
        }
        return sound;
    }

    // Message thread. Drops cache entries no consumer references
    // (use_count()==1 means only the pool holds it). Called by tests and
    // opportunistically by acquire when the cache grows.
    void pruneUnreferenced()
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (it->second.use_count() <= 1)
                it = cache.erase(it);
            else
                ++it;
        }
    }

    // Test hooks.
    int getDecodeCount() const { return decodeCount; }
    int getEntryCount() const { return static_cast<int>(cache.size()); }

private:
    juce::AudioFormatManager& formatManager;
    std::unordered_map<std::string, std::shared_ptr<const DecodedSound>> cache;
    int decodeCount = 0;
};

} // namespace HDAW
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.*
```

Expected: PASS (6/6). Note: `RefcountDropsAndEntryEvictsWhenUnreferenced` passes because the cache holds the ONLY strong ref once the test's local `shared_ptr` goes out of scope; `pruneUnreferenced` sees `use_count()==1`.

- [ ] **Step 5: Commit**

```bash
git add src/engine/DecodedSoundPool.h tests/unit/engine/audio_pool_dedup_test.cpp tests/CMakeLists.txt
git commit -m "feat(engine): DecodedSoundPool — shared decode cache keyed by path"
```

---

## Task 2: Wire `ClipSourceProcessor` to the pool (borrow, no copy)

**Files:**
- Modify: `src/engine/ClipSourceProcessor.h`
- Test: `tests/unit/engine/audio_pool_dedup_test.cpp`

- [ ] **Step 1: Write the failing test (processor-level borrow)**

Append to `tests/unit/engine/audio_pool_dedup_test.cpp`:

```cpp
#include "engine/ClipSourceProcessor.h"
#include "engine/TransportManager.h"

TEST(AudioPoolDedup, TwoClipProcessorsShareOneDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);
    HDAW::TransportManager tm;

    auto file = writeSineWav("proc_share", 44100);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm, &pool);
    HDAW::ClipSourceProcessor b(tm, fm, &pool);
    a.setSourceFile(path);
    b.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    b.prepareToPlay(44100.0, 512);

    EXPECT_EQ(pool.getDecodeCount(), 1);    // one decode, two consumers
    EXPECT_EQ(pool.getEntryCount(), 1);

    // Both processors read the SAME pooled buffer (pointer identity).
    EXPECT_EQ(a.getPreloadedDataForTest(0), b.getPreloadedDataForTest(0));
    file.deleteFile();
}

TEST(AudioPoolDedup, ProcessorWithoutPoolFallsBackToDirectDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::TransportManager tm;

    auto file = writeSineWav("proc_fallback", 44100);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm); // no pool
    a.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    EXPECT_NE(a.getPreloadedDataForTest(0), nullptr);
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.TwoClipProcessorsShareOneDecode
```

Expected: FAIL to compile — `getPreloadedDataForTest` doesn't exist and the ctor takes only 2 args.

- [ ] **Step 3: Implement the pool path in `ClipSourceProcessor.h`**

3a. Add the include and the ctor parameter. In `src/engine/ClipSourceProcessor.h`, after the `#include "StreamingClipSource.h"` line (line 9), add:

```cpp
#include "DecodedSoundPool.h"
```

Change the constructor (line 18-23):

```cpp
    ClipSourceProcessor(HDAW::TransportManager& tm, juce::AudioFormatManager& fm,
                        HDAW::DecodedSoundPool* pool = nullptr)
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          transportManager(tm), formatManager(fm), decodedPool(pool)
    {
    }
```

3b. Replace the members. Find the member block (lines 646-648):

```cpp
    juce::HeapBlock<float> preloadedData[2];
    int preloadedChannels = 0;
    int64_t preloadedLength = 0;
```

Replace with:

```cpp
    std::shared_ptr<const HDAW::DecodedSound> decoded_;
    int preloadedChannels = 0;
    int64_t preloadedLength = 0;
```

3c. Add the pool member next to `formatManager` (line 634):

```cpp
    HDAW::TransportManager& transportManager;
    juce::AudioFormatManager& formatManager;
    HDAW::DecodedSoundPool* decodedPool = nullptr;
```

3d. Rewrite `preloadWholeFile()` (lines 76-93) to route through the pool and keep the missing-file DIAG log:

```cpp
    // Message-thread preload of the whole source file. Borrows the shared
    // decode from the pool when one is set (zero copy); otherwise decodes
    // directly. The pooled buffer stays alive via decoded_'s shared_ptr even
    // across routing rebuilds (Gate 1/10: reacquired, not re-decoded).
    void preloadWholeFile()
    {
        decoded_.reset();
        preloadedChannels = 0;
        preloadedLength = 0;

        auto acquire = [this]() -> std::shared_ptr<const HDAW::DecodedSound>
        {
            if (decodedPool != nullptr)
                return decodedPool->acquire(sourceFile);
            return HDAW::DecodedSound::decode(formatManager, sourceFile);
        };

        decoded_ = acquire();
        if (decoded_ != nullptr)
        {
            preloadedChannels = decoded_->numChannels;
            preloadedLength = decoded_->length;
        }
        else
        {
            HDAW_LOG("DIAG", "ClipSourceProc preload FAIL file=" + sourceFile.toStdString());
        }
    }
```

3e. In `prepareToPlay`, replace the inline legacy decode (lines 222-248, the `if (sourceFile.isNotEmpty())` block) with a call to `preloadWholeFile()`:

```cpp
        if (sourceFile.isNotEmpty())
        {
            preloadWholeFile();
        }
```

This removes the duplicate decode logic — `preloadWholeFile` now handles the missing-file DIAG log.

3f. In `releaseResources()` (lines 258-270), free the pooled reference:

```cpp
    void releaseResources() override
    {
        streamer.stopPlayback();
        decoded_.reset();
        preloadedChannels = 0;
        preloadedLength = 0;
        stretchedData[0].free();
        stretchedData[1].free();
        stretchedChannels = 0;
        stretchedLength = 0;
        activeBuffer.store(0, std::memory_order_release);
    }
```

3g. In `prepareToPlay`, replace the buffer-free preamble (lines 188-191):

```cpp
        decoded_.reset();
        preloadedChannels = 0;
        preloadedLength = 0;
```

3h. In `processBlock`, point the read pointers at the pooled data. Replace lines 346-350:

```cpp
            preloadedPtrs[0] = decoded_ ? decoded_->data[0].get() : nullptr;
            preloadedPtrs[1] = (decoded_ && decoded_->numChannels > 1)
                                   ? decoded_->data[1].get()
                                   : preloadedPtrs[0];
            srcChannels = preloadedChannels;
            srcLength = preloadedLength;
            useFloatBuffer = true;
```

(The mono case aliases channel 1 to channel 0 — `srcCh` mapping in the read loop already clamps to `srcChannels - 1`, so channel 1 is never dereferenced for mono files.)

3i. In `switchToSourceFile` (lines 34-38), free the pooled reference instead of the HeapBlocks:

```cpp
        decoded_.reset();
        preloadedChannels = 0;
        preloadedLength = 0;
        streamer.stopPlayback();
```

3j. Add the test hook (public, near `getSourceFile()`, line 107):

```cpp
    // Test hook: the L buffer of the active preload (null when not preloaded).
    const float* getPreloadedDataForTest(int ch) const
    {
        if (decoded_ == nullptr || ch < 0 || ch > 1)
            return nullptr;
        if (decoded_->numChannels > 1)
            return decoded_->data[ch].get();
        return decoded_->data[0].get();
    }
```

3k. `switchToSourceFile` also calls `preloadWholeFile()` (line 47) and the legacy `preloadWholeFile()` + `activeBuffer.store(0)` (line 66-67) — both unchanged; the rewritten `preloadWholeFile` already handles the pooled path. Do NOT touch the streaming branch (`streamer.prepare`, lines 53-65) — the long-file path keeps its per-clip reader.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.*
```

Expected: PASS. Then run the existing clip suites to confirm no regression:

```bash
build/Debug/hdaw_tests.exe --gtest_filter=ClipSourceProcessor.*:StretchTest.*:ClipStreamingE2E.*:StreamingClipSource.*
```

Expected: PASS — playback is bit-identical (the pooled float decode is byte-for-byte the same data the HeapBlock path produced; only the storage owner changed).

- [ ] **Step 5: Commit**

```bash
git add src/engine/ClipSourceProcessor.h tests/unit/engine/audio_pool_dedup_test.cpp
git commit -m "feat(engine): ClipSourceProcessor borrows shared decode from DecodedSoundPool"
```

---

## Task 3: Wire the sampler path (clip + sampler share one decode)

**Files:**
- Modify: `src/engine/TrackFXSlot.h`
- Modify: `src/engine/Track.h`
- Modify: `src/engine/Track.cpp`
- Test: `tests/unit/engine/audio_pool_dedup_test.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/engine/audio_pool_dedup_test.cpp`:

```cpp
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "model/ProjectModel.h"

// Two clips + one sampler slot all referencing the same file → exactly one
// decode, and a routing rebuild reacquires the pool entry (no re-decode).
TEST(AudioPoolDedup, ClipAndSamplerShareOneDecodeAcrossRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine_share", 44100);
    const juce::String path = file.getFullPathName();

    // Two audio clips on tracks 0 and 2 (track 1 is the MIDI "Synth" track).
    engine.addAudioClip(0, 0.0, 1.0, path.toStdString(), "clipA");
    engine.addAudioClip(2, 0.0, 1.0, path.toStdString(), "clipB");

    // Sampler slot on track 0 with the same file.
    engine.addFxSlot(0, "sampler", 0, "");
    engine.setSamplerSample(0, /*slotIndex*/ 0, path.toStdString(), 60);

    auto& pool = engine.getProjectPool().getDecodedSoundPool();
    EXPECT_EQ(pool.getDecodeCount(), 1);    // one decode, three consumers
    EXPECT_EQ(pool.getEntryCount(), 1);

    // Rebuild the routing graph — the new processors must reacquire the
    // pool entry, not re-decode (Gate 1/10).
    engine.getMainProcessor()->rebuildRoutingGraph();
    EXPECT_EQ(pool.getDecodeCount(), 1);
    EXPECT_EQ(pool.getEntryCount(), 1);

    // And the sampler slot still has its sound on the LIVE processor.
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* slot = track->getFXChain()[0].get();
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->getSamplerSoundForTest() == nullptr);

    file.deleteFile();
}

TEST(AudioPoolDedup, SamplerWithoutPoolStillDecodesLocally)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("sampler_local", 44100);
    juce::ValueTree slotTree(IDs::FX_SLOT);
    slotTree.setProperty(IDs::fxType, "sampler", nullptr);
    slotTree.setProperty(juce::Identifier("sampleFile"), file.getFullPathName(), nullptr);
    slotTree.setProperty(juce::Identifier("rootNote"), 60, nullptr);

    // No pool passed → existing local-decode fallback must still work.
    HDAW::TrackFXSlot slot("sampler");
    slot.loadSamplerState(slotTree); // no format manager, no pool
    EXPECT_FALSE(slot.getSamplerSoundForTest() == nullptr);
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.ClipAndSamplerShareOneDecodeAcrossRebuild
```

Expected: FAIL — `getProjectPool().getDecodedSoundPool()` doesn't exist yet; `getSamplerSoundForTest` doesn't exist.

**Verified command signatures (AudioEngineCommands.h:50,151,164):**
- `int addAudioClip(int trackIndex, double start, double duration, const std::string& sourceFile, const std::string& name)`
- `void addFxSlot(int trackIndex, const std::string& type, int position, const std::string& pluginId)`
- `void setSamplerSample(int trackIndex, int slotIndex, const std::string& filePath, int rootNote = 60)`
- `engine.getProjectPool()` exists (AudioEngine.h:39)

- [ ] **Step 3: Implement the sampler pooled path**

3a. In `src/engine/TrackFXSlot.h`, add the include (top of file):

```cpp
#include "DecodedSoundPool.h"
```

3b. Change `loadSamplerState` (line 626):

```cpp
    void loadSamplerState (const juce::ValueTree& slotTree,
                           juce::AudioFormatManager* formatManager = nullptr,
                           HDAW::DecodedSoundPool* decodedPool = nullptr)
```

3c. In the body, replace the decode section (lines 643-673). Current code decodes directly into a `SamplerSound::Builder`. New version: when `decodedPool` is present, acquire the shared decode and copy it into the builder; otherwise keep the existing direct decode:

```cpp
        juce::String sampleFile = slotTree.getProperty ("sampleFile", "").toString();
        if (sampleFile.isEmpty())
            return;

        std::shared_ptr<const HDAW::DecodedSound> decoded;
        if (decodedPool != nullptr)
            decoded = decodedPool->acquire (sampleFile);

        SamplerSound::Builder builder;
        builder.sampleStart = static_cast<double> (slotTree.getProperty ("sampleStart", 0.0));
        builder.sampleEnd   = static_cast<double> (slotTree.getProperty ("sampleEnd", 1.0));
        builder.loopStart   = static_cast<double> (slotTree.getProperty ("loopStart", 0.0));
        builder.loopEnd     = static_cast<double> (slotTree.getProperty ("loopEnd", 1.0));
        builder.loopEnabled = static_cast<bool> (slotTree.getProperty ("loopEnabled", false));
        builder.rootNote = static_cast<int> (slotTree.getProperty ("rootNote", 60));

        if (decoded != nullptr)
        {
            // Pooled path: one decode shared with clips. Copy into the
            // immutable SamplerSound (the pool still owns the canonical data;
            // decode-count stays 1 across rebuilds).
            builder.numChannels = decoded->numChannels;
            builder.length = decoded->length;
            builder.nativeSampleRate = decoded->sampleRate;
            for (int ch = 0; ch < builder.numChannels; ++ch)
            {
                builder.data[ch] = std::make_unique<float[]> (static_cast<size_t> (builder.length));
                std::memcpy (builder.data[ch].get(), decoded->data[ch].get(),
                             static_cast<size_t> (builder.length) * sizeof (float));
            }
        }
        else
        {
            // Fallback: existing direct decode (no pool / local format manager).
            juce::AudioFormatManager localFmt;
            if (! formatManager)
            {
                localFmt.registerBasicFormats();
                formatManager = &localFmt;
            }
            auto* reader = formatManager->createReaderFor (juce::File (sampleFile));
            if (! reader)
                return;
            const int64_t totalSamples = reader->lengthInSamples;
            const int numChannels = static_cast<int> (reader->numChannels);
            builder.numChannels = std::min (numChannels, 2);
            builder.length = totalSamples;
            builder.nativeSampleRate = reader->sampleRate;
            for (int ch = 0; ch < builder.numChannels; ++ch)
                builder.data[ch] = std::make_unique<float[]> (static_cast<size_t> (totalSamples));
            juce::AudioBuffer<float> readBuf (builder.numChannels, static_cast<int> (totalSamples));
            reader->read (&readBuf, 0, static_cast<int> (totalSamples), 0, true, true);
            for (int ch = 0; ch < builder.numChannels; ++ch)
                std::memcpy (builder.data[ch].get(), readBuf.getReadPointer (ch),
                             static_cast<size_t> (totalSamples) * sizeof (float));
            delete reader;
        }

        auto sound = builder.build();
```

The rest of `loadSamplerState` (mode parsing, `sampler->setSound(sound)`, etc., lines 673-700+) is unchanged.

3d. Add the test hook to `TrackFXSlot` (public section, near `getInternalParamValues`, line 598):

```cpp
    std::shared_ptr<const HDAW::SamplerSound> getSamplerSoundForTest() const
    {
        if (sampler == nullptr)
            return nullptr;
        return sampler->getSoundForTest();
    }
```

3e. In `src/engine/SamplerEngine.h`, add `getSoundForTest()` (message-thread read of the current sound; the member holding the live sound is `activeSound_`, SamplerEngine.h:85):

```cpp
    std::shared_ptr<const HDAW::SamplerSound> getSoundForTest() const { return activeSound_; }
```

3f. In `src/engine/Track.h`, add the pool plumbing next to `setPluginManager` (line 70):

```cpp
    void setDecodedSoundPool(HDAW::DecodedSoundPool* p) { decodedPool = p; }
```

Add the member next to `pluginManager` (line 118):

```cpp
    HDAW::DecodedSoundPool* decodedPool = nullptr;
```

Forward-declare at the top of `Track.h` (before `namespace HDAW`):

```cpp
namespace HDAW {
class DecodedSoundPool;
```

3g. In `src/engine/Track.cpp`, the sampler branch of `rebuildFXChain` (line 226) passes the pool:

```cpp
                slot->loadSamplerState (slotTree, nullptr, decodedPool);
```

- [ ] **Step 4: Run the tests**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.*
```

Expected: PASS. Then the existing sampler suites (no regression — the no-pool fallback is unchanged):

```bash
build/Debug/hdaw_tests.exe --gtest_filter=Sampler*.*
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/engine/TrackFXSlot.h src/engine/Track.h src/engine/Track.cpp src/engine/SamplerEngine.h tests/unit/engine/audio_pool_dedup_test.cpp
git commit -m "feat(engine): sampler loads via DecodedSoundPool; clips+sampler share one decode"
```

---

## Task 4: Wire the pool through ProjectPool → MainAudioProcessor → RoutingManager

**Files:**
- Modify: `src/engine/ProjectPool.h`
- Modify: `src/engine/AudioEngine.cpp`
- Modify: `src/engine/MainAudioProcessor.h`
- Modify: `src/engine/MainAudioProcessor.cpp`
- Modify: `src/engine/RoutingManager.h`
- Modify: `src/engine/RoutingManager.cpp`
- Test: `tests/unit/engine/audio_pool_dedup_test.cpp`

- [ ] **Step 1: Write the failing test (engine-level rebuild gate)**

Append to `tests/unit/engine/audio_pool_dedup_test.cpp`:

```cpp
TEST(AudioPoolDedup, EngineWiresPoolAndRebuildReacquiresWithoutRedecode)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine_wire", 44100);
    const juce::String path = file.getFullPathName();
    engine.addAudioClip(0, 0.0, 1.0, path.toStdString(), "clipA");

    auto& pool = engine.getProjectPool().getDecodedSoundPool();
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Second clip, same file → still one decode.
    engine.addAudioClip(2, 0.0, 1.0, path.toStdString(), "clipB");
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Rebuild the graph twice — decode count must NOT grow (Gate 1/10).
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.getMainProcessor()->rebuildRoutingGraph();
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Live processors actually hold the shared buffer (not just the model).
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->getAudioClipSources().size(), 2u);
    for (const auto& [key, clip] : rm->getAudioClipSources())
    {
        ASSERT_NE(clip, nullptr);
        EXPECT_NE(clip->getPreloadedDataForTest(0), nullptr);
    }

    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.EngineWiresPoolAndRebuildReacquiresWithoutRedecode
```

Expected: FAIL — `engine.getProjectPool().getDecodedSoundPool()` doesn't exist.

- [ ] **Step 3: Implement the wiring**

3a. In `src/engine/ProjectPool.h`, include the pool and own it:

```cpp
#include "DecodedSoundPool.h"
```

In the class, after the thumbnail cache member (line 39):

```cpp
private:
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    HDAW::DecodedSoundPool decodedPool{ formatManager };
```

Public getter (after `getThumbnailCache`, line 23):

```cpp
    HDAW::DecodedSoundPool& getDecodedSoundPool() { return decodedPool; }
```

3b. In `src/engine/AudioEngine.cpp`, `initialize()` — after `mainProcessor->setFormatManager(...)` (line 55):

```cpp
    mainProcessor->setDecodedSoundPool(&projectPool.getDecodedSoundPool());
```

3c. In `src/engine/MainAudioProcessor.h`, add the setter and member:

```cpp
    void setDecodedSoundPool(HDAW::DecodedSoundPool* pool) { decodedSoundPool = pool; }
```

```cpp
    HDAW::DecodedSoundPool* decodedSoundPool = nullptr;
```

Forward-declare at the top (before the class):

```cpp
namespace HDAW {
class DecodedSoundPool;
}
```

3d. In `src/engine/MainAudioProcessor.cpp`, pass the pool at BOTH RoutingManager constructions. Line 65-66:

```cpp
    routingManager = std::make_unique<HDAW::RoutingManager>(
        graph, *projectModel, *formatManager, *transportManager, pluginManager, stretchCache,
        decodedSoundPool);
```

Line 502:

```cpp
        auto fresh = std::make_unique<HDAW::RoutingManager>(
            graph, *projectModel, *formatManager, *transportManager, pluginManager, stretchCache,
            decodedSoundPool);
```

3e. In `src/engine/RoutingManager.h`, extend the ctor declaration (line 21-24):

```cpp
    RoutingManager(juce::AudioProcessorGraph& graph, ProjectModel& model,
                   juce::AudioFormatManager& fm, HDAW::TransportManager& tm,
                   HDAW::PluginManager* pm = nullptr,
                   StretchCache* stretchCache = nullptr,
                   HDAW::DecodedSoundPool* decodedPool = nullptr);
```

Add the member (after `stretchCache`, line 87):

```cpp
    HDAW::DecodedSoundPool* decodedPool = nullptr;
```

Forward-declare at the top of `RoutingManager.h` (before `namespace HDAW`):

```cpp
namespace HDAW {
class DecodedSoundPool;
}
```

3f. In `src/engine/RoutingManager.cpp`:
- Constructor init list (line 10-13): add `decodedPool(pool)` to the initializer list.
- `buildTrackProcessor` (line 110-134): after `newTrack->setPluginManager(pluginManager);` add:

```cpp
    newTrack->setDecodedSoundPool(decodedPool);
```

- `rebuildClipsForTrack` (line 507): pass the pool:

```cpp
            auto clipProc = std::make_unique<ClipSourceProcessor>(transportManager, formatManager, decodedPool);
```

3g. **Export path — do NOT change `ExportManager.cpp`.** It constructs `RoutingManager` with 6 args (line 194); the new 7th parameter defaults to `nullptr`, so export keeps the direct-decode fallback (its render thread must not touch the live message-thread pool). Add a comment there:

```cpp
            // Note: no DecodedSoundPool here — the export render thread must
            // not touch the live (message-thread) pool; clips decode directly.
            RoutingManager routingManager(renderGraph, localModel, *formatManager,
                                          transportManager, pluginManager, stretchCache);
```

- [ ] **Step 4: Run the tests**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe --gtest_filter=AudioPoolDedup.*
```

Expected: PASS (9 tests). Then the full suite:

```bash
build/Debug/hdaw_tests.exe
```

Expected: 791+ PASS, exactly the 5 known pre-existing CrashRecovery/PluginIsolation failures — **no new failures**. Pay special attention to the sampler suites (`Sampler*.*`), clip suites (`ClipSourceProcessor.*`, `ClipStreamingE2E.*`, `StretchTest.*`), and export suites (`Export*.*`).

- [ ] **Step 5: Commit**

```bash
git add src/engine/ProjectPool.h src/engine/AudioEngine.cpp src/engine/MainAudioProcessor.h src/engine/MainAudioProcessor.cpp src/engine/RoutingManager.h src/engine/RoutingManager.cpp src/engine/ExportManager.cpp tests/unit/engine/audio_pool_dedup_test.cpp
git commit -m "feat(engine): wire DecodedSoundPool through ProjectPool/RoutingManager"
```

---

## Task 5: Success-gate verification (completion contract)

**Files:** (none — verification only)

- [ ] **Step 1: Confirm no audio-thread calls into the pool (Gate 3)**

Grep the audio-thread entry points for pool/acquire usage:

```bash
rg -n "decodedPool|DecodedSoundPool|\.acquire\(" src/engine/ClipSourceProcessor.h src/engine/Track.cpp src/engine/TrackFXSlot.h
```

Expected: `DecodedSoundPool::acquire` appears ONLY in message-thread contexts (`preloadWholeFile`, `prepareToPlay`, `loadSamplerState`, `buildTrackProcessor`, `rebuildClipsForTrack`, `rebuildFXChain`). The audio-thread `processBlock` reads only `decoded_->data[ch]` raw pointers — no pool call, no allocation, no lock (Gate 3).

- [ ] **Step 2: Run the full engine suite (Debug) as the completion contract**

```bash
cmake --build build --config Debug --target hdaw_tests
build/Debug/hdaw_tests.exe
```

Expected: PASS — 796 tests, 791 PASS, exactly the 5 known pre-existing proxy-spawn failures; **no new failures, no false positives**.

- [ ] **Step 3: Verify the streaming path is untouched (long-file behavior unchanged)**

```bash
build/Debug/hdaw_tests.exe --gtest_filter=StreamingClipSource.*:ClipStreamingE2E.*
```

Expected: PASS — long files (> 8 s) still stream via per-clip readers; the pool only serves the whole-file preload path. (Shared streaming handles are a documented follow-up, not part of this subsystem.)

- [ ] **Step 4: Verify no anti-patterns in the diff**

- No N-call RPC loops (no new RPC — MCP parity N/A; the pool is an internal engine improvement, no user-facing command).
- No full-tree walk (the pool is a per-file map lookup).
- No `DBG` macro. No audio-thread allocation (processBlock reads pre-existing pooled pointers).
- No `rebuildRoutingGraph()` per-clip (the pool adds zero rebuilds).
- No new `.cpp` needing CMake registration except the test (Task 1 Step 2).

- [ ] **Step 5: Version bump + graph refresh**

- Bump patch in `CMakeLists.txt` (`project(HDAW VERSION 0.22.1 ...)`) and `frontend/package.json` (`"version": "0.22.1"` — kept in sync, AGENTS.md).
- Refresh the knowledge graph: `codebase-memory` `index_repository` (project `D-pdf-roo-projects-hdaw3`, mode `fast`) so `DecodedSoundPool`/`DecodedSound` nodes are known.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt frontend/package.json
git commit -m "chore: bump to 0.22.1 (audio pool dedup)"
```

---

## Success Gates (completion contract — evidence required)

- [ ] G1: `AudioPoolDedup.*` suite passes — shared_ptr equality + decode-count == 1 for two consumers; refcount drop evicts unreferenced entries (prune); referenced entries survive; different files decode separately; missing file returns null.
- [ ] G2: clip + sampler both load via the pool; `rebuildRoutingGraph()` reacquires without re-decoding (decode-count stays 1) — asserted on the LIVE processors (`getAudioClipSources()`) and the live sampler slot (`getSamplerSoundForTest`).
- [ ] G3: no audio-thread calls into the pool — grep confirms `acquire` only in message-thread paths; audio thread reads raw pooled pointers (Gate 3).
- [ ] G4: full `build/Debug/hdaw_tests.exe` passes — no new failures beyond the 5 known pre-existing CrashRecovery/PluginIsolation cases; sampler/clip/streaming/export suites green.
- [ ] G5: version bumped to 0.22.1 in both `CMakeLists.txt` and `frontend/package.json`; knowledge graph refreshed (`index_repository`).

## Dependency Map

- **Blast radius:** `ClipSourceProcessor` (preload path — the pooled consumer), `TrackFXSlot`/`SamplerEngine` (sampler decode), `Track` (pool plumbing), `RoutingManager` (ctor + clip/track build), `MainAudioProcessor` (2 RoutingManager sites), `ProjectPool` (owner), `AudioEngine::initialize`, `ExportManager` (6-arg ctor → default nullptr, unchanged behavior).
- **Upstream:** `AudioEngine` owns `ProjectPool` (format manager + new pool); `MainAudioProcessor::prepareToPlay`/`rebuildRoutingGraph` construct `RoutingManager`; `RoutingManager::rebuildClipsForTrack` builds `ClipSourceProcessor` (RoutingManager.cpp:507); `Track::rebuildFXChain` → `TrackFXSlot::loadSamplerState` (Track.cpp:226).
- **Downstream:** clip audio path (`processBlock` reads `decoded_->data`), sampler render (pooled copy into `SamplerSound`), export (unchanged direct decode).
- **Projections affected:** none — no ValueTree/ReadModel/frontend change (delta/fullSync behavior unchanged).
- **SPSC paths touched:** none new — the pooled pointer is adopted at prepare time on the message thread, exactly like the HeapBlock path it replaces.
- **God nodes in scope:** `RoutingManager`, `ClipSourceProcessor` (both high-degree) — change is 1 ctor arg + 1 member each at well-understood seams.
- **Path integrity:** decode path: `ProjectPool::getDecodedSoundPool` → `MainAudioProcessor::decodedSoundPool` → `RoutingManager::decodedPool` → `ClipSourceProcessor::decodedPool` / `Track::decodedPool` → verified by the engine-level tests in Task 4.

## Pitfall Gates Triggered

- **Gate 1/10 (rebuild state restore):** the pool entry is reacquired on `rebuildRoutingGraph`, not re-decoded — Task 4's test asserts decode-count stays 1 after two rebuilds AND asserts the live processors hold the buffer (`getAudioClipSources()` + `getPreloadedDataForTest`).
- **Gate 3 (audio-thread safety):** `acquire`/`pruneUnreferenced` are message-thread-only; `processBlock` reads raw pointers from the already-swapped `decoded_` member — no allocation/lock/pool call on the audio thread.
- **Gate 4/15 (stale binaries):** new test `.cpp` registered in `tests/CMakeLists.txt` (Task 1 Step 2) or MSBuild skips recompile; verify the binary runs the new suite after build.
- **Lesson 8 (quality):** the pooled decode is identical float data to the path it replaces — bit-identical playback (verified by the existing clip suites); sampler pooled path copies float→float with no conversion.
- **Lesson 17 (visible logs):** the missing-file DIAG log is preserved in `preloadWholeFile` (now the single decode entry point).
- **Gate 14/16:** N/A (no cross-process, no plugin lifecycle).

## Anti-Pattern Scan

- No per-block allocation/logging/locking (pool acquire is message-thread, cached). No `DBG`. No new RPC (MCP parity N/A — internal engine improvement). No full-tree walks (per-file map). No `rebuildRoutingGraph()` per-clip.
