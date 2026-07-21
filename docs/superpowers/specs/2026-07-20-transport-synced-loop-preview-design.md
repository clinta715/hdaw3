# Transport-Synced Loop Preview — Design Spec

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create the implementation plan.

**Goal:** Make the audio preview player follow the project transport and loop region, so users can audition files in sync with the looping project.

**Architecture:** Replace the `AudioTransportSource` inside `AudioPreviewPlayer` with a custom `SyncableAudioSource` that preloads the file into memory and reads its position from either `TransportManager` (sync mode, when transport is playing) or an internal counter (free mode, when transport is stopped). Mode switching is automatic — polled per audio block via an atomic read.

**Tech Stack:** JUCE AudioSource, AudioFormatReader, HeapBlock, std::atomic

---

## Current State

`AudioPreviewPlayer` uses JUCE's `AudioTransportSource` + `ResamplingAudioSource` as a standalone file player. It plays from position 0 to end-of-file with no connection to the project transport or loop. It has rate-based tempo match (pitch shifts with speed).

---

## Design

### SyncableAudioSource

A custom `juce::AudioSource` that replaces `AudioTransportSource` + `ResamplingAudioSource`.

**Responsibilities:**
- Preload the entire audio file into a stereo `HeapBlock<float>` buffer (same pattern as `ClipSourceProcessor`)
- Output audio from the buffer at the correct position
- In sync mode: position comes from `TransportManager::getCurrentSample()`
- In free mode: position comes from an internal atomic counter
- Apply resampling ratio for tempo match (read at `ratio * deviceRate / fileRate` speed)
- Output silence beyond the file's length

**Members:**
```cpp
juce::HeapBlock<float> buffer[2];   // preloaded stereo audio
int64_t lengthInSamples = 0;         // file length in file samples
double fileSampleRate = 44100.0;     // file's native sample rate
double deviceSampleRate = 44100.0;   // set in prepareToPlay
double resamplingRatio = 1.0;        // tempo match ratio (fileBpm / projectBpm)
std::atomic<int64_t> internalPos{0}; // free-mode position (device samples)
std::atomic<bool> playing{false};
TransportManager* transport = nullptr;
```

**`getNextAudioBlock(const AudioSourceChannelInfo& info)`:**
```
if (!playing):
    clear the buffer, return

if (transport != nullptr && transport->isPlayingNow()):
    // SYNC MODE — position from transport
    int64_t transportSample = transport->getCurrentSample()
    // Map transport sample → file sample (handles rate difference + tempo match)
    double filePos = transportSample * (fileSampleRate / deviceSampleRate) * resamplingRatio
    read from buffer at filePos for info.numSamples
    // If filePos + numSamples > lengthInSamples, output silence for the overflow
else:
    // FREE MODE — internal counter
    int64_t pos = internalPos.load()
    double filePos = pos * (fileSampleRate / deviceSampleRate) * resamplingRatio
    read from buffer at filePos for info.numSamples
    internalPos += info.numSamples
    if (internalPos >= lengthInSamples * (deviceSampleRate / fileSampleRate) / resamplingRatio):
        playing = false  // stop at EOF
```

**Reading from the buffer:** Linear interpolation between adjacent samples for fractional positions (same as `ClipSourceProcessor`). If the position is beyond `lengthInSamples`, output silence.

**`prepareToPlay(double sampleRate, int blockSize)`:** Store `deviceSampleRate`. No allocation (buffer is preloaded in `loadFile`).

**`loadFile(const juce::File& file)`:** Open with `AudioFormatReader`, read entire file into `buffer[0]`/`buffer[1]` as float. Store `lengthInSamples` and `fileSampleRate`. Reset `internalPos` to 0.

### AudioPreviewPlayer Changes

**Remove:**
- `juce::AudioTransportSource transportSource`
- `juce::ResamplingAudioSource resamplingSource`
- The `AudioFormatReaderSource` creation/destruction in `loadFile`

**Add:**
- `SyncableAudioSource syncSource` (owned member)
- `void setTransportManager(TransportManager* tm)` — stores the pointer, passes to `syncSource`
- `void setTempoMatch(bool enabled, double fileBpm)` — sets `syncSource.resamplingRatio = enabled ? (fileBpm / projectBpm) : 1.0`
- `void setProjectBpm(double bpm)` — stores for ratio computation

**Keep:**
- `juce::AudioSourcePlayer sourcePlayer` — still the audio callback adapter
- `loadFile()` — now calls `syncSource.loadFile()`
- `play()` / `stop()` — set `syncSource.playing`
- `isPlaying()` — reads `syncSource.playing`
- `setVolume()` — applies gain in `getNextAudioBlock` (or a separate gain atomic)

### AudioEngine Wiring

In `AudioEngine::initialize()`, after creating the preview player:
```cpp
previewPlayer->setTransportManager(&transportManager);
```

No listener needed. Mode switching is automatic — `SyncableAudioSource::getNextAudioBlock()` checks `transport->isPlayingNow()` every block (a single atomic load).

### Loop Behavior

In sync mode, `transport->getCurrentSample()` already wraps at the loop boundary (via `TransportManager::advance()`). The preview reads the wrapped position and outputs the corresponding file audio. The loop is "free" — no extra logic.

If the loop region is shorter than the file, the preview only outputs the portion of the file that maps to the loop's time range. If the loop is longer than the file, the preview outputs silence for the portion beyond the file's length.

### Tempo Match

The resampling ratio adjusts the read speed: `filePos = transportSample * (fileSampleRate / deviceSampleRate) * resamplingRatio`. When `resamplingRatio = fileBpm / projectBpm`, the file plays at the project tempo (pitch shifts with speed). This is the same behavior as the current implementation, just computed differently (per-sample position mapping instead of JUCE's `ResamplingAudioSource`).

### Edge Cases

| Scenario | Behavior |
|----------|----------|
| Preview playing, transport starts | Next block switches to sync mode; position jumps to transport position |
| Preview playing, transport stops | Next block switches to free mode; continues from current position |
| Transport playing, user hits preview play | Starts in sync mode at current transport position |
| File shorter than loop | Silence for the portion beyond file length |
| File longer than loop | Only the loop-mapped portion is heard repeatedly |
| No file loaded | `playing` is false; `getNextAudioBlock` outputs silence |
| Transport not set (nullptr) | Always free mode (existing behavior) |

### What Stays the Same

- The preview is still a separate `AudioIODeviceCallback` (not routed through tracks/FX/master bus)
- RPC surface (`preview.load`, `preview.play`, `preview.stop`, `preview.setVolume`, `preview.setTempoMatch`, `preview.setProjectBpm`, `preview.isPlaying`)
- Frontend UI (play/stop/volume/tempo-match checkbox/BPM input)
- The `AudioSourcePlayer` adapter pattern

### Out of Scope (deferred)

- Drag-over audible preview with waveform ghost
- Preview routing through a track's FX chain (audition in context)
- Loop-boundary crossfade
- Preview seek/scrub RPC
- Preview/transport stop coordination (stopping transport does not stop preview)
