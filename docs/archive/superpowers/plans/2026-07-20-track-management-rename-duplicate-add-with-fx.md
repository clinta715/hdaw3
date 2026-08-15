# Track Management — Rename, Duplicate, Add-with-FX

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the track management surface — fix duplicateTrack ID collision, add duplicate to context menus, add MCP `duplicate_track` tool, and add MCP `add_track_with_fx` convenience tool.

**Architecture:** All track mutations go through `ProjectCommands` (abstract) → `AudioEngineCommands` (concrete) → `ValueTree` via `UndoManager`. UI context menus call `projectCmds->*` directly. MCP tools call `AudioEngine*` methods inline. The duplicate track bug is a shallow `createCopy()` that doesn't reassign clip/note IDs — fixed by walking the copy and re-assigning via `allocateClipID()`/`allocateNoteID()`.

**Tech Stack:** Qt 6, JUCE 8, JUCE ValueTree, gtest, McpServer JSON-RPC

---

## Current State

| Feature | ProjectCommands | Engine impl | GUI menus | MCP tool |
|---------|----------------|-------------|-----------|----------|
| Rename | `setTrackName()` ✅ | ✅ | ✅ inline + ctx menu | `set_track{name}` ✅ |
| Duplicate | `duplicateTrack()` ✅ | ⚠️ ID collision bug | ❌ ctx menus missing | ❌ missing |
| Add-with-FX | `addTrack()` + `addFxSlot()` | ✅ | ✅ toolbar + ctx menu | ❌ missing |

---

## Task 1: Fix `duplicateTrack` ID collision

**Files:**
- Modify: `src/engine/AudioEngineCommands.cpp:239-249`

The current `duplicateTrack` calls `source.createCopy()` which is a deep clone of the ValueTree, but clip IDs and note IDs inside the copy are identical to the source. If both the original and duplicate clips exist, MCP tools and automation references that use `clipID` will collide.

The fix: after `createCopy()`, walk the copy's `CLIP_LIST`, re-assign each clip a fresh `clipID` via `allocateClipID()`, and re-assign each MIDI note a fresh `noteID` via `allocateNoteID()`. Also append " copy" to the track name.

- [ ] **Step 1: Read the current duplicateTrack implementation**

```cpp
// src/engine/AudioEngineCommands.cpp:239-249
int AudioEngineCommands::duplicateTrack(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    auto source = trackList.getChild(trackIndex);
    auto copy = source.createCopy();
    int newIdx = trackList.getNumChildren();
    trackList.addChild(copy, newIdx, &um);
    return newIdx;
}
```

- [ ] **Step 2: Replace with ID-safe implementation**

```cpp
int AudioEngineCommands::duplicateTrack(int trackIndex)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    auto source = trackList.getChild(trackIndex);
    auto copy = source.createCopy();

    // Append " copy" to the track name
    auto name = copy.getProperty(IDs::name).toString();
    copy.setProperty(IDs::name, name + " copy", &um);

    // Re-assign clip IDs to avoid collisions with the source
    auto clipList = copy.getChildWithName(IDs::CLIP_LIST);
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto clip = clipList.getChild(c);
        clip.setProperty(IDs::clipID, model.allocateClipID(), nullptr);

        // Re-assign note IDs inside MIDI clips
        auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto note = noteList.getChild(n);
            note.setProperty(IDs::noteID, model.allocateNoteID(), nullptr);
        }
    }

    int newIdx = trackList.getNumChildren();
    trackList.addChild(copy, newIdx, &um);
    return newIdx;
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngineCommands.cpp
git commit -m "AudioEngineCommands: fix duplicateTrack ID collision and append 'copy' to name"
```

---

## Task 2: Add "Duplicate Track" to TrackHeaderWidget context menu

**Files:**
- Modify: `src/ui/TrackHeaderWidget.cpp:832-950` (the `buildTrackMenu` method)

Currently the track context menu has: Rename, Color, Add FX Slot, MIDI Channel, Delete. Add "Duplicate Track" between Color and Add FX Slot.

- [ ] **Step 1: Add the Duplicate Track action after the color action**

In `TrackHeaderWidget::buildTrackMenu`, after the color action (around line 868) and before `menu.addSeparator()`, add:

```cpp
    auto* dupAction = menu.addAction("Duplicate Track");
    connect(dupAction, &QAction::triggered, this, [this, trackIdx]() {
        projectCmds->duplicateTrack(trackIdx);
        rebuild();
    });
```

The full section should look like:

```cpp
    auto* colorAction = menu.addAction("Track Color...");
    connect(colorAction, &QAction::triggered, this, [this, trackIdx]() {
        auto trackList = engine.getProjectModel().getTrackListTree();
        if (trackIdx >= trackList.getNumChildren()) return;
        auto tree = trackList.getChild(trackIdx);

        int currentColor = tree.getProperty(IDs::color, static_cast<int>(0xFF4488CC));
        QColor initial((currentColor >> 16) & 0xFF, (currentColor >> 8) & 0xFF, currentColor & 0xFF);
        QColor chosen = QColorDialog::getColor(initial, const_cast<QWidget*>(static_cast<const QWidget*>(this)),
            "Choose Track Color");
        if (chosen.isValid())
        {
            int newColor = (0xFF << 24) | (chosen.red() << 16) | (chosen.green() << 8) | chosen.blue();
            projectCmds->setTrackColor(trackIdx, newColor);
            update();
        }
    });

    auto* dupAction = menu.addAction("Duplicate Track");
    connect(dupAction, &QAction::triggered, this, [this, trackIdx]() {
        projectCmds->duplicateTrack(trackIdx);
        rebuild();
    });

    menu.addSeparator();
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/ui/TrackHeaderWidget.cpp
git commit -m "TrackHeaderWidget: add Duplicate Track to context menu"
```

---

## Task 3: Add "Duplicate Track" to MixerStripWidget context menu

**Files:**
- Modify: `src/ui/MixerStripWidget.cpp:356-397` (the `contextMenuEvent` method)

Add "Duplicate Track" between "FX Chain" and the separator before "Delete Track".

- [ ] **Step 1: Add the Duplicate Track action**

In `MixerStripWidget::contextMenuEvent`, after the FX Chain action (line 379) and before `menu.addSeparator()`, add:

```cpp
    auto* dupAction = menu.addAction("Duplicate Track");
    connect(dupAction, &QAction::triggered, this, [this]() {
        projectCmds->duplicateTrack(trackIndex);
        engine.getMainProcessor()->rebuildRoutingGraph();
        emit trackDeleted(); // triggers mixer rebuild
    });
```

The full section should look like:

```cpp
    auto* fxAction = menu.addAction("FX Chain");
    connect(fxAction, &QAction::triggered, this, [this]() {
        emit fxButtonClicked(trackIndex);
    });

    auto* dupAction = menu.addAction("Duplicate Track");
    connect(dupAction, &QAction::triggered, this, [this]() {
        projectCmds->duplicateTrack(trackIndex);
        engine.getMainProcessor()->rebuildRoutingGraph();
        emit trackDeleted();
    });

    menu.addSeparator();
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/ui/MixerStripWidget.cpp
git commit -m "MixerStripWidget: add Duplicate Track to context menu"
```

---

## Task 4: Add `duplicate_track` MCP tool

**Files:**
- Modify: `src/mcp/McpTools.cpp:410-499` (the `registerTrackTools` function)

Add a `duplicate_track` tool after the `move_track` tool. It should call `projectCmds->duplicateTrack()` and then wire routing for the new track.

- [ ] **Step 1: Add the duplicate_track tool registration**

After the `move_track` tool registration (around line 498, before the closing `}` of `registerTrackTools`), add:

```cpp
    s.registerTool({"duplicate_track",
        "Duplicate a track (deep copy with new clip/note IDs). Returns the new track index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto source = tl.getChild(id);
            auto copy = source.createCopy();

            // Append " copy" to name
            auto name = copy.getProperty(IDs::name).toString();
            copy.setProperty(IDs::name, name + " copy", &um);

            // Re-assign clip IDs
            auto clipList = copy.getChildWithName(IDs::CLIP_LIST);
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                auto clip = clipList.getChild(c);
                clip.setProperty(IDs::clipID, m.allocateClipID(), nullptr);
                auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
                for (int n = 0; n < noteList.getNumChildren(); ++n)
                {
                    auto note = noteList.getChild(n);
                    note.setProperty(IDs::noteID, m.allocateNoteID(), nullptr);
                }
            }

            int newIdx = tl.getNumChildren();
            tl.addChild(copy, newIdx, &um);

            // Wire routing
            bool routingOk = false;
            if (auto* rm = e->getMainProcessor()->getRoutingManager())
            {
                rm->addTrack(newIdx, copy);
                routingOk = rm->getTrackNode(newIdx) != nullptr;
            }
            return McpToolResult::text(
                QString("trackId=%1 routed=%2").arg(newIdx).arg(routingOk ? "1" : "0"));
        }});
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools.cpp
git commit -m "McpTools: add duplicate_track tool with ID-safe deep copy"
```

---

## Task 5: Add `add_track_with_fx` MCP tool

**Files:**
- Modify: `src/mcp/McpTools.cpp:410-499` (the `registerTrackTools` function)

Add a convenience `add_track_with_fx` tool that creates a track and adds an FX slot in one call. This mirrors the existing `MainWindow::onAddTrackWithFX()` pattern.

- [ ] **Step 1: Add the add_track_with_fx tool registration**

After the `duplicate_track` tool (from Task 4), before the closing `}` of `registerTrackTools`, add:

```cpp
    s.registerTool({"add_track_with_fx",
        "Add a track with an FX slot. fxType in {eq,compressor,reverb,delay}, or provide pluginId for a VST3/CLAP plugin.",
        objSchema({{"name",     QJsonObject{{"type","string"}}},
                   {"fxType",   QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"eq","compressor","reverb","delay"}}}},
                   {"pluginId", QJsonObject{{"type","string"}}},
                   {"color",    QJsonObject{{"type","integer"}}},
                   {"parentBus",QJsonObject{{"type","integer"}}}}, {"name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            int idx = m.getTrackListTree().getNumChildren();

            // Create the track
            juce::ValueTree t(IDs::TRACK);
            t.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            t.setProperty(IDs::volume, 0.85, &um);
            t.setProperty(IDs::pan, 0.0, &um);
            t.setProperty(IDs::isMuted, false, &um);
            t.setProperty(IDs::isSoloed, false, &um);
            t.setProperty(IDs::parentBus, a.value("parentBus").toInt(0), &um);
            int color = a.contains("color") ? a.value("color").toInt()
                                             : static_cast<int>(ProjectModel::trackColorForIndex(idx));
            t.setProperty(IDs::color, color, &um);
            t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, &um);
            t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, &um);
            t.addChild(ProjectModel::createTrackAutomationList(), -1, &um);
            m.getTrackListTree().addChild(t, -1, &um);

            // Add the FX slot
            std::string fxType = a.value("fxType").toString().toStdString();
            std::string pluginId = a.value("pluginId").toString().toStdString();
            if (fxType.empty() && !pluginId.empty()) fxType = "plugin";
            if (!fxType.empty())
                m.addFxSlot(idx, fxType, -1, pluginId);

            // Wire routing
            bool routingOk = false;
            if (auto* rm = e->getMainProcessor()->getRoutingManager())
            {
                rm->addTrack(idx, t);
                routingOk = rm->getTrackNode(idx) != nullptr;
            }
            return McpToolResult::text(
                QString("trackId=%1 routed=%2 fxType=%3").arg(idx)
                    .arg(routingOk ? "1" : "0")
                    .arg(QString::fromStdString(fxType)));
        }});
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: clean compile

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools.cpp
git commit -m "McpTools: add add_track_with_fx convenience tool"
```

---

## Task 6: Build and run tests

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Debug`
Expected: clean compile with no errors

- [ ] **Step 2: Run the test suite**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=McpServer.*`
Expected: all existing MCP server tests pass

- [ ] **Step 3: Run all tests**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all tests pass

---

## Summary of Changes

| File | Change |
|------|--------|
| `src/engine/AudioEngineCommands.cpp` | Fix `duplicateTrack`: reassign clipIDs/noteIDs, append " copy" to name |
| `src/ui/TrackHeaderWidget.cpp` | Add "Duplicate Track" to `buildTrackMenu` context menu |
| `src/ui/MixerStripWidget.cpp` | Add "Duplicate Track" to `contextMenuEvent` |
| `src/mcp/McpTools.cpp` | Add `duplicate_track` and `add_track_with_fx` MCP tools |
