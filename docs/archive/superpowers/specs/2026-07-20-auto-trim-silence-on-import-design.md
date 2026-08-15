# Auto-Trim Silence on Import — Design Spec

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create the implementation plan.

**Goal:** Automatically detect and skip leading/trailing silence when importing audio files, so clips start and end at the first/last audible content.

**Architecture:** Add a `detectSilenceBounds()` utility that scans an audio file for the first/last sample above -60 dB. Call it at both import sites (`importAudioFile` and `handleFileDrop`). Adjust the clip's `offset` (skip leading silence) and `duration` (exclude trailing silence) accordingly.

**Tech Stack:** JUCE AudioFormatReader, existing clip offset/duration properties

---

## Detection

```cpp
struct SilenceBounds {
    double leadingSeconds = 0.0;
    double trailingSeconds = 0.0;
};

SilenceBounds detectSilenceBounds(juce::AudioFormatReader& reader, float threshold = 0.001f);
```

**Algorithm:**
1. Read the file in 4096-sample blocks into a stereo temp buffer
2. Scan forward: find the first sample where `abs(sample) >= threshold` → `leadingSeconds = sampleIndex / sampleRate`
3. Scan backward: find the last sample where `abs(sample) >= threshold` → `trailingSeconds = totalDuration - (lastIndex + 1) / sampleRate`
4. If the entire file is below threshold, return `{0, 0}` (no trim)
5. Safety: if `totalDuration - leading - trailing < 0.01`, return `{0, 0}`

**Threshold:** -60 dB (0.001 linear). Catches digital silence and very quiet hiss without trimming intentional quiet passages.

## Import Integration

In both `importAudioFile` (AudioImport.cpp) and `handleFileDrop` (TimelineView.cpp), after the reader computes `duration`:

```cpp
auto bounds = HDAW::detectSilenceBounds(*reader);
double clipOffset = bounds.leadingSeconds;
double clipDuration = duration - bounds.leadingSeconds - bounds.trailingSeconds;
if (clipDuration < 0.01) { clipOffset = 0.0; clipDuration = duration; }
```

- Pass `clipDuration` as the clip's duration to `createAudioClip` / `addAudioClip`
- After clip creation, set offset: `projectCmds->setClipOffset(clipId, clipOffset)`
- `sourceDuration` remains the full file length (set by the factory)
- `startTime` is unchanged

## Constraints

- `offset + duration <= sourceDuration` (read window fits inside file)
- `sourceDuration` = full file length (used by timestretch ratio computation)
- The source file is never modified — only the read window changes
- All mutations go through UndoManager (Ctrl+Z works)
