# Incremental Tree Sync — Design

**Date:** 2026-07-24
**Status:** Approved (brainstorming complete, ready for implementation plan)
**Goal:** Eliminate the "lag before the UI reflects an edit" by replacing the
whole-project snapshot re-fetch with a small pushed **delta**, while keeping a
full-sync fallback for safety.

---

## Problem

Every edit today follows this path:

1. Frontend RPC mutates the C++ `ValueTree`.
2. `FrontendTreeWatcher` fires, debounces 16ms, broadcasts a **bare** `notify.treeChanged`.
3. Frontend responds with a second RPC, `read.snapshot`, which walks the **entire**
   project tree and serializes **every track + every clip** to JSON.
4. Frontend parses the whole payload and **replaces the entire snapshot** in Zustand.
5. React re-renders.

Every edit is therefore O(whole project): two RPC round-trips, a full C++
serialize, a whole-project transmit/parse, and a full state replace — regardless
of how tiny the edit was. The user experiences this as a visible lag before the
timeline catches up.

## Solution overview

`FrontendTreeWatcher` already receives *granular* JUCE `ValueTree::Listener`
callbacks. Instead of debouncing to a bare notification, it **classifies and
coalesces** each change over the existing 16ms window and ships a small **delta**
describing only what changed. The frontend **patches its existing snapshot in
place**. A **full-sync fallback** keeps it safe.

The delta is **pushed** (the notification carries the data), so the second RPC is
eliminated entirely. New latency profile:

```
mutation RPC -> 16ms debounce -> push (tiny delta) -> in-place patch -> render
```

---

## Delta payload

Reuses `notify.treeChanged`, now carrying data:

```jsonc
// incremental case
{ "fullSync": false,
  "clipsUpserted":  [ /* full ClipSnapshot for each new/changed clip */ ],
  "clipsRemoved":   [ /* clipIds */ ],
  "tracksUpserted": [ /* full TrackSnapshot for each changed track */ ] }

// fallback case (structural / complex / sub-clip-detail change)
{ "fullSync": true }
```

Frontend TypeScript type (`frontend/src/rpc/types.ts`):

```typescript
interface TreeDelta {
  fullSync: boolean;
  clipsUpserted?: ClipSnapshot[];
  clipsRemoved?: number[];
  tracksUpserted?: TrackSnapshot[];
}
```

---

## C++ side — `FrontendTreeWatcher`

### Accumulated state (reset after each flush)

```cpp
std::unordered_map<int, ClipSnapshot>  clipsUpserted_;   // clipId -> latest snapshot
std::set<int>                          clipsRemoved_;
std::unordered_map<int, TrackSnapshot> tracksUpserted_;  // trackIndex -> latest snapshot
bool fullSync_ = false;
```

### Classification rules

| Callback | Node type | Action |
|----------|-----------|--------|
| `valueTreePropertyChanged` | CLIP | upsert that clip |
| `valueTreePropertyChanged` | TRACK | upsert that track |
| `valueTreePropertyChanged` | NOTE / CC / gain-env point / automation point | `fullSync` (see note below) |
| `valueTreePropertyChanged` | anything else (PROJECT, MARKER, FX, automation lane, modulation…) | `fullSync` |
| `valueTreeChildAdded` | CLIP added to a CLIP_LIST | upsert that clip |
| `valueTreeChildRemoved` | CLIP removed | record `clipsRemoved_` |
| `valueTreeChildAdded/Removed` | TRACK | `fullSync` (track indices shift) |
| `valueTreeChildAdded/Removed` | NOTE / CC / envelope / automation point | `fullSync` (see note below) |
| `valueTreeChildAdded/Removed` | anything else | `fullSync` |
| `valueTreeChildOrderChanged` / `valueTreeParentChanged` | — | `fullSync` |

**Sub-clip-detail note.** The project snapshot never included notes / CC /
gain-envelope / automation points (those are fetched on demand via
`getNotes`/`getCcPoints`/`getClipGainEnvelope`/`getAutomationPoints`). In principle
those changes could be *ignored* by the watcher. However, **automation recording
writes points continuously and currently relies on the `treeChanged`-driven
automation re-fetch**, so ignoring them would silently break lane refresh. To avoid
any regression, sub-clip-detail changes are treated as `fullSync` in this pass —
identical to today's behavior. Ignoring them (a further optimization) is a safe
follow-up once each editor is verified to refresh independently.

### Coalescing invariants

- Upserting a clip removes it from `clipsRemoved_` (re-added cancels the removal);
  removing a clip drops any pending upsert for that id.
- Upserts keep the **latest** snapshot per id — continuous drags collapse to one entry.
- If `fullSync_` is set at any point during the window, the flush sends
  `{ "fullSync": true }` and discards the partial delta.

### Snapshot builders

Extract the inline per-node code from `ReadModelImpl::snapshot()` into two reusable
helpers so there is a single source of truth:

- `buildClipSnapshot(const ValueTree& clipTree)` — reads all clip properties; walks
  up to the parent TRACK (`clipTree.getParent().getParent()`) and finds its index in
  TRACK_LIST to populate `trackIndex`.
- `buildTrackSnapshot(const ValueTree& trackTree)` — reads all track properties;
  `index = trackTree.getParent().indexOf(trackTree)`.

Both `ReadModelImpl::snapshot()` and the watcher use these helpers. JSON
serialization reuses the existing `toJson(ClipSnapshot)` / `toJson(TrackSnapshot)`
in `FrontendRpc.h`.

### Flush (debounce timeout)

- If `fullSync_` → broadcast `{ "fullSync": true }`.
- Else → build the delta `QJsonObject` from the maps (`fullSync:false`,
  `clipsUpserted` array, `clipsRemoved` array, `tracksUpserted` array) and broadcast.
- Reset all accumulated state.

---

## Frontend side

### `main.tsx` handler

Delta if a usable delta is present; otherwise the existing full-sync path. The
automation re-fetch stays on the full-sync branch, so automation recording still
refreshes.

```typescript
cleanups.push(rpc.onNotification("notify.treeChanged", (_, params) => {
  const d = params as TreeDelta | undefined;
  if (d && !d.fullSync && (d.clipsUpserted || d.clipsRemoved || d.tracksUpserted)) {
    useProjectStore.getState().applyDelta(d);
  } else {
    useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
    const t = useAutomationStore.getState().activeTrackIndex;
    if (t !== null) useAutomationStore.getState().fetchForTrack(t, rpc);
  }
}));
```

### `projectStore.applyDelta`

Patches the snapshot in place. Unchanged entities keep their object references
(the `Map` preserves them), which enables a later `React.memo` on the clip
component to skip unchanged clips for free.

```typescript
applyDelta: (d: TreeDelta) => {
  const snap = get().snapshot;
  if (!snap) return;

  let clips = snap.clips;
  if (d.clipsRemoved?.length) {
    const rm = new Set(d.clipsRemoved);
    clips = clips.filter((c) => !rm.has(c.clipId));
  }
  if (d.clipsUpserted?.length) {
    const byId = new Map(clips.map((c) => [c.clipId, c] as const));
    for (const c of d.clipsUpserted) byId.set(c.clipId, c);
    clips = [...byId.values()];
  }

  let tracks = snap.tracks;
  if (d.tracksUpserted?.length) {
    const byIdx = new Map(tracks.map((t) => [t.index, t] as const));
    for (const t of d.tracksUpserted) byIdx.set(t.index, t);
    tracks = [...byIdx.values()].sort((a, b) => a.index - b.index);
  }

  set({ snapshot: { ...snap, clips, tracks }, lastSync: Date.now() });
}
```

---

## Phase 2 — Pending placeholders (Duplicate / Add Track / Add Clip)

ID-minting ops can't show the real clip until the backend returns the id. With the
delta in place this round-trip is already short (~16–30ms), so this is polish added
**after** the delta sync is verified.

Store additions:

```typescript
pendingTempIds: Set<number>;            // negative temp clipIds shown as placeholders
pendingResolution: Map<number, number>; // tempId -> real clipId (from the RPC response)
```

Flow (Duplicate of clips [A, B]):

1. Create placeholders A′, B′ — copies of the sources with **negative temp ids**
   (-1, -2), rendered translucent (`.tl-clip--pending`). Add to `snapshot.clips`.
2. Fire `duplicateClips({ clipIds: [A, B] })`.
3. RPC response returns `newIds = [42, 43]` **in input order** → record
   `pendingResolution: -1→42, -2→43`.
4. The delta arrives with `clipsUpserted: [clip42, clip43]`. `applyDelta` upserts
   them and, for each upserted clip whose id matches a `pendingResolution` value,
   removes the corresponding placeholder. Ghost swaps to real clip.

Add Track / Add Clip follow the same pattern (one placeholder, one returned id).

Edge cases:
- **RPC fails** → catch handler removes the placeholder(s).
- **Never resolved** (overlap-resolution deleted the new clip, or delta lost) → a
  short timeout sweeps unresolved placeholders; the next full sync corrects state.
- **Ordering** → relies on the documented guarantee that `duplicateClips` returns
  ids in input-clip order (already depended on by `useTimelineDrag`).

Visual: a clip whose id is in `pendingTempIds` gets `.tl-clip--pending`
(translucent, non-interactive — distinct class from the real ghost-clip feature).

---

## Safety & fallback

- **Conservative classification** — anything uncertain sets `fullSync`; the existing
  full-`syncSnapshot` path stays intact. Worst case behaves exactly like today.
- **Coalescing invariants** — removed-then-readded cancels; upserts keep latest per
  id; a `fullSync` flag anywhere in the window discards the partial delta.
- **Undo/redo & load** — touch many/structural nodes, so the watcher naturally routes
  them to `fullSync`. No special-casing needed.
- **Optional kill-switch** — a simple flag (env var or debug RPC) forcing `fullSync`
  for every change, so the delta path can be disabled in the field if a drift bug
  surfaces. Decide in the plan.
- **Drift** — no periodic resync initially (conservative classifier makes drift
  unlikely). If observed, a focus/transaction-end resync is the cheap follow-up.

---

## Testing

**C++ (gtest):**
- Watcher classification + coalescing: a sequence of ValueTree mutations → expected
  delta payload.
- `buildClipSnapshot` / `buildTrackSnapshot` correctness.
- Coalescing cases: multi-move → one upsert; remove + re-add → cancel; structural
  change → `fullSync`.

**Frontend (Vitest):**
- `applyDelta`: upsert-new, upsert-replace, remove, track upsert + sort, and that
  unchanged clips keep their object references.
- Delta-vs-fullSync routing in the `main.tsx` handler (via a store-level test).

**Regression guard:**
- Existing `useTimelineDrag` / store tests stay green.
- Manual smoke: move / delete / duplicate clips — confirm correct *and* fast.
- Confirm piano-roll, automation, and gain-envelope editing still refresh (sub-clip
  `fullSync` keeps them working), and undo/redo behaves.

---

## Phasing

- **Phase 1 — Delta sync (the latency win):** C++ watcher classification + coalescing
  + snapshot builders + serialization; frontend `TreeDelta` type, `applyDelta`,
  handler routing; tests.
- **Phase 2 — Pending placeholders:** translucent copy for Duplicate/Add, RPC-id
  reconciliation, timeout sweep.

## Files touched (anticipated)

| File | Change |
|------|--------|
| `src/frontend/FrontendTreeWatcher.h/.cpp` | Accumulate + classify + coalesce + flush delta |
| `src/engine/ReadModelImpl.h/.cpp` | Extract `buildClipSnapshot` / `buildTrackSnapshot` helpers |
| `src/frontend/FrontendRpc.h` | Delta JSON builder (reuse `toJson`) |
| `frontend/src/rpc/types.ts` | `TreeDelta` interface |
| `frontend/src/store/projectStore.ts` | `applyDelta` action (+ phase-2 pending state) |
| `frontend/src/main.tsx` | Delta-vs-fullSync routing in `treeChanged` handler |
| `frontend/src/components/TimelineMinimal.tsx` | Phase-2 `.tl-clip--pending` rendering |
| `frontend/src/components/TimelineMinimal.css` | Phase-2 `.tl-clip--pending` style |
| `tests/unit/...` | gtest for watcher/builders |
| `frontend/src/store/projectStore.test.ts` | `applyDelta` tests |
