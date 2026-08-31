# Handoff: Export silence after exactly 0.6s (28800 samples at 48kHz)

## Status: ✅ RESOLVED — see `docs/handoffs/2026-09-17-export-silence-investigation.md`

Root cause: out-of-range reverb Room Size (`param_0=900`, valid [0,1]) pushed unclamped into JUCE Freeverb → comb feedback ≈ 252 → exponential divergence → inf/NaN → WAV writer emits zeros. NOT a transport/clip-bounds bug.

## Summary

Exported WAV files contain audio for exactly 0.6 seconds (28800 samples at 48kHz) then go to hard zero for the remainder of the file. The file is the correct length and the audio in the first 0.6s sounds correct. This happens across multiple project types but NOT all exports — the pattern of which exports fail is the key clue.

## Evidence — WAV analysis of all files in `renders/`

### Pattern: works vs fails

| Status | Files | Notes |
| -------- | ------- | ------- |
| **WORKS (full audio)** | `DnB_Voltage_Test.wav` (245s), `minimal_test.wav` (10s), all `psytrance_*.wav` (258–360s), all `VoltageRev_*.wav` (243s), `test_0_10.wav` (10s), `test_10_20.wav` (10s), `test_5tracks.wav` (10s), `test_new_project.wav` (10s), `test_no_auto.wav` (15s), `test_simple.wav` (15s) | Audio for full duration |
| **FAILS at exactly 0.6s** | `forest_cathedral_test.wav` (10s), `test30s.wav` (30s), `test5s.wav` (5s), `test_0_2.wav` (2s), `test_0_5.wav` (5s), `test_10s_unmuted.wav` (10s), `test_15s.wav` (15s), `test_6s.wav` (6s), `test_kick_only.wav` (5s) | Last non-zero sample = 28799, first zero at 28800 |
| **FAILS (silence)** | `forest_cathedral_canary.wav` (303s), `forest_cathedral_full.wav` (303s), `test120s.wav` (111s), `test_fresh.wav` (5s) | All-zero from start |
| **FAILS (other)** | `test_long_notes.wav` (last_nz=3.5s), `test_mid.wav` (last_nz=0.49s) | Intermediate cutoffs |

### Key observation

The **exact** cutoff point is sample 28800 = 0.6000s at 48kHz for ALL "0.6s fails". This is NOT at a block boundary (512 samples/block → 28800 = 56.25 blocks). The cutoff is mid-block: samples 28672–28799 are non-zero, samples 28800+ are zero.

### `forest_cathedral.hdaw` project data

- 21 tracks, 39 clip lists, 7688 MIDI notes, 16 MIDI clips, 65 audio clips
- startTimes range from 0.0 to 301.6s — clips exist far past 0.6s
- Duration range: 0.15s to 301.6s

## What was checked and ruled out

### Transport auto-stop: RULED OUT

`ExportManager::renderThreadFunc` creates a local `TransportManager renderTransport` and never calls `setProjectEndSample()`. Default is 0, so `TransportManager::advance()` never triggers auto-stop (`projEnd > 0` guard fails). Transport should advance indefinitely.

### Transport advancement: RULED OUT

```cpp
// ExportManager.cpp render loop
renderGraph.processBlock(buffer, midiBuffer);  // reads transport position
renderTransport.advance(numThisBlock);          // increments by 512
```

The `fetch_add` in `advance()` is atomic and correct. No code path sets `isPlaying=false` during export. `advance()` is called every iteration.

### Clip bounds check: PARTIALLY CHECKED

`ClipSourceProcessor::processBlock` clears buffer when `clipLocalSample >= durSamples`. Clips in `forest_cathedral` have durations up to 301s, so this shouldn't fire at 0.6s. But the exact bounds were NOT verified for each clip in each project that fails.

### Graph topology: LOOKS CORRECT

`RoutingManager::rebuildFromValueTree` creates all nodes and connections. MIDI clips connect via MIDI + audio stubs. Audio clips connect via stereo audio. Track→bus→master→IO chain is built. `reconnectMasterToOutput()` runs after `prepareToPlay`.

### InternalPlayHead: LOOKS CORRECT

Reads from `TransportManager` atomics. Returns `isPlaying`, `timeInSamples`, `timeInSeconds`, `ppqPosition`, `bpm`. No allocation, no lock.

### Proxy/plugin isolation: NOT THE CAUSE for most failures

`forest_cathedral` has no plugin FX per the handoff doc. `minimal_test.wav` (no plugins) works. The 0.6s cutoff occurs in projects with and without plugins.

## What was NOT checked (next steps)

### 1. **The export code path that produces the failing files**

We don't know which code path created each WAV. The working psytrance/VoltageRev files likely came from a different export path (possibly an older engine version or a different `startExport` call). **The critical question is: what's different about the exports that fail?**

Possible differences:

- Different `ExportManager::startExport` call with different parameters
- Different `AudioEngine::exportAudio` path
- Different project loading path (load vs. fresh)
- Different engine build version

### 2. **The `test_0_10.wav` vs `test_0_5.wav` paradox**

Both names suggest start=0. `test_0_10.wav` WORKS (10s of audio). `test_0_5.wav` FAILS (0.6s then silence). If they used the same code path, something else is different — maybe the project content, maybe the engine state at export time.

**Hypothesis:** `test_0_10.wav` was created by a different mechanism (e.g., `test_simple` / `test_no_auto` test which works) and happens to be 10s. The naming is coincidental.

### 3. **The exact `ExportManager::startExport` call for each failing file**

Without knowing the exact parameters (startTime, duration, sampleRate, project tree source), we can't reproduce or narrow down.

### 4. **MidiClipProcessor behavior at the cutoff point**

At sample 28800 (0.6s), the `MidiClipProcessor` checks:

```cpp
double clipLocalSec = currentTimeSec - startSec;
if (clipLocalSec < 0.0 || clipLocalSec >= durSec) { // send note-offs; return }
```

If a clip's `durSec` is exactly 0.6s, this would fire. But we haven't verified whether any clips in the failing projects have `duration=0.6`.

**BUT:** The cutoff is at sample 28800 for ALL failing projects regardless of content. This suggests the cause is NOT clip-specific.

### 5. **The `test_fresh.wav` failure (complete silence from start)**

This is a different failure mode — zero audio from the very first sample. This suggests the graph produces silence for fresh/default projects in some code paths.

### 6. **The `AudioProcessorGraph` render sequence behavior**

JUCE 8's `AudioProcessorGraph::processBlock` in non-realtime mode uses a baked render sequence. We verified the bake wait completes before the first processBlock. But we did NOT verify:

- Whether the render sequence includes all expected nodes
- Whether the sequence changes mid-export (e.g., triggered by `setNonRealtime(true)`)

### 7. **The `renderGraph.prepareToPlay` vs `setNonRealtime(true)` ordering**

```cpp
renderGraph.prepareToPlay(sampleRate, blockSize);
renderGraph.setNonRealtime(true);  // called AFTER prepareToPlay
```

In JUCE 8, `setNonRealtime(true)` may trigger a render sequence rebake. The bake wait accounts for this. But if the rebake produces a different (broken) sequence, that could explain the cutoff.

### 8. **The `Track::processBlock` FX chain behavior**

`Track::processBlock` uses `stateLock.tryEnter()` for FX processing. If `stateLock` is contended (e.g., by a concurrent `rebuildFXChain`), the FX chain is SKIPPED — but the audio buffer is NOT cleared, so audio passes through unprocessed. This shouldn't cause silence.

However, the modulation loop also uses `stateLock.tryEnter()`. If contended, modulation is skipped (modGain/modPan stay 0.0). This wouldn't cause silence either.

### 9. **The WAV peak analysis anomaly**

Failing files show `peak=32768` (max 16-bit) in the first 0.6s. Working files show `peak=10596` (moderate). This suggests the failing exports have MUCH louder audio (clipping at full scale) before the cutoff. This could indicate:

- A gain staging issue specific to the failing code path
- The audio IS correct but very loud, and the cutoff is a separate issue
- The peak is an artifact of how ffmpeg decodes 24-bit WAV to 16-bit

## Relevant files

| File | Role |
| ------ | ------ |
| `src/engine/ExportManager.cpp` | Export render loop, transport setup, graph construction |
| `src/engine/TransportManager.h` | Transport state, `advance()`, `InternalPlayHead` |
| `src/engine/MainAudioProcessor.cpp` | Live `processBlock`, `projectEndSample` computation |
| `src/engine/RoutingManager.cpp` | Graph topology, clip/track/bus wiring |
| `src/engine/Track.cpp` | Per-track processing, FX chain, volume/pan, modulation |
| `src/engine/MidiClipProcessor.h` | MIDI note scheduling, clip bounds check |
| `src/engine/ClipSourceProcessor.h` | Audio clip playback, streaming, gain envelope |
| `src/engine/StreamingClipSource.h` | Background audio streaming |
| `src/proxy/PluginProxySlot.cpp` | Proxy timeout / slotFailed (separate issue) |
| `src/proxy/host/PluginHost.cpp` | Child pacing, render mode (separate issue) |

## Suggested next steps (priority order)

1. **Reproduce the exact failing export.** Run the MCP `export.audio` tool or the test that creates `test_10s_unmuted.wav` and capture the ExportManager debug logs (the per-10-block RMS logging that was added to the render loop). This will show exactly when RMS drops to zero and what the transport position is at that point.

2. **Add a diagnostic log inside `MidiClipProcessor::processBlock`** that fires when `clipLocalSec >= durSec` (the clip-bounds check). This will reveal if clips are being clipped short.

3. **Add a diagnostic log inside `ClipSourceProcessor::processBlock`** at the bounds check (`clipLocalSample >= durSamples`). Same purpose.

4. **Check if `renderTransport.getCurrentSample()` advances correctly during export.** Add a log at the top of the render loop that prints the transport position every block for the first 100 blocks.

5. **Compare a working export path (psytrance) vs a failing one (forest_cathedral).** What are the different `startExport` parameters? Different project loading path? Different engine state?

6. **Test with `HDAW_NO_PLUGIN_ISOLATION=1`** to rule out proxy-related issues entirely.

7. **Test with a project that has ONLY MIDI clips and no audio clips** to rule out `ClipSourceProcessor` / streaming issues.

## Companion docs

- `docs/handoffs/2026-08-31-export-silence-bug.md` — earlier investigation, same symptom
- `docs/handoffs/2026-08-17-staticy-audio-proxy-resync.md` — proxy pacing / stale output (separate issue)
- `docs/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md` — transport forwarding (implemented, not root cause)
- `docs/postmortem-silent-clap-export.md` — CLAP lifecycle / message pump (implemented, not root cause)
