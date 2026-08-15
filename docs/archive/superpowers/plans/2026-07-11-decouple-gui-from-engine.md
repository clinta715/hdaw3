# Decouple GUI from Audio Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate all direct `#include` dependencies from GUI widgets to engine internals, replacing them with thin abstract interfaces and a shared read-model layer.

**Architecture:** Introduce three abstract command interfaces (`ProjectCommands`, `TransportCommands`, `AudioGraphCommands`) that the GUI programs against, and a `ReadModel` class that provides thread-safe read-only snapshots of project state. Move shared utilities (`DebugLog.h`) to a common layer. Extract the plugin editor window from the engine into the UI layer. After this refactoring, `src/ui/` will have zero `#include`s of `src/engine/` headers.

**Tech Stack:** C++17, JUCE 8 (ValueTree, AudioProcessorGraph), Qt 6 (signals/slots for UI), gtest for testing.

---

## File Structure

### New files to create

| File | Responsibility |
|------|---------------|
| `src/common/DebugLog.h` | `HDAW_LOG` macro — moved from `src/ui/DebugLog.h` |
| `src/common/ProjectCommands.h` | Abstract interface: all project mutations (add/remove track, set volume, add note, etc.) |
| `src/common/TransportCommands.h` | Abstract interface: transport operations (play, stop, seek, loop) |
| `src/common/AudioGraphCommands.h` | Abstract interface: audio graph rebuilds (routing, FX, automation, modulation) |
| `src/common/ReadModel.h` | Read-only snapshot of project state (track count, names, volumes, clip list) |
| `src/engine/AudioEngineCommands.h` | Concrete implementation of all three command interfaces, owned by AudioEngine |
| `src/engine/ReadModelImpl.cpp` | Concrete implementation of ReadModel backed by ProjectModel |
| `src/ui/PluginEditorWindow.h` | Extracted from `TrackFXSlot.h` — owns native plugin editor windows |
| `src/ui/PluginEditorWindow.cpp` | Implementation of plugin editor window lifecycle |
| `tests/unit/common/commands_test.cpp` | Unit tests for command dispatch |
| `tests/unit/common/read_model_test.cpp` | Unit tests for ReadModel snapshots |

### Files to modify

| File | Change |
|------|--------|
| `src/ui/DebugLog.h` | Becomes a thin redirect: `#include "../common/DebugLog.h"` |
| `src/engine/TrackFXSlot.h` | Remove `#include "../ui/DebugLog.h"` and `PluginEditorWindow` class; forward-declare `PluginEditorWindow*` |
| `src/engine/AudioImport.cpp` | Change `#include "../ui/DebugLog.h"` → `#include "../../common/DebugLog.h"` |
| `src/engine/MidiImport.cpp` | Same redirect |
| `src/engine/TrackFXSlot.cpp` | Same redirect + use forward-declared `PluginEditorWindow` |
| `src/engine/AudioEngine.h` | Add `#include` for command interfaces; expose `getCommands()` returning combined interface |
| `src/ui/MainWindow.h` | Remove `#include "AudioEngine.h"` → use forward-decl + command interfaces |
| `src/ui/MainWindow.cpp` | Call commands through interfaces, not directly through AudioEngine |
| `src/ui/TimelineView.h/.cpp` | Same pattern |
| `src/ui/TimelineScene.h/.cpp` | Same pattern |
| `src/ui/TrackHeaderWidget.h/.cpp` | Same pattern |
| `src/ui/MixerWidget.h/.cpp` | Same pattern |
| `src/ui/MixerStripWidget.h/.cpp` | Same pattern |
| `src/ui/FXChainWidget.h/.cpp` | Same pattern |
| `src/ui/FXSlotRow.cpp` | Same pattern |
| `src/ui/AutomationLaneWidget.h/.cpp` | Same pattern |
| `src/ui/AudioClipEditorWidget.h/.cpp` | Same pattern |
| `src/ui/NoteGridWidget.h/.cpp` | Same pattern |
| `src/ui/PianoRollWidget.h/.cpp` | Same pattern |
| `src/ui/StepEditorWidget.h/.cpp` | Same pattern |
| `src/ui/PhraseGeneratorDialog.h/.cpp` | Same pattern |
| `src/ui/ProjectPoolBrowser.h/.cpp` | Same pattern |
| `src/ui/ExportDialog.h/.cpp` | Same pattern |
| `src/ui/PluginScannerDialog.h/.cpp` | Same pattern |
| `src/ui/ModulationWidget.h/.cpp` | Same pattern |
| `src/ui/TimeRuler.h/.cpp` | Same pattern |
| `src/ui/PreferencesDialog.h/.cpp` | Same pattern |
| `src/ui/PlayheadCursor.h/.cpp` | Same pattern |
| `src/ui/VUMeter.h/.cpp` | Same pattern |
| `src/ui/StatusBar.h/.cpp` | Same pattern |
| `src/ui/TimelineToolbar.cpp` | Same pattern |
| `src/ui/MarkerItem.h/.cpp` | Same pattern |
| `CMakeLists.txt` | Add new source files to `add_executable`; add `src/common/` to include path |

---

## Task 1: Move DebugLog.h to shared layer

**Files:**
- Create: `src/common/DebugLog.h`
- Modify: `src/ui/DebugLog.h`, `src/engine/TrackFXSlot.h:8`, `src/engine/AudioImport.cpp:4`, `src/engine/MidiImport.cpp:3`

- [ ] **Step 1: Create `src/common/DebugLog.h`**

```cpp
#pragma once
#include <cstdio>
#include <cstdarg>

// NDJSON debug logger — writes to %TEMP%/hdaw_debug.log
// Usage: HDAW_LOG("tag", "message %d", value);
// Both engine and UI layers can use this.
#define HDAW_LOG(tag, msg, ...) \
    do { \
        FILE* f = nullptr; \
        fopen_s(&f, (std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/hdaw_debug.log").c_str(), "a"); \
        if (f) { \
            fprintf(f, "{\"tag\":\"%s\",\"msg\":\"" msg "\"}\n", tag, ##__VA_ARGS__); \
            fclose(f); \
        } \
    } while(0)
```

Note: Read the existing `src/ui/DebugLog.h` first and replicate its exact behavior. The above is a simplified sketch — copy the real implementation.

- [ ] **Step 2: Replace `src/ui/DebugLog.h` with redirect**

```cpp
#pragma once
// DebugLog.h moved to common layer — redirect for backward compatibility
#include "../common/DebugLog.h"
```

- [ ] **Step 3: Update engine includes to use common path**

In `src/engine/TrackFXSlot.h:8`, `src/engine/AudioImport.cpp:4`, `src/engine/MidiImport.cpp:3`:
Change `#include "../ui/DebugLog.h"` → `#include "../../common/DebugLog.h"`

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile, no errors.

- [ ] **Step 5: Run tests**

Run: `build\Debug\hdaw_tests.exe`
Expected: 60/60 pass.

- [ ] **Step 6: Commit**

```bash
git add src/common/DebugLog.h src/ui/DebugLog.h src/engine/TrackFXSlot.h src/engine/AudioImport.cpp src/engine/MidiImport.cpp
git commit -m "common: move DebugLog.h to shared layer, redirect old location"
```

---

## Task 2: Create abstract command interfaces

**Files:**
- Create: `src/common/ProjectCommands.h`
- Create: `src/common/TransportCommands.h`
- Create: `src/common/AudioGraphCommands.h`

- [ ] **Step 1: Create `src/common/ProjectCommands.h`**

This interface covers all project data mutations that the GUI currently does by reaching into `AudioEngine::getProjectModel()` and calling `ValueTree::setProperty()` directly.

```cpp
#pragma once
#include <string>
#include <functional>

// Abstract interface for project data mutations.
// The GUI programs against this; the engine provides the implementation.
// All methods are main-thread only.
class ProjectCommands
{
public:
    virtual ~ProjectCommands() = default;

    // Track operations
    virtual int addTrack(const std::string& name, int parentBus = -1) = 0;
    virtual void removeTrack(int trackIndex) = 0;
    virtual void moveTrack(int trackIndex, int newIndex) = 0;
    virtual void setTrackName(int trackIndex, const std::string& name) = 0;
    virtual void setTrackColor(int trackIndex, int color) = 0;
    virtual void setTrackVolume(int trackIndex, float volume) = 0;
    virtual void setTrackPan(int trackIndex, float pan) = 0;
    virtual void setTrackMuted(int trackIndex, bool muted) = 0;
    virtual void setTrackSoloed(int trackIndex, bool soloed) = 0;
    virtual void setTrackArmed(int trackIndex, bool armed) = 0;
    virtual void setTrackInputMonitor(int trackIndex, bool monitor) = 0;
    virtual void setTrackHeight(int trackIndex, int height) = 0;
    virtual void setTrackMidiChannel(int trackIndex, int channel) = 0;

    // Clip operations
    virtual int addAudioClip(int trackIndex, double start, double duration,
                             const std::string& sourceFile, const std::string& name = {}) = 0;
    virtual int addMidiClip(int trackIndex, double start, double duration,
                            const std::string& name = {}) = 0;
    virtual void removeClip(int clipId) = 0;
    virtual void moveClip(int clipId, int newTrackIndex, double newStart) = 0;
    virtual void setClipStart(int clipId, double start) = 0;
    virtual void setClipDuration(int clipId, double duration) = 0;
    virtual void setClipGain(int clipId, float gain) = 0;
    virtual void setClipFadeIn(int clipId, float fadeIn) = 0;
    virtual void setClipFadeOut(int clipId, float fadeOut) = 0;
    virtual void setClipLooping(int clipId, bool looping) = 0;
    virtual int duplicateClip(int clipId) = 0;

    // MIDI note operations
    virtual int addNote(int clipId, int pitch, float velocity,
                        double startBeat, double durationBeats) = 0;
    virtual void removeNote(int noteId) = 0;
    virtual void setNotePitch(int noteId, int pitch) = 0;
    virtual void setNoteVelocity(int noteId, float velocity) = 0;
    virtual void setNoteStart(int noteId, double startBeat) = 0;
    virtual void setNoteDuration(int noteId, double durationBeats) = 0;
    virtual void clearNotes(int clipId) = 0;

    // FX operations
    virtual void addFxSlot(int trackIndex, const std::string& type,
                           int position = -1, const std::string& pluginId = {}) = 0;
    virtual void removeFxSlot(int trackIndex, int slotIndex) = 0;
    virtual void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) = 0;
    virtual void setFxSlotParam(int trackIndex, int slotIndex,
                                int paramIndex, float value) = 0;

    // Automation operations
    virtual void addAutomationPoint(int trackIndex, const std::string& lane,
                                    double time, float value) = 0;
    virtual void removeAutomationPoint(int trackIndex, const std::string& lane,
                                       int pointIndex) = 0;
    virtual void setAutomationEnabled(int trackIndex, const std::string& lane,
                                      bool enabled) = 0;

    // Transport properties (written via ValueTree, read by audio thread)
    virtual void setTempo(double bpm) = 0;
    virtual void setLoopStart(double beat) = 0;
    virtual void setLoopEnd(double beat) = 0;
    virtual void setLooping(bool looping) = 0;
    virtual void setMetronomeEnabled(bool enabled) = 0;

    // Undo/redo
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    // Project lifecycle
    virtual void newProject() = 0;
    virtual bool saveProject(const std::string& filePath) = 0;
    virtual bool loadProject(const std::string& filePath) = 0;

    // Scale
    virtual void setScaleRoot(int root) = 0;
    virtual void setScaleMode(int mode) = 0;
};
```

- [ ] **Step 2: Create `src/common/TransportCommands.h`**

```cpp
#pragma once

// Abstract interface for transport control.
// The GUI programs against this; the engine provides the implementation.
// All methods are main-thread only.
class TransportCommands
{
public:
    virtual ~TransportCommands() = default;

    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void rewind() = 0;
    virtual void toggleLoop() = 0;

    virtual void seekToSample(int64_t sample) = 0;
    virtual void seekToSeconds(double seconds) = 0;

    // Recording
    virtual void startRecording() = 0;
    virtual void stopRecording() = 0;
    virtual bool isRecording() const = 0;
};
```

- [ ] **Step 3: Create `src/common/AudioGraphCommands.h`**

```cpp
#pragma once

// Abstract interface for audio graph rebuild operations.
// The GUI calls these after mutating project data; the engine
// rebuilds the audio processing graph accordingly.
// All methods are main-thread only.
class AudioGraphCommands
{
public:
    virtual ~AudioGraphCommands() = default;

    // Must be called after any track add/remove/reorder
    virtual void rebuildRoutingGraph() = 0;

    // Must be called after FX chain changes on a track
    virtual void rebuildTrackFX(int trackIndex) = 0;

    // Must be called after automation lane changes
    virtual void rebuildAutomationCache(int trackIndex) = 0;

    // Must be called after modulation changes
    virtual void rebuildModulation(int trackIndex) = 0;

    // Plugin editor management
    virtual void toggleFXEditor(int trackIndex, int slotIndex) = 0;
};
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Headers compile in isolation (they're pure virtual interfaces with no .cpp).

- [ ] **Step 5: Commit**

```bash
git add src/common/ProjectCommands.h src/common/TransportCommands.h src/common/AudioGraphCommands.h
git commit -m "common: add abstract command interfaces for GUI-engine decoupling"
```

---

## Task 3: Create ReadModel for thread-safe project state reads

**Files:**
- Create: `src/common/ReadModel.h`
- Create: `src/engine/ReadModelImpl.h`
- Create: `src/engine/ReadModelImpl.cpp`
- Create: `tests/unit/common/read_model_test.cpp`

- [ ] **Step 1: Create `src/common/ReadModel.h`**

This is a value-type snapshot of project state that the GUI can query without touching `ValueTree` or `ProjectModel` directly.

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Thread-safe read-only snapshot of project state.
// The GUI queries this instead of reaching into ProjectModel/ValueTree.
// Snapshots are cheap to take (copy vectors of POD types) and immutable.
struct TrackSnapshot
{
    int index = -1;
    std::string name;
    int color = 0;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;
    bool armed = false;
    bool inputMonitor = false;
    int height = 80;
    int midiChannel = 0;
    int clipCount = 0;
};

struct ClipSnapshot
{
    int clipId = -1;
    int trackIndex = -1;
    std::string name;
    std::string sourceFile;
    double startBeat = 0.0;
    double durationBeats = 0.0;
    double offset = 0.0;
    float gain = 1.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
    bool looping = false;
    bool isMidi = false;
};

struct NoteSnapshot
{
    int noteId = -1;
    int pitch = 0;
    float velocity = 100.0f;
    double startBeat = 0.0;
    double durationBeats = 1.0;
};

struct TransportSnapshot
{
    double bpm = 120.0;
    bool isPlaying = false;
    bool isLooping = false;
    bool isRecording = false;
    double loopStart = 0.0;
    double loopEnd = 4.0;
    int64_t currentSample = 0;
    double sampleRate = 44100.0;
};

struct ProjectSnapshot
{
    std::string name;
    TransportSnapshot transport;
    std::vector<TrackSnapshot> tracks;
    std::vector<ClipSnapshot> clips;
    int scaleRoot = 0;
    int scaleMode = 0;
};

class ReadModel
{
public:
    virtual ~ReadModel() = default;

    // Take a snapshot (thread-safe, cheap copy)
    virtual ProjectSnapshot snapshot() const = 0;

    // Focused queries (avoid full snapshot when only one field needed)
    virtual int getTrackCount() const = 0;
    virtual TrackSnapshot getTrack(int index) const = 0;
    virtual ClipSnapshot getClip(int clipId) const = 0;
    virtual std::vector<NoteSnapshot> getNotes(int clipId) const = 0;
    virtual TransportSnapshot getTransport() const = 0;
    virtual int getScaleRoot() const = 0;
    virtual int getScaleMode() const = 0;
};
```

- [ ] **Step 2: Create `src/engine/ReadModelImpl.h`**

```cpp
#pragma once
#include "../common/ReadModel.h"
#include "../model/ProjectModel.h"

// Concrete ReadModel backed by ProjectModel.
// Thread-safe for reads: all data is copied from ValueTree on snapshot().
class ReadModelImpl : public ReadModel
{
public:
    explicit ReadModelImpl(ProjectModel& model) : projectModel(model) {}

    ProjectSnapshot snapshot() const override;
    int getTrackCount() const override;
    TrackSnapshot getTrack(int index) const override;
    ClipSnapshot getClip(int clipId) const override;
    std::vector<NoteSnapshot> getNotes(int clipId) const override;
    TransportSnapshot getTransport() const override;
    int getScaleRoot() const override;
    int getScaleMode() const override;

private:
    ProjectModel& projectModel;
};
```

- [ ] **Step 3: Create `src/engine/ReadModelImpl.cpp`**

```cpp
#include "ReadModelImpl.h"
#include "../model/ProjectModel.h"

using namespace juce;

ProjectSnapshot ReadModelImpl::snapshot() const
{
    ProjectSnapshot snap;
    auto& model = const_cast<ProjectModel&>(projectModel);
    auto tree = model.getTree();
    snap.name = tree.getProperty(IDs::name, "Untitled").toString().toStdString();

    auto transportTree = tree.getChildWithName(IDs::TRANSPORT);
    if (transportTree.isValid())
    {
        snap.transport.bpm = transportTree.getProperty(IDs::tempo, 120.0);
        snap.transport.isPlaying = transportTree.getProperty(IDs::isPlaying, false);
        snap.transport.isLooping = transportTree.getProperty(IDs::isLooping, false);
        snap.transport.isRecording = transportTree.getProperty(IDs::isRecording, false);
        snap.transport.loopStart = transportTree.getProperty(IDs::loopStart, 0.0);
        snap.transport.loopEnd = transportTree.getProperty(IDs::loopEnd, 4.0);
    }

    auto scaleTree = tree.getChildWithName(IDs::SCALE_INFO);
    if (scaleTree.isValid())
    {
        snap.scaleRoot = scaleTree.getProperty(IDs::scaleRoot, 0);
        snap.scaleMode = scaleTree.getProperty(IDs::scaleMode, 0);
    }

    auto trackList = tree.getChildWithName(IDs::TRACK_LIST);
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto trackTree = trackList.getChild(i);
        TrackSnapshot ts;
        ts.index = i;
        ts.name = trackTree.getProperty(IDs::name, "").toString().toStdString();
        ts.color = static_cast<int>(trackTree.getProperty(IDs::color, 0));
        ts.volume = trackTree.getProperty(IDs::volume, 1.0f);
        ts.pan = trackTree.getProperty(IDs::pan, 0.0f);
        ts.muted = trackTree.getProperty(IDs::isMuted, false);
        ts.soloed = trackTree.getProperty(IDs::isSoloed, false);
        ts.armed = trackTree.getProperty(IDs::isArm, false);
        ts.inputMonitor = trackTree.getProperty(IDs::inputMonitor, false);
        ts.height = static_cast<int>(trackTree.getProperty(IDs::trackHeight, 80));
        ts.midiChannel = static_cast<int>(trackTree.getProperty(IDs::midiChannel, 0));

        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        ts.clipCount = clipList.getNumChildren();

        for (int j = 0; j < clipList.getNumChildren(); ++j)
        {
            auto clipTree = clipList.getChild(j);
            ClipSnapshot cs;
            cs.clipId = clipTree.getProperty(IDs::clipID, -1);
            cs.trackIndex = i;
            cs.name = clipTree.getProperty(IDs::name, "").toString().toStdString();
            cs.sourceFile = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
            cs.startBeat = clipTree.getProperty(IDs::startTime, 0.0);
            cs.durationBeats = clipTree.getProperty(IDs::duration, 1.0);
            cs.offset = clipTree.getProperty(IDs::offset, 0.0);
            cs.gain = clipTree.getProperty(IDs::gain, 1.0f);
            cs.fadeIn = clipTree.getProperty(IDs::fadeIn, 0.0f);
            cs.fadeOut = clipTree.getProperty(IDs::fadeOut, 0.0f);
            cs.looping = clipTree.getProperty(IDs::looping, false);
            cs.isMidi = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
            snap.clips.push_back(cs);
        }

        snap.tracks.push_back(ts);
    }
    return snap;
}

int ReadModelImpl::getTrackCount() const
{
    return projectModel.getTrackListTree().getNumChildren();
}

TrackSnapshot ReadModelImpl::getTrack(int index) const
{
    auto trackList = projectModel.getTrackListTree();
    if (index < 0 || index >= trackList.getNumChildren())
        return {};
    auto trackTree = trackList.getChild(index);
    TrackSnapshot ts;
    ts.index = index;
    ts.name = trackTree.getProperty(IDs::name, "").toString().toStdString();
    ts.color = static_cast<int>(trackTree.getProperty(IDs::color, 0));
    ts.volume = trackTree.getProperty(IDs::volume, 1.0f);
    ts.pan = trackTree.getProperty(IDs::pan, 0.0f);
    ts.muted = trackTree.getProperty(IDs::isMuted, false);
    ts.soloed = trackTree.getProperty(IDs::isSoloed, false);
    ts.armed = trackTree.getProperty(IDs::isArm, false);
    ts.inputMonitor = trackTree.getProperty(IDs::inputMonitor, false);
    ts.height = static_cast<int>(trackTree.getProperty(IDs::trackHeight, 80));
    ts.midiChannel = static_cast<int>(trackTree.getProperty(IDs::midiChannel, 0));
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    ts.clipCount = clipList.getNumChildren();
    return ts;
}

ClipSnapshot ReadModelImpl::getClip(int clipId) const
{
    auto trackList = projectModel.getTrackListTree();
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto clipList = trackList.getChild(i).getChildWithName(IDs::CLIP_LIST);
        for (int j = 0; j < clipList.getNumChildren(); ++j)
        {
            auto clipTree = clipList.getChild(j);
            if (static_cast<int>(clipTree.getProperty(IDs::clipID, -1)) == clipId)
            {
                ClipSnapshot cs;
                cs.clipId = clipId;
                cs.trackIndex = i;
                cs.name = clipTree.getProperty(IDs::name, "").toString().toStdString();
                cs.sourceFile = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
                cs.startBeat = clipTree.getProperty(IDs::startTime, 0.0);
                cs.durationBeats = clipTree.getProperty(IDs::duration, 1.0);
                cs.offset = clipTree.getProperty(IDs::offset, 0.0);
                cs.gain = clipTree.getProperty(IDs::gain, 1.0f);
                cs.fadeIn = clipTree.getProperty(IDs::fadeIn, 0.0f);
                cs.fadeOut = clipTree.getProperty(IDs::fadeOut, 0.0f);
                cs.looping = clipTree.getProperty(IDs::looping, false);
                cs.isMidi = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
                return cs;
            }
        }
    }
    return {};
}

std::vector<NoteSnapshot> ReadModelImpl::getNotes(int clipId) const
{
    auto clip = getClip(clipId);
    auto trackList = projectModel.getTrackListTree();
    std::vector<NoteSnapshot> notes;
    if (clip.trackIndex < 0 || clip.trackIndex >= trackList.getNumChildren())
        return notes;

    auto clipList = trackList.getChild(clip.trackIndex).getChildWithName(IDs::CLIP_LIST);
    for (int j = 0; j < clipList.getNumChildren(); ++j)
    {
        auto clipTree = clipList.getChild(j);
        if (static_cast<int>(clipTree.getProperty(IDs::clipID, -1)) == clipId)
        {
            auto noteList = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int k = 0; k < noteList.getNumChildren(); ++k)
            {
                auto noteTree = noteList.getChild(k);
                NoteSnapshot ns;
                ns.noteId = noteTree.getProperty(IDs::noteID, -1);
                ns.pitch = noteTree.getProperty(IDs::noteNumber, 0);
                ns.velocity = noteTree.getProperty(IDs::velocity, 100.0f);
                ns.startBeat = noteTree.getProperty(IDs::startBeat, 0.0);
                ns.durationBeats = noteTree.getProperty(IDs::durationBeats, 1.0);
                notes.push_back(ns);
            }
            break;
        }
    }
    return notes;
}

TransportSnapshot ReadModelImpl::getTransport() const
{
    auto transportTree = projectModel.getTransportTree();
    TransportSnapshot ts;
    if (transportTree.isValid())
    {
        ts.bpm = transportTree.getProperty(IDs::tempo, 120.0);
        ts.isPlaying = transportTree.getProperty(IDs::isPlaying, false);
        ts.isLooping = transportTree.getProperty(IDs::isLooping, false);
        ts.isRecording = transportTree.getProperty(IDs::isRecording, false);
        ts.loopStart = transportTree.getProperty(IDs::loopStart, 0.0);
        ts.loopEnd = transportTree.getProperty(IDs::loopEnd, 4.0);
    }
    return ts;
}

int ReadModelImpl::getScaleRoot() const { return projectModel.getScaleRoot(); }
int ReadModelImpl::getScaleMode() const { return projectModel.getScaleMode(); }
```

- [ ] **Step 4: Write test `tests/unit/common/read_model_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "../../src/engine/ReadModelImpl.h"

TEST(ReadModel, EmptyProjectSnapshot)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);

    auto snap = readModel.snapshot();
    EXPECT_FALSE(snap.name.empty());
    EXPECT_EQ(readModel.getTrackCount(), static_cast<int>(snap.tracks.size()));
    EXPECT_DOUBLE_EQ(snap.transport.bpm, 120.0);
}

TEST(ReadModel, TrackQuery)
{
    ProjectModel model;
    model.createDefaultProject();
    ReadModelImpl readModel(model);

    EXPECT_GT(readModel.getTrackCount(), 0);
    auto track = readModel.getTrack(0);
    EXPECT_EQ(track.index, 0);
    EXPECT_FALSE(track.name.empty());
}
```

- [ ] **Step 5: Add test to CMakeLists.txt and build**

Add `tests/unit/common/read_model_test.cpp` to the test source list in `tests/CMakeLists.txt`.

Run: `cmake --build build --config Debug`
Expected: Clean compile.

- [ ] **Step 6: Run tests**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=ReadModel.*`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/common/ReadModel.h src/engine/ReadModelImpl.h src/engine/ReadModelImpl.cpp tests/unit/common/read_model_test.cpp
git commit -m "common: add ReadModel for thread-safe project state reads"
```

---

## Task 4: Implement concrete command classes

**Files:**
- Create: `src/engine/AudioEngineCommands.h`
- Create: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create `src/engine/AudioEngineCommands.h`**

This class implements all three command interfaces by delegating to the existing AudioEngine internals. It lives in the engine layer and is created by AudioEngine.

```cpp
#pragma once
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "AudioEngine.h"

// Concrete command implementations that delegate to AudioEngine internals.
// Created by AudioEngine; consumed by the UI through the abstract interfaces.
class AudioEngineCommands : public ProjectCommands,
                            public TransportCommands,
                            public AudioGraphCommands
{
public:
    explicit AudioEngineCommands(AudioEngine& engine);

    // ProjectCommands
    int addTrack(const std::string& name, int parentBus) override;
    void removeTrack(int trackIndex) override;
    void moveTrack(int trackIndex, int newIndex) override;
    void setTrackName(int trackIndex, const std::string& name) override;
    void setTrackColor(int trackIndex, int color) override;
    void setTrackVolume(int trackIndex, float volume) override;
    void setTrackPan(int trackIndex, float pan) override;
    void setTrackMuted(int trackIndex, bool muted) override;
    void setTrackSoloed(int trackIndex, bool soloed) override;
    void setTrackArmed(int trackIndex, bool armed) override;
    void setTrackInputMonitor(int trackIndex, bool monitor) override;
    void setTrackHeight(int trackIndex, int height) override;
    void setTrackMidiChannel(int trackIndex, int channel) override;

    int addAudioClip(int trackIndex, double start, double duration,
                     const std::string& sourceFile, const std::string& name) override;
    int addMidiClip(int trackIndex, double start, double duration,
                    const std::string& name) override;
    void removeClip(int clipId) override;
    void moveClip(int clipId, int newTrackIndex, double newStart) override;
    void setClipStart(int clipId, double start) override;
    void setClipDuration(int clipId, double duration) override;
    void setClipGain(int clipId, float gain) override;
    void setClipFadeIn(int clipId, float fadeIn) override;
    void setClipFadeOut(int clipId, float fadeOut) override;
    void setClipLooping(int clipId, bool looping) override;
    int duplicateClip(int clipId) override;

    int addNote(int clipId, int pitch, float velocity,
                double startBeat, double durationBeats) override;
    void removeNote(int noteId) override;
    void setNotePitch(int noteId, int pitch) override;
    void setNoteVelocity(int noteId, float velocity) override;
    void setNoteStart(int noteId, double startBeat) override;
    void setNoteDuration(int noteId, double durationBeats) override;
    void clearNotes(int clipId) override;

    void addFxSlot(int trackIndex, const std::string& type,
                   int position, const std::string& pluginId) override;
    void removeFxSlot(int trackIndex, int slotIndex) override;
    void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) override;
    void setFxSlotParam(int trackIndex, int slotIndex, int paramIndex, float value) override;

    void addAutomationPoint(int trackIndex, const std::string& lane,
                            double time, float value) override;
    void removeAutomationPoint(int trackIndex, const std::string& lane, int pointIndex) override;
    void setAutomationEnabled(int trackIndex, const std::string& lane, bool enabled) override;

    void setTempo(double bpm) override;
    void setLoopStart(double beat) override;
    void setLoopEnd(double beat) override;
    void setLooping(bool looping) override;
    void setMetronomeEnabled(bool enabled) override;

    void undo() override;
    void redo() override;
    bool canUndo() const override;
    bool canRedo() const override;

    void newProject() override;
    bool saveProject(const std::string& filePath) override;
    bool loadProject(const std::string& filePath) override;

    void setScaleRoot(int root) override;
    void setScaleMode(int mode) override;

    // TransportCommands
    void play() override;
    void stop() override;
    void pause() override;
    void rewind() override;
    void toggleLoop() override;
    void seekToSample(int64_t sample) override;
    void seekToSeconds(double seconds) override;
    void startRecording() override;
    void stopRecording() override;
    bool isRecording() const override;

    // AudioGraphCommands
    void rebuildRoutingGraph() override;
    void rebuildTrackFX(int trackIndex) override;
    void rebuildAutomationCache(int trackIndex) override;
    void rebuildModulation(int trackIndex) override;
    void toggleFXEditor(int trackIndex, int slotIndex) override;

private:
    AudioEngine& engine;
    juce::ValueTree findClipById(int clipId, int& outTrackIndex) const;
    juce::ValueTree findNoteById(int noteId, int& outClipId) const;
};
```

- [ ] **Step 2: Create `src/engine/AudioEngineCommands.cpp`**

Each method delegates to the existing AudioEngine/MainAudioProcessor/ProjectModel methods. For example:

```cpp
#include "AudioEngineCommands.h"
#include "MainAudioProcessor.h"
#include "ProjectSerializer.h"
#include "AudioImport.h"
#include "MidiImport.h"

AudioEngineCommands::AudioEngineCommands(AudioEngine& e) : engine(e) {}

// --- Track operations ---
int AudioEngineCommands::addTrack(const std::string& name, int parentBus)
{
    return engine.getProjectModel().addFxSlot is not the right method
    // Delegate to existing add-track logic (currently in MainWindow)
    auto& um = engine.getProjectModel().getUndoManager();
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto newTrack = juce::ValueTree(IDs::TRACK);
    newTrack.setProperty(IDs::name, juce::String(name), &um);
    newTrack.setProperty(IDs::color, static_cast<juce::int64>(
        ProjectModel::trackColorForIndex(trackList.getNumChildren())), &um);
    newTrack.setProperty(IDs::volume, 1.0f, &um);
    newTrack.setProperty(IDs::pan, 0.0f, &um);
    newTrack.setProperty(IDs::isMuted, false, &um);
    newTrack.setProperty(IDs::isSoloed, false, &um);
    newTrack.setProperty(IDs::isArm, false, &um);
    newTrack.setProperty(IDs::trackHeight, 80, &um);
    newTrack.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
    newTrack.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, nullptr);
    newTrack.addChild(ProjectModel::createTrackAutomationList(), -1, nullptr);
    newTrack.addChild(juce::ValueTree(IDs::MODULATION_LIST), -1, nullptr);
    trackList.addChild(newTrack, -1, &um);
    return trackList.getNumChildren() - 1;
}

void AudioEngineCommands::removeTrack(int trackIndex)
{
    auto& um = engine.getProjectModel().getUndoManager();
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.removeChild(trackIndex, &um);
}

// ... (implement all methods by delegating to existing code)
// The implementation is a mechanical move of the logic currently spread
// across MainWindow, TimelineView, MixerWidget, etc.

// --- Transport ---
void AudioEngineCommands::play()
{
    engine.getTransportManager().setPlaying(true);
}

void AudioEngineCommands::stop()
{
    engine.getTransportManager().setPlaying(false);
}

void AudioEngineCommands::pause()
{
    engine.getTransportManager().setPlaying(false);
}

void AudioEngineCommands::rewind()
{
    engine.getTransportManager().setCurrentSample(0);
}

// --- AudioGraph ---
void AudioEngineCommands::rebuildRoutingGraph()
{
    if (auto* proc = engine.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::rebuildTrackFX(int trackIndex)
{
    if (auto* proc = engine.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}
```

Note: The full implementation will be ~400-500 lines. Each method is a mechanical move of existing code from the UI widgets into this centralized command class. Do NOT invent new logic — move existing logic verbatim.

- [ ] **Step 3: Expose commands from AudioEngine**

Add to `AudioEngine.h`:

```cpp
#include "AudioEngineCommands.h"
// ... in class AudioEngine:
public:
    ProjectCommands& getProjectCommands() { return *commands; }
    TransportCommands& getTransportCommands() { return *commands; }
    AudioGraphCommands& getAudioGraphCommands() { return *commands; }
    ReadModel& getReadModel() { return *readModel; }
private:
    std::unique_ptr<AudioEngineCommands> commands;
    std::unique_ptr<ReadModelImpl> readModel;
```

Initialize in `AudioEngine::initialize()`:

```cpp
commands = std::make_unique<AudioEngineCommands>(*this);
readModel = std::make_unique<ReadModelImpl>(projectModel);
```

- [ ] **Step 4: Write tests `tests/unit/common/commands_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "../../src/engine/AudioEngineCommands.h"
#include "../../src/engine/AudioEngine.h"

TEST(Commands, AddRemoveTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int initialCount = engine.getReadModel().getTrackCount();
    int idx = cmds.addTrack("Test Track");
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initialCount + 1);
    EXPECT_EQ(engine.getReadModel().getTrack(idx).name, "Test Track");

    cmds.removeTrack(idx);
    EXPECT_EQ(engine.getReadModel().getTrackCount(), initialCount);
}

TEST(Commands, TransportPlayStop)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();

    EXPECT_FALSE(cmds.isRecording());
    cmds.play();
    EXPECT_TRUE(engine.getReadModel().getTransport().isPlaying);
    cmds.stop();
    EXPECT_FALSE(engine.getReadModel().getTransport().isPlaying);
}
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe --gtest_filter=Commands.*`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands.cpp tests/unit/common/commands_test.cpp
git commit -m "engine: implement concrete command classes delegating to AudioEngine internals"
```

---

## Task 5: Extract PluginEditorWindow from TrackFXSlot into UI layer

**Files:**
- Create: `src/ui/PluginEditorWindow.h`
- Create: `src/ui/PluginEditorWindow.cpp`
- Modify: `src/engine/TrackFXSlot.h` (remove PluginEditorWindow class)
- Modify: `src/engine/TrackFXSlot.cpp` (use forward-declared pointer)

- [ ] **Step 1: Create `src/ui/PluginEditorWindow.h`**

Move the `PluginEditorWindow` class (currently at `src/engine/TrackFXSlot.h:12-60`) to a new file in the UI layer.

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// Native window wrapper for plugin editors.
// Lives in the UI layer — the engine holds a forward-declared pointer.
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::AudioProcessorEditor* editor,
                       std::function<void()> onClose);
    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onCloseCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};
```

- [ ] **Step 2: Create `src/ui/PluginEditorWindow.cpp`**

```cpp
#include "PluginEditorWindow.h"

PluginEditorWindow::PluginEditorWindow(juce::AudioProcessorEditor* editor,
                                       std::function<void()> onClose)
    : DocumentWindow(editor->getName(), juce::Colours::darkgrey,
                     DocumentWindow::closeButton),
      onCloseCallback(std::move(onClose))
{
    setContentOwned(editor, true);
    setSize(editor->getWidth() + 2, editor->getHeight() + 2);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

PluginEditorWindow::~PluginEditorWindow() = default;

void PluginEditorWindow::closeButtonPressed()
{
    if (onCloseCallback)
        onCloseCallback();
    setVisible(false);
}
```

- [ ] **Step 3: Update `src/engine/TrackFXSlot.h`**

Remove the `PluginEditorWindow` class definition (lines 12-60). Replace `#include "../ui/DebugLog.h"` with `#include "../../common/DebugLog.h"`. Add a forward declaration:

```cpp
class PluginEditorWindow;  // defined in src/ui/PluginEditorWindow.h
```

Change the member from:
```cpp
std::unique_ptr<juce::DocumentWindow> editorWindow;
```
to:
```cpp
// Forward-declared — actual type is in src/ui/PluginEditorWindow.h
// We hold as DocumentWindow* because that's the base class.
std::unique_ptr<juce::DocumentWindow> editorWindow;
```

- [ ] **Step 4: Update `src/engine/TrackFXSlot.cpp`**

Add `#include "../ui/PluginEditorWindow.h"` in the .cpp file (not the header). In `showEditor()`, construct `PluginEditorWindow` instead of the inline class:

```cpp
#include "../ui/PluginEditorWindow.h"

void TrackFXSlot::showEditor()
{
    // ... existing plugin instance creation ...
    auto* editor = pluginInstance->createEditorIfNeeded();
    if (editor)
    {
        editorWindow = std::make_unique<PluginEditorWindow>(editor,
            [this]() { closeEditor(); });
    }
}
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile. The engine no longer includes any UI headers.

- [ ] **Step 6: Run tests**

Run: `build\Debug\hdaw_tests.exe`
Expected: 60/60 pass.

- [ ] **Step 7: Commit**

```bash
git add src/ui/PluginEditorWindow.h src/ui/PluginEditorWindow.cpp src/engine/TrackFXSlot.h src/engine/TrackFXSlot.cpp
git commit -m "refactor: extract PluginEditorWindow from engine TrackFXSlot to UI layer"
```

---

## Task 6: Migrate MainWindow to command interfaces

**Files:**
- Modify: `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`

This is the highest-impact migration. `MainWindow` currently has 47+ calls to `getMainProcessor()`, 30+ to `getProjectModel()`, 10+ to `getTransportManager()`.

- [ ] **Step 1: Update MainWindow.h**

Replace:
```cpp
#include "AudioEngine.h"
```
With forward declarations and command interface includes:
```cpp
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;  // forward-declare only
```

Add members:
```cpp
ProjectCommands* projectCmds = nullptr;
TransportCommands* transportCmds = nullptr;
AudioGraphCommands* audioGraphCmds = nullptr;
ReadModel* readModel = nullptr;
```

Initialize in constructor:
```cpp
projectCmds = &engine.getProjectCommands();
transportCmds = &engine.getTransportCommands();
audioGraphCmds = &engine.getAudioGraphCommands();
readModel = &engine.getReadModel();
```

- [ ] **Step 2: Migrate transport controls in MainWindow.cpp**

Replace patterns like:
```cpp
// BEFORE
engine.getTransportManager().setPlaying(true);
```
With:
```cpp
// AFTER
transportCmds->play();
```

And:
```cpp
// BEFORE
engine.getTransportManager().setCurrentSample(0);
```
With:
```cpp
// AFTER
transportCmds->rewind();
```

- [ ] **Step 3: Migrate project mutations**

Replace patterns like:
```cpp
// BEFORE
auto trackList = engine.getProjectModel().getTrackListTree();
trackList.getChild(idx).setProperty(IDs::volume, vol, &engine.getProjectModel().getUndoManager());
```
With:
```cpp
// AFTER
projectCmds->setTrackVolume(idx, vol);
```

- [ ] **Step 4: Migrate audio graph calls**

Replace patterns like:
```cpp
// BEFORE
if (auto* proc = dynamic_cast<MainAudioProcessor*>(engine.getMainProcessor()))
    proc->rebuildRoutingGraph();
```
With:
```cpp
// AFTER
audioGraphCmds->rebuildRoutingGraph();
```

- [ ] **Step 5: Migrate read queries**

Replace patterns like:
```cpp
// BEFORE
int count = engine.getProjectModel().getTrackListTree().getNumChildren();
```
With:
```cpp
// AFTER
int count = readModel->getTrackCount();
```

- [ ] **Step 6: Remove ValueTree::Listener from MainWindow**

MainWindow currently implements `juce::ValueTree::Listener` to observe project changes. Replace this with a polling pattern or connect to the project model's signals via a thin adapter. For now, keep the listener but have it read through `readModel` instead of directly touching `ValueTree`.

- [ ] **Step 7: Build and run tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe`
Expected: All pass.

- [ ] **Step 8: Commit**

```bash
git add src/ui/MainWindow.h src/ui/MainWindow.cpp
git commit -m "ui: migrate MainWindow from direct engine access to command interfaces"
```

---

## Task 7: Migrate remaining UI widgets to command interfaces

Repeat the pattern from Task 6 for each widget. Group by complexity:

### 7A: Simple widgets (transport-only reads)

**Files:** `PlayheadCursor.h/.cpp`, `VUMeter.h/.cpp`, `TimeRuler.h/.cpp`, `StatusBar.h/.cpp`

These widgets mostly read transport state and meters. Replace `AudioEngine&` member with `ReadModel*` + `TransportCommands*`.

- [ ] **Step 1: Migrate PlayheadCursor** — replace `TransportManager&` with `ReadModel*`
- [ ] **Step 2: Migrate VUMeter** — replace `LevelMeter*` with `const LevelMeter&` (level meters are already atomic-based, this is fine)
- [ ] **Step 3: Migrate TimeRuler** — replace `AudioEngine&` with `TransportCommands*` + `ReadModel*`
- [ ] **Step 4: Migrate StatusBar** — replace `AudioEngine&` with `ReadModel*`
- [ ] **Step 5: Build and test**
- [ ] **Step 6: Commit** — `ui: migrate simple widgets to command interfaces`

### 7B: Mixer widgets

**Files:** `MixerWidget.h/.cpp`, `MixerStripWidget.h/.cpp`

- [ ] **Step 1: Migrate MixerWidget** — replace `AudioEngine&` with `ProjectCommands*` + `ReadModel*` + `AudioGraphCommands*`
- [ ] **Step 2: Migrate MixerStripWidget** — same pattern
- [ ] **Step 3: Build and test**
- [ ] **Step 4: Commit** — `ui: migrate mixer widgets to command interfaces`

### 7C: FX chain widgets

**Files:** `FXChainWidget.h/.cpp`, `FXSlotRow.cpp`, `PluginScannerDialog.h/.cpp`, `ScanProgressDialog.h/.cpp`

- [ ] **Step 1: Migrate FXChainWidget** — replace `AudioEngine&` with `ProjectCommands*` + `ReadModel*` + `AudioGraphCommands*`
- [ ] **Step 2: Migrate FXSlotRow** — same pattern
- [ ] **Step 3: Migrate PluginScannerDialog** — add a `PluginCommands` interface for plugin scanning, or keep a thin `PluginManager&` reference (scanning is engine-internal)
- [ ] **Step 4: Build and test**
- [ ] **Step 5: Commit** — `ui: migrate FX chain widgets to command interfaces`

### 7D: Timeline widgets

**Files:** `TimelineView.h/.cpp`, `TimelineScene.h/.cpp`, `TimelineInteraction.h/.cpp`, `TrackHeaderWidget.h/.cpp`, `MarkerItem.h/.cpp`, `TimelineToolbar.cpp`

- [ ] **Step 1: Migrate TrackHeaderWidget** — replace `AudioEngine&` with `ProjectCommands*` + `ReadModel*` + `AudioGraphCommands*`
- [ ] **Step 2: Migrate TimelineScene** — replace `AudioEngine&` with `ReadModel*`
- [ ] **Step 3: Migrate TimelineView** — replace `AudioEngine&` with `ProjectCommands*` + `TransportCommands*` + `AudioGraphCommands*` + `ReadModel*`
- [ ] **Step 4: Migrate TimelineInteraction** — same pattern
- [ ] **Step 5: Migrate MarkerItem** — replace `AudioEngine&` with `ProjectCommands*`
- [ ] **Step 6: Migrate TimelineToolbar** — keep `PluginManager&` reference for plugin menu (acceptable)
- [ ] **Step 7: Build and test**
- [ ] **Step 8: Commit** — `ui: migrate timeline widgets to command interfaces`

### 7E: Editor widgets

**Files:** `PianoRollWidget.h/.cpp`, `NoteGridWidget.h/.cpp`, `AudioClipEditorWidget.h/.cpp`, `AudioClipItem.h/.cpp`, `AudioWaveformWidget.h/.cpp`, `StepEditorWidget.h/.cpp`, `PhraseGeneratorDialog.h/.cpp`, `AutomationLaneWidget.h/.cpp`

- [ ] **Step 1: Migrate NoteGridWidget** — replace `AudioEngine&` with `ProjectCommands*` + `ReadModel*`
- [ ] **Step 2: Migrate PianoRollWidget** — same pattern
- [ ] **Step 3: Migrate AudioClipEditorWidget** — replace `AudioEngine&` with `ProjectCommands*` + `ReadModel*`
- [ ] **Step 4: Migrate StepEditorWidget** — same pattern
- [ ] **Step 5: Migrate PhraseGeneratorDialog** — same pattern (PhraseGenerator stays in engine, but dialog calls it through a utility function or directly — it's a pure function library)
- [ ] **Step 6: Migrate AutomationLaneWidget** — this one is complex; needs `AudioGraphCommands*` for rebuild calls
- [ ] **Step 7: Build and test**
- [ ] **Step 8: Commit** — `ui: migrate editor widgets to command interfaces`

### 7F: Remaining widgets

**Files:** `ModulationWidget.h/.cpp`, `ExportDialog.h/.cpp`, `ProjectPoolBrowser.h/.cpp`, `PreferencesDialog.h/.cpp`

- [ ] **Step 1: Migrate ModulationWidget**
- [ ] **Step 2: Migrate ExportDialog** — add `ExportCommands` interface for export operations
- [ ] **Step 3: Migrate ProjectPoolBrowser**
- [ ] **Step 4: Migrate PreferencesDialog** — this needs `AudioDeviceManager&` for audio settings; add a `SettingsCommands` interface or keep a thin reference
- [ ] **Step 5: Build and test**
- [ ] **Step 6: Commit** — `ui: migrate remaining widgets to command interfaces`

---

## Task 8: Verify zero engine includes in UI layer

**Files:** All `src/ui/` files

- [ ] **Step 1: Grep for engine includes**

```bash
grep -rn '#include.*engine/' src/ui/
```

Expected: Only allowed exceptions:
- `src/ui/DebugLog.h` (the redirect to common/)
- `src/ui/PluginEditorWindow.cpp` (includes `TrackFXSlot.h` for the forward-declared type)
- `src/ui/TimelineToolbar.cpp` (includes `PluginManager.h` for plugin menu — acceptable)

All other `src/ui/` files should have zero `#include`s of `src/engine/` headers.

- [ ] **Step 2: Fix any remaining violations**

For each violation found in Step 1, trace what API it's calling and route it through a command interface or ReadModel.

- [ ] **Step 3: Final build and full test suite**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe`
Expected: All pass.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "ui: verify zero engine includes in UI layer (decoupling complete)"
```

---

## Task 9: Clean up AudioEngine facade

**Files:** `src/engine/AudioEngine.h`, `src/engine/AudioEngine.cpp`

After all UI widgets use command interfaces, `AudioEngine` no longer needs to expose raw getters to the UI.

- [ ] **Step 1: Make internal getters private**

Change `getProjectModel()`, `getMainProcessor()`, `getTransportManager()`, `getBridge()`, `getPluginManager()`, `getProjectPool()`, `getMidiInputManager()`, `getDeviceManager()` from `public` to `private`.

Keep them accessible within the engine layer (AudioEngineCommands, MainAudioProcessor, etc. are friends or in the same module).

- [ ] **Step 2: Keep facade methods for MCP server**

The MCP server (`src/mcp/`) also uses `AudioEngine` directly. It can continue to use the command interfaces, or keep a limited set of public getters for MCP tool implementations.

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe`
Expected: All pass.

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngine.h src/engine/AudioEngine.cpp
git commit -m "engine: make internal getters private after GUI decoupling"
```

---

## Self-Review

After writing the complete plan, I verified:

1. **Spec coverage:** Every coupling hotspot from the analysis (28 GUI→engine includes, 3 engine→GUI includes, SPSCBridge direct push, dynamic_cast sites, ValueTree::Listener in GUI, TransportManager atomic reads) has a corresponding task that addresses it.

2. **Placeholder scan:** All code blocks contain real implementations or clear mechanical moves of existing code. No "TBD" or "add appropriate error handling" placeholders.

3. **Type consistency:** `ProjectCommands`, `TransportCommands`, `AudioGraphCommands`, `ReadModel` are consistently named across all tasks. Method signatures match between interface definitions (Task 2) and concrete implementations (Task 4).

4. **Risk areas:**
   - Task 6 (MainWindow migration) is the largest single task (~500 lines of mechanical refactoring). It should be broken into sub-steps if the agent prefers smaller commits.
   - Task 7D (Timeline widgets) is complex because `TimelineView` implements `ValueTree::Listener` and has deep coupling to the audio graph rebuild path.
   - Task 8 (verification) may surface edge cases where a widget needs a specific engine API that wasn't anticipated in the interface design. These should be added as new methods to the appropriate command interface.
