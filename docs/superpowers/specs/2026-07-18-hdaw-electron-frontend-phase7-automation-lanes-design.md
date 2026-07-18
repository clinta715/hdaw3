# HDAW Electron/React Frontend — Phase 7 Design: Automation Lanes

## Status

Approved. Adds a tabbed bottom panel and an automation lane editor that
displays and edits track automation (volume, pan, mute, plugin params)
with a canvas-based point editor.

## Architecture

The bottom panel switches from a flat side-by-side layout to a tabbed
layout. Three tabs: Mixer, Piano Roll, Automation. FX Chain deferred.

```
App
└── .bottom-panel
    ├── .bp-tab-bar
    │   ├── Mixer tab button
    │   ├── Piano Roll tab button
    │   └── Automation tab button
    └── .bp-content
        ├── Mixer     (existing, wrapped in tab panel)
        ├── Piano Roll (existing, wrapped in tab panel)
        └── AutomationPanel (new)
```

## AutomationPanel

Reads `selectedClipId` from `useUiStore`, resolves the parent track,
and shows all automation lanes for that track.

```
.AutomationPanel
├── .ap-header
│   ├── "Track: {name}" label
│   └── + Add Lane button → dropdown menu of automatable params
├── .ap-lanes (scrollable)
│   └── .ap-lane[]  (one per automation lane)
│       ├── .ap-lane-header
│       │   ├── .ap-lane-name  (e.g. "Volume")
│       │   ├── .ap-lane-enabled  [toggle switch]
│       │   └── .ap-lane-remove  [× button]
│       └── AutomationLaneCanvas  (canvas, fixed height 80px)
```

When no clip is selected: show "Select a clip to view automation."

### Canvas Interaction

Each `AutomationLaneCanvas`:

- **X axis**: time in beats, range `[0, viewRange]` (default 32 beats = 8 bars)
- **Y axis**: normalized value `0` (bottom) to `1` (top)
- **Grid**: bar lines every 4 beats; value lines at 0.0, 0.25, 0.5, 0.75, 1.0
- **Curve**: polyline connecting sorted points
- **Points**: 6px circles (white fill, accent border)
- **Playhead**: 1px vertical line at current beat position (read-only visual, not interactive)
- **Loop region**: semi-transparent band if looping is active (read-only visual, not interactive)

| Action | Result |
|--------|--------|
| Click empty area | `project.addAutomationPoint` at (time, value) |
| Drag existing point | Update visual during drag; `project.setAutomationPointValue` on mouseup |
| Double-click point | `project.removeAutomationPoint` at that time |
| Toggle enable checkbox | `project.setAutomationEnabled` |
| Click × remove lane | `project.removeAutomationLane` (no confirmation needed) |
| Click + Add Lane | `read.getAutomatableParams` → dropdown; selecting calls `project.addAutomationLane` |

### Data Flow

```
Track selection resolves from clip selection:
  `trackIndex = snapshot.clips.find(c → c.clipId === selectedClipId)?.trackIndex`
  → automationStore.fetchForTrack(trackIndex, rpc)
  → read.getAutomationLanes(trackIndex) → lane list
  → read.getAutomationPoints(trackIndex, laneName) → per lane

When selectedClipId is null: show "Select a clip to view automation."

Mutation (add/move/delete point, toggle lane)
  → project.* RPC call
  → engine broadcasts notify.treeChanged
  → automationStore re-fetches for the active track
  → re-render

on mount + on notify.treeChanged:
  if activeTrackIndex != null → automationStore.fetchForTrack(...)
```

### View Range

Default: 32 beats (8 bars). The user can scroll horizontally within the
canvas if points extend beyond 32 beats. The view range is a fixed
constant for Phase 7; zoom/scroll control deferred.

## New Files

| File | Purpose |
|------|---------|
| `frontend/src/store/automationStore.ts` | Zustand store: lanes, points, activeTrackIndex, fetch/refresh |
| `frontend/src/components/BottomTabs.tsx` | Tab bar + content switching |
| `frontend/src/components/BottomTabs.css` | Tab bar styles |
| `frontend/src/components/AutomationPanel.tsx` | Main automation panel (header, lane list, add button) |
| `frontend/src/components/AutomationPanel.css` | Automation panel styles |
| `frontend/src/components/AutomationLaneCanvas.tsx` | Canvas per lane: render curve/points/grid/playhead, mouse handlers |
| `frontend/src/components/AutomationLaneCanvas.css` | Canvas wrapper styles |

## Modified Files

| File | Change |
|------|--------|
| `frontend/src/App.tsx` | Replace `.bottom-panel` flat sections with BottomTabs + content |
| `frontend/src/App.css` | Bottom panel layout changes for tabbed design |
| `frontend/src/rpc/types.ts` | Add AutomationLaneSnapshot, AutomationPointSnapshot, AutomatableParamSnapshot types |

## Out of Scope (Phase 7)

- Zoom/scroll controls on automation canvas
- FX chain tab (separate phase)
- Plugin param automation (lane creation from `getAutomatableParams` is included, but full FX chain tab is deferred)
- Multi-lane point selection / bulk edit
- Automation recording (arm lane, write during playback)
- LFO/modulation lanes
