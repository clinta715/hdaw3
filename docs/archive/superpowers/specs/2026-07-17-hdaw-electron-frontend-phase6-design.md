# HDAW Electron/React Frontend — Phase 6 Design

## Status

Approved. Adds a per-clip audio editor panel that appears when a clip is
selected in the timeline. Provides gain, fades, looping, timestretch, and
gain envelope editing.

## Architecture

A Zustand `uiStore` holds `selectedClipId` (set by TimelineMinimal on
click). The ClipEditor component reads the clip from the project store
and renders controls in a panel below the timeline.

```
App
├── timeline area
│   └── TimelineMinimal → on clip click → uiStore.selectClip(id)
├── ClipEditor (shown when selectedClipId != null)
│   ├── Clip info bar (name, type, position)
│   ├── Gain slider
│   ├── Fade in/out sliders
│   ├── Looping toggle
│   ├── Timestretch section (source BPM, mode dropdown, ratio)
│   └── GainEnvelopeEditor (canvas)
└── bottom panel (mixer + piano roll)
```

## RPC Methods Used

| Method | Params | Purpose |
|--------|--------|---------|
| `project.setClipGain` | `{ clipId, gain }` | Set clip gain |
| `project.setClipFadeIn` | `{ clipId, fadeIn }` | Set fade-in duration |
| `project.setClipFadeOut` | `{ clipId, fadeOut }` | Set fade-out duration |
| `project.setClipLooping` | `{ clipId, looping }` | Toggle looping |
| `project.setClipSourceBpm` | `{ clipId, bpm }` | Set source BPM |
| `project.setClipStretchMode` | `{ clipId, mode }` | 0=Off, 1=TempoMatch, 2=Manual |
| `project.setClipStretchRatio` | `{ clipId, ratio }` | Set stretch ratio |
| `read.getClipGainEnvelope` | `{ clipId }` | Get gain envelope points |
| `project.addGainEnvelopePoint` | `{ clipId, time, gain }` | Add point |
| `project.moveGainEnvelopePoint` | `{ clipId, pointIndex, time, gain }` | Move point |
| `project.removeGainEnvelopePoint` | `{ clipId, pointIndex }` | Delete point |

## GainEnvelopeEditor

A `<canvas>`-based editor showing gain over time (normalized 0–2x).
Click to add points, drag existing points to move, double-click to
delete. The canvas height is a fixed 80px, width matches the clip
duration in beats × 40px. Points are stored as `{ time, gain }`.

## Files

| File | Change |
|------|--------|
| `frontend/src/store/uiStore.ts` | New — selectedClipId state + select/deselect actions |
| `frontend/src/components/ClipEditor.tsx` | New — property panel |
| `frontend/src/components/ClipEditor.css` | New |
| `frontend/src/components/GainEnvelopeEditor.tsx` | New — canvas gain editor |
| `frontend/src/components/GainEnvelopeEditor.css` | New |
| `frontend/src/components/TimelineMinimal.tsx` | Add onClipSelect → uiStore on click |
| `frontend/src/App.tsx` | Render ClipEditor when clip selected |
