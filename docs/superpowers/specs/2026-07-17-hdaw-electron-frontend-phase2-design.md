# HDAW Electron/React Frontend — Phase 2 Design

## Status

Approved after Phase 1 completion. Replaces the polling loop with push
notifications for transport and tree-changed events.

## Problem

Phase 1 uses a 500ms `setTimeout` poll loop that calls `read.snapshot`
and `read.transport` every half-second. This is wasteful (two RPCs per
tick) and laggy — transport timecode updates at 2 Hz instead of the
engine's 30 Hz push cadence.

## Solution

Subscribe to three engine push notifications and eliminate the poll loop:

| Notification | Frequency | Payload | Client Action |
|---|---|---|---|
| `notify.transport` | 30 Hz | Full `TransportSnapshot` | `transportStore.update(payload)` |
| `notify.meters` | 30 Hz | `{master, tracks[]}` | `meterStore.update(payload)` |
| `notify.treeChanged` | Debounced 16ms | `{}` (bare signal) | `projectStore.syncSnapshot(rpc)` |

## Architecture Change

**Before (Phase 1):**
```
init → connect → [poll every 500ms: read.snapshot + read.transport]
               → onMessage("notify.meters") → meterStore
```

**After (Phase 2):**
```
init → connect → subscribe("notify.transport") → transportStore
               → subscribe("notify.treeChanged") → syncSnapshot(rpc)
               → subscribe("notify.meters") → meterStore
               → [initial syncSnapshot once]
```

## Reconnection

When the WebSocket reconnects (engine crash/restart), subscriptions
are re-established via `onopen`. The `main.tsx` connect function
re-runs the subscription wiring on every new `onopen`.

## Exit Criteria

1. Transport timecode updates at 30 Hz (smooth) instead of 2 Hz (janky)
2. Clip/track/mixer edits update within ~50ms (treeChanged debounce)
3. No periodic `read.transport` or `read.snapshot` calls in the poll loop
4. All three subscriptions survive engine restart (auto-reconnect path)

## Files Changed

| File | Change |
|------|--------|
| `frontend/src/main.tsx` | Remove poll loop, add notify.transport + notify.treeChanged subscriptions, re-wire on reconnect |
| `frontend/src/store/transportStore.ts` | No change (already has `update()`) |
| `frontend/src/store/projectStore.ts` | No change (already has `syncSnapshot()`) |
