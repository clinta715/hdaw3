# Insert Silence & Duplicate Region Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two arrangement-editing operations that complete the ripple-delete pillar: **insert silence** (split clips at a point, shift later content right to open a gap) and **duplicate region** (copy all content in a range and paste it at the range end, shifting later content right). Both reachable from the timeline context menu (loop-region range or derived selection), over RPC, and over MCP.

**Architecture:** Both compose from the same proven primitives as `rippleDelete` (`AudioEngineCommands_Clips.cpp`): `ProjectModel::sliceClipAtTimes` (model-level, no per-clip rebuild), direct `startTime` shifts, and — for duplicate region — `clip.createCopy()` + `ProjectModel::allocateClipID()`. Each runs in one `beginTransaction`/`endTransaction` with a single `rebuildRoutingGraph()` at the end (per AGENTS.md lesson 6). Beats at the RPC boundary, seconds inside (lesson #1). The three region ops (ripple/insert/duplicate) share structure but vary in slice-point count (1 vs 2), shift direction (left vs right), and whether they remove or copy — they're kept as separate readable commands rather than an over-abstracted helper (each is ~30 lines; a shared helper would obscure the per-op logic). `rippleDelete` is the reference implementation to mirror.

**Tech Stack:** C++/JUCE engine, JSON-RPC 2.0, React 19 + TypeScript + Zustand, gtest, Vitest + Playwright, MCP.

**Verified facts (from the ripple-delete work):**
- `ProjectModel::sliceClipAtTimes(clip, times, &um)` takes timeline-absolute **seconds**, removes the original, assigns new ids to pieces. `sliceClipAtTimes` (command-layer) rebuilds per call — **don't** use it in a loop; call the model static directly.
- `clip.createCopy()` deep-copies the ValueTree subtree (MIDI notes, gain envelope, offset, etc.). `ProjectModel::allocateClipID()` mints a unique id (see `duplicateClips` at `AudioEngineCommands_Clips.cpp:386-388`).
- Default BPM 120 → factor 0.5 s/beat. Default project: **track 0 is empty**, track 1 ("Synth") ships Melody/Chords — tests use track 0 and scope snapshot assertions by `trackIndex` (AGENTS.md lesson 7).
- `requireDouble` at `FrontendRouter.cpp:51`; MCP tools register via `s.registerTool({...})` in `registerClipTools` (`McpTools_Project.cpp:312`).
- `beatsToSeconds` is a file-local static at the top of `AudioEngineCommands_Clips.cpp`.

**Range semantics (mirrors ripple delete for consistency):** the loop region `[loopStart, loopEnd)` (or the selected clips' bounds) defines `[Rs, Re]`. For insert silence, `Rs` is the insertion point and `Re - Rs` is the duration of the gap opened. For duplicate region, content in `[Rs, Re]` is copied to `[Re, Re + (Re-Rs)]`.

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `src/common/ProjectCommands.h` | Abstract command interface | Modify — add `insertSilence`, `duplicateRegion` virtuals |
| `src/engine/AudioEngineCommands.h` | Concrete command decls | Modify — add `override` decls |
| `src/engine/AudioEngineCommands_Clips.cpp` | Implementations | Modify — add both impls (next to `rippleDelete`) |
| `tests/unit/engine/region_ops_test.cpp` | gtest suite | Create |
| `src/frontend/FrontendRouter.cpp` | JSON-RPC dispatch | Modify — add `project.insertSilence`, `project.duplicateRegion` |
| `src/mcp/McpTools_Project.cpp` | MCP tools | Modify — add `insert_silence`, `duplicate_region` in `registerClipTools` |
| `frontend/src/components/TimelineContextMenu.tsx` | Timeline menu | Modify — add four items (two per op: range + selection) |
| `frontend/e2e/timeline-context-menu.spec.ts` | Playwright E2E | Modify — add two tests |

---

## Task 1: `insertSilence` engine command

Slice at the single insertion point, shift later content right. One slice point (simpler than ripple's two).

**Files:**
- Modify: `src/common/ProjectCommands.h`, `src/engine/AudioEngineCommands.h`, `src/engine/AudioEngineCommands_Clips.cpp`
- Create: `tests/unit/engine/region_ops_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/engine/region_ops_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

#include <cmath>
#include <vector>

namespace {

// Region ops are project-wide; the default project ships track 1 ("Synth")
// with Melody/Chords. Tests use the EMPTY track 0 and scope by trackIndex
// (AGENTS.md lesson 7).
std::vector<ClipSnapshot> clipsOnTrack0(AudioEngine& engine)
{
    std::vector<ClipSnapshot> out;
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.trackIndex == 0)
            out.push_back(c);
    return out;
}

} // namespace

// BPM 120 -> 0.5 s/beat; all values convert exactly.

// Insert silence [2,6): a clip after 6 shifts right by 4; the gap [2,6) is empty.
TEST(InsertSilence, ShiftsLaterClipsRightAndOpensGap)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int after = cmds.addMidiClip(0, 8.0, 4.0, "after");   // beats [8,12)
    ASSERT_GT(after, 0);

    cmds.insertSilence(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_EQ(t0[0].clipId, after);
    EXPECT_NEAR(t0[0].startBeat, 12.0, 1e-6);   // 8 + 4
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}

// A clip straddling the insertion point is split: [0,4) over insert at 2 -> [0,2) + [6,10).
TEST(InsertSilence, SplitsClipStraddlingInsertionPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 4.0, "head");   // beats [0,4), insertion point at beat 2

    cmds.insertSilence(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 2u);
    bool foundHead = false, foundTail = false;
    for (const auto& c : t0)
    {
        if (std::abs(c.startBeat - 0.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundHead = true; }
        // tail [2,4) shifted right by 4 -> [6,10)
        if (std::abs(c.startBeat - 6.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundTail = true; }
    }
    EXPECT_TRUE(foundHead);
    EXPECT_TRUE(foundTail);
}

// Empty / inverted range is a no-op.
TEST(InsertSilence, EmptyRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int c = cmds.addMidiClip(0, 4.0, 4.0, "untouched");
    ASSERT_GT(c, 0);

    cmds.insertSilence(4.0, 4.0);
    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_NEAR(t0[0].startBeat, 4.0, 1e-6);
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}
```

- [ ] **Step 2: Register the test file**

In `tests/CMakeLists.txt`, after the `unit/engine/ripple_delete_test.cpp` line, add:

```cmake
    unit/engine/region_ops_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails (compile error)**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String "insertSilence|error"`
Expected: `error C2039: 'insertSilence': is not a member of 'ProjectCommands'`.

- [ ] **Step 4: Declare the command**

In `src/common/ProjectCommands.h`, after the `rippleDelete` virtual declaration, add:

```cpp
    // Insert silence: split any clip crossing startBeat, then shift every clip
    // that starts at or after startBeat rightward by (endBeat - startBeat),
    // opening an empty gap [startBeat, endBeat). Beats at this boundary.
    virtual void insertSilence(double startBeat, double endBeat) = 0;
```

In `src/engine/AudioEngineCommands.h`, after the `rippleDelete` override decl, add:

```cpp
    void insertSilence(double startBeat, double endBeat) override;
```

- [ ] **Step 5: Implement `insertSilence`**

In `src/engine/AudioEngineCommands_Clips.cpp`, immediately after the `rippleDelete` impl, add:

```cpp
void AudioEngineCommands::insertSilence(double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;  // empty/invalid range: no-op

    double bpm = engine_.getTransportManager().getBPM();
    double rs = beatsToSeconds(startBeat, bpm);   // insertion point, seconds
    double re = beatsToSeconds(endBeat, bpm);
    double gapLen = re - rs;
    if (gapLen <= 0.0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Insert silence");

    // Phase 1: slice clips straddling the insertion point rs (only one slice
    // point — re just defines the gap length). Model-level slice: no rebuild.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        std::vector<int> toSlice;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            if (cs < rs && rs < ce)                       // straddles insertion point
                toSlice.push_back(static_cast<int>(clip.getProperty(IDs::clipID)));
        }
        for (int id : toSlice)
        {
            int ti = -1;
            auto clip = findClipById(id, ti);
            if (clip.isValid())
                ProjectModel::sliceClipAtTimes(clip, { rs }, &um);
        }
    }

    // Phase 2: shift every clip with start >= rs right by gapLen. After phase 1
    // no clip straddles rs, so "start >= rs" cleanly selects the moved tail.
    std::vector<std::pair<juce::ValueTree, double>> toShift;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            if (cs >= rs)
                toShift.push_back({ clip, cs + gapLen });
        }
    }
    for (auto& s : toShift)
        s.first.setProperty(IDs::startTime, s.second, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();

    endTransaction();
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=InsertSilence.* }`
Expected: `[  PASSED  ] 3 tests`.

- [ ] **Step 7: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Clips.cpp tests/unit/engine/region_ops_test.cpp tests/CMakeLists.txt
git commit -m "feat(engine): insertSilence — split at point, shift later content right"
```

---

## Task 2: `duplicateRegion` engine command

Slice at both range boundaries, copy the inside clips to `start + D`, shift after-clips right by `D`. Shift happens **before** copying so the copies (which land at `>= Re`) are not swept into the shift.

**Files:**
- Modify: `src/engine/AudioEngineCommands_Clips.cpp`, `src/engine/AudioEngineCommands.h`, `src/common/ProjectCommands.h`
- Modify: `tests/unit/engine/region_ops_test.cpp` (append)

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/engine/region_ops_test.cpp`:

```cpp
// ─── duplicateRegion ───────────────────────────────────────────────────────

// Duplicate region [2,6): inside clip [3,5) is copied to [7,9); after clip
// [8,12) shifts right by 4 to [12,16).
TEST(DuplicateRegion, CopiesInsideAndShiftsAfterRight)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int inside = cmds.addMidiClip(0, 3.0, 2.0, "inside");  // beats [3,5) inside [2,6)
    ASSERT_GT(inside, 0);
    int after  = cmds.addMidiClip(0, 8.0, 4.0, "after");   // beats [8,12) after 6
    ASSERT_GT(after, 0);

    cmds.duplicateRegion(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 3u);   // original inside + copy + shifted after
    // Original inside clip stays at [3,5).
    bool foundOrigInside = false, foundCopy = false, foundAfter = false;
    for (const auto& c : t0)
    {
        if (std::abs(c.startBeat - 3.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundOrigInside = true; }
        // copy of [3,5) lands at 3+4=7 -> [7,9)
        if (std::abs(c.startBeat - 7.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundCopy = true; }
        // after [8,12) shifts to [12,16)
        if (std::abs(c.startBeat - 12.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 4.0, 1e-6); foundAfter = true; }
    }
    EXPECT_TRUE(foundOrigInside);
    EXPECT_TRUE(foundCopy);
    EXPECT_TRUE(foundAfter);
}

// A clip spanning the whole region is split: [0,10) over [2,6) -> [0,2) kept,
// [2,6) copied to [6,10), [6,10) tail shifted to [10,14).
TEST(DuplicateRegion, SplitsSpanningClipAndDuplicatesInside)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 10.0, "span");   // beats [0,10)

    cmds.duplicateRegion(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    // head [0,2) + inside [2,6) + copy [6,10) + tail-shifted [10,14) = 4 clips
    ASSERT_EQ(t0.size(), 4u);
}

// Empty range is a no-op.
TEST(DuplicateRegion, EmptyRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int c = cmds.addMidiClip(0, 4.0, 4.0, "untouched");
    ASSERT_GT(c, 0);

    cmds.duplicateRegion(4.0, 4.0);
    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_NEAR(t0[0].startBeat, 4.0, 1e-6);
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=DuplicateRegion.* }`
Expected: compile error — `duplicateRegion` not a member.

- [ ] **Step 3: Declare the command**

In `src/common/ProjectCommands.h`, after the `insertSilence` virtual, add:

```cpp
    // Duplicate region: copy all clip content within [startBeat, endBeat) and
    // paste it at endBeat, shifting every clip that starts at or after endBeat
    // rightward by (endBeat - startBeat). Beats at this boundary.
    virtual void duplicateRegion(double startBeat, double endBeat) = 0;
```

In `src/engine/AudioEngineCommands.h`, after the `insertSilence` override decl, add:

```cpp
    void duplicateRegion(double startBeat, double endBeat) override;
```

- [ ] **Step 4: Implement `duplicateRegion`**

In `src/engine/AudioEngineCommands_Clips.cpp`, after the `insertSilence` impl, add. Note the **shift-before-copy** ordering: the after-clips are collected and shifted first, so the copies (which will be added with `start >= re`) are never swept into the shift pass.

```cpp
void AudioEngineCommands::duplicateRegion(double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;  // empty/invalid range: no-op

    double bpm = engine_.getTransportManager().getBPM();
    double rs = beatsToSeconds(startBeat, bpm);
    double re = beatsToSeconds(endBeat, bpm);
    double regionLen = re - rs;
    if (regionLen <= 0.0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Duplicate region");

    // Phase 1: slice clips straddling rs or re, so inside/before/after are clean.
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        std::vector<std::pair<int, std::vector<double>>> toSlice;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            std::vector<double> times;
            if (cs < rs && rs < ce) times.push_back(rs);
            if (cs < re && re < ce) times.push_back(re);
            if (!times.empty())
                toSlice.push_back({ static_cast<int>(clip.getProperty(IDs::clipID)), times });
        }
        for (auto& [id, times] : toSlice)
        {
            int ti = -1;
            auto clip = findClipById(id, ti);
            if (clip.isValid())
                ProjectModel::sliceClipAtTimes(clip, times, &um);
        }
    }

    // Phase 2: collect inside clips (to copy) and after-clip ids (to shift).
    // Collect BEFORE copying so the copies are never in the shift set.
    struct InsideClip { juce::ValueTree clip; int trackIndex; double startSec; };
    std::vector<InsideClip> insideClips;
    std::vector<int> afterIds;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            double cs = clip.getProperty(IDs::startTime);
            double ce = cs + static_cast<double>(clip.getProperty(IDs::duration));
            if (cs >= rs && ce <= re)
                insideClips.push_back({ clip, t, cs });
            else if (cs >= re)
                afterIds.push_back(static_cast<int>(clip.getProperty(IDs::clipID)));
        }
    }

    // Phase 3: shift after-clips right by regionLen (by id, so copies added
    // next are not affected).
    for (int id : afterIds)
    {
        int ti = -1;
        auto clip = findClipById(id, ti);
        if (clip.isValid())
        {
            double cs = clip.getProperty(IDs::startTime);
            clip.setProperty(IDs::startTime, cs + regionLen, &um);
        }
    }

    // Phase 4: copy each inside clip to startSec + regionLen (lands in [re, re+len]).
    // createCopy deep-copies notes/gain-envelope; mint a fresh id.
    for (const auto& ic : insideClips)
    {
        auto newClip = ic.clip.createCopy();
        newClip.setProperty(IDs::clipID, ProjectModel::allocateClipID(), &um);
        newClip.setProperty(IDs::startTime, ic.startSec + regionLen, &um);
        juce::String origName = newClip.getProperty(IDs::name).toString();
        if (!origName.endsWith(" copy"))
            newClip.setProperty(IDs::name, origName + " copy", &um);
        auto clipList = trackList.getChild(ic.trackIndex).getChildWithName(IDs::CLIP_LIST);
        if (clipList.isValid())
            clipList.addChild(newClip, -1, &um);
    }

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();

    endTransaction();
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter="InsertSilence.*:DuplicateRegion.*" }`
Expected: `[  PASSED  ] 6 tests`.

- [ ] **Step 6: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Clips.cpp tests/unit/engine/region_ops_test.cpp
git commit -m "feat(engine): duplicateRegion — copy inside range, shift after right"
```

---

## Task 3: RPC dispatch for both commands

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp`

- [ ] **Step 1: Add the two dispatch cases**

In `src/frontend/FrontendRouter.cpp`, immediately after the `rippleDelete` block (which sits after `removeClips`), add:

```cpp
    if (m == "insertSilence") {
        double sb, eb;
        if (!requireDouble(o, "startBeat", sb, nullptr) || !requireDouble(o, "endBeat", eb, nullptr))
            return makeError(-32602, "startBeat and endBeat required");
        c.insertSilence(sb, eb);
        return { false, QJsonValue::Null };
    }
    if (m == "duplicateRegion") {
        double sb, eb;
        if (!requireDouble(o, "startBeat", sb, nullptr) || !requireDouble(o, "endBeat", eb, nullptr))
            return makeError(-32602, "startBeat and endBeat required");
        c.duplicateRegion(sb, eb);
        return { false, QJsonValue::Null };
    }
```

- [ ] **Step 2: Build and smoke-check the frontend RPC suite**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter="FrontendServer.*" }`
Expected: existing FrontendServer tests pass (additive change).

- [ ] **Step 3: Commit**

```bash
git add src/frontend/FrontendRouter.cpp
git commit -m "feat(rpc): project.insertSilence + project.duplicateRegion dispatch"
```

---

## Task 4: MCP tools for both (parity)

**Files:**
- Modify: `src/mcp/McpTools_Project.cpp` (in `registerClipTools`, after the `ripple_delete` tool)

- [ ] **Step 1: Add the two MCP tools**

In `src/mcp/McpTools_Project.cpp`, immediately after the `ripple_delete` tool registration, add:

```cpp
    s.registerTool({"insert_silence",
        "Insert silence: split any clip crossing startBeat and shift all later "
        "content right by (endBeat - startBeat), opening an empty gap.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().insertSilence(sb, eb);
            return McpToolResult::text(QString("inserted silence [%1, %2)").arg(sb).arg(eb));
        }});

    s.registerTool({"duplicate_region",
        "Duplicate region: copy all clip content within [startBeat, endBeat) "
        "and paste it at endBeat, shifting later content right.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().duplicateRegion(sb, eb);
            return McpToolResult::text(QString("duplicated [%1, %2) to %3").arg(sb).arg(eb).arg(eb));
        }});
```

- [ ] **Step 2: Build and run MCP schema/registry tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter="Mcp.*:Schema.*:ToolRegistry.*" }`
Expected: pass (new tools accepted).

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools_Project.cpp
git commit -m "feat(mcp): insert_silence + duplicate_region tools for parity"
```

---

## Task 5: Frontend context-menu items

Four items mirroring ripple delete's two entry points (loop-region range + selection bounds), grouped under a divider. Place them right after the existing ripple-delete items in the empty-lane menu.

**Files:**
- Modify: `frontend/src/components/TimelineContextMenu.tsx`

- [ ] **Step 1: Add the four menu items**

In `frontend/src/components/TimelineContextMenu.tsx`, the ripple-delete items live in the `emptyContextMenu` block between two `<div className="ctx-separator" />` elements. Immediately after the ripple-delete selection IIFE (`Ripple Delete Selection` button) and **before** the second separator, add:

```tsx
              {transport.loopEnd > transport.loopStart && (
                <button onMouseDown={(e) => {
                  e.stopPropagation();
                  rpc.call("project.insertSilence", {
                    startBeat: transport.loopStart,
                    endBeat: transport.loopEnd,
                  }).then(() => {
                    useProjectStore.setState({ isDirty: true });
                  }).catch((err) => console.error("Insert silence failed:", err));
                  onClose();
                }}>
                  Insert Silence
                </button>
              )}
              {(() => {
                const selClips = clips.filter((c) => selectedClipIds.has(c.clipId));
                if (selClips.length === 0) return null;
                const minStart = Math.min(...selClips.map((c) => c.startBeat));
                const maxEnd = Math.max(...selClips.map((c) => c.startBeat + c.durationBeats));
                if (maxEnd <= minStart) return null;
                return (
                  <>
                    <button onMouseDown={(e) => {
                      e.stopPropagation();
                      rpc.call("project.insertSilence", {
                        startBeat: minStart,
                        endBeat: maxEnd,
                      }).then(() => {
                        useProjectStore.setState({ isDirty: true });
                      }).catch((err) => console.error("Insert silence failed:", err));
                      onClose();
                    }}>
                      Insert Silence at Selection
                    </button>
                    <button onMouseDown={(e) => {
                      e.stopPropagation();
                      rpc.call("project.duplicateRegion", {
                        startBeat: minStart,
                        endBeat: maxEnd,
                      }).then(() => {
                        useProjectStore.setState({ isDirty: true });
                      }).catch((err) => console.error("Duplicate region failed:", err));
                      onClose();
                    }}>
                      Duplicate Region
                    </button>
                  </>
                );
              })()}
```

- [ ] **Step 2: Typecheck and run the component test**

Run: `cd frontend; npx tsc --noEmit; if ($?) { npm test -- --run TimelineContextMenu }`
Expected: typecheck clean; 8 existing component tests pass (the new buttons render conditionally).

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TimelineContextMenu.tsx
git commit -m "feat(ui): insert silence + duplicate region menu items"
```

---

## Task 6: Playwright E2E tests

**Files:**
- Modify: `frontend/e2e/timeline-context-menu.spec.ts`

- [ ] **Step 1: Add the two E2E tests**

In `frontend/e2e/timeline-context-menu.spec.ts`, after the ripple-delete E2E tests, add (inside the `test.describe` block, before the closing `});`):

```typescript
  test("Insert Silence opens a gap, shifting later clips right", async ({ page }) => {
    const before = await addMidiClip(page, { trackIndex: 0, start: 8, duration: 4, name: "after" });
    const beforeLeft = await clipLeft(page, before);

    await rpcCall(page, "project.setLoopStart", { beat: 2 });
    await rpcCall(page, "project.setLoopEnd", { beat: 6 });

    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Insert Silence" }).click();

    // The clip shifted right (gap opened before it); still present.
    await expect(page.locator(`.tl-clip[data-clip-id="${before}"]`)).toBeVisible({ timeout: 10000 });
    const afterLeft = await clipLeft(page, before);
    expect(afterLeft).toBeGreaterThan(beforeLeft);
  });

  test("Duplicate Region copies content and shifts later clips right", async ({ page }) => {
    const inside = await addMidiClip(page, { trackIndex: 0, start: 3, duration: 2, name: "inside" });
    const after  = await addMidiClip(page, { trackIndex: 0, start: 8, duration: 4, name: "after" });
    const beforeAfterLeft = await clipLeft(page, after);
    const beforeCount = await page.locator(".tl-clip").count();

    // Select the inside clip so the selection-derived range covers [3,5).
    await rpcCall(page, "project.setLoopStart", { beat: 0 });
    await rpcCall(page, "project.setLoopEnd", { beat: 0 });
    await page.locator(`.tl-clip[data-clip-id="${inside}"]`).click();

    await rightClickEmptyTimeline(page);
    await waitForContextMenu(page);
    await page.locator(".clip-context-menu button", { hasText: "Duplicate Region" }).click();

    // A new clip appeared (the copy); the after-clip shifted right.
    await expect(page.locator(".tl-clip")).toHaveCount(beforeCount + 1, { timeout: 10000 });
    await expect(page.locator(`.tl-clip[data-clip-id="${after}"]`)).toBeVisible({ timeout: 10000 });
    const afterAfterLeft = await clipLeft(page, after);
    expect(afterAfterLeft).toBeGreaterThan(beforeAfterLeft);
  });
```

- [ ] **Step 2: Rebuild the engine binary (RPC layer changed) and run the E2E tests**

Run: `cmake --build build --config Debug --target HDAW; if ($?) { cd frontend; npx playwright test timeline-context-menu --grep "Insert Silence|Duplicate Region" --project=chromium }`
Expected: both tests pass.

- [ ] **Step 3: Commit**

```bash
git add frontend/e2e/timeline-context-menu.spec.ts
git commit -m "test(e2e): insert silence + duplicate region via context menu"
```

---

## Self-Review

**1. Spec coverage:**
- *Insert silence: split clips crossing boundary, shift later content right* → Task 1 (slice at rs + shift right).
- *Insert silence opens an empty gap* → Task 1 test asserts the gap (after-clip moved, count stays 1).
- *Duplicate region: copy content in range, insert at region end* → Task 2 (copy inside clips to `start + D`).
- *Duplicate region: shift-and-paste (later content moves right)* → Task 2 (shift after-clips before copying).
- *Loop-region + selection range sources* → Task 5 (both ops, mirroring ripple).
- *RPC + MCP parity* → Tasks 3, 4.
- *Tests (gtest + E2E)* → Tasks 1, 2, 6.

**2. Placeholder scan:** No TBD/TODO/"handle edge cases". Every code step contains full code. The one external-contract dependency (`addMidiClip` returns the clip id) was verified in the ripple work (`e2e/helpers.ts:52`).

**3. Type/name consistency:**
- `insertSilence(double startBeat, double endBeat)` and `duplicateRegion(double startBeat, double endBeat)` — identical signatures across `ProjectCommands.h`, `AudioEngineCommands.h`, `.cpp`, RPC dispatch, MCP tools, frontend.
- RPC method strings `project.insertSilence` / `project.duplicateRegion` — match between dispatch and frontend calls.
- MCP tool names `insert_silence` / `duplicate_region` — consistent in Task 4.
- Shift-before-copy ordering in `duplicateRegion` is called out in the architecture and implemented in phases 2→3→4 (collect → shift → copy).

**Ordering risk (duplicate region):** if the shift pass ran after copies were added, copies (start `>= re`) would be re-shifted. The implementation collects `afterIds` and `insideClips` in phase 2 (before any mutation), shifts by id in phase 3, then adds copies in phase 4 — so copies are never in the shift set. The Task 2 `CopiesInsideAndShiftsAfterRight` test pins this (copy lands at `[7,9)`, not `[11,13)`).

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-28-insert-silence-and-duplicate-region.md`. Executing inline (engine foundation and patterns are already established from the ripple-delete work).
