# Audio Editor Region Cut/Copy/Paste Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add cut, copy, and paste of selected audio regions within the audio clip editor, using keyboard shortcuts (Ctrl+X/Ctrl+C/Ctrl+V) and/or buttons.

**Architecture:** Region clipboard is a new static `RegionClipboard` (analogous to the existing `ClipClipboard`) storing `{sourceFile, offset, duration}`. Copy stores selection bounds, cut does copy + slice at boundaries + remove middle, paste creates a new clip at playhead using the stored source file with adjusted offset/duration. The waveform's existing region selection (`selStart`/`selEnd`, `DragMode::SelectRegion`) is reused.

**Tech Stack:** Qt 6, JUCE 8, C++17

**Files:**
- Create: `src/engine/RegionClipboard.h`, `src/engine/RegionClipboard.cpp`
- Modify: `src/ui/AudioClipEditorWidget.h`, `src/ui/AudioClipEditorWidget.cpp`, `src/ui/AudioWaveformWidget.h`, `src/ui/AudioWaveformWidget.cpp`, `src/common/ProjectCommands.h`, `src/engine/AudioEngineCommands.h`, `src/engine/AudioEngineCommands.cpp`, `CMakeLists.txt`
- Test: `tests/unit/engine/region_clipboard_test.cpp`

---

### Task 1: RegionClipboard — static storage class

**Files:**
- Create: `src/engine/RegionClipboard.h`
- Create: `src/engine/RegionClipboard.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/unit/engine/region_clipboard_test.cpp`

A simple module-level static struct (same pattern as `ClipClipboard`).

`src/engine/RegionClipboard.h`:
```cpp
#pragma once
#include <juce_core/juce_core.h>

namespace HDAW {

struct RegionClipboardEntry {
    juce::String sourceFile;
    double offset = 0.0;      // sample offset into the source file
    double duration = 0.0;    // length of the copied region in seconds
};

class RegionClipboard {
public:
    static void store(const RegionClipboardEntry& entry);
    static const RegionClipboardEntry& get();
    static bool hasContent();
    static void clear();

private:
    static RegionClipboardEntry& entry();
};

} // namespace HDAW
```

`src/engine/RegionClipboard.cpp`:
```cpp
#include "RegionClipboard.h"

namespace HDAW {
namespace {
RegionClipboardEntry g_entry;
bool g_hasContent = false;
}

void RegionClipboard::store(const RegionClipboardEntry& e) {
    g_entry = e;
    g_hasContent = true;
}

const RegionClipboardEntry& RegionClipboard::get() {
    return g_entry;
}

bool RegionClipboard::hasContent() {
    return g_hasContent;
}

void RegionClipboard::clear() {
    g_entry = {};
    g_hasContent = false;
}

RegionClipboardEntry& RegionClipboard::entry() {
    return g_entry;
}
} // namespace HDAW
```

- [ ] **Step 1: Write `RegionClipboard.h`**
- [ ] **Step 2: Write `RegionClipboard.cpp`**
- [ ] **Step 3: Add both to `CMakeLists.txt`** (`HDAW_lib` source list, alongside `ClipClipboard`)
- [ ] **Step 4: Write failing test** (`tests/unit/engine/region_clipboard_test.cpp`):

```cpp
#include <gtest/gtest.h>
#include "src/engine/RegionClipboard.h"

TEST(RegionClipboard, StoreAndRetrieve) {
    HDAW::RegionClipboard::store({"test.wav", 1.5, 4.0});
    EXPECT_TRUE(HDAW::RegionClipboard::hasContent());
    auto& e = HDAW::RegionClipboard::get();
    EXPECT_EQ(e.sourceFile, "test.wav");
    EXPECT_DOUBLE_EQ(e.offset, 1.5);
    EXPECT_DOUBLE_EQ(e.duration, 4.0);
}

TEST(RegionClipboard, Clear) {
    HDAW::RegionClipboard::store({"test.wav", 0.0, 2.0});
    HDAW::RegionClipboard::clear();
    EXPECT_FALSE(HDAW::RegionClipboard::hasContent());
}

TEST(RegionClipboard, Overwrite) {
    HDAW::RegionClipboard::store({"a.wav", 0.0, 1.0});
    HDAW::RegionClipboard::store({"b.wav", 2.0, 3.0});
    EXPECT_EQ(HDAW::RegionClipboard::get().sourceFile, "b.wav");
}
```

- [ ] **Step 5: Build tests** (`cmake --build build --config Debug --target hdaw_tests`) and run (`build/Debug/hdaw_tests.exe --gtest_filter=RegionClipboard.*`)
- [ ] **Step 6: Commit** (`git add ... && git commit -m "engine: add RegionClipboard static storage class"`)

---

### Task 2: ProjectCommands — region clip commands

**Files:**
- Modify: `src/common/ProjectCommands.h`
- Modify: `src/engine/AudioEngineCommands.h`
- Modify: `src/engine/AudioEngineCommands.cpp`

Add three new pure-virtual methods to `ProjectCommands`:

`src/common/ProjectCommands.h` — add after the clipping/slicing block (~line 124):
```cpp
    // Region cut/copy/paste (audio clip editor)
    virtual int copyAudioClipRegion(int clipId, double regionStart, double regionEnd) = 0;
    virtual int cutAudioClipRegion(int clipId, double regionStart, double regionEnd) = 0;
    virtual int pasteAudioClipRegion(int clipId, double pasteTime) = 0;
```
Return value: the newly created clip's ID, or -1 on failure.

`src/engine/AudioEngineCommands.h` — add override declarations:
```cpp
    int copyAudioClipRegion(int clipId, double regionStart, double regionEnd) override;
    int cutAudioClipRegion(int clipId, double regionStart, double regionEnd) override;
    int pasteAudioClipRegion(int clipId, double pasteTime) override;
```

`src/engine/AudioEngineCommands.cpp` — implementations:

```cpp
int AudioEngineCommands::copyAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIdx >= trackList.getNumChildren()) return -1;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile).toString();
    double clipOffset = clip.getProperty(IDs::offset, 0.0);
    double clipStart = clip.getProperty(IDs::startTime, 0.0);

    // Region clipboard stores the absolute source file offset and duration.
    double regOffset = clipOffset + regionStart;
    double regDuration = std::max(0.001, regionEnd - regionStart);

    HDAW::RegionClipboard::store({sourceFile, regOffset, regDuration});
    return 0; // no new clip created for copy
}

int AudioEngineCommands::cutAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    // First: copy the region to clipboard.
    copyAudioClipRegion(clipId, regionStart, regionEnd);

    // Then: slice the clip so we can remove the middle.
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto& um = engine_.getProjectModel().getUndoManager();
    double startTime = clip.getProperty(IDs::startTime, 0.0);
    double slice1 = startTime + regionStart;
    double slice2 = startTime + regionEnd;

    auto slices = ProjectModel::sliceClipAtTimes(clip, {slice1, slice2}, &um);
    // sliceClipAtTimes may split the original clip into fragments.
    // After slicing, find the middle segment (at slice1 time) and remove it.
    // The original `clip` reference may be invalidated; re-find by track.
    auto trackTree = trackList.getChild(trackIdx);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);

    // Find the middle segment — one whose startTime ≈ regionStart+originalStartTime.
    for (int i = 0; i < clipList.getNumChildren(); ++i) {
        auto c = clipList.getChild(i);
        double st = c.getProperty(IDs::startTime, 0.0);
        // The slice at regionStart creates a clip starting just past it.
        // Tolerance of 0.01 beats.
        if (std::abs(st - slice1) < 0.01) {
            clipList.removeChild(i, &um);
            break;
        }
    }

    engine_.getMainProcessor()->rebuildRoutingGraph();
    return 0;
}

int AudioEngineCommands::pasteAudioClipRegion(int clipId, double pasteTime)
{
    if (!HDAW::RegionClipboard::hasContent()) return -1;

    // Find which track the source clip lives on.
    int trackIdx = -1;
    auto srcClip = findClipById(clipId, trackIdx);
    if (!srcClip.isValid() || trackIdx < 0) return -1;

    const auto& reg = HDAW::RegionClipboard::get();
    juce::String clipName = srcClip.getProperty(IDs::name).toString();
    juce::String newName = clipName + " (pasted)";

    return addAudioClip(trackIdx, pasteTime, reg.duration,
                        reg.sourceFile.toStdString(), newName.toStdString());
}
```

Wait — `addAudioClip` doesn't accept offset. Let me check the signature again.

`addAudioClip(int trackIndex, double start, double duration, const std::string& sourceFile, const std::string& name)` — it takes start time and duration but not offset. I need to verify how offset is handled.

Let me check `addAudioClip` implementation:

```cpp
int AudioEngineCommands::addAudioClip(int trackIndex, double start, double duration,
                                       const std::string& sourceFile, const std::string& name)
```

It internally creates a clip with `IDs::offset` set to 0.0 by default in `ProjectModel::createAudioClip`. For paste, the offset is in the clipboard — I need to add a version that accepts offset, or set it after creation.

Simpler approach for paste: call `addAudioClip` with offset=0, then immediately set the offset property on the returned clip:

```cpp
int AudioEngineCommands::pasteAudioClipRegion(int clipId, double pasteTime)
{
    if (!HDAW::RegionClipboard::hasContent()) return -1;

    int trackIdx = -1;
    auto srcClip = findClipById(clipId, trackIdx);
    if (!srcClip.isValid() || trackIdx < 0) return -1;

    const auto& reg = HDAW::RegionClipboard::get();
    juce::String clipName = srcClip.getProperty(IDs::name).toString();
    juce::String newName = clipName + " (pasted)";

    int newId = addAudioClip(trackIdx, pasteTime, reg.duration,
                             reg.sourceFile.toStdString(), newName.toStdString());
    if (newId < 0) return -1;

    // Set the offset to match the source region.
    int newTrackIdx = -1;
    auto newClip = findClipById(newId, newTrackIdx);
    if (newClip.isValid()) {
        auto& um = engine_.getProjectModel().getUndoManager();
        newClip.setProperty(IDs::offset, reg.offset, &um);
    }

    engine_.getMainProcessor()->rebuildRoutingGraph();
    return newId;
}
```

- [ ] **Step 1: Add `copyAudioClipRegion`, `cutAudioClipRegion`, `pasteAudioClipRegion` to `ProjectCommands.h`**
- [ ] **Step 2: Add override declarations to `AudioEngineCommands.h`**
- [ ] **Step 3: Implement in `AudioEngineCommands.cpp`** — ensure `#include "RegionClipboard.h"` is added
- [ ] **Step 4: Build check** (`cmake --build build --config Debug --target HDAW_lib`) — verify it compiles
- [ ] **Step 5: Commit** (`git commit -am "commands: add audio region cut/copy/paste commands"`)

---

### Task 3: AudioWaveformWidget — add `hasSelection()` and fix `regionSelected` signal

**Files:**
- Modify: `src/ui/AudioWaveformWidget.h`
- Modify: `src/ui/AudioWaveformWidget.cpp`

Add public accessors:
```cpp
    bool hasSelection() const { return selStart >= 0.0 && selEnd > selStart; }
    double getSelectionStart() const { return selStart; }
    double getSelectionEnd() const { return selEnd; }
    void clearSelection() { selStart = -1.0; selEnd = -1.0; update(); }
```

The `getSelectionStart()` and `getSelectionEnd()` already exist (added in previous work). Need to add `hasSelection()` and `clearSelection()`.

Also ensure the `regionSelected` signal actually fires during drag end. Check `mouseReleaseEvent`:

In `AudioWaveformWidget.cpp`, after the region-select drag completes, emit `regionSelected(selStart, selEnd)`.

- [ ] **Step 1: Add `hasSelection()` and `clearSelection()` to `AudioWaveformWidget.h`**
- [ ] **Step 2: In `AudioWaveformWidget.cpp::mouseReleaseEvent`**, when `DragMode::SelectRegion` ends and `selEnd > selStart`, emit `regionSelected(selStart, selEnd)`
- [ ] **Step 3: Commit** (`git commit -am "waveform: add hasSelection/clearSelection, emit regionSelected"`)

---

### Task 4: AudioClipEditorWidget — keyboard shortcuts and button handlers

**Files:**
- Modify: `src/ui/AudioClipEditorWidget.h`
- Modify: `src/ui/AudioClipEditorWidget.cpp`

Add slots and keyboard shortcuts for cut/copy/paste.

`src/ui/AudioClipEditorWidget.h` — add private slots:
```cpp
    void onCopyRegion();
    void onCutRegion();
    void onPasteRegion();
```

Also add `pasteAtPlayheadBtn` button pointer and `selectionLabel`:
```cpp
    QPushButton* copyRegionBtn = nullptr;
    QPushButton* cutRegionBtn = nullptr;
    QPushButton* pasteRegionBtn = nullptr;
    QLabel* selectionLabel = nullptr;
```

`src/ui/AudioClipEditorWidget.cpp`:

**In `setupUI()`** — add a selection info label and three buttons in the control bar (after the slicing buttons section):
```cpp
    // Selection region label
    selectionLabel = new QLabel("No selection", controlBar);
    selectionLabel->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(selectionLabel);

    // Region copy/paste buttons
    auto* sep4 = new QFrame(controlBar);
    sep4->setFrameShape(QFrame::VLine);
    sep4->setFixedHeight(20);
    controlLayout->addWidget(sep4);

    copyRegionBtn = new QPushButton("Copy Region", controlBar);
    copyRegionBtn->setFixedHeight(20);
    copyRegionBtn->setEnabled(false);
    copyRegionBtn->setToolTip("Copy selected region");
    controlLayout->addWidget(copyRegionBtn);

    cutRegionBtn = new QPushButton("Cut Region", controlBar);
    cutRegionBtn->setFixedHeight(20);
    cutRegionBtn->setEnabled(false);
    cutRegionBtn->setToolTip("Cut selected region");
    controlLayout->addWidget(cutRegionBtn);

    pasteRegionBtn = new QPushButton("Paste", controlBar);
    pasteRegionBtn->setFixedHeight(20);
    pasteRegionBtn->setEnabled(false);
    pasteRegionBtn->setToolTip("Paste region at playhead");
    controlLayout->addWidget(pasteRegionBtn);
```

**In `connectSignals()`** — connect buttons and waveform signals:
```cpp
    // Region selection tracking
    connect(waveform, &AudioWaveformWidget::regionSelected, this, [this](double startBeat, double endBeat) {
        if (isLoaded) {
            double dur = endBeat - startBeat;
            selectionLabel->setText(QString("Sel: %1s-%2s (%3s)")
                .arg(startBeat, 0, 'f', 2).arg(endBeat, 0, 'f', 2).arg(dur, 0, 'f', 2));
            copyRegionBtn->setEnabled(true);
            cutRegionBtn->setEnabled(true);
        }
    });

    connect(copyRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCopyRegion);
    connect(cutRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCutRegion);
    connect(pasteRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onPasteRegion);
```

Also update playhead position changes to enable paste button:
```cpp
    // Enable paste when clipboard has content — check on load/clipboard change
    pasteRegionBtn->setEnabled(HDAW::RegionClipboard::hasContent());
```

Also need to connect to a playhead-moved signal. The `updatePlayhead` method is called from `MainWindow` — we can check there or simply enable paste if clipboard has content at any point.

Add keyboard shortcut support via `keyPressEvent` override:
```cpp
void AudioClipEditorWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_C:
            onCopyRegion();
            return;
        case Qt::Key_X:
            onCutRegion();
            return;
        case Qt::Key_V:
            onPasteRegion();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
```

Declare this in the header:
```cpp
    void keyPressEvent(QKeyEvent* event) override;
```

**Implement the handlers:**

```cpp
void AudioClipEditorWidget::onCopyRegion()
{
    if (!currentClip.isValid() || !waveform->hasSelection()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;

    projectCmds->copyAudioClipRegion(clipId, selStart, selEnd);
    pasteRegionBtn->setEnabled(true);
}

void AudioClipEditorWidget::onCutRegion()
{
    if (!currentClip.isValid() || !waveform->hasSelection()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;

    projectCmds->cutAudioClipRegion(clipId, selStart, selEnd);
    pasteRegionBtn->setEnabled(true);

    // Reload: the clip was split and middle removed.
    reloadClip();
}

void AudioClipEditorWidget::onPasteRegion()
{
    if (!currentClip.isValid() || !HDAW::RegionClipboard::hasContent()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));

    // Paste at the current playhead position.
    auto transport = readModel->getTransport();
    double pasteTime = transport.currentTimeSeconds;

    projectCmds->pasteAudioClipRegion(clipId, pasteTime);

    // Reload the clip list sidebar or scene.
    // The current editor may now show a different clip — reload or close.
    reloadClip();
}
```

**In `loadClip()` / `clear()`** — update button states:
```cpp
// In loadClip, after isLoaded = true:
pasteRegionBtn->setEnabled(HDAW::RegionClipboard::hasContent());

// In clear:
copyRegionBtn->setEnabled(false);
cutRegionBtn->setEnabled(false);
pasteRegionBtn->setEnabled(false);
selectionLabel->setText("No selection");
```

Add `#include "RegionClipboard.h"` and `#include <QKeyEvent>` at the top of `AudioClipEditorWidget.cpp`.

- [ ] **Step 1: Add headers** (`<QKeyEvent>`, `RegionClipboard.h`) to `AudioClipEditorWidget.cpp`
- [ ] **Step 2: Add member declarations to `AudioClipEditorWidget.h`**: buttons, label, slot methods, `keyPressEvent` override
- [ ] **Step 3: Add UI elements in `setupUI()`** — selection label, copy/cut/paste buttons
- [ ] **Step 4: Wire signals in `connectSignals()`** — `regionSelected`→enable buttons, button clicks
- [ ] **Step 5: Implement `onCopyRegion`, `onCutRegion`, `onPasteRegion`**
- [ ] **Step 6: Implement `keyPressEvent`** — Ctrl+C/X/V dispatch
- [ ] **Step 7: Add `#include "RegionClipboard.h"` to `AudioClipEditorWidget.cpp`**
- [ ] **Step 8: Update `loadClip()` and `clear()`** to set button enabled states
- [ ] **Step 9: Build HDAW** (`cmake --build build --config Debug --target HDAW`)
- [ ] **Step 10: Commit** (`git commit -am "ui: add audio editor region cut/copy/paste"`)

---

### Task 5: Integration build & test

**Files:** (none)

- [ ] **Step 1: Build all** (`cmake --build build --config Debug — wait for completion`)
- [ ] **Step 2: Run full test suite** (`build/Debug/hdaw_tests.exe` — expect all 125+ tests passing)
- [ ] **Step 3: Final commit** if any fixes were needed during build
