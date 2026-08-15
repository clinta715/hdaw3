# Incremental Tree Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the whole-project snapshot re-fetch on every edit with a small pushed delta (changed clips/tracks), eliminating edit latency, with a full-sync fallback for safety.

**Architecture:** `FrontendTreeWatcher` classifies granular JUCE `ValueTree` callbacks into a coalesced delta (via a new `TreeDeltaAccumulator`) and broadcasts it on `notify.treeChanged`; the frontend patches its snapshot in place via a new `applyDelta` store action, falling back to the existing full `syncSnapshot` for structural/complex changes.

**Tech Stack:** C++20 / JUCE 8 / Qt 6 (WebSocket), React 19 + TypeScript + Zustand, gtest (C++), Vitest (frontend).

**Spec:** `docs/superpowers/specs/2026-07-24-incremental-tree-sync-design.md`

**Build/test commands:**
- C++ build: `cmake --build build --config Debug`
- C++ tests: `build\Debug\hdaw_tests.exe` (filter: `--gtest_filter=TreeDelta.*`)
- Frontend typecheck: `cd frontend && npx tsc --noEmit`
- Frontend tests: `cd frontend && npx vitest run`
- Frontend build + repackage (to see changes in the packaged app): `cd frontend && npm run build && npx electron-builder --win --x64 --dir`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/engine/ReadModelImpl.h` | Modify | Declare `buildClipSnapshotFromTree` / `buildTrackSnapshotFromTree` free functions |
| `src/engine/ReadModelImpl.cpp` | Modify | Define the builders; refactor `snapshot()` to reuse them |
| `src/frontend/TreeDeltaAccumulator.h` | Create | Classify + coalesce ValueTree changes into a delta (or fullSync flag) |
| `src/frontend/TreeDeltaAccumulator.cpp` | Create | Implementation |
| `src/frontend/FrontendTreeWatcher.h` | Modify | Forward callbacks to the accumulator; override order/parent-change |
| `src/frontend/FrontendTreeWatcher.cpp` | Modify | Flush delta JSON (or fullSync) on debounce timeout |
| `tests/unit/frontend/tree_delta_accumulator_test.cpp` | Create | gtest for builders + accumulator |
| `CMakeLists.txt` | Modify | Add `TreeDeltaAccumulator.cpp` to `HDAW_lib` |
| `tests/CMakeLists.txt` | Modify | Add the new test source |
| `frontend/src/rpc/types.ts` | Modify | Add `TreeDelta` interface |
| `frontend/src/store/projectStore.ts` | Modify | Add `applyDelta` action |
| `frontend/src/store/projectStore.test.ts` | Modify | `applyDelta` tests |
| `frontend/src/main.tsx` | Modify | Delta-vs-fullSync routing in `treeChanged` handler |

---

# Phase 1 — Delta sync

## Task 1: Snapshot builders (extract from `ReadModelImpl::snapshot`)

**Files:**
- Modify: `src/engine/ReadModelImpl.h`
- Modify: `src/engine/ReadModelImpl.cpp`
- Create: `tests/unit/frontend/tree_delta_accumulator_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing builder tests**

Create `tests/unit/frontend/tree_delta_accumulator_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>
#include "model/ProjectModel.h"
#include "engine/ReadModelImpl.h"

using namespace juce;

namespace {

// Build a minimal PROJECT > TRACK_LIST > TRACK > CLIP_LIST > CLIP hierarchy so
// buildClipSnapshotFromTree can walk up to compute trackIndex.
ValueTree makeClipTree(int clipId, double startBeat, const String& name) {
    ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipID, clipId, nullptr);
    clip.setProperty(IDs::name, name, nullptr);
    clip.setProperty(IDs::startTime, startBeat, nullptr);
    clip.setProperty(IDs::duration, 4.0, nullptr);
    clip.setProperty(IDs::gain, 0.5, nullptr);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    clip.setProperty(IDs::looping, true, nullptr);
    return clip;
}

ValueTree makeTrackTree(const String& name, double volume) {
    ValueTree track(IDs::TRACK);
    track.setProperty(IDs::name, name, nullptr);
    track.setProperty(IDs::volume, volume, nullptr);
    track.setProperty(IDs::color, 7, nullptr);
    track.addChild(ValueTree(IDs::CLIP_LIST), -1, nullptr);
    return track;
}

} // namespace

TEST(TreeDeltaBuilders, ClipSnapshotFromTree) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 0.8);
    ValueTree clipList = track.getChildWithName(IDs::CLIP_LIST);
    ValueTree clip = makeClipTree(42, 4.0, "Riff");
    clipList.addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    ClipSnapshot cs = buildClipSnapshotFromTree(clip);
    EXPECT_EQ(cs.clipId, 42);
    EXPECT_EQ(cs.trackIndex, 0);          // first (only) track
    EXPECT_EQ(cs.name, "Riff");
    EXPECT_DOUBLE_EQ(cs.startBeat, 4.0);
    EXPECT_DOUBLE_EQ(cs.durationBeats, 4.0);
    EXPECT_DOUBLE_EQ(cs.gain, 0.5);
    EXPECT_TRUE(cs.isMidi);
    EXPECT_TRUE(cs.looping);
}

TEST(TreeDeltaBuilders, ClipSnapshotTrackIndexReflectsPosition) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree t0 = makeTrackTree("A", 1.0);
    ValueTree t1 = makeTrackTree("B", 1.0);
    ValueTree clip = makeClipTree(7, 0.0, "OnTrack1");
    t1.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(t0, -1, nullptr);
    trackList.addChild(t1, -1, nullptr);

    ClipSnapshot cs = buildClipSnapshotFromTree(clip);
    EXPECT_EQ(cs.trackIndex, 1);          // second track
}

TEST(TreeDeltaBuilders, TrackSnapshotFromTree) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Drums", 0.6);
    trackList.addChild(track, -1, nullptr);

    TrackSnapshot ts = buildTrackSnapshotFromTree(track);
    EXPECT_EQ(ts.index, 0);
    EXPECT_EQ(ts.name, "Drums");
    EXPECT_DOUBLE_EQ(ts.volume, 0.6);
    EXPECT_EQ(ts.color, 7);
    EXPECT_EQ(ts.clipCount, 0);
}
```

- [ ] **Step 2: Add the test source to `tests/CMakeLists.txt`**

In `tests/CMakeLists.txt`, add this line after `unit/frontend/ghost_clips_rpc_test.cpp` (line 43):

```cmake
    unit/frontend/tree_delta_accumulator_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails to compile**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: FAIL — `buildClipSnapshotFromTree` / `buildTrackSnapshotFromTree` are not declared.

- [ ] **Step 4: Declare the builders in `src/engine/ReadModelImpl.h`**

Add these free-function declarations after the `ReadModelImpl` class (before the file's end), and ensure `#include <juce_data_structures/juce_data_structures.h>` is present (it already is):

```cpp
// Per-node snapshot builders, shared by ReadModelImpl::snapshot() and the
// frontend delta path (FrontendTreeWatcher). Each reads a single ValueTree node
// and walks up to resolve container indices.
ClipSnapshot buildClipSnapshotFromTree(const juce::ValueTree& clipTree);
TrackSnapshot buildTrackSnapshotFromTree(const juce::ValueTree& trackTree);
```

- [ ] **Step 5: Define the builders and refactor `snapshot()` in `src/engine/ReadModelImpl.cpp`**

Add these definitions (e.g. just above `ReadModelImpl::snapshot()`):

```cpp
ClipSnapshot buildClipSnapshotFromTree(const juce::ValueTree& clipTree)
{
    ClipSnapshot cs;
    cs.clipId        = static_cast<int>(clipTree.getProperty(IDs::clipID, 0));
    cs.name          = clipTree.getProperty(IDs::name, "").toString().toStdString();
    cs.sourceFile    = clipTree.getProperty(IDs::sourceFile, "").toString().toStdString();
    cs.startBeat     = clipTree.getProperty(IDs::startTime, 0.0);
    cs.durationBeats = clipTree.getProperty(IDs::duration, 0.0);
    cs.offset        = clipTree.getProperty(IDs::offset, 0.0);
    cs.gain          = clipTree.getProperty(IDs::gain, 1.0);
    cs.fadeIn        = clipTree.getProperty(IDs::fadeIn, 0.0);
    cs.fadeOut       = clipTree.getProperty(IDs::fadeOut, 0.0);
    cs.looping       = clipTree.getProperty(IDs::looping, false);
    cs.muted         = clipTree.getProperty(IDs::muted, false);
    cs.isMidi        = clipTree.getProperty(IDs::clipType, "audio").toString() == "midi";
    cs.sourceBpm     = clipTree.getProperty(IDs::sourceBpm, 0.0);
    cs.stretchMode   = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
    cs.stretchRatio  = clipTree.getProperty(IDs::stretchRatio, 1.0);
    cs.sourceDuration= clipTree.getProperty(IDs::sourceDuration, 0.0);
    cs.isGhost       = static_cast<bool>(clipTree.getProperty(IDs::isGhost, 0));
    cs.ghostSourceId = static_cast<int>(clipTree.getProperty(IDs::ghostSourceId, -1));
    // CLIP -> CLIP_LIST -> TRACK -> position within TRACK_LIST
    auto track = clipTree.getParent().getParent();
    cs.trackIndex = track.getParent().indexOf(track);
    return cs;
}

TrackSnapshot buildTrackSnapshotFromTree(const juce::ValueTree& trackTree)
{
    TrackSnapshot ts;
    ts.index         = trackTree.getParent().indexOf(trackTree);
    ts.name          = trackTree.getProperty(IDs::name, "Track").toString().toStdString();
    ts.color         = static_cast<int>(trackTree.getProperty(IDs::color, 0));
    ts.volume        = trackTree.getProperty(IDs::volume, 1.0);
    ts.pan           = trackTree.getProperty(IDs::pan, 0.0);
    ts.muted         = trackTree.getProperty(IDs::isMuted, false);
    ts.soloed        = trackTree.getProperty(IDs::isSoloed, false);
    ts.armed         = trackTree.getProperty(IDs::isArm, false);
    ts.inputMonitor  = trackTree.getProperty(IDs::inputMonitor, false);
    ts.height        = trackTree.getProperty(IDs::trackHeight, 80.0);
    ts.midiChannel   = trackTree.getProperty(IDs::midiChannel, 1);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    ts.clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
    return ts;
}
```

Then replace the body of `ReadModelImpl::snapshot()` (currently lines 13–74) with:

```cpp
ProjectSnapshot ReadModelImpl::snapshot() const
{
    ProjectSnapshot snap;
    snap.name = model_.getTree().getProperty(IDs::name, "Untitled").toString().toStdString();
    snap.transport = getTransport();
    snap.scaleRoot = model_.getScaleRoot();
    snap.scaleMode = model_.getScaleMode();

    auto trackList = model_.getTrackListTree();
    const int numTracks = trackList.getNumChildren();
    snap.tracks.reserve(numTracks);

    for (int t = 0; t < numTracks; ++t) {
        auto trackTree = trackList.getChild(t);
        snap.tracks.push_back(buildTrackSnapshotFromTree(trackTree));

        auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid())
            continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
            snap.clips.push_back(buildClipSnapshotFromTree(clipList.getChild(c)));
    }

    return snap;
}
```

- [ ] **Step 6: Build and run the builder tests**

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=TreeDeltaBuilders.*`
Expected: PASS (3 tests).

- [ ] **Step 7: Run the full C++ suite to confirm the `snapshot()` refactor didn't regress**

Run: `build\Debug\hdaw_tests.exe`
Expected: all tests pass (same count as before, plus the 3 new).

- [ ] **Step 8: Commit**

```bash
git add src/engine/ReadModelImpl.h src/engine/ReadModelImpl.cpp tests/unit/frontend/tree_delta_accumulator_test.cpp tests/CMakeLists.txt
git commit -m "refactor: extract per-node snapshot builders from ReadModelImpl"
```

---

## Task 2: `TreeDeltaAccumulator` (classification + coalescing)

**Files:**
- Create: `src/frontend/TreeDeltaAccumulator.h`
- Create: `src/frontend/TreeDeltaAccumulator.cpp`
- Modify: `tests/unit/frontend/tree_delta_accumulator_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing accumulator tests**

Append to `tests/unit/frontend/tree_delta_accumulator_test.cpp` (add `#include "frontend/TreeDeltaAccumulator.h"` to the includes at the top):

```cpp
using frontend::TreeDeltaAccumulator;

TEST(TreeDelta, ClipPropertyChangeUpsertsClip) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAcc acc;
    acc.notePropertyChanged(clip);

    EXPECT_FALSE(acc.fullSync());
    ASSERT_EQ(acc.clipsUpserted().size(), 1u);
    EXPECT_EQ(acc.clipsUpserted().at(5).clipId, 5);
    EXPECT_TRUE(acc.clipsRemoved().empty());
}

TEST(TreeDelta, RepeatedClipChangesCoalesceToLatest) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    clip.setProperty(IDs::startTime, 9.0, nullptr);   // simulate a drag
    acc.notePropertyChanged(clip);

    EXPECT_EQ(acc.clipsUpserted().size(), 1u);        // coalesced
    EXPECT_DOUBLE_EQ(acc.clipsUpserted().at(5).startBeat, 9.0);  // latest wins
}

TEST(TreeDelta, ClipRemovedThenReaddedCancels) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.noteChildRemoved(clip);
    EXPECT_EQ(acc.clipsRemoved().size(), 1u);
    acc.noteChildAdded(clip);                          // re-added
    EXPECT_TRUE(acc.clipsRemoved().empty());           // removal cancelled
    EXPECT_EQ(acc.clipsUpserted().size(), 1u);
}

TEST(TreeDelta, ClipAddAfterRemoveOfSameIdIsUpsert) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.noteChildAdded(clip);                          // upsert
    acc.noteChildRemoved(clip);                        // then removed
    EXPECT_EQ(acc.clipsRemoved().size(), 1u);
    EXPECT_TRUE(acc.clipsUpserted().empty());          // upsert dropped
}

TEST(TreeDelta, TrackPropertyChangeUpsertsTrack) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(track);
    EXPECT_FALSE(acc.fullSync());
    ASSERT_EQ(acc.tracksUpserted().size(), 1u);
    EXPECT_EQ(acc.tracksUpserted().at(0).name, "Synth");
}

TEST(TreeDelta, SubClipDetailChangeIsFullSync) {
    ValueTree note(IDs::MIDI_NOTE);
    note.setProperty(IDs::noteNumber, 60, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(note);
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, TrackAddIsFullSync) {
    ValueTree track = makeTrackTree("New", 1.0);
    TreeDeltaAccumulator acc;
    acc.noteChildAdded(track);
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, StructuralChangeIsFullSync) {
    TreeDeltaAccumulator acc;
    acc.noteStructuralChange();
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, FullSyncDiscardsPendingDelta) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    acc.noteStructuralChange();                        // escalates to fullSync
    EXPECT_TRUE(acc.fullSync());
}

TEST(TreeDelta, ResetClearsState) {
    ValueTree trackList(IDs::TRACK_LIST);
    ValueTree track = makeTrackTree("Synth", 1.0);
    ValueTree clip = makeClipTree(5, 0.0, "C");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, nullptr);
    trackList.addChild(track, -1, nullptr);

    TreeDeltaAccumulator acc;
    acc.notePropertyChanged(clip);
    acc.reset();
    EXPECT_TRUE(acc.empty());
    EXPECT_FALSE(acc.fullSync());
}
```

- [ ] **Step 2: Add the source to `CMakeLists.txt`**

In `CMakeLists.txt`, add this line after `src/frontend/FrontendTreeWatcher.cpp` (line 120):

```cmake
    src/frontend/TreeDeltaAccumulator.cpp
```

- [ ] **Step 3: Run the test to verify it fails to compile**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: FAIL — `frontend/TreeDeltaAccumulator.h` not found.

- [ ] **Step 4: Create `src/frontend/TreeDeltaAccumulator.h`**

```cpp
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "../common/ReadModel.h"
#include <unordered_map>
#include <set>

namespace frontend {

// Accumulates granular juce::ValueTree change callbacks over a debounce window
// and coalesces them into a minimal delta (changed/removed clips, changed
// tracks). Any change it cannot cleanly represent as a delta (track add/remove,
// markers, tempo, FX, automation, sub-clip detail, reorder) sets a fullSync flag
// so the client falls back to a whole-snapshot re-fetch.
//
// Used by FrontendTreeWatcher. Unit-testable in isolation (no server needed).
class TreeDeltaAccumulator {
public:
    void notePropertyChanged(const juce::ValueTree& tree);
    void noteChildAdded(const juce::ValueTree& child);
    void noteChildRemoved(const juce::ValueTree& child);
    void noteStructuralChange();

    bool fullSync() const { return fullSync_; }
    const std::unordered_map<int, ClipSnapshot>& clipsUpserted() const { return clipsUpserted_; }
    const std::set<int>& clipsRemoved() const { return clipsRemoved_; }
    const std::unordered_map<int, TrackSnapshot>& tracksUpserted() const { return tracksUpserted_; }
    bool empty() const {
        return !fullSync_ && clipsUpserted_.empty() && clipsRemoved_.empty() && tracksUpserted_.empty();
    }
    void reset();

private:
    void upsertClip(const juce::ValueTree& clipTree);
    void removeClip(const juce::ValueTree& clipTree);
    void upsertTrack(const juce::ValueTree& trackTree);

    std::unordered_map<int, ClipSnapshot> clipsUpserted_;   // clipId -> latest snapshot
    std::set<int> clipsRemoved_;
    std::unordered_map<int, TrackSnapshot> tracksUpserted_; // trackIndex -> latest snapshot
    bool fullSync_ = false;
};

} // namespace frontend
```

- [ ] **Step 5: Create `src/frontend/TreeDeltaAccumulator.cpp`**

```cpp
#include "TreeDeltaAccumulator.h"
#include "../engine/ReadModelImpl.h"
#include "../model/ProjectModel.h"

namespace frontend {

void TreeDeltaAccumulator::notePropertyChanged(const juce::ValueTree& tree) {
    if (fullSync_) return;
    const auto type = tree.getType();
    if (type == IDs::CLIP)        upsertClip(tree);
    else if (type == IDs::TRACK)  upsertTrack(tree);
    else                          fullSync_ = true;   // PROJECT/MARKER/FX/automation/sub-clip detail/...
}

void TreeDeltaAccumulator::noteChildAdded(const juce::ValueTree& child) {
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) upsertClip(child);
    else                              fullSync_ = true;  // TRACK add (indices shift), notes, markers, ...
}

void TreeDeltaAccumulator::noteChildRemoved(const juce::ValueTree& child) {
    if (fullSync_) return;
    if (child.getType() == IDs::CLIP) removeClip(child);
    else                              fullSync_ = true;  // TRACK remove, notes, markers, ...
}

void TreeDeltaAccumulator::noteStructuralChange() {
    fullSync_ = true;
}

void TreeDeltaAccumulator::upsertClip(const juce::ValueTree& clipTree) {
    ClipSnapshot snap = buildClipSnapshotFromTree(clipTree);
    clipsUpserted_[snap.clipId] = snap;
    clipsRemoved_.erase(snap.clipId);   // re-added cancels a pending removal
}

void TreeDeltaAccumulator::removeClip(const juce::ValueTree& clipTree) {
    const int clipId = static_cast<int>(clipTree.getProperty(IDs::clipID, 0));
    clipsRemoved_.insert(clipId);
    clipsUpserted_.erase(clipId);       // removed drops a pending upsert
}

void TreeDeltaAccumulator::upsertTrack(const juce::ValueTree& trackTree) {
    TrackSnapshot snap = buildTrackSnapshotFromTree(trackTree);
    tracksUpserted_[snap.index] = snap;
}

void TreeDeltaAccumulator::reset() {
    clipsUpserted_.clear();
    clipsRemoved_.clear();
    tracksUpserted_.clear();
    fullSync_ = false;
}

} // namespace frontend
```

- [ ] **Step 6: Build and run the accumulator tests**

Run: `cmake --build build --config Debug --target hdaw_tests && build\Debug\hdaw_tests.exe --gtest_filter=TreeDelta.*`
Expected: PASS (10 tests).

- [ ] **Step 7: Commit**

```bash
git add src/frontend/TreeDeltaAccumulator.h src/frontend/TreeDeltaAccumulator.cpp CMakeLists.txt tests/unit/frontend/tree_delta_accumulator_test.cpp
git commit -m "feat: TreeDeltaAccumulator classifies and coalesces ValueTree changes"
```

---

## Task 3: Wire `FrontendTreeWatcher` to the accumulator + broadcast delta

**Files:**
- Modify: `src/frontend/FrontendTreeWatcher.h`
- Modify: `src/frontend/FrontendTreeWatcher.cpp`

- [ ] **Step 1: Update `src/frontend/FrontendTreeWatcher.h`**

Replace the private listener-callback block and add the accumulator member. Change the three inline callbacks into declarations, add two more overrides, and add the accumulator + a flush helper:

Replace lines 31–41 (the `private:` section through `debounceTimer_`) with:

```cpp
private:
    // juce::ValueTree::Listener — feed the accumulator, then schedule a flush.
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;
    void valueTreeParentChanged(juce::ValueTree&) override;

    void scheduleNotify();
    void flush();

    AudioEngine& engine_;
    FrontendServer& server_;
    class QTimer* debounceTimer_ = nullptr;
    TreeDeltaAccumulator accumulator_;
```

Add the include near the top (after `#include <juce_data_structures/juce_data_structures.h>`):

```cpp
#include "TreeDeltaAccumulator.h"
```

- [ ] **Step 2: Update `src/frontend/FrontendTreeWatcher.cpp`**

Replace the whole file body with:

```cpp
#include "FrontendTreeWatcher.h"
#include "FrontendServer.h"
#include "FrontendRpc.h"
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"

#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

namespace frontend {

namespace {
constexpr int kDebounceMs = 16;   // coalesce burst edits into one broadcast
}

FrontendTreeWatcher::FrontendTreeWatcher(AudioEngine& engine, FrontendServer& server, QObject* parent)
    : QObject(parent), engine_(engine), server_(server)
{
    // Attach to the ROOT project tree (documented-safe pattern; survives
    // File->New / load rebuilds). See AGENTS.md "ValueTree listener orphans".
    engine_.getProjectModel().getTree().addListener(this);

    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kDebounceMs);
    connect(debounceTimer_, &QTimer::timeout, this, [this]() { flush(); });
}

FrontendTreeWatcher::~FrontendTreeWatcher() {
    engine_.getProjectModel().getTree().removeListener(this);
}

void FrontendTreeWatcher::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) {
    accumulator_.notePropertyChanged(tree);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) {
    accumulator_.noteChildAdded(child);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) {
    accumulator_.noteChildRemoved(child);
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeChildOrderChanged(juce::ValueTree&, int, int) {
    accumulator_.noteStructuralChange();
    scheduleNotify();
}

void FrontendTreeWatcher::valueTreeParentChanged(juce::ValueTree&) {
    accumulator_.noteStructuralChange();
    scheduleNotify();
}

void FrontendTreeWatcher::scheduleNotify() {
    // Single-shot debounce: repeated calls during a burst just keep it pending.
    if (!debounceTimer_->isActive())
        debounceTimer_->start();
}

void FrontendTreeWatcher::flush() {
    if (accumulator_.fullSync()) {
        server_.broadcastNotification(notify::TreeChanged,
            QJsonObject{ { "fullSync", true } });
    } else if (!accumulator_.empty()) {
        QJsonObject payload{ { "fullSync", false } };

        QJsonArray clipsUpserted;
        for (const auto& [id, c] : accumulator_.clipsUpserted())
            clipsUpserted.append(toJson(c));
        payload.insert("clipsUpserted", clipsUpserted);

        QJsonArray clipsRemoved;
        for (int id : accumulator_.clipsRemoved())
            clipsRemoved.append(id);
        payload.insert("clipsRemoved", clipsRemoved);

        QJsonArray tracksUpserted;
        for (const auto& [id, t] : accumulator_.tracksUpserted())
            tracksUpserted.append(toJson(t));
        payload.insert("tracksUpserted", tracksUpserted);

        server_.broadcastNotification(notify::TreeChanged, payload);
    }
    // else: nothing snapshot-relevant changed -> no broadcast.
    accumulator_.reset();
}

} // namespace frontend
```

- [ ] **Step 3: Build the C++ project**

Run: `cmake --build build --config Debug`
Expected: build succeeds.

- [ ] **Step 4: Run the full C++ suite**

Run: `build\Debug\hdaw_tests.exe`
Expected: all tests pass (the watcher change is exercised indirectly by frontend_server_test; no regressions).

- [ ] **Step 5: Commit**

```bash
git add src/frontend/FrontendTreeWatcher.h src/frontend/FrontendTreeWatcher.cpp
git commit -m "feat: FrontendTreeWatcher broadcasts coalesced deltas (fullSync fallback)"
```

---

## Task 4: Frontend `TreeDelta` type + `applyDelta` store action

**Files:**
- Modify: `frontend/src/rpc/types.ts`
- Modify: `frontend/src/store/projectStore.ts`
- Modify: `frontend/src/store/projectStore.test.ts`

- [ ] **Step 1: Write the failing `applyDelta` tests**

Append to `frontend/src/store/projectStore.test.ts` (inside the existing top-level `describe`, or a new `describe("applyDelta", ...)` block). Add a helper to build a clip:

```typescript
import type { TreeDelta, ClipSnapshot, TrackSnapshot } from "../rpc/types";

const mkClip = (clipId: number, trackIndex: number, startBeat: number): ClipSnapshot => ({
  clipId, trackIndex, name: `Clip ${clipId}`, sourceFile: "", startBeat,
  durationBeats: 4, offset: 0, gain: 1, fadeIn: 0, fadeOut: 0, looping: false,
  muted: false, isMidi: true, sourceBpm: 0, stretchMode: 0, stretchRatio: 1,
  sourceDuration: 0, isGhost: false, ghostSourceId: -1, gainEnvelope: [],
});

describe("applyDelta", () => {
  beforeEach(() => {
    useProjectStore.setState({ snapshot: structuredClone(mockSnapshot), lastSync: 0 });
  });

  it("upserts a new clip", () => {
    const before = useProjectStore.getState().snapshot!.clips.length;
    const delta: TreeDelta = { fullSync: false, clipsUpserted: [mkClip(999, 0, 16)] };
    useProjectStore.getState().applyDelta(delta);
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.length).toBe(before + 1);
    expect(clips.find((c) => c.clipId === 999)?.startBeat).toBe(16);
  });

  it("replaces an existing clip in place", () => {
    const existing = useProjectStore.getState().snapshot!.clips[0];
    const updated = { ...existing, startBeat: 123 };
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [updated] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.filter((c) => c.clipId === existing.clipId).length).toBe(1);
    expect(clips.find((c) => c.clipId === existing.clipId)?.startBeat).toBe(123);
  });

  it("removes clips by id", () => {
    const target = useProjectStore.getState().snapshot!.clips[0];
    const before = useProjectStore.getState().snapshot!.clips.length;
    useProjectStore.getState().applyDelta({ fullSync: false, clipsRemoved: [target.clipId] });
    const clips = useProjectStore.getState().snapshot!.clips;
    expect(clips.length).toBe(before - 1);
    expect(clips.find((c) => c.clipId === target.clipId)).toBeUndefined();
  });

  it("upserts a track and keeps tracks sorted by index", () => {
    const t: TrackSnapshot = { ...useProjectStore.getState().snapshot!.tracks[0], volume: 0.25 };
    useProjectStore.getState().applyDelta({ fullSync: false, tracksUpserted: [t] });
    const tracks = useProjectStore.getState().snapshot!.tracks;
    expect(tracks.find((x) => x.index === 0)?.volume).toBe(0.25);
    for (let i = 1; i < tracks.length; i++) expect(tracks[i].index).toBeGreaterThan(tracks[i - 1].index);
  });

  it("keeps object references for unchanged clips", () => {
    const clipsBefore = useProjectStore.getState().snapshot!.clips;
    const unchanged = clipsBefore[clipsBefore.length - 1];
    useProjectStore.getState().applyDelta({ fullSync: false, clipsUpserted: [mkClip(999, 0, 0)] });
    const clipsAfter = useProjectStore.getState().snapshot!.clips;
    expect(clipsAfter.find((c) => c.clipId === unchanged.clipId)).toBe(unchanged); // same reference
  });

  it("is a no-op when there is no snapshot yet", () => {
    useProjectStore.setState({ snapshot: null });
    expect(() => useProjectStore.getState().applyDelta({ fullSync: false, clipsRemoved: [1] })).not.toThrow();
    expect(useProjectStore.getState().snapshot).toBeNull();
  });
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd frontend && npx vitest run projectStore`
Expected: FAIL — `TreeDelta` type missing and `applyDelta` is not a function.

- [ ] **Step 3: Add the `TreeDelta` interface to `frontend/src/rpc/types.ts`**

Add after the `ProjectSnapshot` interface (around line 73):

```typescript
export interface TreeDelta {
  fullSync: boolean;
  clipsUpserted?: ClipSnapshot[];
  clipsRemoved?: number[];
  tracksUpserted?: TrackSnapshot[];
}
```

- [ ] **Step 4: Add `applyDelta` to `frontend/src/store/projectStore.ts`**

Add `applyDelta` to the `ProjectState` interface (after `syncNotes`):

```typescript
  applyDelta: (delta: TreeDelta) => void;
```

Add the import of `TreeDelta` to the existing type import line:

```typescript
import { ProjectSnapshot, TrackSnapshot, ClipSnapshot, NoteSnapshot, TreeDelta } from "../rpc/types";
```

Add the implementation inside the `create<ProjectState>((set, get) => ({ ... }))` body (after `syncNotes`):

```typescript
  applyDelta: (delta: TreeDelta) => {
    const snap = get().snapshot;
    if (!snap) return;

    let clips = snap.clips;
    if (delta.clipsRemoved && delta.clipsRemoved.length > 0) {
      const rm = new Set(delta.clipsRemoved);
      clips = clips.filter((c) => !rm.has(c.clipId));
    }
    if (delta.clipsUpserted && delta.clipsUpserted.length > 0) {
      const byId = new Map(clips.map((c) => [c.clipId, c] as const));
      for (const c of delta.clipsUpserted) byId.set(c.clipId, c);
      clips = [...byId.values()];
    }

    let tracks = snap.tracks;
    if (delta.tracksUpserted && delta.tracksUpserted.length > 0) {
      const byIdx = new Map(tracks.map((t) => [t.index, t] as const));
      for (const t of delta.tracksUpserted) byIdx.set(t.index, t);
      tracks = [...byIdx.values()].sort((a, b) => a.index - b.index);
    }

    set({ snapshot: { ...snap, clips, tracks }, lastSync: Date.now() });
  },
```

- [ ] **Step 5: Run the frontend tests**

Run: `cd frontend && npx vitest run projectStore`
Expected: PASS (the 6 new `applyDelta` tests + existing tests).

- [ ] **Step 6: Typecheck**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 7: Commit**

```bash
git add frontend/src/rpc/types.ts frontend/src/store/projectStore.ts frontend/src/store/projectStore.test.ts
git commit -m "feat: applyDelta store action patches snapshot from tree deltas"
```

---

## Task 5: Delta-vs-fullSync routing in `main.tsx`

**Files:**
- Modify: `frontend/src/main.tsx`

- [ ] **Step 1: Update the `notify.treeChanged` handler**

In `frontend/src/main.tsx`, add `TreeDelta` to the types import (line 10):

```typescript
import { TransportSnapshot, MetersPayload, TreeDelta } from "./rpc/types";
```

Replace the existing `notify.treeChanged` subscription (lines 41–48):

```typescript
  cleanups.push(rpc.onNotification("notify.treeChanged", (_, params) => {
    const d = params as TreeDelta | undefined;
    if (d && !d.fullSync && (d.clipsUpserted || d.clipsRemoved || d.tracksUpserted)) {
      // Incremental path: patch the existing snapshot in place.
      useProjectStore.getState().applyDelta(d);
    } else {
      // Full-sync fallback (structural/complex change, or bare notification).
      useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
      // Refresh automation lanes only on the full-sync path; automation edits
      // route through fullSync, so recording still refreshes.
      const activeTrack = useAutomationStore.getState().activeTrackIndex;
      if (activeTrack !== null) {
        useAutomationStore.getState().fetchForTrack(activeTrack, rpc);
      }
    }
  }));
```

- [ ] **Step 2: Typecheck**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors.

- [ ] **Step 3: Run the full frontend suite**

Run: `cd frontend && npx vitest run`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/main.tsx
git commit -m "feat: route treeChanged to applyDelta with full-sync fallback"
```

---

## Task 6: Build, repackage, and integration smoke test

- [ ] **Step 1: Full C++ build + tests**

Run: `cmake --build build --config Debug && build\Debug\hdaw_tests.exe`
Expected: build succeeds, all tests pass.

- [ ] **Step 2: Frontend build + typecheck + tests**

Run: `cd frontend && npx tsc --noEmit && npx vitest run && npm run build`
Expected: all green.

- [ ] **Step 3: Repackage the Electron app (the binary the user runs)**

Run: `cd frontend && npx electron-builder --win --x64 --dir`
Expected: packaging completes (signing warnings are harmless).

- [ ] **Step 4: Manual smoke test**

Launch `frontend\release\win-unpacked\HDAW.exe`. Verify:
1. Move a clip → it follows and settles with no perceptible lag.
2. Delete a clip (keyboard and context menu) → disappears promptly.
3. Duplicate a clip → new clip appears.
4. Multi-select several clips → delete → all removed.
5. Edit a MIDI note in the piano roll → note updates (sub-clip fullSync path still works).
6. Record/toggle automation → lanes refresh.
7. Undo/redo → state correct.
8. Add/remove a track → full sync, correct.

- [ ] **Step 5: Commit any fixes; tag the milestone**

```bash
git add -A
git commit -m "test: incremental tree sync integration smoke verified"
```

---

# Phase 2 — Pending placeholders (Duplicate / Add)

## Task 7: Pending-placeholder store state + rendering

**Files:**
- Modify: `frontend/src/store/projectStore.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

- [ ] **Step 1: Add pending state to `projectStore.ts`**

Add to the `ProjectState` interface:

```typescript
  pendingTempIds: Set<number>;
  pendingResolution: Map<number, number>;
  addPendingClip: (placeholder: ClipSnapshot) => void;
  resolvePending: (tempId: number, realId: number) => void;
  removePending: (tempId: number) => void;
```

Add defaults in the store body:

```typescript
  pendingTempIds: new Set(),
  pendingResolution: new Map(),
```

Add implementations:

```typescript
  addPendingClip: (placeholder: ClipSnapshot) => set((state) => {
    const snap = state.snapshot;
    if (!snap) return {};
    const pendingTempIds = new Set(state.pendingTempIds);
    pendingTempIds.add(placeholder.clipId);
    return {
      snapshot: { ...snap, clips: [...snap.clips, placeholder] },
      pendingTempIds,
    };
  }),

  resolvePending: (tempId: number, realId: number) => set((state) => {
    const pendingResolution = new Map(state.pendingResolution);
    pendingResolution.set(tempId, realId);
    return { pendingResolution };
  }),

  removePending: (tempId: number) => set((state) => {
    const snap = state.snapshot;
    const pendingTempIds = new Set(state.pendingTempIds);
    pendingTempIds.delete(tempId);
    const pendingResolution = new Map(state.pendingResolution);
    pendingResolution.delete(tempId);
    if (!snap) return { pendingTempIds, pendingResolution };
    return {
      snapshot: { ...snap, clips: snap.clips.filter((c) => c.clipId !== tempId) },
      pendingTempIds,
      pendingResolution,
    };
  }),
```

Also update `applyDelta` so that when a real clip arrives whose id matches a pending resolution, the placeholder is removed. Add at the end of `applyDelta` (before the final `set`), computing a cleaned `clips` and pruned pending sets:

```typescript
    // Swap resolved placeholders out for the real clips that just arrived.
    let pendingTempIds = get().pendingTempIds;
    let pendingResolution = get().pendingResolution;
    if (pendingResolution.size > 0 && delta.clipsUpserted) {
      const arrivedReal = new Set(delta.clipsUpserted.map((c) => c.clipId));
      const resolvedTemps: number[] = [];
      for (const [tempId, realId] of pendingResolution) {
        if (arrivedReal.has(realId)) resolvedTemps.push(tempId);
      }
      if (resolvedTemps.length > 0) {
        const resolvedSet = new Set(resolvedTemps);
        clips = clips.filter((c) => !resolvedSet.has(c.clipId));
        pendingTempIds = new Set([...pendingTempIds].filter((id) => !resolvedSet.has(id)));
        pendingResolution = new Map([...pendingResolution].filter(([t]) => !resolvedSet.has(t)));
      }
    }
```

and include `pendingTempIds, pendingResolution` in the final `set({ ... })` of `applyDelta`.

- [ ] **Step 2: Render pending clips translucent in `TimelineMinimal.tsx`**

In the clip-rendering `className` expression (around line 565), add a pending flag. Read the pending set near the other store reads:

```typescript
const pendingTempIds = useProjectStore((s) => s.pendingTempIds);
```

and extend the clip `className`:

```typescript
className={`tl-clip ${clip.isMidi ? "tl-clip--midi" : "tl-clip--audio"}${isDragging ? " tl-clip--dragging" : ""}${isSelected ? " tl-clip--selected" : ""}${clip.isGhost ? " tl-clip--ghost" : ""}${pendingTempIds.has(clip.clipId) ? " tl-clip--pending" : ""}`}
```

- [ ] **Step 3: Add the `.tl-clip--pending` style to `TimelineMinimal.css`**

```css
.tl-clip--pending {
  opacity: 0.4;
  pointer-events: none;
}
```

- [ ] **Step 4: Typecheck + tests**

Run: `cd frontend && npx tsc --noEmit && npx vitest run`
Expected: green.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/store/projectStore.ts frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "feat: pending-placeholder state + translucent rendering"
```

---

## Task 8: Wire Duplicate / Add to placeholders + reconciliation

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx` (`handleDuplicateClip`)
- Modify: `frontend/src/components/TimelineContextMenu.tsx` (Add MIDI Clip)

- [ ] **Step 1: Make `handleDuplicateClip` show placeholders, then reconcile**

Replace `handleDuplicateClip` in `TimelineMinimal.tsx`:

```typescript
  const handleDuplicateClip = useCallback(() => {
    const { selectedClipIds } = useUiStore.getState();
    const snap = useProjectStore.getState().snapshot;
    if (!snap) return;
    const ids = selectedClipIds.size > 0 ? [...selectedClipIds] : (contextMenu?.clip ? [contextMenu.clip.clipId] : []);
    if (ids.length === 0) return;

    // Create translucent placeholders (negative temp ids) for instant feedback.
    const tempIds: number[] = [];
    ids.forEach((id, i) => {
      const src = snap.clips.find((c) => c.clipId === id);
      if (!src) return;
      const tempId = -(Date.now() + i);
      tempIds.push(tempId);
      useProjectStore.getState().addPendingClip({ ...src, clipId: tempId });
    });

    (async () => {
      try {
        const res = await rpc.call("project.duplicateClips", { clipIds: ids });
        const newIds: number[] = Array.isArray(res) ? res.filter((x): x is number => typeof x === "number") : [];
        // Map placeholder -> real id by order (duplicateClips returns ids in input order).
        tempIds.forEach((tempId, i) => {
          if (newIds[i] != null) useProjectStore.getState().resolvePending(tempId, newIds[i]);
        });
        useProjectStore.setState({ isDirty: true });
      } catch {
        tempIds.forEach((t) => useProjectStore.getState().removePending(t));
      }
      // Sweep any still-unresolved placeholders shortly (delta normally swaps them).
      setTimeout(() => tempIds.forEach((t) => {
        if (useProjectStore.getState().pendingTempIds.has(t)) useProjectStore.getState().removePending(t);
      }), 1500);
    })();
  }, [contextMenu]);
```

- [ ] **Step 2: Make "Add MIDI Clip" (empty context menu) show a placeholder**

In `TimelineContextMenu.tsx`, replace the "Add MIDI Clip" button handler:

```typescript
          <button onMouseDown={(e) => {
            e.stopPropagation();
            const tempId = -Date.now();
            useProjectStore.getState().addPendingClip({
              clipId: tempId, trackIndex: 0, name: "New MIDI Clip", sourceFile: "",
              startBeat: emptyContextMenu.beat, durationBeats: 4, offset: 0, gain: 1,
              fadeIn: 0, fadeOut: 0, looping: false, muted: false, isMidi: true,
              sourceBpm: 0, stretchMode: 0, stretchRatio: 1, sourceDuration: 0,
              isGhost: false, ghostSourceId: -1, gainEnvelope: [],
            });
            rpc.call("project.addMidiClip", {
              trackIndex: 0, start: emptyContextMenu.beat, duration: 4, name: "New MIDI Clip",
            }).then((res) => {
              const realId = typeof res === "number" ? res : (res && typeof res === "object" && "clipId" in (res as any) ? (res as any).clipId : null);
              if (realId != null) useProjectStore.getState().resolvePending(tempId, realId);
              else useProjectStore.getState().removePending(tempId);
            }).catch(() => useProjectStore.getState().removePending(tempId));
            setTimeout(() => {
              if (useProjectStore.getState().pendingTempIds.has(tempId)) useProjectStore.getState().removePending(tempId);
            }, 1500);
            onClose();
          }}>
            Add MIDI Clip
          </button>
```

- [ ] **Step 3: Typecheck + tests**

Run: `cd frontend && npx tsc --noEmit && npx vitest run`
Expected: green.

- [ ] **Step 4: Repackage + manual smoke**

Run: `cd frontend && npm run build && npx electron-builder --win --x64 --dir`
Launch `frontend\release\win-unpacked\HDAW.exe`. Verify:
1. Duplicate a clip → translucent placeholder appears instantly, swaps to the real clip.
2. Add MIDI Clip (right-click empty space) → placeholder appears, swaps to real clip.
3. If the RPC is blocked/fails → placeholder is removed (no orphan).

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineContextMenu.tsx
git commit -m "feat: pending placeholders for duplicate/add with delta reconciliation"
```

---

## Final verification

- [ ] **Full C++ build + all gtest:** `cmake --build build --config Debug && build\Debug\hdaw_tests.exe`
- [ ] **Frontend typecheck + all Vitest:** `cd frontend && npx tsc --noEmit && npx vitest run`
- [ ] **Repackage:** `cd frontend && npm run build && npx electron-builder --win --x64 --dir`
- [ ] **Manual regression pass** (Task 6 Step 4 list + Phase 2 placeholder checks).
