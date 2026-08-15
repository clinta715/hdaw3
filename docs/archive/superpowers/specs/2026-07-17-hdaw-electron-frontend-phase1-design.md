# HDAW Electron/React Frontend — Phase 1 Design

## Status

Architecture and phasing documented in the existing implementation plan
(`.zcode/plans/plan-sess_568153bd-a062-484e-b6f7-1fe411456cdf.md`).
This spec captures the confirmed Phase 1 scope for the design-review gate.

## Phase 1 Goal

An Electron app that spawns the headless engine, renders transport +
tracks + mixer + piano roll + minimal timeline, and round-trips real
mutations. Validates the wire protocol, push channel, and React render
performance before committing to the full surface.

## Decisions (confirmed by user, 2026-07-17)

| Question | Decision |
|----------|----------|
| Canvas for piano roll | `<canvas>` as per plan |
| Timeline | Static clip rectangles (colored rects, no drag) |
| Mixer strips | Full dynamic list (not a fixed subset) |
| Electron packaging | Dev mode only (Vite + Electron, no `npm run build`/package config yet) |

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Electron                                           │
│  ┌─────────────────────────────────────────────────┐│
│  │  Renderer (React + Vite)                        ││
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ ││
│  │  │ Zustand  │ │ Zustand  │ │ Zustand          │ ││
│  │  │ project  │ │transport │ │ meter            │ ││
│  │  │ Store    │ │ Store    │ │ Store            │ ││
│  │  └────┬─────┘ └────┬─────┘ └───────┬──────────┘ ││
│  │       └─────────────┼───────────────┘            ││
│  │               ┌─────┴──────┐                     ││
│  │               │ RPC Client │ (WebSocket)         ││
│  │               └─────┬──────┘                     ││
│  └─────────────────────┼───────────────────────────┘│
│                        │ ws://127.0.0.1:8766        │
│  ┌─────────────────────┼───────────────────────────┐│
│  │  Main Process       │                           ││
│  │  spawns HDAW.exe ───┘──headless --port=8766     ││
│  │  monitors child, restart-on-crash                ││
│  └─────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│  HDAW.exe --headless (child process)                 │
│  ┌──────────────────────────────────────────────────┐│
│  │  QCoreApplication + AudioEngine + FrontendServer ││
│  │  ┌──────────┐  ┌────────────┐  ┌──────────────┐ ││
│  │  │Project   │  │ Transport  │  │ LevelMeter   │ ││
│  │  │Commands  │  │ Commands   │  │ (atomics)    │ ││
│  │  │ etc.     │  │ etc.       │  │              │ ││
│  │  └──────────┘  └────────────┘  └──────────────┘ ││
│  │  FrontendServer (QWebSocketServer :8766)         ││
│  │  ┌────────┐ ┌──────┐ ┌────────────────────┐     ││
│  │  │Router  │ │RPC   │ │TreeWatcher          │     ││
│  │  │dispatch│ │consts│ │(ValueTree::Listener) │     ││
│  │  └────────┘ └──────┘ └────────────────────┘     ││
│  └──────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────┘
```

## Frontend files (new `frontend/` directory)

### Electron shell
- `frontend/package.json` — Electron, Vite, React, TypeScript, Zustand
- `frontend/electron/main.ts` — spawn `HDAW.exe --headless --port=<N>`,
  wait for port, create `BrowserWindow`, kill on quit, restart-on-crash
- `frontend/electron/preload.ts` — expose `window.hdaw` RPC handle
- `frontend/vite.config.ts` — React plugin, dev server config
- `frontend/tsconfig.json`

### Renderer core
- `frontend/src/rpc/client.ts` — JSON-RPC 2.0 over WebSocket, auto-reconnect
- `frontend/src/store/projectStore.ts` — Zustand, refreshed on `notify.treeChanged`
- `frontend/src/store/transportStore.ts` — Zustand, 30 Hz `notify.transport`
- `frontend/src/store/meterStore.ts` — Zustand, 30 Hz `notify.meters`
- `frontend/src/theme.ts` — CSS variables ported from `Theme.h`

### Components
- `App.tsx` — top-level layout
- `components/TransportBar.tsx` — play/stop/rewind/record/loop/BPM/timecode
- `components/Mixer.tsx` + `MixerStrip.tsx` — fader/pan/mute/solo/VU (full list)
- `components/PianoRoll.tsx` + `NoteGrid.tsx` — `<canvas>`-based note editing
- `components/TimelineMinimal.tsx` — static clip rectangles, playhead driven by transportStore
- `components/TrackHeaders.tsx` — name/volume/pan/mute/solo per track

## Wire protocol re-use

The engine-side `src/frontend/` (WebSocket server, router, RPC constants,
tree watcher) already exists and is functional — only `--headless` mode
and CMake wiring need to be committed. This spec assumes that Phase 0
is treated as done and committed before or alongside Phase 1.

## Exit criteria

1. Launch Electron app → sees default 3-track project
2. Add/remove tracks → UI updates
3. Play/stop → playhead moves, transport timecode updates
4. Drag mixer faders → VU meters respond, params update
5. Edit MIDI notes in piano roll (add/move/delete)
6. <50 ms click-to-visible for discrete ops, smooth 60 FPS meters/playhead
7. Kill and restart the engine process → Electron detects, shows restart dialog

## Risks

| Risk | Mitigation |
|------|------------|
| Child process WS port not ready when Electron loads | Main process polls until WS accepts |
| Burst edits flood `notify.treeChanged` | 16 ms debounce in tree watcher (already in Phase 0 server) |
| Native plugin editors are out of scope for Phase 1 | Documented; generic HTML param sliders deferred too |
