# Full Automation Parameter Coverage — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose all automatable track parameters (Volume, Pan, Mute) as default automation lanes, and build infrastructure for plugin FX parameter automation with on-demand lane creation.

**Architecture:** Part A adds default lanes for Pan (paramID 2) and Mute (paramID 3) alongside existing Volume (paramID 1) at every track-creation site. Part B adds a compound paramID scheme (`100 + slot*100 + paramIndex`) for plugin params, an atomic-value cache in `TrackFXSlot`, audio-thread dispatch in `Track::processBlock`, and a UI for creating/removing lanes on demand via `AutomationLaneWidget`.

**Tech Stack:** C++20, Qt 6, JUCE 8, CMake

---

## File Structure

| File | Role | Part |
|------|------|------|
| `src/model/ProjectModel.cpp` | Shared helper `createTrackAutomationList()` builds Volume+Pan+Mute | A |
| `src/ui/MainWindow.cpp` | `onAddTrack()` uses shared helper | A |
| `src/engine/AudioEngine.cpp` | Remove mute-recording exclusion in `valueTreePropertyChanged` | A |
| `src/engine/Track.cpp` | Add pid==3 + pid>=100 dispatch; call `applyAutomation()` in FX loop | A+B |
| `src/mcp/McpTools.cpp` | `add_track` creates 3 lanes instead of empty list | A |
| `src/engine/TrackFXSlot.h` | `ParamInfo`, `getAutomatableParams()`, `setAutomationParam()`, `applyAutomation()`, `paramValues` | B |
| `src/engine/Track.h` | Keep existing `getFXChain()` (already public) | B |
| `src/ui/AutomationLaneWidget.h` | Add Lane / Remove Lane buttons, `addAutomationLane()` method | B |
| `src/ui/AutomationLaneWidget.cpp` | Menu building, lane creation, removal | B |

---

### Task 1: Add shared helper for default automation lanes

**Files:**
- Modify: `src/model/ProjectModel.cpp`

**Context:** Currently `createAutomationList()` builds only a Volume lane. Need a public static method `ProjectModel::createTrackAutomationList()` that builds Volume, Pan, and Mute lanes — callable from `MainWindow.cpp` and `McpTools.cpp`. All default-track call sites (`createDefaultProject` lines 338, 358, 375) update to use it. The old `createVolumeAutomation()` and `createAutomationList()` are replaced.

- [ ] **Step 1: Add `createTrackAutomationList()` declaration to header**

In `ProjectModel.h`, public section (after `trackColorForIndex` declaration), add:
```cpp
    static juce::ValueTree createTrackAutomationList();
```

- [ ] **Step 2: Replace `createVolumeAutomation()` and `createAutomationList()` with `createTrackAutomationList()`**

In `ProjectModel.cpp`, replace the two static helpers at lines 33-54:

```cpp
static juce::ValueTree createPoint(double time, double value)
{
    juce::ValueTree point(IDs::POINT);
    point.setProperty(IDs::startTime, time, nullptr);
    point.setProperty(IDs::gain, value, nullptr);
    return point;
}

static juce::ValueTree createAutomationLane(const juce::String& name, int paramID, double defaultVal)
{
    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, name, nullptr);
    autoTree.setProperty(IDs::paramID, paramID, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);

    juce::ValueTree pointList(IDs::POINT_LIST);
    pointList.addChild(createPoint(0.0, defaultVal), -1, nullptr);
    pointList.addChild(createPoint(16.0, defaultVal), -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);
    return autoTree;
}

juce::ValueTree ProjectModel::createTrackAutomationList()
{
    juce::ValueTree list(IDs::AUTOMATION_LIST);
    list.addChild(createAutomationLane("Volume", 1, 1.0), -1, nullptr);
    list.addChild(createAutomationLane("Pan", 2, 0.5), -1, nullptr);
    list.addChild(createAutomationLane("Mute", 3, 0.0), -1, nullptr);
    return list;
}
```

- [ ] **Step 2: Update call sites in `createDefaultProject()`**

In `ProjectModel.cpp`, replace all three occurrences of `createAutomationList()` with `createTrackAutomationList()` (lines 338, 358, 375):

```
// Before (3 occurrences):
track1.addChild(createAutomationList(), -1, nullptr);

// After:
track1.addChild(createTrackAutomationList(), -1, nullptr);
```

- [ ] **Step 3: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/model/ProjectModel.cpp
git commit -m "ProjectModel: add shared createTrackAutomationList() with Volume+Pan+Mute"
```

---

### Task 2: Update MainWindow::onAddTrack to use shared helper

**Files:**
- Modify: `src/ui/MainWindow.cpp`

**Context:** `MainWindow::onAddTrack()` (lines 1041–1058) hand-rolls a Volume-only automation lane. Replace with a call to the shared helper.

- [ ] **Step 1: Replace inline automation-lane creation with shared helper**

In `src/ui/MainWindow.cpp`, replace lines 1041–1058:

```cpp
    // Before (lines 1041-1058):
    juce::ValueTree autoList(IDs::AUTOMATION_LIST);
    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, "Volume", nullptr);
    autoTree.setProperty(IDs::paramID, 1, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);
    // ... point creation ...
    track.addChild(autoList, -1, &model.getUndoManager());

    // After:
    auto autoList = ProjectModel::createTrackAutomationList();
    track.addChild(autoList, -1, &model.getUndoManager());
```

In `MainWindow.cpp`, use the public static method that was added in Task 1:
```cpp
juce::ValueTree autoList = ProjectModel::createTrackAutomationList();
track.addChild(autoList, -1, &model.getUndoManager());
```

- [ ] **Step 2: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add src/model/ProjectModel.cpp src/model/ProjectModel.h src/ui/MainWindow.cpp
git commit -m "MainWindow: use shared createTrackAutomationList() in onAddTrack"
```

---

### Task 3: Fix mute dispatch in Track::processBlock

**Files:**
- Modify: `src/engine/Track.cpp`

**Context:** The automation dispatch loop at lines 217–229 handles pid 1 (volume) and 2 (pan) but not 3 (mute). Add pid==3 dispatch. The automation value is normalized 0.0–1.0; treat ≥0.5 as muted.

- [ ] **Step 1: Add pid==3 dispatch**

In `src/engine/Track.cpp` lines 223–227, add the mute branch:

```cpp
                    if (pid == 1)
                        volumeGain.setTargetValue(static_cast<float>(value));
                    else if (pid == 2)
                        panPosition.setTargetValue(static_cast<float>(value * 2.0f - 1.0f));
                    else if (pid == 3)
                        isMuted.store(value >= 0.5f);
```

Also, the `processBlock` function currently returns early for mute at line 203:
```cpp
    if (isMuted.load())
    {
        buffer.clear();
        return;
    }
```

This is correct for regular mute, but when automation is driving mute, the `isMuted` value changes during playback. The early return clears the buffer and exits, which is the right behavior — muted = silence. No change needed here.

However, we need to think about the interaction: the automation loop runs inside the `if (!isMuted)` block. So if the track is muted by automation, we never read automation. But that's fine — when it becomes unmuted, the next block will read the automation.

Actually wait — the issue is that `isMuted` is checked at the top of `processBlock` (line 203). If mute automation fires during playback, the check happens before the automation loop. So:
- If isMuted was `true` at block start → we skip automation → we can't unmute via automation
- If isMuted was `false` at block start → we run automation → automation can set it to true

This means once muted by automation, the track stays muted until some other path changes `isMuted`. The automation curve that un-mutes later won't take effect because automation itself is guarded by `!isMuted`.

**Fix**: Move the mute check to AFTER the automation loop, or at least only clear output (don't early-return). Restructure: compute automation values even when muted (so mute can be turned off), but zero the output if muted.

Replace the early-return with output-clearing at the end:

```cpp
void Track::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    // Read playhead position for automation
    double timeSec = 0.0;
    bool hasPosition = false;
    if (auto* ph = getPlayHead())
    {
        auto pos = ph->getPosition();
        if (pos && pos->getIsPlaying())
        {
            timeSec = pos->getTimeInSeconds().orFallback(0.0);
            hasPosition = true;
        }
    }

    // Apply automation
    if (hasPosition && stateLock.tryEnter())
    {
        for (const auto& am : automationManagers)
        {
            if (!am) continue;
            double value = am->getValueAtTime(timeSec);
            if (value >= 0.0)
            {
                int pid = am->getParamID();
                if (pid == 1)
                    volumeGain.setTargetValue(static_cast<float>(value));
                else if (pid == 2)
                    panPosition.setTargetValue(static_cast<float>(value * 2.0f - 1.0f));
                else if (pid == 3)
                    isMuted.store(value >= 0.5f);
            }
        }
        stateLock.exit();
    }

    // Early silent exit if muted (but we still ran automation above so mute can turn off)
    if (isMuted.load())
    {
        buffer.clear();
        return;
    }
    // ... rest unchanged
```

- [ ] **Step 2: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add src/engine/Track.cpp
git commit -m "Track: add mute (pid=3) dispatch in processBlock automation loop"
```

---

### Task 4: Enable mute automation recording

**Files:**
- Modify: `src/engine/AudioEngine.cpp`

**Context:** Line 255 excludes mute from on-the-fly recording with `property != IDs::isMuted`. Remove this exclusion.

- [ ] **Step 1: Remove mute exclusion**

In `AudioEngine.cpp` line 255, change:
```cpp
if (transportManager.isPlayingNow() && property != IDs::isMuted)
```
to:
```cpp
if (transportManager.isPlayingNow())
```

- [ ] **Step 2: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngine.cpp
git commit -m "AudioEngine: enable mute automation recording during playback"
```

---

### Task 5: Update MCP add_track to create 3 automation lanes

**Files:**
- Modify: `src/mcp/McpTools.cpp`

**Context:** The MCP `add_track` tool (line 360) creates an empty `AUTOMATION_LIST`. Replace with `ProjectModel::createTrackAutomationList()`.

- [ ] **Step 1: Update add_track handler**

In `McpTools.cpp` line 360, change:
```cpp
t.addChild(juce::ValueTree(IDs::AUTOMATION_LIST), -1, &um);
```
to:
```cpp
t.addChild(ProjectModel::createTrackAutomationList(), -1, &um);
```

Ensure the include for `ProjectModel.h` is present (it likely already is via other headers).

- [ ] **Step 2: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools.cpp
git commit -m "MCP: add_track creates Volume+Pan+Mute automation lanes"
```

---

### Task 6: Add plugin param registry and atomic cache to TrackFXSlot

**Files:**
- Modify: `src/engine/TrackFXSlot.h`

**Context:** Add `ParamInfo`, `getAutomatableParams()`, `setAutomationParam()`, `applyAutomation()`, and the `paramValues` atomic vector to `TrackFXSlot`.

- [ ] **Step 1: Add ParamInfo struct, paramValues vector, and new methods**

In `src/engine/TrackFXSlot.h`, inside the `public:` section of `TrackFXSlot` (after line 105 — after `getPluginInstance()`), add:

```cpp
    struct ParamInfo {
        juce::String name;
        int index;
    };

    const std::vector<ParamInfo>& getAutomatableParams() const
    {
        return cachedParams;
    }

    void setAutomationParam(int paramIndex, float normalizedValue)
    {
        if (paramIndex >= 0 && paramIndex < static_cast<int>(paramValues.size()))
            paramValues[paramIndex].store(normalizedValue, std::memory_order_relaxed);
    }

    void applyAutomation()
    {
        if (!isExternal || !pluginInstance) return;
        auto& params = pluginInstance->getParameters();
        for (int i = 0; i < static_cast<int>(paramValues.size()) && i < params.size(); ++i)
        {
            float v = paramValues[i].load(std::memory_order_relaxed);
            if (v >= 0.0f && v <= 1.0f)
                params[i]->setValue(v);
        }
    }
```

Add to `private:` section (after line 280 — before `};`):

```cpp
    mutable std::vector<ParamInfo> cachedParams;
    std::vector<std::atomic<float>> paramValues;
```

Add `rebuildParamCache()` call to the plugin constructor and at the end of `prepare()` when the plugin instance is set.

In the plugin constructor (`TrackFXSlot(std::unique_ptr<juce::AudioPluginInstance> plugin, ...)`), after line 92 (`activeType = ActiveType::Plugin;`), add:

```cpp
        rebuildParamCache();
```

Add `rebuildParamCache()` as a private method:

```cpp
    void rebuildParamCache()
    {
        cachedParams.clear();
        paramValues.clear();
        if (!isExternal || !pluginInstance) return;
        auto& params = pluginInstance->getParameters();
        paramValues.resize(params.size());
        cachedParams.reserve(params.size());
        for (int i = 0; i < params.size(); ++i)
        {
            cachedParams.push_back({params[i]->getName(64), i});
            paramValues[i].store(params[i]->getValue(), std::memory_order_relaxed);
        }
    }
```

Call `rebuildParamCache()` from `prepare()` when the plugin instance exists — add at the top of `prepare()` after the `isExternal && pluginInstance` check:

```cpp
    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        if (isExternal && pluginInstance)
        {
            pluginInstance->prepareToPlay(spec.sampleRate, spec.maximumBlockSize);
            rebuildParamCache();
            return;
        }
        // ... rest unchanged
    }
```

- [ ] **Step 2: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add src/engine/TrackFXSlot.h
git commit -m "TrackFXSlot: add plugin param registry and atomic automation cache"
```

---

### Task 7: Add plugin param dispatch in Track::processBlock

**Files:**
- Modify: `src/engine/Track.cpp`

**Context:** After the existing pid 1/2/3 dispatch, add fallthrough for pid >= 100 (plugin params). Also call `applyAutomation()` before each slot's `process()`.

- [ ] **Step 1: Add pid >= 100 dispatch**

In `Track::processBlock`, after the mute branch at line 227, add:

```cpp
                    else if (pid >= 100)
                    {
                        int si = (pid - 100) / 100;
                        int pi = (pid - 100) % 100;
                        if (si < static_cast<int>(fxChain.size()) && fxChain[si])
                            fxChain[si]->setAutomationParam(pi, static_cast<float>(value));
                    }
```

- [ ] **Step 2: Add applyAutomation() before slot->process()**

In `Track::processBlock`, in the FX chain loop (lines 236–242), modify:

```cpp
    if (stateLock.tryEnter())
    {
        for (const auto& slot : fxChain)
        {
            if (slot)
            {
                slot->applyAutomation();
                slot->process(buffer, midiMessages);
            }
        }
        stateLock.exit();
    }
```

- [ ] **Step 3: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/engine/Track.cpp
git commit -m "Track: dispatch plugin param automation (pid>=100) and apply before FX process"
```

---

### Task 8: Add "Add Automation Lane" UI button and menu

**Files:**
- Modify: `src/ui/AutomationLaneWidget.h`
- Modify: `src/ui/AutomationLaneWidget.cpp`

**Context:** Add a `+` button beside the param combobox that opens a menu listing all available automatable parameters (track-level + per-slot plugin params). Selecting one creates a new automation lane. Add a `-` button to remove the current lane.

- [ ] **Step 1: Add buttons and methods to header**

In `src/ui/AutomationLaneWidget.h`, after `paramCombo` member, add:

```cpp
    QPushButton* addLaneBtn;
    QPushButton* removeLaneBtn;
```

Add private method declarations:

```cpp
    void addAutomationLane(const QString& name, int paramID);
    void removeCurrentLane();
    void showAddLaneMenu();
```

Add `#include <QPushButton>` and `#include <QMenu>` to the includes (or add them in the .cpp).

- [ ] **Step 2: Create buttons in constructor**

In `src/ui/AutomationLaneWidget.cpp`, after `headerLayout->addWidget(paramCombo, 1);` (line 33), add:

```cpp
    addLaneBtn = new QPushButton("+", header);
    addLaneBtn->setFixedSize(22, 22);
    addLaneBtn->setToolTip("Add automation lane");
    connect(addLaneBtn, &QPushButton::clicked, this, &AutomationLaneWidget::showAddLaneMenu);
    headerLayout->addWidget(addLaneBtn);

    removeLaneBtn = new QPushButton("-", header);
    removeLaneBtn->setFixedSize(22, 22);
    removeLaneBtn->setToolTip("Remove current automation lane");
    connect(removeLaneBtn, &QPushButton::clicked, this, &AutomationLaneWidget::removeCurrentLane);
    headerLayout->addWidget(removeLaneBtn);
```

- [ ] **Step 3: Implement showAddLaneMenu**

Add before `mousePressEvent`:

```cpp
void AutomationLaneWidget::showAddLaneMenu()
{
    if (currentTrack < 0) return;

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(currentTrack);
    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);

    // Collect existing paramIDs to avoid duplicates
    QSet<int> existingParamIDs;
    if (autoList.isValid())
    {
        for (int i = 0; i < autoList.getNumChildren(); ++i)
            existingParamIDs.insert(static_cast<int>(autoList.getChild(i).getProperty(IDs::paramID)));
    }

    QMenu menu(this);

    // Track-level parameters
    QAction* trackHeader = menu.addAction("Track Parameters");
    trackHeader->setEnabled(false);
    QFont f = menu.font();
    f.setBold(true);
    trackHeader->setFont(f);

    auto addEntry = [&](const QString& name, int paramID) {
        if (!existingParamIDs.contains(paramID))
        {
            auto* action = menu.addAction(name);
            connect(action, &QAction::triggered, this, [this, name, paramID]() {
                addAutomationLane(name, paramID);
            });
        }
    };

    addEntry("Volume", 1);
    addEntry("Pan", 2);
    addEntry("Mute", 3);

    // Per-slot plugin parameters
    auto* routing = engine.getMainProcessor()->getRoutingManager();
    if (routing != nullptr)
    {
        auto* track = routing->getTrackNode(currentTrack);
        if (track != nullptr)
        {
            auto& fxChain = track->getFXChain();
            for (int si = 0; si < static_cast<int>(fxChain.size()); ++si)
            {
                auto& slot = fxChain[si];
                if (!slot || !slot->isPlugin() || slot->isBypassed()) continue;

                auto params = slot->getAutomatableParams();
                if (params.empty()) continue;

                menu.addSeparator();
                juce::String slotName = "Slot " + juce::String(si + 1);
                if (auto* inst = slot->getPluginInstance())
                    slotName += ": " + inst->getName();
                QAction* slotHeader = menu.addAction(QString::fromUtf8(slotName.toRawUTF8()));
                slotHeader->setEnabled(false);
                slotHeader->setFont(f);

                for (const auto& p : params)
                {
                    int compoundID = 100 + si * 100 + p.index;
                    QString paramName = QString::fromUtf8(p.name.toRawUTF8());
                    if (paramName.trimmed().isEmpty())
                        paramName = QString("Param %1").arg(p.index);
                    if (!existingParamIDs.contains(compoundID))
                    {
                        auto* action = menu.addAction(paramName);
                        connect(action, &QAction::triggered, this, [this, paramName, compoundID]() {
                            addAutomationLane(paramName, compoundID);
                        });
                    }
                }
            }
        }
    }

    if (menu.actions().size() <= 1)
    {
        menu.addAction("(no available parameters)")->setEnabled(false);
    }

    menu.exec(addLaneBtn->mapToGlobal(QPoint(0, addLaneBtn->height())));
}
```

- [ ] **Step 4: Implement addAutomationLane**

```cpp
void AutomationLaneWidget::addAutomationLane(const QString& name, int paramID)
{
    if (currentTrack < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (currentTrack >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(currentTrack);

    juce::ValueTree autoTree(IDs::AUTOMATION);
    autoTree.setProperty(IDs::name, juce::String(name.toUtf8().constData()), nullptr);
    autoTree.setProperty(IDs::paramID, paramID, nullptr);
    autoTree.setProperty(IDs::curveType, "linear", nullptr);
    autoTree.setProperty(IDs::automationEnabled, false, nullptr);

    juce::ValueTree pointList(IDs::POINT_LIST);
    juce::ValueTree pt(IDs::POINT);
    pt.setProperty(IDs::startTime, 0.0, nullptr);
    pt.setProperty(IDs::gain, 0.5, nullptr);
    pointList.addChild(pt, -1, nullptr);
    autoTree.addChild(pointList, -1, nullptr);

    auto autoList = trackTree.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
    {
        autoList = juce::ValueTree(IDs::AUTOMATION_LIST);
        trackTree.addChild(autoList, -1, &engine.getProjectModel().getUndoManager());
    }
    int newIdx = autoList.getNumChildren();
    autoList.addChild(autoTree, -1, &engine.getProjectModel().getUndoManager());
    currentParamIndex = newIdx;
    refreshParamCombo();
    emit automationChanged();
    update();
}
```

- [ ] **Step 5: Implement removeCurrentLane**

```cpp
void AutomationLaneWidget::removeCurrentLane()
{
    if (currentTrack < 0 || currentParamIndex < 0) return;
    auto autoTree = currentAutoTree();
    if (!autoTree.isValid()) return;

    auto autoList = autoTree.getParent();
    if (!autoList.isValid()) return;

    autoList.removeChild(currentParamIndex, &engine.getProjectModel().getUndoManager());

    if (currentParamIndex >= autoList.getNumChildren())
        currentParamIndex = std::max(0, autoList.getNumChildren() - 1);

    refreshParamCombo();
    emit automationChanged();
    update();
}
```

- [ ] **Step 6: Add includes in .cpp**

In `AutomationLaneWidget.cpp`, add:
```cpp
#include <QPushButton>
#include <QMenu>
#include <QSet>
#include <QAction>
```
And forward-declare or include the routing header. The `RoutingManager` is accessed via `engine.getMainProcessor()->getRoutingManager()`. The `Track` class needs to be fully defined for `getFXChain()`. Add:
```cpp
#include "../engine/RoutingManager.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
```

- [ ] **Step 7: Build & verify**

```bash
cmake --build build --config Debug 2>&1 | tail -20
```

Expected: compiles without errors.

- [ ] **Step 8: Commit**

```bash
git add src/ui/AutomationLaneWidget.h src/ui/AutomationLaneWidget.cpp
git commit -m "AutomationLaneWidget: add/add/remove lane UI for track and plugin params"
```

---

### Task 9: Verify and test

- [ ] **Step 1: Run full build**

```bash
cmake --build build --config Debug 2>&1 | tail -40
```

Expected: clean compile, no warnings.

- [ ] **Step 2: Run tests**

```bash
ctest --test-dir build -C Debug --output-on-failure 2>&1
```

Expected: all existing tests pass (no regressions). Note: there are no specific automation tests yet, so existing tests should pass unchanged.

- [ ] **Step 3: Smoke test checklist (manual)**

1. Launch `build/Debug/HDAW.exe`
2. Check each default track's Automation tab shows Volume, Pan, Mute
3. Add a new track → same 3 lanes present
4. Draw a pan curve → playback pans correctly
5. Draw a mute curve → track mutes/unmutes at curve transitions
6. Save project → reload → lanes and points persist
7. Add a plugin to an FX slot → "+" menu shows plugin params
8. Create a lane for a plugin param → param changes during playback
9. Remove a plugin → stale lane in list doesn't crash
10. Remove a lane via "-" button → lane removed, no crash

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "automation: full parameter coverage (Volume+Pan+Mute+plugin params)"
```
