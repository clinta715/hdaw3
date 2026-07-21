# GUI Inspection MCP Tools — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development for parallel task execution where tasks are independent.

**Goal:** Add 8 `gui.*` MCP tools that let an LLM client observe HDAW's Qt GUI layout (clip positions, track headers, selection, scroll, panel state, piano roll, hit-test), plus a 9-case gtest integration suite verifying headless fallback behavior.

> **Architecture note:** The primary GUI is the React/Electron frontend.
> The Qt 6 desktop GUI is deprecated (`-DHDAW_GUI=ON` only). These tools
> inspect Qt widget state and return `{available: false}` in the default
> browser/Electron modes. For the React frontend, model-state inspection
> is already covered by existing MCP tools (`read.snapshot`, `list_tracks`,
> `list_clips`, etc.).

**Architecture:** A `GuiInspector` helper class (`src/ui/GuiInspector.{h,cpp}`) walks the widget tree from `MainWindow*` (when available) and returns structured `QJsonObject`/`QJsonArray`. When `HDAW_GUI` is not defined, a stub implementation returns empty/unavailable responses. A new `registerGuiInspectTools` registrar adds 8 tools to the existing `McpServer`. Tests use the existing `TransportLoopback` seam to verify headless fallback behavior.

**Tech Stack:** C++17, Qt 6 Widgets, JUCE ValueTree, gtest, MCP JSON-RPC

**Reference:** `docs/superpowers/specs/2026-07-21-gui-inspection-mcp-tools-design.md`

---

## File structure

**New files (module):**
- `src/ui/GuiInspector.h` — helper class declaration
- `src/ui/GuiInspector.cpp` — widget-tree walking implementation
- `src/mcp/McpGuiInspectTools.h` — registrar declaration
- `src/mcp/McpGuiInspectTools.cpp` — 8 tool registrations

**Modified files:**
- `src/engine/AudioEngine.h` — add `mainWindow_` pointer + `setMainWindow`/`getMainWindow`
- `src/engine/AudioEngine.cpp` — implement getter/setter
- `src/ui/MainWindow.h` — add `#include` forward-decl, call `engine.setMainWindow(this)` in ctor
- `src/ui/MainWindow.cpp` — wire `setMainWindow(this)` in constructor, `setMainWindow(nullptr)` in destructor
- `src/mcp/McpTools.cpp` — add `#include "McpGuiInspectTools.h"` and call `registerGuiInspectTools(s, e)` in `registerAllTools`
- `CMakeLists.txt` — add 4 new source files to `HDAW_lib`
- `tests/CMakeLists.txt` — add `gui_inspect_test.cpp` to test sources

**New test files:**
- `tests/integration/mcp/gui_inspect_test.cpp` — 9 integration tests (tool registration + 8 headless fallback)

---

## Task 1: AudioEngine MainWindow pointer

**Files:** `src/engine/AudioEngine.h`, `src/engine/AudioEngine.cpp`

- [ ] **Step 1: Add forward declaration and members to AudioEngine.h**

In `src/engine/AudioEngine.h`, add a forward declaration before the class and two methods in the public section:

```cpp
class MainWindow;
```

In the public section of `AudioEngine`:

```cpp
void setMainWindow(MainWindow* mw) { mainWindow_ = mw; }
MainWindow* getMainWindow() const { return mainWindow_; }
```

In the private section:

```cpp
MainWindow* mainWindow_ = nullptr;
```

- [ ] **Step 2: No .cpp changes needed**

The getter/setter are inline in the header. No `AudioEngine.cpp` modification required.

---

## Task 2: MainWindow wires the pointer

**Files:** `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp`

- [ ] **Step 1: Wire setMainWindow in MainWindow constructor**

At the end of `MainWindow::MainWindow(...)` (after `setupLayout()` and `restoreWindowGeometry()`), add:

```cpp
engine.setMainWindow(this);
```

- [ ] **Step 2: Clear pointer in destructor**

In `MainWindow::~MainWindow()`, add at the top:

```cpp
engine.setMainWindow(nullptr);
```

---

## Task 3: GuiInspector helper class

**Files:** `src/ui/GuiInspector.h`, `src/ui/GuiInspector.cpp`

- [ ] **Step 1: Write GuiInspector.h**

```cpp
#pragma once
#include <QJsonObject>
#include <QJsonArray>

class MainWindow;

namespace HDAW {

class GuiInspector {
public:
    explicit GuiInspector(MainWindow* mw);

    bool isAvailable() const;

    QJsonObject snapshot() const;
    QJsonArray clipGeometry(int clipId = -1) const;
    QJsonArray trackLayout() const;
    QJsonObject selectionState() const;
    QJsonObject scrollState() const;
    QJsonObject panelState() const;
    QJsonObject pianoRollState() const;
    QJsonObject hitTest(double sceneX, double sceneY) const;

private:
    MainWindow* mw;
};

} // namespace HDAW
```

- [ ] **Step 2: Write GuiInspector.cpp**

```cpp
#include "GuiInspector.h"
#include "MainWindow.h"
#include "TimelineView.h"
#include "TimelineScene.h"
#include "ClipItem.h"
#include "TrackHeaderWidget.h"
#include "PianoRollWidget.h"
#include "NoteGridWidget.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"
#include <QGraphicsView>
#include <QScrollBar>
#include <QStackedWidget>
#include <QGraphicsScene>

namespace HDAW {

GuiInspector::GuiInspector(MainWindow* mw) : mw(mw) {}

bool GuiInspector::isAvailable() const { return mw != nullptr; }

QJsonObject GuiInspector::snapshot() const
{
    QJsonObject obj;
    obj["available"] = isAvailable();
    if (!isAvailable())
    {
        obj["reason"] = "no_gui";
        obj["hint"] = "Launch with --gui to enable GUI inspection tools";
        return obj;
    }

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    double pps = scene ? scene->getPixelsPerSecond() : 0.0;
    obj["pixelsPerSecond"] = pps;

    QJsonObject timeline;
    if (scene)
    {
        auto sr = scene->sceneRect();
        timeline["sceneWidth"] = sr.width();
        timeline["sceneHeight"] = sr.height();
        timeline["trackCount"] = scene->getTrackCount();
    }
    auto clips = clipGeometry();
    timeline["clipCount"] = clips.size();
    obj["timeline"] = timeline;

    obj["selection"] = selectionState();
    obj["panel"] = panelState();
    obj["tracks"] = trackLayout();
    obj["clips"] = clips;
    return obj;
}

QJsonArray GuiInspector::clipGeometry(int clipId) const
{
    QJsonArray arr;
    if (!isAvailable()) return arr;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) return arr;

    double pps = scene->getPixelsPerSecond();

    for (auto* item : scene->items())
    {
        auto* clip = dynamic_cast<ClipItem*>(item);
        if (!clip) continue;

        auto tree = clip->getClipTree();
        int id = tree.getProperty(IDs::clipId, 0);
        if (clipId >= 0 && id != clipId) continue;

        int trackIdx = scene->trackIndexAtY(clip->pos().y());
        QJsonObject c;
        c["clipId"] = id;
        c["trackIndex"] = trackIdx;
        c["x"] = clip->pos().x();
        c["y"] = clip->pos().y();
        c["width"] = clip->boundingRect().width();
        c["height"] = clip->boundingRect().height();
        c["selected"] = clip->isSelected();
        c["visible"] = clip->isVisible();
        c["type"] = QString::fromStdString(
            tree.getProperty(IDs::clipType).toString().toStdString());
        c["name"] = QString::fromStdString(
            tree.getProperty(IDs::name).toString().toStdString());
        arr.append(c);
    }
    return arr;
}

QJsonArray GuiInspector::trackLayout() const
{
    QJsonArray arr;
    if (!isAvailable()) return arr;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) return arr;

    int count = scene->getTrackCount();
    for (int i = 0; i < count; ++i)
    {
        QJsonObject t;
        t["index"] = i;
        t["y"] = scene->getTrackY(i);
        t["height"] = scene->getTrackHeight(i);

        auto trackList = mw->getEngine().getProjectModel().getTrackListTree();
        if (i < trackList.getNumChildren())
        {
            auto trackTree = trackList.getChild(i);
            t["name"] = QString::fromStdString(
                trackTree.getProperty(IDs::name).toString().toStdString());
            t["muted"] = trackTree.getProperty(IDs::isMuted, false);
            t["soloed"] = trackTree.getProperty(IDs::isSoloed, false);
            t["armed"] = trackTree.getProperty(IDs::isArmed, false);
            t["clipCount"] = trackTree.getChildWithName(IDs::CLIP_LIST).getNumChildren();
        }
        arr.append(t);
    }
    return arr;
}

QJsonObject GuiInspector::selectionState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;

    QJsonArray selectedClips;
    if (scene)
    {
        for (auto* item : scene->selectedItems())
        {
            auto* clip = dynamic_cast<ClipItem*>(item);
            if (clip)
                selectedClips.append(
                    clip->getClipTree().getProperty(IDs::clipId, 0));
        }
    }
    obj["selectedClips"] = selectedClips;
    obj["selectedTrack"] = tv ? tv->property("selectedTrack").toInt() : -1;
    return obj;
}

QJsonObject GuiInspector::scrollState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* tv = mw->findChild<TimelineView*>();
    if (!tv) return obj;

    auto* gv = tv->findChild<QGraphicsView*>();
    if (gv)
    {
        obj["timelineScrollX"] = gv->horizontalScrollBar()->value();
        obj["timelineScrollY"] = gv->verticalScrollBar()->value();
    }

    auto* scene = tv->getScene();
    if (scene)
        obj["pixelsPerSecond"] = scene->getPixelsPerSecond();

    return obj;
}

QJsonObject GuiInspector::panelState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* stack = mw->findChild<QStackedWidget*>();
    if (!stack) return obj;

    int idx = stack->currentIndex();
    static const char* tabNames[] = {
        "Mixer", "PianoRoll", "FxChain", "Automation",
        "AudioEditor", "StepSequencer", "Modulation"
    };
    obj["activeTabIndex"] = idx;
    obj["activeTab"] = (idx >= 0 && idx < 7) ? tabNames[idx] : "Unknown";
    obj["tabCount"] = stack->count();

    QJsonArray tabs;
    for (int i = 0; i < stack->count() && i < 7; ++i)
        tabs.append(QString(tabNames[i]));
    obj["tabs"] = tabs;
    return obj;
}

QJsonObject GuiInspector::pianoRollState() const
{
    QJsonObject obj;
    if (!isAvailable()) return obj;

    auto* pr = mw->findChild<PianoRollWidget*>();
    if (!pr) { obj["loaded"] = false; return obj; }

    auto* noteGrid = pr->findChild<NoteGridWidget*>();
    obj["loaded"] = (noteGrid != nullptr);

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    double pps = scene ? scene->getPixelsPerSecond() : 10.0;

    auto trackList = mw->getEngine().getProjectModel().getTrackListTree();
    QJsonArray notes;

    for (int ti = 0; ti < trackList.getNumChildren(); ++ti)
    {
        auto trackTree = trackList.getChild(ti);
        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
        {
            auto clipTree = clipList.getChild(ci);
            if (!clipTree.getProperty(IDs::isMidi, false)) continue;
            auto noteList = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int ni = 0; ni < noteList.getNumChildren(); ++ni)
            {
                auto noteTree = noteList.getChild(ni);
                QJsonObject n;
                n["noteId"] = noteTree.getProperty(IDs::noteId, 0);
                n["pitch"] = noteTree.getProperty(IDs::pitch, 60);
                n["velocity"] = noteTree.getProperty(IDs::velocity, 100);
                n["startBeat"] = noteTree.getProperty(IDs::startTime, 0.0);
                n["durationBeats"] = noteTree.getProperty(IDs::duration, 1.0);
                notes.append(n);
            }
        }
    }
    obj["notes"] = notes;
    return obj;
}

QJsonObject GuiInspector::hitTest(double sceneX, double sceneY) const
{
    QJsonObject obj;
    if (!isAvailable()) { obj["hit"] = false; return obj; }

    auto* tv = mw->findChild<TimelineView*>();
    auto* scene = tv ? tv->getScene() : nullptr;
    if (!scene) { obj["hit"] = false; return obj; }

    auto* item = scene->itemAt(sceneX, sceneY, QTransform());
    auto* clip = dynamic_cast<ClipItem*>(item);
    if (clip)
    {
        auto tree = clip->getClipTree();
        obj["hit"] = true;
        obj["type"] = "clip";
        obj["clipId"] = tree.getProperty(IDs::clipId, 0);
        obj["trackIndex"] = scene->trackIndexAtY(clip->pos().y());
        obj["name"] = QString::fromStdString(
            tree.getProperty(IDs::name).toString().toStdString());
    }
    else
    {
        obj["hit"] = false;
    }
    return obj;
}

} // namespace HDAW
```

---

## Task 4: MCP tool registrations

**Files:** `src/mcp/McpGuiInspectTools.h`, `src/mcp/McpGuiInspectTools.cpp`

- [ ] **Step 1: Write McpGuiInspectTools.h**

```cpp
#pragma once
class McpServer;
class AudioEngine;
namespace mcp { void registerGuiInspectTools(McpServer& server, AudioEngine* e); }
```

- [ ] **Step 2: Write McpGuiInspectTools.cpp**

```cpp
#include "McpGuiInspectTools.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../ui/GuiInspector.h"
#include "../ui/MainWindow.h"
#include <QJsonDocument>

namespace mcp {

static QJsonObject objSchema(const QJsonObject& props,
                             const QJsonArray& required = {})
{
    QJsonObject s;
    s["type"] = "object";
    s["properties"] = props;
    if (!required.isEmpty()) s["required"] = required;
    s["additionalProperties"] = false;
    return s;
}

static McpToolResult jsonResult(const QJsonObject& obj)
{
    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

static McpToolResult jsonResult(const QJsonArray& arr)
{
    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void registerGuiInspectTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({
        "gui.snapshot",
        "Full structured dump of the GUI state: timeline, tracks, clips, selection, panel.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.snapshot());
        }
    });

    s.registerTool({
        "gui.get_clip_geometry",
        "Get scene-space position and size of clips on the timeline.",
        objSchema({{"clipId", QJsonObject{{"type", "integer"},
            {"description", "Optional clip ID filter; omit for all clips"}}}}),
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            int clipId = a.contains("clipId") ? a["clipId"].toInt() : -1;
            return jsonResult(inspector.clipGeometry(clipId));
        }
    });

    s.registerTool({
        "gui.get_track_layout",
        "Get track header geometry and state (name, y, height, mute/solo/arm).",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.trackLayout());
        }
    });

    s.registerTool({
        "gui.get_selection",
        "Get current selection state (selected track, clips, notes).",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.selectionState());
        }
    });

    s.registerTool({
        "gui.get_scroll",
        "Get timeline scroll position and zoom (pixelsPerSecond).",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.scrollState());
        }
    });

    s.registerTool({
        "gui.get_panel_state",
        "Get the active bottom-panel tab and tab list.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.panelState());
        }
    });

    s.registerTool({
        "gui.get_piano_roll",
        "Get piano-roll contents (notes with positions) for the loaded MIDI clip.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.pianoRollState());
        }
    });

    s.registerTool({
        "gui.hit_test",
        "Hit-test a scene coordinate and return what item is there.",
        objSchema({
            {"x", QJsonObject{{"type", "number"}, {"description", "Scene X coordinate"}}},
            {"y", QJsonObject{{"type", "number"}, {"description", "Scene Y coordinate"}}}
        }, {"x", "y"}),
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.hitTest(a["x"].toDouble(), a["y"].toDouble()));
        }
    });
}

} // namespace mcp
```

---

## Task 5: Wire into registerAllTools and CMake

**Files:** `src/mcp/McpTools.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Add include and call in McpTools.cpp**

At the top of `src/mcp/McpTools.cpp`, add:

```cpp
#include "McpGuiInspectTools.h"
```

In `registerAllTools`, after `registerProjectTools(s, e);`, add:

```cpp
registerGuiInspectTools(s, e);
```

- [ ] **Step 2: Add sources to CMakeLists.txt**

In the `HDAW_lib` source list, add:

```cmake
src/ui/GuiInspector.h
src/ui/GuiInspector.cpp
src/mcp/McpGuiInspectTools.h
src/mcp/McpGuiInspectTools.cpp
```

---

## Task 6: MainWindow public accessor for engine

**Files:** `src/ui/MainWindow.h`

- [ ] **Step 1: Add public getEngine accessor**

`GuiInspector` needs to reach `AudioEngine&` from `MainWindow*`. Add to the public section of `MainWindow`:

```cpp
AudioEngine& getEngine() { return engine; }
```

If this accessor already exists (check before editing), skip this step.

---

## Task 7: Integration tests

**Files:** `tests/integration/mcp/gui_inspect_test.cpp`, `tests/CMakeLists.txt`

- [ ] **Step 1: Add test file to tests/CMakeLists.txt**

Add to the test source list:

```cmake
integration/mcp/gui_inspect_test.cpp
```

- [ ] **Step 2: Write gui_inspect_test.cpp**

Tests verify tool registration and headless fallback behavior. Since the
default build has no Qt Widgets (`HDAW_GUI=OFF`), all `gui.*` tools return
`{available: false}` or empty results.

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

QJsonObject callTool(mcp::TransportLoopback& tp, const QString& name,
                     const QJsonObject& args = {})
{
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = "tools/call";
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    req["params"] = params;

    tp.drainOutgoing();
    tp.pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    if (!tp.waitForOutgoing(500, &out)) return {};
    auto resp = parseOne(out);
    auto result = resp.value("result").toObject();
    auto content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    auto text = content[0].toObject()["text"].toString();
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QJsonArray callToolArray(mcp::TransportLoopback& tp, const QString& name,
                         const QJsonObject& args = {})
{
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = "tools/call";
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    req["params"] = params;

    tp.drainOutgoing();
    tp.pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    if (!tp.waitForOutgoing(500, &out)) return {};
    auto resp = parseOne(out);
    auto result = resp.value("result").toObject();
    auto content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    auto text = content[0].toObject()["text"].toString();
    return QJsonDocument::fromJson(text.toUtf8()).array();
}

} // namespace

TEST(GuiInspect, ToolsRegistered) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);

    const auto& tools = s.tools();
    EXPECT_TRUE(tools.contains("gui.snapshot"));
    EXPECT_TRUE(tools.contains("gui.get_clip_geometry"));
    EXPECT_TRUE(tools.contains("gui.get_track_layout"));
    EXPECT_TRUE(tools.contains("gui.get_selection"));
    EXPECT_TRUE(tools.contains("gui.get_scroll"));
    EXPECT_TRUE(tools.contains("gui.get_panel_state"));
    EXPECT_TRUE(tools.contains("gui.get_piano_roll"));
    EXPECT_TRUE(tools.contains("gui.hit_test"));
}

TEST(GuiInspect, HeadlessSnapshotReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto snap = callTool(tp, "gui.snapshot");
    EXPECT_FALSE(snap["available"].toBool());
    EXPECT_EQ(snap["reason"].toString().toStdString(), "no_gui");

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessClipGeometryReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto clips = callToolArray(tp, "gui.get_clip_geometry");
    EXPECT_TRUE(clips.isEmpty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessTrackLayoutReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto tracks = callToolArray(tp, "gui.get_track_layout");
    EXPECT_TRUE(tracks.isEmpty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessHitTestReturnsNoHit) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto hit = callTool(tp, "gui.hit_test", {{"x", 100.0}, {"y", 100.0}});
    EXPECT_FALSE(hit["hit"].toBool());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessPanelStateReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto panel = callTool(tp, "gui.get_panel_state");
    EXPECT_TRUE(panel.empty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessPianoRollReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto pr = callTool(tp, "gui.get_piano_roll");
    EXPECT_FALSE(pr["loaded"].toBool());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessScrollStateReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto scroll = callTool(tp, "gui.get_scroll");
    EXPECT_TRUE(scroll.empty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessSelectionReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto sel = callTool(tp, "gui.get_selection");
    EXPECT_TRUE(sel.empty());

    s.stop();
    s.setTransport(nullptr);
}
```

---

## Task 8: Build and verify

- [ ] **Step 1: Build**

```powershell
cmake --build build --config Debug
```

- [ ] **Step 2: Run tests**

```powershell
build\Debug\hdaw_tests.exe --gtest_filter=GuiInspect.*
```

Expected: 9 tests pass (tool registration + 8 headless fallback tests).

- [ ] **Step 3: Verify MCP tool listing**

Launch HDAW with `--mcp-stdio`, send an `initialize` + `tools/list` request,
and confirm the 8 `gui.*` tools appear in the response. In the default
browser mode, calling any `gui.*` tool returns `{available: false}`.

Launch HDAW with `--gui --mcp-stdio`, send an `initialize` + `tools/list` request, and confirm the 8 `gui.*` tools appear in the response.
