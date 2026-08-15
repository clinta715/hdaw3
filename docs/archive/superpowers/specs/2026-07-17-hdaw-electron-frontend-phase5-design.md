# HDAW Electron/React Frontend — Phase 5 Design

## Status

Approved. Adds a full timeline with playhead cursor, time ruler, loop
region markers, and horizontal zoom to the existing TimelineMinimal
component.

## Features

| Feature | Source | Update Rate |
|---------|--------|-------------|
| Playhead cursor | `transportStore.currentTimeSeconds` | 30 Hz (`notify.transport`) |
| Time ruler | `PIXELS_PER_BEAT * beatIndex` | On zoom / scroll |
| Loop region | `transportStore.loopStart/End, isLooping` | On user drag / mutation |
| Horizontal zoom | Ctrl+Wheel or +/- buttons | Interactive |

## Architecture

The timeline layout changes from a flat `.tl-tracks` div to a stacked
container:

```
.timeline-wrapper
├── .tl-toolbar
│   ├── zoom buttons (+ / - / fit)
│   └── current zoom level label
├── .tl-body               (overflow hidden, position: relative)
│   ├── .tl-ruler           (height 24px, scrollLeft synced)
│   │   ├── beat/bar markers (positioned via PIXELS_PER_BEAT)
│   │   ├── loop-start marker (draggable)
│   │   ├── loop-end marker (draggable)
│   │   └── loop-highlight (colored band between start/end)
│   ├── .tl-playhead        (2px vertical line, z-index: 100)
│   └── .tl-tracks          (overflow: auto, onScroll sync to ruler)
│       ├── track rows (existing)
│       └── clip divs (existing)
└── .tl-scrollbar           (the native scrollbar of tl-tracks)
```

### Playhead

A 2px-wide accent-colored vertical line spanning the full height of the
track area. Position: `left = beats * PIXELS_PER_BEAT` where
`beats = currentTimeSeconds * (bpm / 60)`. Updated via Zustand selector
at 30 Hz — React re-renders just the playhead's `style.left`.

### Time Ruler

A 24px-tall bar showing:
- **Bar lines** (every 4 beats) — thicker, labeled "1", "2", "3"...
- **Beat lines** — thinner, unlabeled
- Label font: 9px, `textSecondary` color, positioned near each bar line

### Loop Region

When `transport.isLooping` is true:
- A semi-transparent blue/purple band between `loopStart` and `loopEnd`
- Start/end handlebars on the ruler (3px wide, draggable, cursor: ew-resize)
- Drag commits to `transport.setLoopStart`/`transport.setLoopEnd` RPC
- Phase 2 `notify.treeChanged` handles the state refresh

### Horizontal Zoom

`PIXELS_PER_BEAT` is a state variable in `TimelineMinimal`:
- Default: `40`
- Range: `[10, 200]`
- Ctrl+MouseWheel on `.tl-body`: delta → `PIXELS_PER_BEAT *= 1.1` or `/= 1.1`
- Toolbar buttons: `+` (zoom in), `-` (zoom out), `fit` (zoom to show all clips)

## Files Changed

| File | Change |
|------|--------|
| `frontend/src/components/TimelineMinimal.tsx` | Playhead, ruler, loop, zoom, scroll sync, restructured layout |
| `frontend/src/components/TimelineMinimal.css` | Ruler, playhead, loop, toolbar, scroll sync styles |
