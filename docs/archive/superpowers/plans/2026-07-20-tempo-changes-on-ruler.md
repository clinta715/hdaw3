# Tempo Changes on the Ruler — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add tempo point interactions to the TimeRuler — add/remove/edit via context menu, drag to reposition.

**Architecture:** Add 4 tempo-point CRUD commands to `ProjectCommands`/`AudioEngineCommands`. Extend `TimeRuler` with hit-testing, context menu actions, and drag interaction. The existing `AudioEngine` ValueTree listeners already trigger `rebuildTempoMap()` on any tempo point mutation — no engine changes needed.

**Tech Stack:** Qt 6 (QMenu, QInputDialog, QGraphicsSceneMouseEvent), JUCE ValueTree + UndoManager

---

## File Structure

| File | Change |
|------|--------|
| `src/common/ProjectCommands.h` | Add 4 virtual tempo-point methods |
| `src/engine/AudioEngineCommands.h` | Add 4 override declarations |
| `src/engine/AudioEngineCommands.cpp` | Implement 4 tempo-point methods |
| `src/ui/TimeRuler.h` | Add `DragTempoPoint` mode, `draggingTempoIndex`, `tempoPointIndexAtX()` |
| `src/ui/TimeRuler.cpp` | Add tempo actions to context menu, drag logic in mouse events |

---

## Task 1: Add tempo-point CRUD commands

**Files:**
- Modify: `src/common/ProjectCommands.h:91-94` (after `setTempo`)
- Modify: `src/engine/AudioEngineCommands.h:113-119` (after `setTempo` override)
- Modify: `src/engine/AudioEngineCommands.cpp:969-973` (after `setTempo` impl)

- [ ] **Step 1: Add virtual methods to ProjectCommands.h**

After `virtual void setTempo(double bpm) = 0;` (line 92), add:

```cpp
    // Tempo point operations (tempo map)
    virtual int addTempoPoint(double timeSeconds, double bpm) = 0;
    virtual void removeTempoPoint(int index) = 0;
    virtual void setTempoPointBpm(int index, double bpm) = 0;
    virtual void setTempoPointTime(int index, double timeSeconds) = 0;
```

- [ ] **Step 2: Add override declarations to AudioEngineCommands.h**

After `void setTempo(double bpm) override;` (line 114), add:

```cpp
    int addTempoPoint(double timeSeconds, double bpm) override;
    void removeTempoPoint(int index) override;
    void setTempoPointBpm(int index, double bpm) override;
    void setTempoPointTime(int index, double timeSeconds) override;
```

- [ ] **Step 3: Implement in AudioEngineCommands.cpp**

After the `setTempo` implementation (line 973), add:

```cpp
int AudioEngineCommands::addTempoPoint(double timeSeconds, double bpm)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid())
    {
        tempoList = juce::ValueTree(IDs::TEMPO_POINT_LIST);
        model.getTree().addChild(tempoList, -1, &um);
    }
    juce::ValueTree pt(IDs::TEMPO_POINT);
    pt.setProperty(IDs::startTime, timeSeconds, &um);
    pt.setProperty(IDs::tempo, bpm, &um);
    int idx = tempoList.getNumChildren();
    tempoList.addChild(pt, -1, &um);
    return idx;
}

void AudioEngineCommands::removeTempoPoint(int index)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.removeChild(index, &um);
}

void AudioEngineCommands::setTempoPointBpm(int index, double bpm)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.getChild(index).setProperty(IDs::tempo, bpm, &um);
}

void AudioEngineCommands::setTempoPointTime(int index, double timeSeconds)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.getChild(index).setProperty(IDs::startTime, timeSeconds, &um);
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 5: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands.cpp
git commit -m "ProjectCommands: add tempo point CRUD commands"
```

---

## Task 2: Extend TimeRuler with tempo drag state and hit-testing

**Files:**
- Modify: `src/ui/TimeRuler.h:64-68` (DragMode enum + members)

- [ ] **Step 1: Add DragTempoPoint to the DragMode enum**

Change line 64 from:
```cpp
    enum DragMode { None, Seek, DragLoopStart, DragLoopEnd, DragLoopRegion };
```
to:
```cpp
    enum DragMode { None, Seek, DragLoopStart, DragLoopEnd, DragLoopRegion, DragTempoPoint };
```

- [ ] **Step 2: Add draggingTempoIndex member and hit-test helper**

After `double dragStartX = 0.0;` (line 68), add:

```cpp
    int draggingTempoIndex = -1;
    int tempoPointIndexAtX(double x) const;
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile (the new method is declared but not yet defined — will fail at link if used, but no usage yet)

- [ ] **Step 4: Commit**

```bash
git add src/ui/TimeRuler.h
git commit -m "TimeRuler: add DragTempoPoint mode and hit-test declaration"
```

---

## Task 3: Implement tempo interactions in TimeRuler.cpp

**Files:**
- Modify: `src/ui/TimeRuler.cpp:201-330` (mouse events + context menu)

- [ ] **Step 1: Implement tempoPointIndexAtX**

Add at the end of the file (after `contextMenuEvent`, before the closing of the file):

```cpp
int TimeRuler::tempoPointIndexAtX(double x) const
{
    auto points = readModel->getTempoPoints();
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        double ptX = xFromTime(points[i].timeSeconds);
        if (std::abs(x - ptX) <= 6.0)
            return i;
    }
    return -1;
}
```

- [ ] **Step 2: Add tempo drag to mousePressEvent**

In `mousePressEvent`, after the loop-region drag check (the `else if (x >= lx && x <= rx)` block ending at line 228) and before the final `else` (seek), insert a tempo point check:

Replace the block from line 229 (`else`) through line 234 (end of seek block) with:

```cpp
    else if (int tpIdx = tempoPointIndexAtX(x); tpIdx >= 0)
    {
        dragMode = DragTempoPoint;
        draggingTempoIndex = tpIdx;
    }
    else
    {
        dragMode = Seek;
        transportCmds->seekToSeconds(t);
        emit seekRequested(t);
    }
```

- [ ] **Step 3: Add tempo drag to mouseMoveEvent**

In `mouseMoveEvent`, after the `DragLoopRegion` block (ending at line 269) and before the closing `}` of the function, add:

```cpp
    else if (dragMode == DragTempoPoint && draggingTempoIndex >= 0)
    {
        double newTime = (std::max)(0.0, t);
        projectCmds->setTempoPointTime(draggingTempoIndex, newTime);
        update();
    }
```

- [ ] **Step 4: Reset drag state in mouseReleaseEvent**

Replace the current `mouseReleaseEvent` (lines 272-277):

```cpp
void TimeRuler::mouseReleaseEvent(QGraphicsSceneMouseEvent*)
{
    if (dragMode != Seek && dragMode != None)
        commitLoopBounds();
    dragMode = None;
}
```

with:

```cpp
void TimeRuler::mouseReleaseEvent(QGraphicsSceneMouseEvent*)
{
    if (dragMode == DragLoopStart || dragMode == DragLoopEnd || dragMode == DragLoopRegion)
        commitLoopBounds();
    dragMode = None;
    draggingTempoIndex = -1;
}
```

- [ ] **Step 5: Add tempo actions to contextMenuEvent**

In `contextMenuEvent`, after the "Add Marker Here..." action (line 326) and before `menu.exec(event->screenPos())` (line 328), add:

```cpp
    menu.addSeparator();

    // Tempo point actions
    int tempoIdx = tempoPointIndexAtX(event->pos().x());
    if (tempoIdx >= 0)
    {
        auto points = readModel->getTempoPoints();
        double currentBpm = points[tempoIdx].bpm;

        auto* editBpm = menu.addAction("Edit BPM...");
        connect(editBpm, &QAction::triggered, this, [this, tempoIdx, currentBpm]() {
            bool ok = false;
            double newBpm = QInputDialog::getDouble(
                QApplication::activeWindow(), "Edit Tempo",
                "BPM:", currentBpm, 20.0, 999.0, 1, &ok);
            if (!ok) return;
            projectCmds->setTempoPointBpm(tempoIdx, newBpm);
        });

        auto* removeTempo = menu.addAction("Remove Tempo Point");
        connect(removeTempo, &QAction::triggered, this, [this, tempoIdx]() {
            projectCmds->removeTempoPoint(tempoIdx);
        });

        menu.addSeparator();
    }

    auto* addTempo = menu.addAction("Add Tempo Change Here...");
    connect(addTempo, &QAction::triggered, this, [this, t]() {
        bool ok = false;
        double newBpm = QInputDialog::getDouble(
            QApplication::activeWindow(), "Tempo Change",
            QString("BPM at %1s:").arg(t, 0, 'f', 2),
            readModel->getTransport().bpm, 20.0, 999.0, 1, &ok);
        if (!ok) return;
        projectCmds->addTempoPoint(t, newBpm);
    });
```

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 7: Commit**

```bash
git add src/ui/TimeRuler.cpp
git commit -m "TimeRuler: add tempo point context menu and drag-to-reposition"
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
| `src/common/ProjectCommands.h` | +5 | 4 virtual tempo-point methods |
| `src/engine/AudioEngineCommands.h` | +4 | 4 override declarations |
| `src/engine/AudioEngineCommands.cpp` | +45 | 4 implementations |
| `src/ui/TimeRuler.h` | +3 | DragTempoPoint mode, draggingTempoIndex, hit-test decl |
| `src/ui/TimeRuler.cpp` | +50 | Hit-test impl, drag logic, context menu actions |
