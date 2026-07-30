# Typed Tracks Design Spec

**Date:** 2026-07-27
**Phase:** 1 — Arrangement & Timeline
**Scope:** Audio, Instrument, Folder track types

## Summary

Add a `trackType` property to tracks, enabling visual differentiation between
audio/instrument tracks and introducing folder tracks for organization. Folder
tracks provide collapse/expand and mute/solo cascade behavior. This is the
foundation for Phase 4 (instruments) and future bus/group track types.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Track types | audio, instrument, folder | Core three; bus/group deferred |
| Folder hierarchy | `childIds` array on folder | Fits flat TRACK_LIST model |
| Mute/solo cascade | Backend effective-state via ReadModel | Authoritative, clean undo |
| Existing tracks | Default to `"audio"` | Simplest migration |

## ValueTree Schema

### New properties on every TRACK node

```
trackType: "audio" | "instrument" | "folder"
```

### New properties on folder tracks only

```
childIds: "3,7,12"    // comma-separated track indices (string for ValueTree)
isCollapsed: false     // folder expand/collapse state
```

### New properties on child tracks (non-folder tracks inside a folder)

```
parentId: 5            // track index of the parent folder (-1 or absent if not in a folder)
```

Note: All indices are track positions in the TRACK_LIST, consistent with
existing `parentBus` indexing. Indices must be updated when tracks are
reordered.

### Defaults

- Existing tracks: `trackType = "audio"`, no `childIds`
- New tracks: `trackType` from RPC parameter, defaults to `"audio"`
- `isCollapsed`: defaults to `false`

### Migration

On project load, if any track lacks `trackType`, inject `"audio"` as default.
No user prompt. Backward compatible with all existing projects.

## ReadModel — Effective State

`TrackSnapshot` gains two new computed fields:

```cpp
struct TrackSnapshot {
    // ... existing fields ...
    std::string trackType;       // "audio" | "instrument" | "folder"
    bool isCollapsed;            // folder only
    bool effectiveMuted;         // true if this track OR any ancestor folder is muted
    bool effectiveSoloed;        // true if this track OR any ancestor folder is soloed
};
```

### Computation logic

1. Build a parentId -> trackIndex lookup map from all tracks' `childIds`.
2. For each track, walk up the parent chain checking `isMuted` / `isSoloed`.
3. `effectiveMuted = isMuted || parent.isMuted || grandparent.isMuted ...`
4. `effectiveSoloed = isSoloed || parent.isSoloed || grandparent.isSoloed ...`

Frontend reads `effectiveMuted` / `effectiveSoloed` for meter silence and clip
dimming. No cascade logic in React.

## Audio Graph — Folder Tracks

Folder tracks are **visual-only** — they don't participate in audio routing.

- `AudioProcessorGraph` building **skips** folder tracks — no node created,
  no connections.
- Existing `parentBus` routing on child tracks continues to work independently.
  Folder hierarchy is purely for UI organization and mute/solo cascade.
- `RoutingManager` gets a helper: `bool isFolderTrack(int index)` that checks
  `trackType == "folder"`.

Zero impact on audio latency, CPU, or signal flow.

## Frontend — TrackHeaders & Timeline

### TrackHeaders.tsx

- Each track row shows a **type indicator**: icon or colored badge (waveform
  for audio, keyboard for instrument, folder icon for folder).
- Folder tracks get a **collapse/expand chevron** on the left. Clicking it
  toggles `isCollapsed`.
- Child tracks of a collapsed folder are **hidden** from the track list
  (filtered out in render).
- Child tracks are **indented** visually (left padding) when folder is expanded.
- Mute/Solo buttons on folder tracks cascade: clicking Mute on a folder calls
  `setTrackMuted(folderIndex, true)` on the backend, which propagates
  `effectiveMuted` to children via ReadModel.

### TimelineMinimal.tsx

- Collapsed folder children are **hidden** from the timeline (same filter).
- Folder track rows get a subtle background color to distinguish them from
  content tracks.
- Child tracks are indented to match TrackHeaders.

### MixerStrip.tsx

- Folder tracks show in mixer with collapsed state.
- Meters on child tracks reflect `effectiveMuted` (silenced when parent folder
  is muted).

## RPCs and API

### Modified RPCs

- `project.addTrack(name, color, parentBus, trackType)` — new optional
  `trackType` param, defaults to `"audio"`.
- `project.duplicateTrack(trackIndex)` — copies `trackType`, `childIds`,
  `isCollapsed`.

### New RPCs

- `project.setTrackType(trackIndex, type)` — change a track's type
  (audio <-> instrument <-> folder).
- `project.setTrackCollapsed(trackIndex, collapsed)` — toggle folder collapse
  state.
- `project.moveTrackIntoFolder(trackIndex, folderIndex)` — add track to
  folder's `childIds`, set child's `parentId`.
- `project.moveTrackOutOfFolder(trackIndex)` — remove from parent folder's
  `childIds`, clear `parentId`.

### MCP tools

`add_track` and `duplicate_track` get optional `trackType` parameter.

## Undo/Redo and Delta Sync

### Undo

Each folder operation (move into/out, collapse, type change) is a single
undoable command. Moving a track into a folder is one undo step — it sets
`parentId` on the child and appends to the folder's `childIds` atomically.

### Delta sync

The `TreeDeltaAccumulator` already propagates `PropertyChanged` for any
ValueTree property. Adding `trackType`, `childIds`, `isCollapsed`, `parentId`
as properties means they auto-delta — no special wiring needed. A folder
collapse triggers a `PropertyChanged` on `isCollapsed`, which the frontend
patches in-place.

### Full sync fallback

Track add/remove already triggers full sync. Folder structural changes (move
in/out) also trigger full sync since they affect multiple tracks' `parentId`/
`childIds` — safer than trying to delta partial parent-child state.

## Files to Modify

### C++ Layer

| File | Change |
|------|--------|
| `src/model/ProjectModel.h` | Add `DECLARE_ID(trackType)`, `DECLARE_ID(childIds)`, `DECLARE_ID(parentId)`, `DECLARE_ID(isCollapsed)` |
| `src/model/ProjectModel.cpp` | Set `trackType` in `createDefaultProject()` |
| `src/engine/AudioEngineCommands.cpp` | Set `trackType` in `createTrackValueTree()` |
| `src/common/ReadModel.h` | Add `trackType`, `isCollapsed`, `effectiveMuted`, `effectiveSoloed` to `TrackSnapshot` |
| `src/engine/ReadModelImpl.cpp` | Read new properties, compute effective state |
| `src/common/ProjectCommands.h` | Add `setTrackType()`, `setTrackCollapsed()`, `moveTrackIntoFolder()`, `moveTrackOutOfFolder()` |
| `src/engine/AudioEngineCommands.h/.cpp` | Implement new commands |
| `src/engine/RoutingManager.h/.cpp` | Add `isFolderTrack()` helper, skip folders in graph build |
| `src/frontend/FrontendRouter.cpp` | Pass `trackType` in `addTrack`, register new RPC handlers |
| `src/mcp/McpTools_Project.cpp` | Add `trackType` param to MCP tools |

### Frontend Layer

| File | Change |
|------|--------|
| `frontend/src/rpc/types.ts` | Add `trackType`, `isCollapsed`, `effectiveMuted`, `effectiveSoloed` to `TrackSnapshot` |
| `frontend/src/store/projectStore.ts` | Folder-aware track filtering for collapsed children |
| `frontend/src/components/TrackHeaders.tsx` | Type indicator, folder chevron, indent, collapse/expand |
| `frontend/src/components/TrackHeaders.css` | Styles for folder rows, type badges, indentation |
| `frontend/src/components/TimelineMinimal.tsx` | Hide collapsed children, folder row styling, indent |
| `frontend/src/components/MixerStrip.tsx` | Folder state display, effective mute on meters |
| `frontend/src/rpc/rpc.ts` | Add `setTrackType`, `setTrackCollapsed`, `moveTrackIntoFolder`, `moveTrackOutOfFolder` wrappers |

### Tests

| File | Change |
|------|--------|
| `tests/` (gtest) | Add tests for typed track creation, folder hierarchy, effective state computation, undo |
| `frontend/src/store/*.test.ts` | Add tests for folder-aware filtering |
| `e2e/*.spec.ts` | Add E2E tests for folder create, collapse, mute cascade |
