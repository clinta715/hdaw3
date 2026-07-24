# ValueTree Listener Contract

This document defines the canonical pattern for registering `juce::ValueTree::Listener`
on the HDAW project model. It consolidates knowledge previously scattered across
three domains: Qt widget pitfalls, engine-side sync bugs, and the GUI-engine
decoupling architecture.

**Cross-references:**
- [`realtime-safety.md`](realtime-safety.md) — engine-side listener sync requirements
- [`architecture.md`](architecture.md) — ReadModel and abstract command interfaces

---

## 1. The Problem

`ProjectSerializer::createNew()` and `ProjectSerializer::load()` call
`projectTree.removeAllChildren()`, which **detaches every child node** from the
root tree. Any `ValueTree::Listener` registered on an orphaned child
(`TRANSPORT`, `TRACK_LIST`, `MIDI_NOTE_LIST`, `TEMPO_POINT_LIST`, `MARKER_LIST`)
**silently stops working** — the old child instance is reference-counted but no
longer part of the project hierarchy.

This bug has been fixed 3+ times across versions (v0.4.2 `MainWindow`, v0.12.0
`AudioEngine` transport sync, `TimelineScene` track removal, `TimelineView`
marker removal). Each time, the root cause was the same: a listener on a child
node instead of the root tree.

## 2. The Correct Pattern

**Always register on the root tree.** Filter by node type inside the listener:

```cpp
// In constructor:
engine.getProjectModel().getTree().addListener(this);

// Property changes — filter by node type:
void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                              const juce::Identifier& property) override {
    if (treeWhosePropertyHasChanged.hasType(IDs::TRANSPORT)
        && property == IDs::isLooping)
        syncLoopUI(treeWhosePropertyHasChanged.getProperty(IDs::isLooping));
}

// Child additions — handle initial state that fires before listener attaches:
void valueTreeChildAdded(juce::ValueTree& /*parent*/,
                         juce::ValueTree& childWhichHasBeenAdded) override {
    if (childWhichHasBeenAdded.hasType(IDs::TRANSPORT))
        syncLoopUI(childWhichHasBeenAdded.getProperty(IDs::isLooping));
}

// Child removals — clean up stale state:
void valueTreeChildRemoved(juce::ValueTree& /*parent*/,
                           juce::ValueTree& /*childWhichHasBeenRemoved*/,
                           int /*indexFromWhichChildWasRemoved*/) override {
    if (childWhichHasBeenRemoved.hasType(IDs::TRACK))
        removeTrackRow(childWhichHasBeenRemoved, index);
}
```

**Key properties on child nodes are set *before* `addChild()`**, so
`valueTreePropertyChanged` never fires for their initial state. The
`valueTreeChildAdded` handler must sync the same state.

## 3. Engine-Side Manifestations

The same pattern causes bugs in the audio engine:

| Component | Node | Bug | Fix Location |
|-----------|------|-----|-------------|
| `AudioEngine` | `TRANSPORT` | Loop state atomics retained stale values after File→New until next user toggle | `AudioEngine::valueTreeChildAdded` now syncs all transport properties |
| `AudioEngine` | `TRACK` | Audio clip deletion didn't stop playback (missing `valueTreeChildRemoved` → `rebuildRoutingGraph()`) | `AudioEngine::valueTreeChildRemoved` calls `mainProcessor->rebuildRoutingGraph()` |
| `TimelineScene` | `TRACK` | Track removal left stale row in scene | `TimelineScene::valueTreeChildRemoved` handles `IDs::TRACK` |
| `TimelineView` | `MARKER_LIST` | UI teardown crash on project rebuild | Listener attaches/detaches in `rebuildUI()` |

## 4. The Architectural Solution: ReadModel

The GUI-engine decoupling (v0.7.0) introduces `ReadModel` — a read-only snapshot
interface that replaces direct ValueTree access for paint paths and timers:

```cpp
// BEFORE (direct ValueTree access, fragile):
trackNameLabel.setText(engine.getProjectModel().getTrackTree().getChild(idx)
    .getProperty(IDs::name).toString());

// AFTER (ReadModel snapshot, safe):
trackNameLabel.setText(readModel->getTrack(idx).name);
```

`ReadModel` provides 19 read-only methods returning `TrackSnapshot`,
`ClipSnapshot`, `TransportSnapshot`, etc. — all derived from ValueTree at the
time of the call. Widgets that use `ReadModel` for paint paths are **immune** to
the listener orphan problem because they don't hold references to child nodes.

**Remaining listeners:** Widgets that must observe changes in real time (loop
marker drag, automation lane editing, VU meters) still need `ValueTree::Listener`.
These must follow the root-tree pattern from §2.

## 5. Audit Checklist

Every `addListener(this)` call in the codebase should be verified:

```bash
# Find all listener registrations:
grep -rn "\.addListener(this)" src/ --include="*.cpp"
```

For each result, verify:

1. **Is it on the root tree?** `getTree().addListener(this)` — ✅ safe
2. **Is it on a child tree?** `getTransportTree().addListener(this)` — ❌ orphanable
3. **If on a child tree, is it re-attached on every rebuild?** Check for
   `detachListener()`/`attachListener()` around rebuild paths — ⚠️ fragile but safe

**Known safe child-tree listeners (re-attach pattern):**
- `TimelineScene::attachListener()` / `detachListener()` in `rebuildFromValueTree()`

**Known safe root-tree listeners:**
- `AudioEngine` (constructor)
- `MainWindow` (v0.4.2 fix)
- `TrackHeaderWidget` (v0.3.x hardening)
- `MixerStripWidget` (v0.3.x hardening)
- `FrontendTreeWatcher` (constructor)

## 6. Adding a New Listener

When adding a new `ValueTree::Listener` to the codebase:

1. **Use the root tree.** If you need to scope to a specific node, filter by
   `tree.hasType(IDs::XXX)` inside the listener method.
2. **Handle `valueTreeChildAdded`.** Properties on the new child are set before
   `addChild()`, so the *add* event is your only chance to sync initial state.
3. **Handle `valueTreeChildRemoved`** if your UI tracks child nodes (track list,
   clip list). Otherwise stale UI elements persist.
4. **Prefer `ReadModel` for paint paths.** If you're calling `addListener` only
   to refresh a label or paint a widget, use `ReadModel` snapshots instead.
5. **Document the contract.** Add a comment at the registration site:
   ```cpp
   // ROOT-TREE LISTENER — survives project rebuilds.
   // Handles: TRANSPORT property changes, TRACK add/remove.
   engine.getProjectModel().getTree().addListener(this);
   ```
