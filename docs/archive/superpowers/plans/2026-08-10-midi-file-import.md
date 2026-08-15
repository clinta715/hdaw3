# MIDI File Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire up the existing `importMidiFile` engine function as an RPC endpoint so the frontend can import `.mid`/`.midi` files with notes intact, supporting per-track import into new project tracks.

**Architecture:** The existing `HDAW::importMidiFile` in `src/engine/MidiImport.cpp` already parses MIDI files via JUCE's `MidiFile` and creates clips with notes. It is currently dead code (not in CMakeLists.txt, not wired to any RPC). We modify it to support creating a new HDAW track per MIDI track (when `trackIdx == -1`), add it to the `ProjectCommands` interface, implement in `AudioEngineCommands`, wire an RPC handler, and update the frontend to call it.

**Tech Stack:** C++ (JUCE MidiFile), Qt JSON-RPC router, React/TypeScript frontend, gtest.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `CMakeLists.txt:73` | Modify | Add `src/engine/MidiImport.cpp` to `HDAW_lib` sources |
| `src/engine/MidiImport.h` | Modify | Change signature to return `std::vector<int>` (clip IDs), add `createNewTracks` param |
| `src/engine/MidiImport.cpp` | Modify | Support per-track import into new tracks, return clip IDs |
| `src/common/ProjectCommands.h` | Modify | Add `importMidiFile` pure virtual |
| `src/engine/AudioEngineCommands.h` | Modify | Add `importMidiFile` override declaration |
| `src/engine/AudioEngineCommands.cpp` | Modify | Implement `importMidiFile` (delegate to `HDAW::importMidiFile`) |
| `src/frontend/FrontendRouter.cpp:36` | Modify | Handle `project.importMidiFile` specially (needs `AudioEngine&`) |
| `frontend/src/components/ImportDialog.tsx` | Modify | Call `project.importMidiFile` instead of `project.addMidiClip` |
| `frontend/src/components/TimelineMinimal/useTimelineDrop.ts` | Modify | Call `project.importMidiFile` for `.mid` drops |
| `tests/unit/engine/midi_import_test.cpp` | Create | gtest for the import function |

---

## Task 1: Add MidiImport.cpp to the build

**Files:**
- Modify: `CMakeLists.txt:73-157`

- [ ] **Step 1: Add MidiImport.cpp to HDAW_lib sources**

In `CMakeLists.txt`, add `src/engine/MidiImport.cpp` after `src/engine/MidiInputManager.cpp` (line 106):

```cmake
    src/engine/MidiInputManager.cpp
    src/engine/MidiImport.cpp
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Build succeeds (MidiImport.cpp compiles, no linker errors since nothing calls it yet).

---

## Task 2: Modify MidiImport to support per-track import and return clip IDs

**Files:**
- Modify: `src/engine/MidiImport.h`
- Modify: `src/engine/MidiImport.cpp`

- [ ] **Step 1: Update the header**

Replace `src/engine/MidiImport.h` contents:

```cpp
#pragma once
#include <QString>
#include <vector>
#include "../engine/AudioEngine.h"

namespace HDAW
{
    // Import MIDI tracks from a .mid file.
    // trackIdx >= 0: place all MIDI tracks as clips on that track (legacy).
    // trackIdx == -1: create a new HDAW track per MIDI track (default).
    // Returns clip IDs of imported clips (empty on failure).
    std::vector<int> importMidiFile(AudioEngine& engine, const QString& path, int trackIdx = -1);
}
```

- [ ] **Step 2: Rewrite MidiImport.cpp**

Replace `src/engine/MidiImport.cpp` with:

```cpp
#include "MidiImport.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

std::vector<int> HDAW::importMidiFile(AudioEngine& engine, const QString& path, int trackIdx)
{
    std::vector<int> importedClipIds;
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();

    juce::File midiFile(path.toUtf8().constData());
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
    {
        HDAW_LOG("MidiImport", "could not open MIDI file: " + path);
        return {};
    }

    juce::MidiFile midiData;
    if (!midiData.readFrom(stream))
    {
        HDAW_LOG("MidiImport", "failed to read MIDI file: " + path);
        return {};
    }

    int midiTimeFormat = static_cast<int>(midiData.getTimeFormat());
    if (midiTimeFormat <= 0)
    {
        HDAW_LOG("MidiImport", "SMPTE timecode MIDI files are not supported: " + path);
        return {};
    }
    int midiTicksPerQuarterNote = midiTimeFormat;
    double bpm = 120.0;

    if (midiData.getNumTracks() > 0)
    {
        auto* tempoTrack = midiData.getTrack(0);
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e)
        {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTempoMetaEvent())
            {
                double secPerQuarter = ev->message.getTempoSecondsPerQuarterNote();
                if (secPerQuarter > 0.0)
                    bpm = 60.0 / secPerQuarter;
                break;
            }
        }
    }

    double secondsPerTick = (60.0 / bpm) / static_cast<double>(midiTicksPerQuarterNote);

    for (int mt = 0; mt < midiData.getNumTracks(); ++mt)
    {
        auto* midiTrack = midiData.getTrack(mt);
        if (midiTrack == nullptr || midiTrack->getNumEvents() == 0)
            continue;

        double clipDuration = 4.0;
        auto* lastEventHolder = midiTrack->getEventPointer(midiTrack->getNumEvents() - 1);
        if (lastEventHolder != nullptr)
            clipDuration = lastEventHolder->message.getTimeStamp() * secondsPerTick + 1.0;

        // Resolve target track
        int targetTrackIdx = trackIdx;
        if (targetTrackIdx < 0)
        {
            // Create a new track for this MIDI track
            juce::String trackName = "MIDI Track " + juce::String(mt + 1);
            targetTrackIdx = engine.getProjectCommands().addTrack(
                trackName.toRawUTF8(), -1, -1, 1 /* MIDI */);
            if (targetTrackIdx < 0)
            {
                HDAW_LOG("MidiImport", "failed to create track for MIDI track " + juce::String(mt + 1));
                continue;
            }
        }

        auto trackTree = trackList.getChild(targetTrackIdx);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
        {
            clipList = juce::ValueTree(IDs::CLIP_LIST);
            trackTree.addChild(clipList, -1, &model.getUndoManager());
        }

        double clipStartTime = 0.0;
        for (int i = 0; i < clipList.getNumChildren(); ++i)
        {
            auto c = clipList.getChild(i);
            double end = static_cast<double>(c.getProperty(IDs::startTime))
                       + static_cast<double>(c.getProperty(IDs::duration));
            clipStartTime = (std::max)(clipStartTime, end);
        }

        auto clip = ProjectModel::createMidiClipEmpty(
            ("MIDI Track " + juce::String(mt + 1)).toRawUTF8(),
            clipStartTime, clipDuration);
        auto midiNotes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);

        for (int e = 0; e < midiTrack->getNumEvents(); ++e)
        {
            auto* eventHolder = midiTrack->getEventPointer(e);
            if (eventHolder == nullptr) continue;

            auto& msg = eventHolder->message;
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                double tickTime = msg.getTimeStamp();
                double beatTime = tickTime / static_cast<double>(midiTicksPerQuarterNote);

                double noteDurBeats = 0.25;
                int noteNum = msg.getNoteNumber();
                for (int e2 = e + 1; e2 < midiTrack->getNumEvents(); ++e2)
                {
                    auto* ev2 = midiTrack->getEventPointer(e2);
                    if (ev2 != nullptr && ev2->message.isNoteOff() &&
                        ev2->message.getNoteNumber() == noteNum)
                    {
                        double offTick = ev2->message.getTimeStamp();
                        noteDurBeats = (offTick - tickTime) / static_cast<double>(midiTicksPerQuarterNote);
                        break;
                    }
                }

                midiNotes.addChild(ProjectModel::createMidiNote(
                    noteNum, static_cast<float>(msg.getVelocity()) / 127.0f,
                    beatTime, noteDurBeats), -1, nullptr);
            }
        }

        if (midiNotes.getNumChildren() > 0)
        {
            clipList.addChild(clip, -1, &model.getUndoManager());
            // Read back the clip ID assigned by the model
            int clipId = clip.getProperty(IDs::clipID, -1).toInt();
            if (clipId >= 0)
                importedClipIds.push_back(clipId);
        }
    }

    engine.getMainProcessor()->rebuildRoutingGraph();
    return importedClipIds;
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Build succeeds.

---

## Task 3: Add importMidiFile to ProjectCommands interface

**Files:**
- Modify: `src/common/ProjectCommands.h:48-49`

- [ ] **Step 1: Add the pure virtual method**

After the `addMidiClip` declaration (line 49), add:

```cpp
    virtual std::vector<int> importMidiFile(const std::string& filePath, int trackIndex = -1) = 0;
```

- [ ] **Step 2: Verify it compiles (will fail — missing override)**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compile error in `AudioEngineCommands` — missing `importMidiFile` override. This is expected; Task 4 adds it.

---

## Task 4: Implement importMidiFile in AudioEngineCommands

**Files:**
- Modify: `src/engine/AudioEngineCommands.h:52-53`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Add override declaration in header**

After the `addMidiClip` declaration (line 53), add:

```cpp
    std::vector<int> importMidiFile(const std::string& filePath, int trackIndex = -1) override;
```

- [ ] **Step 2: Add include in AudioEngineCommands.cpp**

Add at the top of `src/engine/AudioEngineCommands.cpp`:

```cpp
#include "MidiImport.h"
```

- [ ] **Step 3: Implement the method**

Add to `src/engine/AudioEngineCommands.cpp` (at the end, before the closing of the file):

```cpp
std::vector<int> AudioEngineCommands::importMidiFile(const std::string& filePath, int trackIndex)
{
    return HDAW::importMidiFile(engine_, QString::fromStdString(filePath), trackIndex);
}
```

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Build succeeds.

---

## Task 5: Wire the RPC handler

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp:36`

- [ ] **Step 1: Add the RPC handler**

In `FrontendRouter.cpp`, after the `dispatchProject` call on line 36, add a special case for `importMidiFile` (which needs `AudioEngine&`, not just `ProjectCommands&`):

```cpp
    if (ns == method::Project) {
        // importMidiFile needs AudioEngine& (not just ProjectCommands&)
        if (m == "importMidiFile") {
            const auto o = paramsObject(params);
            std::string filePath;
            if (!requireString(o, "filePath", filePath, nullptr))
                return makeError(-32602, "filePath required");
            int trackIndex = optInt(o, "trackIndex", -1, nullptr);
            auto clipIds = engine.getProjectCommands().importMidiFile(filePath, trackIndex);
            QJsonArray arr;
            for (int id : clipIds) arr.append(id);
            return { false, QJsonObject{ { "clipIds", arr }, { "trackCount", static_cast<int>(clipIds.size()) } } };
        }
        return dispatchProject(engine.getProjectCommands(), m, params);
    }
```

And remove the original line 36:
```cpp
    // DELETE: if      (ns == method::Project)     return dispatchProject(engine.getProjectCommands(), m, params);
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Build succeeds.

---

## Task 6: Update the frontend ImportDialog

**Files:**
- Modify: `frontend/src/components/ImportDialog.tsx:27-53`

- [ ] **Step 1: Replace the MIDI import RPC call**

In `ImportDialog.tsx`, replace the `handleImport` function's MIDI branch (lines 43-51):

```tsx
  const handleImport = async () => {
    if (!filePath.trim()) return;

    const tr = useTransportStore.getState().transport;
    const startBeat = tr.currentTimeSeconds * (tr.bpm / 60);
    const fileName = filePath.trim().split(/[/\\]/).pop() ?? filePath.trim();

    if (mode === "audio") {
      const params: Record<string, unknown> = {
        trackIndex: trackChoice === "new" ? 0 : parseInt(trackChoice, 10),
        start: startBeat,
        duration: 4,
        sourceFile: filePath.trim(),
        name: fileName,
      };
      await rpc.call("project.addAudioClip", params).catch(() => {});
    } else {
      const trackIndex = trackChoice === "new" ? -1 : parseInt(trackChoice, 10);
      await rpc.call("project.importMidiFile", {
        filePath: filePath.trim(),
        trackIndex,
      }).catch(() => {});
    }
    onImport();
    onClose();
  };
```

- [ ] **Step 2: Verify frontend builds**

Run: `cd frontend && npm run build 2>&1 | tail -5`
Expected: Build succeeds with no TypeScript errors.

---

## Task 7: Update the timeline drag-drop handler

**Files:**
- Modify: `frontend/src/components/TimelineMinimal/useTimelineDrop.ts:44-60`

- [ ] **Step 1: Replace the MIDI import RPC call in the file-browser drag handler**

In `useTimelineDrop.ts`, replace the MIDI branch in the `doImport` function (lines 53-59):

```tsx
            } else if (midiExts.includes(ext)) {
              await rpc.call("project.importMidiFile", {
                filePath,
                trackIndex: targetTrack,
              });
            }
```

- [ ] **Step 2: Replace the MIDI import RPC call in the external-file drag handler**

Similarly, replace lines 123-128:

```tsx
          } else {
            await rpc.call("project.importMidiFile", {
              filePath: (file as any).path ?? file.name,
              trackIndex: targetTrack,
            });
          }
```

- [ ] **Step 3: Verify frontend builds**

Run: `cd frontend && npm run build 2>&1 | tail -5`
Expected: Build succeeds.

---

## Task 8: Write gtest for MIDI file import

**Files:**
- Create: `tests/unit/engine/midi_import_test.cpp`

- [ ] **Step 1: Create the test file**

```cpp
#include <gtest/gtest.h>
#include "../../src/engine/MidiImport.h"
#include "../../src/engine/AudioEngine.h"
#include "../../src/model/ProjectModel.h"
#include <juce_core/juce_core.h>
#include <fstream>

namespace
{

// Write a minimal valid MIDI file with one track, one note.
// Format 0, 1 track, ticks-per-quarter = 480, tempo = 120 BPM.
std::string writeTestMidiFile()
{
    char path[MAX_PATH];
    GetTempFileNameA(GetTempPathA(), "mid", 0, path);

    // Minimal MIDI file: header + one track with one note-on/note-off
    const uint8_t midiData[] = {
        // Header chunk: "MThd", length=6, format=0, nTracks=1, division=480
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,1,480,
        // Track chunk: "MTrk", length
        'M','T','r','k', 0,0,0,37,
        // Tempo meta event: FF 51 03 07 A1 20 (120 BPM = 500000 us/quarter)
        0, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        // Note On: ch0, note 60, vel 100, delta=0
        0, 0x90, 60, 100,
        // Note Off: ch0, note 60, delta=480 (1 beat)
        0, 1, 0x80, 60, 0,
        // End of track
        0, 0xFF, 0x2F, 0
    };

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(midiData), sizeof(midiData));
    f.close();
    return std::string(path);
}

TEST(MidiImportTest, ImportIntoExistingTrackCreatesClipWithNotes)
{
    AudioEngine engine;
    engine.initialize();

    // Create a MIDI track
    int trackIdx = engine.getProjectCommands().addTrack("Target", -1, -1, 1);
    ASSERT_GE(trackIdx, 0);

    auto midiPath = writeTestMidiFile();
    auto clipIds = HDAW::importMidiFile(engine, QString::fromStdString(midiPath), trackIdx);

    ASSERT_EQ(clipIds.size(), 1u);

    // Verify the clip has notes
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    auto track = trackList.getChild(trackIdx);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    ASSERT_EQ(clipList.getNumChildren(), 1);

    auto clip = clipList.getChild(0);
    auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    EXPECT_GT(notes.getNumChildren(), 0);

    // Clean up temp file
    juce::File(midiPath).deleteFile();
}

TEST(MidiImportTest, ImportIntoNewTracksCreatesTracksAndClips)
{
    AudioEngine engine;
    engine.initialize();

    auto midiPath = writeTestMidiFile();
    // trackIdx = -1 => create new tracks
    auto clipIds = HDAW::importMidiFile(engine, QString::fromStdString(midiPath), -1);

    // Our test MIDI has 1 track, so expect 1 new track + 1 clip
    ASSERT_EQ(clipIds.size(), 1u);

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    // Should now have at least 1 track (the imported one)
    EXPECT_GE(trackList.getNumChildren(), 1);

    juce::File(midiPath).deleteFile();
}

TEST(MidiImportTest, ImportNonexistentFileReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();

    auto clipIds = HDAW::importMidiFile(engine, "C:/nonexistent/path.mid", 0);
    EXPECT_TRUE(clipIds.empty());
}

} // namespace
```

- [ ] **Step 2: Add the test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `unit/engine/midi_import_test.cpp` to the `hdaw_tests` source list.

- [ ] **Step 3: Build and run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | tail -5`
Then: `build/Debug/hdaw_tests.exe --gtest_filter=MidiImportTest.*`
Expected: All 3 tests pass.

---

## Task 9: Verify full build and existing tests

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors.

- [ ] **Step 2: Run full test suite**

Run: `build/Debug/hdaw_tests.exe`
Expected: All existing tests pass, plus the 3 new MidiImportTest cases.

- [ ] **Step 3: Run frontend tests**

Run: `cd frontend && npm test`
Expected: All frontend tests pass.

---

## Success Gates

- [ ] `cmake --build build --config Debug` succeeds
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=MidiImportTest.*` — all 3 tests pass
- [ ] `cd frontend && npm run build` succeeds
- [ ] `cd frontend && npm test` — no regressions
- [ ] Drag-dropping a `.mid` file onto the timeline creates a new track with a clip containing notes
- [ ] File > Import MIDI creates a new track with a clip containing notes
- [ ] Importing into an existing track places the clip on that track
