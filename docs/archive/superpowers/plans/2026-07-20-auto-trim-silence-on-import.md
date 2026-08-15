# Auto-Trim Silence on Import — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically detect and skip leading/trailing silence (-60 dB threshold) when importing audio files.

**Architecture:** Add `detectSilenceBounds()` to `AudioImport.h/cpp` that scans a file for first/last sample above threshold. Call it at both import sites and adjust clip `offset`/`duration`.

**Tech Stack:** JUCE AudioFormatReader, existing clip offset/duration properties

---

## File Structure

| File | Change |
|------|--------|
| `src/engine/AudioImport.h` | Add `SilenceBounds` struct + `detectSilenceBounds()` declaration |
| `src/engine/AudioImport.cpp` | Implement `detectSilenceBounds()`, integrate into `importAudioFile` |
| `src/ui/TimelineView.cpp` | Integrate into `handleFileDrop` |

---

## Task 1: Add detectSilenceBounds utility

**Files:**
- Modify: `src/engine/AudioImport.h`
- Modify: `src/engine/AudioImport.cpp`

- [ ] **Step 1: Add struct and declaration to AudioImport.h**

After the existing declarations (line 12), before the closing `}`:

```cpp
    struct SilenceBounds {
        double leadingSeconds = 0.0;
        double trailingSeconds = 0.0;
    };

    SilenceBounds detectSilenceBounds(juce::AudioFormatReader& reader, float threshold = 0.001f);
```

- [ ] **Step 2: Implement detectSilenceBounds in AudioImport.cpp**

Add at the end of the file (before the closing of namespace or after the last function):

```cpp
HDAW::SilenceBounds HDAW::detectSilenceBounds(juce::AudioFormatReader& reader, float threshold)
{
    SilenceBounds result;
    const int64_t totalSamples = static_cast<int64_t>(reader.lengthInSamples);
    if (totalSamples <= 0) return result;

    const double sr = reader.sampleRate;
    const int numChannels = static_cast<int>(reader.numChannels);
    constexpr int blockSize = 4096;

    juce::AudioBuffer<float> tempBuffer(numChannels, blockSize);

    // Scan forward for first non-silent sample
    int64_t firstNonSilent = totalSamples; // default: all silent
    for (int64_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(blockSize), totalSamples - pos));
        for (int ch = 0; ch < numChannels; ++ch)
            reader.read(&tempBuffer, ch, 1, pos, toRead, true);

        for (int i = 0; i < toRead; ++i)
        {
            bool above = false;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (std::abs(tempBuffer.getSample(ch, i)) >= threshold)
                {
                    above = true;
                    break;
                }
            }
            if (above)
            {
                firstNonSilent = pos + i;
                break;
            }
        }
        if (firstNonSilent < totalSamples) break;
    }

    if (firstNonSilent >= totalSamples)
        return result; // entire file is silent

    // Scan backward for last non-silent sample
    int64_t lastNonSilent = 0;
    for (int64_t pos = totalSamples - blockSize; pos >= 0; pos -= blockSize)
    {
        int64_t readStart = (std::max)(static_cast<int64_t>(0), pos);
        int toRead = static_cast<int>(std::min(static_cast<int64_t>(blockSize), totalSamples - readStart));
        for (int ch = 0; ch < numChannels; ++ch)
            reader.read(&tempBuffer, ch, 1, readStart, toRead, true);

        for (int i = toRead - 1; i >= 0; --i)
        {
            bool above = false;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (std::abs(tempBuffer.getSample(ch, i)) >= threshold)
                {
                    above = true;
                    break;
                }
            }
            if (above)
            {
                lastNonSilent = readStart + i;
                break;
            }
        }
        if (lastNonSilent > 0) break;
    }

    result.leadingSeconds = static_cast<double>(firstNonSilent) / sr;
    result.trailingSeconds = static_cast<double>(totalSamples - 1 - lastNonSilent) / sr;

    // Safety: don't trim if the audible portion is too short
    double audible = static_cast<double>(totalSamples) / sr - result.leadingSeconds - result.trailingSeconds;
    if (audible < 0.01)
    {
        result.leadingSeconds = 0.0;
        result.trailingSeconds = 0.0;
    }

    return result;
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioImport.h src/engine/AudioImport.cpp
git commit -m "AudioImport: add detectSilenceBounds utility for auto-trim on import"
```

---

## Task 2: Integrate into importAudioFile

**Files:**
- Modify: `src/engine/AudioImport.cpp:62-83`

- [ ] **Step 1: Add silence detection after duration computation**

In `importAudioFile`, after the reader computes `duration` (line 64) and before `startTime` computation (line 71), add silence detection. Then adjust the clip creation to use trimmed duration and set offset after creation.

Replace lines 80-83 (the `createAudioClip` + `addChild` block):

```cpp
    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              startTime, duration,
                                              path.toUtf8().constData());
    clipList.addChild(clip, -1, &model.getUndoManager());
```

with:

```cpp
    // Auto-trim leading/trailing silence
    double clipOffset = 0.0;
    double clipDuration = duration;
    if (reader != nullptr)
    {
        auto bounds = detectSilenceBounds(*reader);
        clipOffset = bounds.leadingSeconds;
        clipDuration = duration - bounds.leadingSeconds - bounds.trailingSeconds;
        if (clipDuration < 0.01) { clipOffset = 0.0; clipDuration = duration; }
    }

    auto clip = ProjectModel::createAudioClip(fi.baseName().toUtf8().constData(),
                                              startTime, clipDuration,
                                              path.toUtf8().constData());
    if (clipOffset > 0.0)
        clip.setProperty(IDs::offset, clipOffset, &model.getUndoManager());
    clipList.addChild(clip, -1, &model.getUndoManager());
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioImport.cpp
git commit -m "AudioImport: auto-trim silence in importAudioFile"
```

---

## Task 3: Integrate into handleFileDrop

**Files:**
- Modify: `src/ui/TimelineView.cpp:918-933`

- [ ] **Step 1: Add silence detection after duration computation**

In `handleFileDrop`, after the reader computes `duration` (line 925) and before `addAudioClip` (line 928), add silence detection:

Replace lines 928-933:

```cpp
    int newClipId = projectCmds->addAudioClip(
        trackIndex,
        (std::max)(0.0, timeSeconds),
        duration,
        filePath.toUtf8().constData(),
        fi.baseName().toUtf8().constData());
```

with:

```cpp
    // Auto-trim leading/trailing silence
    double clipOffset = 0.0;
    double clipDuration = duration;
    if (reader != nullptr)
    {
        auto bounds = HDAW::detectSilenceBounds(*reader);
        clipOffset = bounds.leadingSeconds;
        clipDuration = duration - bounds.leadingSeconds - bounds.trailingSeconds;
        if (clipDuration < 0.01) { clipOffset = 0.0; clipDuration = duration; }
    }

    int newClipId = projectCmds->addAudioClip(
        trackIndex,
        (std::max)(0.0, timeSeconds),
        clipDuration,
        filePath.toUtf8().constData(),
        fi.baseName().toUtf8().constData());

    if (newClipId >= 0 && clipOffset > 0.0)
        projectCmds->setClipOffset(newClipId, clipOffset);
```

- [ ] **Step 2: Add the AudioImport.h include if not already present**

Check the includes at the top of `TimelineView.cpp`. If `#include "../engine/AudioImport.h"` is not there, add it.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/ui/TimelineView.cpp
git commit -m "TimelineView: auto-trim silence on drag-and-drop audio import"
```

---

## Task 4: Build and run tests

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 2: Run all tests**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all tests pass

---

## Summary of Changes

| File | Lines | Change |
|------|-------|--------|
| `src/engine/AudioImport.h` | +7 | `SilenceBounds` struct + `detectSilenceBounds()` declaration |
| `src/engine/AudioImport.cpp` | +65 | `detectSilenceBounds()` implementation + integration in `importAudioFile` |
| `src/ui/TimelineView.cpp` | +12 | Integration in `handleFileDrop` |
