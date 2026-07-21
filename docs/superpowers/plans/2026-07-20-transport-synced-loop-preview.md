# Transport-Synced Loop Preview — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the audio preview player follow the project transport and loop region via a custom `SyncableAudioSource` that reads position from `TransportManager` when the transport is playing.

**Architecture:** Create a new `SyncableAudioSource` (preloads file into memory, reads position from TransportManager in sync mode or internal counter in free mode). Rewrite `AudioPreviewPlayer` to use it instead of `AudioTransportSource` + `ResamplingAudioSource`. Wire `TransportManager*` in `AudioEngine::initialize()`.

**Tech Stack:** JUCE AudioSource, HeapBlock, std::atomic, TransportManager

---

## File Structure

| File | Change |
|------|--------|
| `src/engine/SyncableAudioSource.h` | **Create** — custom AudioSource with sync/free modes |
| `src/engine/AudioPreviewPlayer.h` | Rewrite to use SyncableAudioSource |
| `src/engine/AudioPreviewPlayer.cpp` | Rewrite implementation |
| `src/engine/AudioEngine.cpp` | Wire TransportManager to preview player |
| `CMakeLists.txt` | No change needed (header-only SyncableAudioSource) |

---

## Task 1: Create SyncableAudioSource

**Files:**
- Create: `src/engine/SyncableAudioSource.h`

- [ ] **Step 1: Write the SyncableAudioSource header**

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

class TransportManager;

namespace HDAW {

class SyncableAudioSource : public juce::AudioSource
{
public:
    SyncableAudioSource() = default;
    ~SyncableAudioSource() override = default;

    void prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate) override
    {
        deviceSampleRate = sampleRate;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        if (!playing.load(std::memory_order_relaxed) || lengthInSamples == 0)
        {
            info.clearActiveBufferRegion();
            return;
        }

        double filePos;
        if (transport != nullptr && transport->isPlayingNow())
        {
            int64_t transportSample = transport->getCurrentSample();
            double transportSr = transport->getSampleRate();
            if (transportSr <= 0.0) transportSr = deviceSampleRate;
            filePos = static_cast<double>(transportSample)
                      * (fileSampleRate / transportSr) * resamplingRatio;
        }
        else
        {
            int64_t pos = internalPos.load(std::memory_order_relaxed);
            filePos = static_cast<double>(pos)
                      * (fileSampleRate / deviceSampleRate) * resamplingRatio;
            internalPos.fetch_add(info.numSamples, std::memory_order_relaxed);

            double maxPos = static_cast<double>(lengthInSamples) / resamplingRatio
                            * (deviceSampleRate / fileSampleRate);
            if (static_cast<double>(internalPos.load(std::memory_order_relaxed)) >= maxPos)
                playing.store(false, std::memory_order_relaxed);
        }

        readBlock(filePos, info);
    }

    bool loadFile(const juce::File& file, juce::AudioFormatManager& fm)
    {
        playing.store(false, std::memory_order_relaxed);
        internalPos.store(0, std::memory_order_relaxed);
        lengthInSamples = 0;

        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr) return false;

        fileSampleRate = reader->sampleRate;
        lengthInSamples = static_cast<int64_t>(reader->lengthInSamples);
        if (lengthInSamples <= 0) return false;

        int numChannels = static_cast<int>(reader->numChannels);
        for (int ch = 0; ch < 2; ++ch)
            buffer[ch].allocate(static_cast<size_t>(lengthInSamples), true);

        if (numChannels == 1)
        {
            reader->read(buffer[0].getData(), 0, lengthInSamples, 0, true, false);
            std::memcpy(buffer[1].getData(), buffer[0].getData(),
                        static_cast<size_t>(lengthInSamples) * sizeof(float));
        }
        else
        {
            float* dests[2] = { buffer[0].getData(), buffer[1].getData() };
            reader->read(dests, 2, 0, lengthInSamples, 0, true, true);
        }
        return true;
    }

    void play()  { internalPos.store(0, std::memory_order_relaxed); playing.store(true, std::memory_order_relaxed); }
    void stop()  { playing.store(false, std::memory_order_relaxed); }
    bool isPlaying() const { return playing.load(std::memory_order_relaxed); }

    void setTransportManager(TransportManager* tm) { transport = tm; }
    void setResamplingRatio(double ratio) { resamplingRatio = juce::jlimit(0.25, 4.0, ratio); }
    void setGain(float g) { gain.store(g, std::memory_order_relaxed); }

    double getLengthInSeconds() const
    {
        if (fileSampleRate <= 0.0 || lengthInSamples == 0) return 0.0;
        return static_cast<double>(lengthInSamples) / fileSampleRate;
    }

private:
    void readBlock(double filePos, const juce::AudioSourceChannelInfo& info)
    {
        auto& outBuffer = *info.buffer;
        float g = gain.load(std::memory_order_relaxed);

        for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
        {
            float* dest = outBuffer.getWritePointer(ch, info.startSample);
            const float* src = buffer[ch < 2 ? ch : 0].getData();

            for (int i = 0; i < info.numSamples; ++i)
            {
                double pos = filePos + static_cast<double>(i) * resamplingRatio;
                int64_t idx = static_cast<int64_t>(pos);

                if (idx < 0 || idx >= lengthInSamples)
                {
                    dest[i] = 0.0f;
                }
                else if (idx + 1 < lengthInSamples)
                {
                    double frac = pos - static_cast<double>(idx);
                    dest[i] = static_cast<float>(
                        (src[idx] * (1.0 - frac) + src[idx + 1] * frac) * g);
                }
                else
                {
                    dest[i] = src[idx] * g;
                }
            }
        }
    }

    juce::HeapBlock<float> buffer[2];
    int64_t lengthInSamples = 0;
    double fileSampleRate = 44100.0;
    double deviceSampleRate = 44100.0;
    double resamplingRatio = 1.0;
    std::atomic<int64_t> internalPos{0};
    std::atomic<bool> playing{false};
    std::atomic<float> gain{0.8f};
    TransportManager* transport = nullptr;
};

} // namespace HDAW
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile (header-only, not yet used)

- [ ] **Step 3: Commit**

```bash
git add src/engine/SyncableAudioSource.h
git commit -m "SyncableAudioSource: add transport-synced audio source with preloaded buffer"
```

---

## Task 2: Rewrite AudioPreviewPlayer to use SyncableAudioSource

**Files:**
- Modify: `src/engine/AudioPreviewPlayer.h`
- Modify: `src/engine/AudioPreviewPlayer.cpp`

- [ ] **Step 1: Rewrite AudioPreviewPlayer.h**

Replace the entire file with:

```cpp
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SyncableAudioSource.h"

class TransportManager;

namespace HDAW {

class AudioPreviewPlayer
{
public:
    AudioPreviewPlayer(juce::AudioDeviceManager& dm, juce::AudioFormatManager& fm);
    ~AudioPreviewPlayer();

    void loadFile(const juce::File& file);
    void play();
    void stop();
    bool isPlaying() const { return syncSource.isPlaying(); }

    void setVolume(float vol);
    float getVolume() const { return volume; }

    void setTempoMatch(bool enabled, double fileBpm = 0.0);
    bool isTempoMatched() const { return tempoMatch; }

    void setProjectBpm(double bpm);
    double getPlaybackLength() const { return syncSource.getLengthInSeconds(); }

    void setTransportManager(TransportManager* tm) { syncSource.setTransportManager(tm); }

    juce::File getCurrentFile() const { return currentFile; }

private:
    void applyPlaybackRate();

    juce::AudioDeviceManager& deviceManager;
    juce::AudioFormatManager& formatManager;

    juce::File currentFile;
    SyncableAudioSource syncSource;
    juce::AudioSourcePlayer sourcePlayer;

    float volume = 0.8f;
    bool tempoMatch = false;
    double fileBpm = 0.0;
    double projectBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreviewPlayer)
};

} // namespace HDAW
```

- [ ] **Step 2: Rewrite AudioPreviewPlayer.cpp**

Replace the entire file with:

```cpp
#include "AudioPreviewPlayer.h"

namespace HDAW {

AudioPreviewPlayer::AudioPreviewPlayer(juce::AudioDeviceManager& dm,
                                       juce::AudioFormatManager& fm)
    : deviceManager(dm), formatManager(fm)
{
    sourcePlayer.setSource(&syncSource);
}

AudioPreviewPlayer::~AudioPreviewPlayer()
{
    stop();
}

void AudioPreviewPlayer::loadFile(const juce::File& file)
{
    stop();
    if (!file.existsAsFile()) return;
    currentFile = file;
    syncSource.loadFile(file, formatManager);
    syncSource.setGain(volume);
    applyPlaybackRate();
}

void AudioPreviewPlayer::play()
{
    if (syncSource.getLengthInSeconds() <= 0.0) return;
    deviceManager.addAudioCallback(&sourcePlayer);
    syncSource.play();
}

void AudioPreviewPlayer::stop()
{
    if (syncSource.isPlaying())
    {
        syncSource.stop();
        deviceManager.removeAudioCallback(&sourcePlayer);
    }
}

void AudioPreviewPlayer::setVolume(float vol)
{
    volume = juce::jlimit(0.0f, 1.0f, vol);
    syncSource.setGain(volume);
}

void AudioPreviewPlayer::setTempoMatch(bool enabled, double newFileBpm)
{
    tempoMatch = enabled;
    if (newFileBpm > 0.0)
        fileBpm = newFileBpm;
    applyPlaybackRate();
}

void AudioPreviewPlayer::setProjectBpm(double bpm)
{
    if (bpm > 0.0)
        projectBpm = bpm;
    applyPlaybackRate();
}

void AudioPreviewPlayer::applyPlaybackRate()
{
    double ratio = 1.0;
    if (tempoMatch && fileBpm > 0.0 && projectBpm > 0.0)
        ratio = fileBpm / projectBpm;
    syncSource.setResamplingRatio(ratio);
}

} // namespace HDAW
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioPreviewPlayer.h src/engine/AudioPreviewPlayer.cpp
git commit -m "AudioPreviewPlayer: rewrite to use SyncableAudioSource with transport sync"
```

---

## Task 3: Wire TransportManager in AudioEngine

**Files:**
- Modify: `src/engine/AudioEngine.cpp` (the preview player init section, around line 118-119)

- [ ] **Step 1: Add setTransportManager call**

After the preview player creation (around line 118-119):
```cpp
    previewPlayer = std::make_unique<HDAW::AudioPreviewPlayer>(
        deviceManager, projectPool.getFormatManager());
```

Add:
```cpp
    previewPlayer->setTransportManager(&transportManager);
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngine.cpp
git commit -m "AudioEngine: wire TransportManager to preview player for sync mode"
```

---

## Task 4: Build and run tests

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: clean compile with no errors

- [ ] **Step 2: Run all tests**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all tests pass

---

## Summary of Changes

| File | Lines | Change |
|------|-------|--------|
| `src/engine/SyncableAudioSource.h` | ~130 (new) | Custom AudioSource: preloaded buffer, sync/free modes, linear interpolation |
| `src/engine/AudioPreviewPlayer.h` | ~50 (rewrite) | Uses SyncableAudioSource, adds setTransportManager |
| `src/engine/AudioPreviewPlayer.cpp` | ~75 (rewrite) | Simplified: delegates to SyncableAudioSource |
| `src/engine/AudioEngine.cpp` | +1 | Wire TransportManager to preview player |
