# Merge Clips Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge multiple selected MIDI clips into a single clip, with all notes preserved at their correct timeline positions.

**Architecture:** Single backend RPC (`project.mergeClips`) that validates, creates a merged clip, copies notes with beat offsets, and removes originals — all in one undo transaction. Frontend triggers via context menu + Ctrl+M keyboard shortcut.

**Tech Stack:** C++ (JUCE ValueTree, gtest), TypeScript/React (Playwright E2E)

**Spec:** `docs/superpowers/specs/2026-07-24-merge-clips-design.md`

---

## File Map

| File | Action | Purpose |
|---|---|---|
| `src/common/ProjectCommands.h` | Modify | Add virtual `mergeClips` interface |
| `src/engine/AudioEngineCommands.h` | Modify | Add `mergeClips` declaration |
| `src/engine/AudioEngineCommands_Clips.cpp` | Modify | Implement `mergeClips` |
| `src/frontend/FrontendRouter.cpp` | Modify | RPC dispatch for `project.mergeClips` |
| `tests/unit/engine/merge_clips_test.cpp` | Create | C++ gtest for merge logic |
| `frontend/src/components/TimelineContextMenu.tsx` | Modify | "Merge Clips" context menu button |
| `frontend/src/components/TimelineMinimal.tsx` | Modify | Ctrl+M keyboard shortcut |
| `frontend/e2e/editing.spec.ts` | Modify | E2E test for merge workflow |

---

### Task 1: Add virtual interface in ProjectCommands.h

**Files:**
- Modify: `src/common/ProjectCommands.h:80-88`

- [ ] **Step 1: Add mergeClips to the virtual interface**

In `src/common/ProjectCommands.h`, add after the `clearNotes` declaration (line 88):

```cpp
    virtual void clearNotes(int clipId) = 0;

    // Merge multiple MIDI clips into one. All clips must be on the same track.
    // The merged clip spans the full range; notes are offset to preserve their
    // absolute timeline positions. Original clips are removed. Wrapped in one
    // undo transaction.
    virtual int mergeClips(const std::vector<int>& clipIds) = 0;
```

- [ ] **Step 2: Verify the file compiles**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String "error|Error"`
Expected: no errors (the pure virtual is satisfied by the existing override chain, but AudioEngineCommands will fail until Task 2 — that's expected).

---

### Task 2: Add declaration in AudioEngineCommands.h

**Files:**
- Modify: `src/engine/AudioEngineCommands.h:90-98`

- [ ] **Step 1: Add mergeClips override declaration**

In `src/engine/AudioEngineCommands.h`, add after the `clearNotes` declaration (line 98):

```cpp
    void clearNotes(int clipId) override;
    int mergeClips(const std::vector<int>& clipIds) override;
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String "error|Error"`
Expected: linker error (mergeClips not implemented yet) — that's fine, we implement it in Task 3.

---

### Task 3: Implement mergeClips

**Files:**
- Modify: `src/engine/AudioEngineCommands_Clips.cpp` (append after existing clip operations)

- [ ] **Step 1: Write the mergeClips implementation**

Append to `src/engine/AudioEngineCommands_Clips.cpp`:

```cpp
int AudioEngineCommands::mergeClips(const std::vector<int>& clipIds)
{
    if (clipIds.size() < 2) return -1;

    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();

    // Validate: all clips exist, are MIDI, and on the same track.
    struct ClipInfo { juce::ValueTree tree; int trackIndex; };
    std::vector<ClipInfo> infos;
    infos.reserve(clipIds.size());

    for (int id : clipIds)
    {
        int trackIdx = -1;
        auto clip = findClipById(id, trackIdx);
        if (!clip.isValid()) return -1;
        if (clip.getProperty(IDs::clipType).toString() != "midi") return -1;
        infos.push_back({ clip, trackIdx });
    }

    // All must be on the same track.
    int targetTrack = infos[0].trackIndex;
    for (const auto& info : infos)
    {
        if (info.trackIndex != targetTrack) return -1;
    }

    // Compute merged range.
    double newStart = std::numeric_limits<double>::max();
    double newEnd = std::numeric_limits<double>::lowest();
    for (const auto& info : infos)
    {
        double start = static_cast<double>(info.tree.getProperty(IDs::startTime));
        double dur = static_cast<double>(info.tree.getProperty(IDs::duration));
        newStart = std::min(newStart, start);
        newEnd = std::max(newEnd, start + dur);
    }
    double newDuration = newEnd - newStart;

    // Create merged clip.
    auto mergedClip = ProjectModel::createMidiClipEmpty(
        juce::String("Merged"), newStart, newDuration);
    int mergedId = static_cast<int>(mergedClip.getProperty(IDs::clipID, 0));

    // Copy notes from all source clips, offsetting startBeat.
    auto mergedNoteList = mergedClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    for (const auto& info : infos)
    {
        double clipStart = static_cast<double>(info.tree.getProperty(IDs::startTime));
        double offset = clipStart - newStart;

        auto noteList = info.tree.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto srcNote = noteList.getChild(n);
            auto newNote = ProjectModel::createMidiNote(
                static_cast<int>(srcNote.getProperty(IDs::noteNumber)),
                static_cast<float>(static_cast<double>(srcNote.getProperty(IDs::velocity))),
                static_cast<double>(srcNote.getProperty(IDs::startBeat)) + offset,
                static_cast<double>(srcNote.getProperty(IDs::durationBeats)));
            mergedNoteList.addChild(newNote, -1, &um);
        }
    }

    // Add merged clip to the track.
    auto trackList = model.getTrackListTree();
    auto track = trackList.getChild(targetTrack);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, &um);
    }
    clipList.addChild(mergedClip, -1, &um);

    // Remove originals (iterate in reverse to avoid index shifting).
    for (int i = static_cast<int>(infos.size()) - 1; i >= 0; --i)
    {
        auto parent = infos[i].tree.getParent();
        if (parent.isValid())
            parent.removeChild(infos[i].tree, &um);
    }

    return mergedId;
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String "error|Error"`
Expected: no errors.

---

### Task 4: Add RPC dispatch

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp:190` (after `createGhostClip`, before `paintClips`)

- [ ] **Step 1: Add the mergeClips RPC handler**

In `src/frontend/FrontendRouter.cpp`, add after the `createGhostClip` line (line 190) and before `paintClips` (line 191):

```cpp
    if (m == "mergeClips") {
        auto idsArr = o.value("clipIds");
        if (!idsArr.isArray()) return makeError(-32602, "clipIds array required");
        std::vector<int> ids;
        for (const auto& e : idsArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "clipIds element not a number");
            ids.push_back(static_cast<int>(e.toDouble()));
        }
        int result = c.mergeClips(ids);
        if (result < 0) return makeError(-32602, "merge failed: clips must be ≥2 MIDI clips on the same track");
        return { false, result };
    }
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: successful build.

---

### Task 5: Write C++ gtest

**Files:**
- Create: `tests/unit/engine/merge_clips_test.cpp`

- [ ] **Step 1: Write the merge_clips test file**

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

struct EngineHarness {
    AudioEngine engine;
    void setUp() { engine.initialize(); }
    void tearDown() { engine.shutdown(); }
};

} // namespace

TEST(MergeClips, MergeTwoContiguousClips) {
    EngineHarness h;
    h.setUp();
    auto& cmds = h.engine.getProjectCommands();

    // Add two MIDI clips on track 1 (the MIDI track).
    int c1 = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int c2 = cmds.addMidiClip(1, 4.0, 4.0, "B");
    ASSERT_GT(c1, 0);
    ASSERT_GT(c2, 0);

    // Add notes to each clip.
    cmds.addNote(c1, 60, 100, 0.0, 1.0);
    cmds.addNote(c1, 64, 80, 2.0, 0.5);
    cmds.addNote(c2, 67, 90, 0.0, 1.0);
    cmds.addNote(c2, 72, 70, 2.0, 0.5);

    // Merge.
    int mergedId = cmds.mergeClips({ c1, c2 });
    EXPECT_GT(mergedId, 0);

    // The merged clip should span 0..8 beats.
    auto& model = h.engine.getProjectModel();
    int trackIdx = -1;
    auto merged = [&]() -> juce::ValueTree {
        auto tl = model.getTrackListTree();
        for (int t = 0; t < tl.getNumChildren(); ++t) {
            auto cl = tl.getChild(t).getChildWithName(IDs::CLIP_LIST);
            for (int i = 0; i < cl.getNumChildren(); ++i) {
                if (static_cast<int>(cl.getChild(i).getProperty(IDs::clipID)) == mergedId)
                    return cl.getChild(i);
            }
        }
        return {};
    }();
    ASSERT_TRUE(merged.isValid());
    EXPECT_DOUBLE_EQ(static_cast<double>(merged.getProperty(IDs::startTime)), 0.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(merged.getProperty(IDs::duration)), 8.0);

    // Should have 4 notes total.
    auto noteList = merged.getChildWithName(IDs::MIDI_NOTE_LIST);
    EXPECT_EQ(noteList.getNumChildren(), 4);

    // Original clips should be removed.
    int tmpIdx = -1;
    EXPECT_FALSE(h.engine.getProjectCommands().findClipById /* not exposed */);
    // Verify via the model: track 1 clip count should be 1 (just the merged clip).
    auto clipList = model.getTrackListTree().getChild(1).getChildWithName(IDs::CLIP_LIST);
    EXPECT_EQ(clipList.getNumChildren(), 1);

    h.tearDown();
}

TEST(MergeClips, MergeWithGap) {
    EngineHarness h;
    h.setUp();
    auto& cmds = h.engine.getProjectCommands();

    int c1 = cmds.addMidiClip(1, 0.0, 2.0, "A");
    int c2 = cmds.addMidiClip(1, 8.0, 2.0, "B");
    cmds.addNote(c1, 60, 100, 0.0, 1.0);
    cmds.addNote(c2, 67, 90, 0.0, 1.0);

    int mergedId = cmds.mergeClips({ c1, c2 });
    EXPECT_GT(mergedId, 0);

    // Merged clip spans 0..10 (gap 2..8 is silence).
    auto& model = h.engine.getProjectModel();
    auto clipList = model.getTrackListTree().getChild(1).getChildWithName(IDs::CLIP_LIST);
    ASSERT_EQ(clipList.getNumChildren(), 1);
    auto merged = clipList.getChild(0);
    EXPECT_DOUBLE_EQ(static_cast<double>(merged.getProperty(IDs::duration)), 10.0);

    // Note from clip B should be at absolute beat 8 + offset 0 = 8 in the merged clip.
    auto noteList = merged.getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_EQ(noteList.getNumChildren(), 2);
    // Find the note with pitch 67 (from clip B).
    bool found = false;
    for (int i = 0; i < noteList.getNumChildren(); ++i) {
        auto n = noteList.getChild(i);
        if (static_cast<int>(n.getProperty(IDs::noteNumber)) == 67) {
            EXPECT_DOUBLE_EQ(static_cast<double>(n.getProperty(IDs::startBeat)), 8.0);
            found = true;
        }
    }
    EXPECT_TRUE(found);

    h.tearDown();
}

TEST(MergeClips, RejectsLessThanTwoClips) {
    EngineHarness h;
    h.setUp();
    int c1 = h.engine.getProjectCommands().addMidiClip(1, 0.0, 4.0, "A");
    EXPECT_EQ(h.engine.getProjectCommands().mergeClips({ c1 }), -1);
    EXPECT_EQ(h.engine.getProjectCommands().mergeClips({}), -1);
    h.tearDown();
}

TEST(MergeClips, RejectsMixedTracks) {
    EngineHarness h;
    h.setUp();
    auto& cmds = h.engine.getProjectCommands();
    int c1 = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int c2 = cmds.addMidiClip(2, 0.0, 4.0, "B");
    EXPECT_EQ(cmds.mergeClips({ c1, c2 }), -1);
    h.tearDown();
}

TEST(MergeClips, RejectsNonMidiClips) {
    EngineHarness h;
    h.setUp();
    auto& cmds = h.engine.getProjectCommands();
    int c1 = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int c2 = cmds.addAudioClip(0, 0.0, 4.0, "test.wav", "B");
    EXPECT_EQ(cmds.mergeClips({ c1, c2 }), -1);
    h.tearDown();
}

TEST(MergeClips, UndoRestoresOriginals) {
    EngineHarness h;
    h.setUp();
    auto& cmds = h.engine.getProjectCommands();
    auto& um = h.engine.getProjectModel().getUndoManager();

    int c1 = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int c2 = cmds.addMidiClip(1, 4.0, 4.0, "B");
    cmds.addNote(c1, 60, 100, 0.0, 1.0);
    cmds.addNote(c2, 67, 90, 0.0, 1.0);

    cmds.mergeClips({ c1, c2 });

    // After merge: 1 clip on track 1.
    auto& model = h.engine.getProjectModel();
    auto clipList = model.getTrackListTree().getChild(1).getChildWithName(IDs::CLIP_LIST);
    EXPECT_EQ(clipList.getNumChildren(), 1);

    // Undo: should restore 2 clips.
    um.undo();
    EXPECT_EQ(clipList.getNumChildren(), 2);

    h.tearDown();
}
```

- [ ] **Step 2: Build and run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests && .\build\Debug\hdaw_tests.exe --gtest_filter=MergeClips.*`
Expected: all 5 tests pass.

---

### Task 6: Add context menu button

**Files:**
- Modify: `frontend/src/components/TimelineContextMenu.tsx:128-130` (before the Split button)

- [ ] **Step 1: Add the Merge Clips button**

In `TimelineContextMenu.tsx`, add before the Split button (line 128). The button is visible only when ≥2 non-ghost MIDI clips are selected:

```tsx
              {(() => {
                const { selectedClipIds } = useUiStore.getState();
                const selClips = clips.filter((c) => selectedClipIds.has(c.clipId));
                const canMerge = selClips.length >= 2
                  && selClips.every((c) => c.isMidi && !c.isGhost);
                if (!canMerge) return null;
                return (
                  <button onMouseDown={(e) => {
                    e.stopPropagation();
                    const ids = selClips.map((c) => c.clipId);
                    rpc.call("project.mergeClips", { clipIds: ids }).then((res) => {
                      const newId = typeof res === "number" ? res : null;
                      if (newId != null && newId > 0) {
                        useUiStore.setState({ selectedClipIds: new Set([newId]) });
                      }
                      useProjectStore.setState({ isDirty: true });
                    }).catch((err) => console.error("Merge failed:", err));
                    onClose();
                  }}>
                    Merge Clips
                  </button>
                );
              })()}
              <button onMouseDown={(e) => { e.stopPropagation(); onClose(); onSplitClip(); }}>
                Split
              </button>
```

- [ ] **Step 2: Verify the frontend builds**

Run: `cd frontend && npx vitest run 2>&1 | Select-Object -Last 5`
Expected: all tests pass (no syntax/import errors).

---

### Task 7: Add Ctrl+M keyboard shortcut

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx:424` (after the Ctrl+D handler)

- [ ] **Step 1: Add the Ctrl+M handler**

In `TimelineMinimal.tsx`, add after the Ctrl+D handler (after line 428 `handleDuplicateClip();`):

```tsx
      } else if ((e.ctrlKey || e.metaKey) && e.code === "KeyM") {
        e.preventDefault();
        const snap = useProjectStore.getState().snapshot;
        if (!snap) return;
        const selClips = snap.clips.filter((c) => selectedClipIds.has(c.clipId));
        const canMerge = selClips.length >= 2
          && selClips.every((c) => c.isMidi && !c.isGhost);
        if (canMerge) {
          const ids = selClips.map((c) => c.clipId);
          rpc.call("project.mergeClips", { clipIds: ids }).then((res) => {
            const newId = typeof res === "number" ? res : null;
            if (newId != null && newId > 0) {
              useUiStore.setState({ selectedClipIds: new Set([newId]) });
            }
            useProjectStore.setState({ isDirty: true });
          }).catch((err) => console.error("Merge failed:", err));
        }
```

- [ ] **Step 2: Verify the frontend builds**

Run: `cd frontend && npx vitest run 2>&1 | Select-Object -Last 5`
Expected: all tests pass.

---

### Task 8: Write E2E test

**Files:**
- Modify: `frontend/e2e/editing.spec.ts` (append before closing `});`)

- [ ] **Step 1: Add the merge-clips E2E test**

Append to the test describe block in `editing.spec.ts`:

```tsx
  test("Ctrl+M merges selected MIDI clips into one", async ({ page }) => {
    await startApp(page);
    await expect
      .poll(() => page.locator(".tl-clip").count(), { timeout: 10000 })
      .toBe(2);
    const countBefore = 2;

    // Create two MIDI clips on the same track.
    const c1 = await addMidiClip(page, { trackIndex: 0, start: 0, duration: 2, name: "M1" });
    const c2 = await addMidiClip(page, { trackIndex: 0, start: 4, duration: 2, name: "M2" });
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);

    // Select both clips.
    await clipLocator(page, c1).click();
    await clipLocator(page, c2).click({ modifiers: ["Control"] });

    // Merge via Ctrl+M.
    await page.keyboard.press("Control+m");

    // After merge: 2 original clips replaced by 1 merged clip → count drops by 1.
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 1);

    // The merged clip spans beats 0..6 (clip 1 at 0-2, clip 2 at 4-6).
    const snap = await rpcCall<{ clips: { trackIndex: number; startBeat: number; durationBeats: number }[] }>(
      page, "read.snapshot",
    );
    const track0 = snap.clips.filter((c) => c.trackIndex === 0);
    expect(track0.length).toBe(1);
    expect(track0[0].startBeat).toBe(0);
    expect(track0[0].durationBeats).toBe(6);

    // Undo restores the originals.
    await page.locator('header.transport-bar button[title="Undo (Ctrl+Z)"]').click();
    await expect.poll(() => page.locator(".tl-clip").count(), { timeout: 5000 }).toBe(countBefore + 2);
  });
```

- [ ] **Step 2: Run the E2E test**

Run: `cd frontend && npx playwright test e2e/editing.spec.ts -g "merge" --reporter=list`
Expected: test passes.

---

### Task 9: Run full test suites

- [ ] **Step 1: Run C++ tests**

Run: `.\build\Debug\hdaw_tests.exe --gtest_filter=MergeClips.*`
Expected: all pass.

- [ ] **Step 2: Run frontend unit tests**

Run: `cd frontend && npx vitest run`
Expected: all pass.

- [ ] **Step 3: Run full E2E suite**

Run: `cd frontend && npx playwright test --reporter=list`
Expected: all pass (including the new merge test).

- [ ] **Step 4: Commit**

```bash
git add src/common/ProjectCommands.h src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Clips.cpp src/frontend/FrontendRouter.cpp tests/unit/engine/merge_clips_test.cpp frontend/src/components/TimelineContextMenu.tsx frontend/src/components/TimelineMinimal.tsx frontend/e2e/editing.spec.ts
git commit -m "feat: merge MIDI clips (project.mergeClips RPC, context menu, Ctrl+M)"
```
