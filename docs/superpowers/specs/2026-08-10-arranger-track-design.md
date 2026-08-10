# Arranger Track Design

**Date:** 2026-08-10
**Status:** Approved for implementation
**Scope:** Cubase-style arranger track — named timeline regions, chain editor, non-linear playback, multiple chains, flatten.

---

## Overview

The arranger track lets users define named sections on the timeline (Intro, Verse, Chorus, etc.), then build playback chains from those sections to create alternate arrangements without moving clips. This is HDAW's remix/arrangement tool.

### Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Region placement | Dedicated lane on timeline (between ruler and tracks) | Matches Cubase UX, visible without opening a panel |
| Chain editor | Bottom panel tab | Follows HDAW's "one detail view" pattern |
| Playback mode | Only chain entries play; unmarked content is skipped | Cleanest for remixing — full control over what plays |
| Overlaps | Non-overlapping, tiled regions | Simpler model, cleaner chain behavior |
| Data storage | ValueTree-native (new `ARRANGER_LIST` / `ARRANGER_CHAIN_LIST`) | Follows marker/clip patterns — undo, delta-sync, save/load are automatic |

---

## Data Model

New ValueTree IDs in `ProjectModel.h`:

```
PROJECT (root)
  ├── ARRANGER_LIST
  │     └── ARRANGER_REGION { regionID, regionName, startTime, duration, color }
  ├── ARRANGER_CHAIN_LIST
  │     └── ARRANGER_CHAIN { chainID, chainName, isActive }
  │           └── CHAIN_ENTRY { regionID, repeatCount }
  └── ... (existing nodes)
```

### Properties

**ARRANGER_REGION:**

| Property | Type | Unit | Description |
|----------|------|------|-------------|
| `regionID` | string | — | UUID, stable reference for chains |
| `regionName` | string | — | Display name ("Intro", "Verse 1") |
| `startTime` | double | beats | Region start on the timeline |
| `duration` | double | beats | Region length |
| `color` | int | ARGB | Display color (default: `--accent` amber) |

**ARRANGER_CHAIN:**

| Property | Type | Description |
|----------|------|-------------|
| `chainID` | string | UUID |
| `chainName` | string | Display name ("Arrangement A") |
| `isActive` | bool | Only one chain active at a time |

**CHAIN_ENTRY:**

| Property | Type | Description |
|----------|------|-------------|
| `regionID` | string | References an ARRANGER_REGION |
| `repeatCount` | int | How many times to play (min 1) |

### Invariants

- Regions do not overlap (enforced by frontend snapping/validation).
- Regions tile the timeline (no gaps within the arranger range — gaps outside are fine).
- Only one chain has `isActive = true` at any time.
- Removing a region cascades: all chain entries referencing it are removed.
- `ARRANGER_LIST` and `ARRANGER_CHAIN_LIST` are lazily created (not present in default project).

---

## C++ Engine

### Commands

Declared in `ProjectCommands.h`, implemented in `AudioEngineCommands_Arranger.cpp`, dispatched via `Router_Project.cpp`.

| Command | Signature | Behavior |
|---------|-----------|----------|
| `addArrangerRegion` | `(name, startTime, duration, color) -> string` | Creates `ARRANGER_LIST` if needed, appends region, returns `regionID` |
| `removeArrangerRegion` | `(regionID)` | Removes region + cascading chain entries |
| `setArrangerRegionName` | `(regionID, name)` | Renames |
| `setArrangerRegionBounds` | `(regionID, startTime, duration)` | Moves/resizes |
| `setArrangerRegionColor` | `(regionID, color)` | Recolor |
| `addArrangerChain` | `(name) -> string` | Creates `ARRANGER_CHAIN_LIST` if needed, appends chain, returns chainID. Auto-activates if it's the first chain. |
| `removeArrangerChain` | `(chainID)` | Removes chain; activates another if it was active |
| `setArrangerChainName` | `(chainID, name)` | Renames |
| `setArrangerChainActive` | `(chainID)` | Sets this chain active, deactivates all others |
| `addChainEntry` | `(chainID, regionID, repeatCount) -> int` | Appends entry, returns index |
| `removeChainEntry` | `(chainID, entryIndex)` | Removes entry |
| `reorderChainEntry` | `(chainID, fromIndex, toIndex)` | Moves entry within chain |
| `setChainEntryRepeat` | `(chainID, entryIndex, repeatCount)` | Changes repeat count |
| `flattenArranger` | `()` | Renders the active chain into linear timeline |

All mutations go through `UndoManager` — fully undoable.

### ReadModel

New structs and methods in `ReadModel.h`:

```cpp
struct ArrangerRegionSnapshot {
    std::string regionID;
    std::string name;
    double startTime;    // beats
    double duration;     // beats
    int color;
};

struct ChainEntrySnapshot {
    std::string regionID;
    int repeatCount;
};

struct ArrangerChainSnapshot {
    std::string chainID;
    std::string name;
    bool isActive;
    std::vector<ChainEntrySnapshot> entries;
};

// New methods on ReadModel:
virtual std::vector<ArrangerRegionSnapshot> getArrangerRegions() const = 0;
virtual std::vector<ArrangerChainSnapshot> getArrangerChains() const = 0;
```

Implemented in `ReadModelImpl.cpp` — walks `ARRANGER_LIST` and `ARRANGER_CHAIN_LIST` children.

### RPC Routes

| Route | Maps to |
|-------|---------|
| `project.addArrangerRegion` | `ProjectCommands::addArrangerRegion` |
| `project.removeArrangerRegion` | `ProjectCommands::removeArrangerRegion` |
| `project.setArrangerRegionName` | `ProjectCommands::setArrangerRegionName` |
| `project.setArrangerRegionBounds` | `ProjectCommands::setArrangerRegionBounds` |
| `project.setArrangerRegionColor` | `ProjectCommands::setArrangerRegionColor` |
| `project.addArrangerChain` | `ProjectCommands::addArrangerChain` |
| `project.removeArrangerChain` | `ProjectCommands::removeArrangerChain` |
| `project.setArrangerChainName` | `ProjectCommands::setArrangerChainName` |
| `project.setArrangerChainActive` | `ProjectCommands::setArrangerChainActive` |
| `project.addChainEntry` | `ProjectCommands::addChainEntry` |
| `project.removeChainEntry` | `ProjectCommands::removeChainEntry` |
| `project.reorderChainEntry` | `ProjectCommands::reorderChainEntry` |
| `project.setChainEntryRepeat` | `ProjectCommands::setChainEntryRepeat` |
| `project.flattenArranger` | `ProjectCommands::flattenArranger` |
| `read.getArrangerRegions` | `ReadModel::getArrangerRegions` |
| `read.getArrangerChains` | `ReadModel::getArrangerChains` |

### Delta Sync

Arranger mutations trigger **fullSync** (same as markers). The `TreeDeltaAccumulator` escalates any non-clip/track node change to fullSync. Frontend re-fetches regions and chains after fullSync.

### Transport State

New properties on the `TRANSPORT` ValueTree node:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `arrangerEnabled` | bool | false | Master toggle — playback follows active chain |
| `arrangerChainPosition` | int | 0 | Current chain entry index |
| `arrangerRepeatIndex` | int | 0 | Current repeat within entry (0-based) |

These are pushed to the frontend via `notify.transport` (existing mechanism).

**Audio-thread access:** `arrangerEnabled`, `arrangerChainPosition`, and `arrangerRepeatIndex` are synced from the ValueTree to `std::atomic` members on the transport object, following the same pattern as `isPlaying`/`position`. The audio thread reads only the atomics; the ValueTree is the source of truth for the message thread and frontend.

---

## Playback Engine

When `arrangerEnabled = false` — behavior unchanged (linear playback).

When `arrangerEnabled = true`:

1. **Play start / seek**: `resolveChainPosition(beat)` scans the active chain, accumulating each entry's time span (`region.duration * repeatCount`), to find which entry contains the given beat.

2. **Within a region**: playback loops the region `repeatCount` times. Each repeat restarts at `region.startTime` on the timeline. At the sample level in `processBlock`, when the playhead crosses `region.startTime + region.duration`, it resets to `region.startTime` and increments `repeatIndex`.

3. **Region boundary**: when all repeats finish, the engine jumps to the next entry's `region.startTime` and resets `repeatIndex` to 0. This happens at the sample level — no message-thread round-trip, no audible gap.

4. **Chain end**: when the last entry's last repeat finishes, playback stops (auto-stop). If `isLooping` is true, the entire chain restarts from entry 0.

5. **Seeking**: clicking on the timeline sets the position normally. The engine resolves which chain entry contains that beat and updates `arrangerChainPosition` / `arrangerRepeatIndex`.

### Resolution Function

```
resolveChainPosition(beat):
    accumulated = 0
    for each entry in activeChain:
        region = findRegion(entry.regionID)
        entryDuration = region.duration * entry.repeatCount
        if beat >= accumulated and beat < accumulated + entryDuration:
            offsetInEntry = beat - accumulated
            repeatIndex = floor(offsetInEntry / region.duration)
            offsetInRepeat = offsetInEntry - (repeatIndex * region.duration)
            return (entryIndex, repeatIndex, region.startTime + offsetInRepeat)
        accumulated += entryDuration
    // beat is past the chain end
    return (lastEntry, lastRepeat, chainEnd)
```

### Constraints

- **Loop region ignored** when arranger mode is active — the chain defines order.
- **Recording disabled** when arranger mode is active — record always goes to linear timeline. The record button is grayed out.
- **Auto-stop** at chain end (last entry's last repeat finishes).

---

## Frontend

### Arranger Lane (Timeline)

**Location:** In `TimelineMinimal.tsx`, between `.tl-ruler` and `.tl-tracks`. Fixed height of 32px, collapsible when no regions exist.

**Structure:**
```
.tl-arranger-lane
    .tl-arranger-region (one per region, absolutely positioned)
        .tl-arranger-region-label (text, truncated)
        .tl-arranger-region-handle-left (resize)
        .tl-arranger-region-handle-right (resize)
```

**Rendering:**
- `left = startTime * pps`, `width = duration * pps`
- Background color from region's `color` property
- Name rendered inside, truncated with ellipsis if too narrow
- Subtle resize handles on left/right edges

**Interactions:**
- **Draw**: Click-drag on empty lane space. Snaps to beat grid. Creates region via `addArrangerRegion`.
- **Select**: Click a region. Highlight border on selection.
- **Move**: Drag region body. Adjacent regions snap to maintain tiling.
- **Resize**: Drag left/right edge. Adjacent region adjusts.
- **Rename**: Double-click name → inline text edit.
- **Context menu**: Right-click → Rename, Delete, Change Color.
- **Delete**: Select + Delete key, or context menu.

**Tiling enforcement:** When dragging a region boundary, the adjacent region's boundary moves to match. Regions always tile with no gaps and no overlaps within the arranger range.

### Chain Editor (Bottom Panel Tab)

**Tab:** "Arranger" in the bottom panel, alongside Mixer, Piano Roll, etc.

**Layout:**
```
┌──────────────────────────────────────────────────────────────────┐
│  Arranger Chain: [Arrangement A ▼]  [+ New Chain] [Delete]     │
├────────────────────────────────┬─────────────────────────────────┤
│      ACTIVE CHAIN              │      AVAILABLE REGIONS          │
│      (left column)             │      (right column)             │
│                                │                                 │
│  1. Intro      [×2]  [×]     │  Intro        0–8               │
│  2. Verse 1    [×1]  [×]     │  Verse 1      8–24              │
│  3. Chorus     [×1]  [×]     │  Chorus       24–40             │
│  4. Verse 2    [×1]  [×]     │  Verse 2      40–56             │
│  5. Chorus     [×2]  [×]     │  Bridge       56–64             │
│  6. Outro      [×1]  [×]     │  Outro        64–80             │
│                                │                                 │
│  [▶ Play Chain]  [Flatten]    │  Drag → to add to chain         │
└────────────────────────────────┴─────────────────────────────────┘
```

**Left column (Active Chain):**
- Ordered list of entries (region name + repeat count)
- Drag to reorder entries
- Click `[×N]` badge to edit repeat count (min 1)
- Click `[×]` to remove entry
- Click entry name → playhead seeks to that region on the timeline
- Context menu: Remove Entry, Set Repeat, Jump To

**Right column (Available Regions):**
- All regions in timeline order with name + beat range
- Drag from right → left to add to chain (appends or inserts at drop position)
- Double-click to append to chain
- Regions can appear multiple times in the chain

**Toolbar:**
- Chain selector dropdown (switch between chains)
- "New Chain" button — creates new chain, duplicates current entries
- Delete Chain button
- "Play Chain" toggle — activates arranger mode (`setArrangerChainActive`)
- "Flatten" button — renders chain into linear timeline

### Zustand Store

New `arrangerStore.ts`:

```typescript
interface ArrangerRegionSnapshot {
    regionID: string;
    name: string;
    startTime: number;
    duration: number;
    color: number;
}

interface ChainEntrySnapshot {
    regionID: string;
    repeatCount: number;
}

interface ArrangerChainSnapshot {
    chainID: string;
    name: string;
    isActive: boolean;
    entries: ChainEntrySnapshot[];
}

interface ArrangerStore {
    regions: ArrangerRegionSnapshot[];
    chains: ArrangerChainSnapshot[];
    syncRegions: (regions: ArrangerRegionSnapshot[]) => void;
    syncChains: (chains: ArrangerChainSnapshot[]) => void;
}
```

Synced on fullSync (alongside markers in `projectStore.ts`).

---

## Flatten

Flatten always operates on the **active chain**. If no chain is active, flatten is a no-op.

### Flatten In-Place

1. Build a temporary clip list in chain playback order:
   - For each chain entry (respecting `repeatCount`):
     - Find all clips overlapping `[region.startTime, region.startTime + region.duration)` on each track
     - For each clip: `outputBeat = currentOutputOffset + (clip.startBeat - region.startTime)`
     - If `repeatCount > 1`, duplicate clip set for each repeat, offset by `region.duration * repeatIndex`
2. Delete all existing clips from all tracks (single operation).
3. Insert computed clips at new positions.
4. Remove arranger data (regions, chains).
5. One `rebuildRoutingGraph()` at the end.

### Flatten to New Project

Same computation, writes to a new `.hdaw` file via `ProjectModel::save()`. Original project untouched.

### Edge Cases

- Empty regions (no clips) → skipped silently.
- Overlapping clips within a region → handled by normal clip overlap logic.
- MIDI notes, CC, gain envelope, automation → carried with clips automatically.

---

## Save/Load

- `ARRANGER_LIST` and `ARRANGER_CHAIN_LIST` serialize automatically with the root ValueTree.
- No format version bump — older versions ignore unknown child nodes.
- On load, if no arranger data exists, the feature is dormant.

---

## MCP Tools

All arranger operations exposed as MCP tools in the `project` namespace:

| Tool | Maps to |
|------|---------|
| `project.addArrangerRegion` | `addArrangerRegion` |
| `project.removeArrangerRegion` | `removeArrangerRegion` |
| `project.setArrangerRegionName` | `setArrangerRegionName` |
| `project.setArrangerRegionBounds` | `setArrangerRegionBounds` |
| `project.setArrangerRegionColor` | `setArrangerRegionColor` |
| `project.addArrangerChain` | `addArrangerChain` |
| `project.removeArrangerChain` | `removeArrangerChain` |
| `project.setArrangerChainName` | `setArrangerChainName` |
| `project.setArrangerChainActive` | `setArrangerChainActive` |
| `project.addChainEntry` | `addChainEntry` |
| `project.removeChainEntry` | `removeChainEntry` |
| `project.reorderChainEntry` | `reorderChainEntry` |
| `project.setChainEntryRepeat` | `setChainEntryRepeat` |
| `project.flattenArranger` | `flattenArranger` |
| `read.getArrangerRegions` | `getArrangerRegions` |
| `read.getArrangerChains` | `getArrangerChains` |

---

## Testing

### C++ gtest

| Test | Coverage |
|------|----------|
| `ArrangerRegion.CRUD` | Add, remove, rename, recolor, reposition regions |
| `ArrangerRegion.CascadeRemove` | Removing a region removes all chain entries referencing it |
| `ArrangerRegion.NonOverlapping` | Regions at specified positions (engine accepts any values) |
| `ArrangerChain.CRUD` | Add, remove, rename chains |
| `ArrangerChain.SingleActive` | Only one chain active; activating deactivates others |
| `ArrangerChainEntry.CRUD` | Add, remove, reorder, set repeat |
| `ArrangerChainEntry.RepeatMin` | Repeat count minimum is 1 |
| `ArrangerPlayback.ResolveChainPosition` | Unit tests for the resolution function |
| `ArrangerPlayback.RegionLooping` | Playhead loops within region for repeatCount > 1 |
| `ArrangerPlayback.ChainAdvance` | Playhead jumps to next region on boundary |
| `ArrangerPlayback.ChainEnd` | Auto-stop at chain end |
| `ArrangerPlayback.ChainLoop` | Entire chain loops when isLooping enabled |
| `ArrangerPlayback.SeekInRegion` | Seeking resolves correct chain entry |
| `ArrangerPlayback.RecordingDisabled` | Arranger mode disabled during recording |
| `ArrangerFlatten.InPlace` | Clips end up at correct positions after flatten |
| `ArrangerFlatten.WithRepeats` | Repeated entries produce correctly offset clip copies |
| `ArrangerFlatten.EmptyRegion` | Empty regions produce no clips |
| `ArrangerSaveLoad.RoundTrip` | Regions and chains survive save/load |
| `ArrangerUndo.FullRoundTrip` | All operations undo/redo correctly |

### Vitest

| Test | Coverage |
|------|----------|
| `arrangerStore.syncRegions` | Region state synced correctly |
| `arrangerStore.syncChains` | Chain state synced correctly |

### Playwright E2E

| Test | Coverage |
|------|----------|
| `Draw region on timeline` | Click-drag creates region with correct bounds |
| `Rename region` | Double-click inline edit |
| `Move region` | Drag preserves tiling |
| `Resize region` | Edge drag adjusts adjacent region |
| `Delete region` | Removes from timeline and chain |
| `Chain editor: add entry` | Drag/double-click adds region to chain |
| `Chain editor: reorder` | Drag reorders entries |
| `Chain editor: set repeat` | Click badge edits repeat count |
| `Chain editor: switch chains` | Dropdown switches, entries update |
| `Chain editor: new chain` | Creates duplicate of current |
| `Arranger playback` | Playhead follows chain order |
| `Flatten` | Clips rearranged correctly |

---

## Integration Notes

- **Generative composition**: `ArrangementGenerator` sections could optionally mint arranger regions as a starting point (future enhancement, not v1).
- **Markers**: Arranger regions coexist with point markers. Both visible on timeline (markers as triangle pins, regions as colored bands).
- **Region operations**: Existing `rippleDelete`/`insertSilence`/`duplicateRegion` operate on beat ranges independently of arranger regions. No conflict.
- **Loop region**: Ignored when arranger mode is active.
- **Recording**: Disabled when arranger mode is active.
