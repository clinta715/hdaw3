# Export Bug: Audio Cuts Off After 0.6 Seconds

**Date:** 2026-08-31  
**Status:** Investigating  
**Priority:** High (blocks all composition work)

## Summary

Export renders only ~0.6 seconds of audio, then produces silence for the remainder of the requested duration. The render loop completes successfully and writes the full file, but audio content stops at exactly 28800 samples (0.6s at 48kHz).

## Symptoms

- Export completes without errors
- File size is correct (e.g., 87MB for 5-minute export)
- First 0.6 seconds contain audio
- Everything after 0.6s is silence (RMS = 0.0)
- Cutoff point is **exactly** 28800 samples every time
- Happens across different projects (forest_cathedral, minimal_test, 5tracks_test)
- Happens with different track configurations (single track, multiple tracks, audio clips, MIDI clips)

## What We've Tried

### Fixed Issues (Not the Root Cause)

1. **Pitch > 127 crash** - Fixed array bounds in MidiClipProcessor (lines 191, 222, 301, 331)
   - Added `juce::jlimit(0, 127, note.noteNumber)` clamping
   - Added regression tests (all passing)
   - This was causing crashes during export but not the silence bug

2. **BreakPatternGenerator pitch overflow** - Fixed slice count capping
   - Added `baseNote` parameter to Params struct
   - Capped sliceCount to `128 - baseNote` to prevent MIDI pitch > 127
   - Added defensive clamp in `asNotes()`
   - This was causing the pitch > 127 issue but not the silence bug

### Debugging Attempts

1. **Added debug logging to ExportManager** (lines 420-435)
   - Logs transport position and buffer RMS every 10 blocks
   - Shows transport advancing correctly
   - Shows buffer RMS dropping to 0 after 0.6s
   - **Log file not being created** - HDAW_LOG may not be flushing in export context

2. **Tested various scenarios:**
   - Single track with one clip → silence after 0.6s
   - Multiple tracks → silence after 0.6s
   - Different BPM (120, 148) → silence after 0.6s (different sample counts)
   - Different durations (5s, 10s, 303s) → silence after 0.6s
   - Audio clips vs MIDI clips → both fail the same way

3. **Checked transport state:**
   - `renderTransport.setPlaying(true)` is called (line 238)
   - `renderTransport.advance(numThisBlock)` is called in loop (line 421)
   - No early exit conditions triggered
   - `projectEndSample` not set (defaults to 0, should not cause early stop)

4. **Checked clip processing:**
   - MidiClipProcessor::processBlock() looks correct
   - ClipSourceProcessor::processBlock() looks correct
   - No obvious early-exit conditions
   - Clips have correct start/duration values

## What We Know

### The Cutoff Point

- **28800 samples at 48kHz = 0.6 seconds**
- At 148 BPM: 0.6s = 1.48 beats
- At 120 BPM: 0.6s = 1.2 beats
- The cutoff is time-based, not beat-based

### The Render Loop

```cpp
// ExportManager.cpp lines 408-438
while (samplesRendered < totalSamples && !cancelFlag.load())
{
    int numThisBlock = static_cast<int>((std::min)(
        static_cast<int64_t>(blockSize), totalSamples - samplesRendered));

    buffer.clear();
    midiBuffer.clear();

    renderGraph.processBlock(buffer, midiBuffer);  // <-- Produces silence after 0.6s
    
    renderTransport.advance(numThisBlock);  // <-- Transport advances correctly

    writer->writeFromAudioSampleBuffer(buffer, 0, numThisBlock);  // <-- Writes silence

    samplesRendered += numThisBlock;
    ++blocksDone;
}
```

### The Graph Processing

- `renderGraph.processBlock()` is called on all nodes in topological order
- MidiClipProcessor → Track → MasterBus → AudioOutput
- Transport is advancing correctly
- But the buffer contains silence after 0.6s

## Hypotheses

### 1. ClipSourceProcessor Streaming Issue

Audio clips use a streaming reader that may have a buffer size limit or seek issue.

**To investigate:**

- Check if `streamer.readNextBlock()` stops reading after 0.6s
- Add logging to ClipSourceProcessor::processBlock() to see if it's being called
- Check if `sourceSample` calculation is correct

### 2. MidiClipProcessor Note Scheduling

MIDI clips may have an issue with note scheduling beyond 0.6s.

**To investigate:**

- Add logging to MidiClipProcessor::processBlock() to see note triggers
- Check if `currentBeat` calculation is correct
- Verify note start/end times are being compared correctly

### 3. AudioProcessorGraph Render Sequence

The graph's render sequence may be cached or baked incorrectly.

**To investigate:**

- Check if `renderGraph.setNonRealtime(true)` affects render sequence baking
- Verify all nodes are connected correctly
- Check if there's a timeout or limit in the graph processing

### 4. Transport Position Calculation

The transport may be calculating position incorrectly for non-realtime rendering.

**To investigate:**

- Add logging to TransportManager::advance()
- Check if `currentSample` is being updated correctly
- Verify `secondsToPpq()` conversion is correct

### 5. Buffer Clearing Issue

Something may be clearing the buffer after 0.6s.

**To investigate:**

- Add logging before and after `renderGraph.processBlock()`
- Check if any node is calling `buffer.clear()` unexpectedly
- Verify buffer is not being reused incorrectly

## Next Steps

### Immediate (Priority 1)

1. **Enable HDAW_LOG for export**
   - Check why log file isn't being created
   - May need to flush or redirect output
   - Critical for seeing what's happening in the render loop

2. **Add logging to MidiClipProcessor::processBlock()**
   - Log `transportSample`, `currentBeat`, note triggers
   - See if MIDI processing stops after 0.6s

3. **Add logging to ClipSourceProcessor::processBlock()**
   - Log `transportSample`, `clipLocalSample`, `sourceSample`
   - See if audio streaming stops after 0.6s

### Short-term (Priority 2)

4. **Test with a minimal project**
   - Create a project with just one MIDI note at beat 2
   - Export 5 seconds
   - See if the note plays or if it's silent

2. **Check AudioProcessorGraph internals**
   - Verify all nodes are being processed
   - Check if there's a render sequence cache issue
   - Look for any timeout or limit in non-realtime mode

3. **Compare with live playback**
   - Does the same project play correctly in real-time?
   - If yes, the issue is specific to export/non-realtime mode
   - If no, the issue is in the clip/transport processing

### Long-term (Priority 3)

7. **Add export unit tests**
   - Test export with various project configurations
   - Verify audio content at different time points
   - Catch regressions

2. **Profile export performance**
   - Check if there's a performance cliff at 0.6s
   - Look for memory leaks or resource exhaustion
   - Verify disk I/O isn't blocking

## Related Files

- `src/engine/ExportManager.cpp` - Export render loop
- `src/engine/MidiClipProcessor.h` - MIDI clip processing
- `src/engine/ClipSourceProcessor.h` - Audio clip processing
- `src/engine/TransportManager.h` - Transport position tracking
- `src/engine/RoutingManager.cpp` - Graph construction

## Test Files

- `renders/forest_cathedral_full.wav` - 5-minute export, silence after 0.6s
- `renders/test_debug.wav` - 5-second export attempt (file not created)
- `renders/test_0_5.wav` - 0.5-second export (works, but that's < 0.6s)
- `renders/test_10s_unmuted.wav` - 10-second export, silence after 0.6s

## Notes

- The bug was discovered while trying to compose psytrance tracks
- The pitch > 127 crash was a separate issue that we fixed first
- The silence bug is blocking all composition work
- We need to fix this before we can continue with Track A or Track B
