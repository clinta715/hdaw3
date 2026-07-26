# Phase 1 — MIDI Editing: Concrete Implementation Tasks

Grounded in the current codebase (v0.13.0). See [`roadmap.md`](roadmap.md)
for the full parity roadmap and [`feature_parity.md`](feature_parity.md)
for the gap analysis.

Sizes: **S** <1d · **M** 1–3d · **L** 3–7d · **XL** epic (1–2+ weeks)

---

## Corrections discovered during exploration

The live MIDI editing UI is the **React frontend** (`frontend/src`), not
the Qt widgets listed in the README's stale project layout.

**Already built (scope shrinks):**
- Step sequencer **exists** (`frontend/src/components/StepSequencer.tsx`,
  8×16, edits real notes via `project.addNote`/`removeNote`) — just basic.
- CC lane **exists** (`frontend/src/components/CCLane.tsx`) — but
  click-add only, single lane.

**Hidden gaps not in the original parity list (bigger than expected):**
- **CC playback is broken** — `MidiClipProcessor` never reads recorded
  `CC_LIST`; recorded CC does nothing on playback (only CC7-from-gain
  is emitted).
- **No MIDI note recording** — MIDI input is playthrough-only; only
  audio gets recorded (`isArm` gates audio recording exclusively).
- **CC Rec is stubbed** — `setMidiCcCallback` / `setMidiCcRecordArmed`
  (`src/engine/AudioEngine.h:70-72`) have zero callers.

**Key architecture notes:**
- Notes are child ValueTrees: `CLIP → MIDI_NOTE_LIST → MIDI_NOTE*`
  (`src/model/ProjectModel.h:16-17,48-51`). CC lives in a separate
  lazily-created `CC_LIST → CC_POINT*` child of the clip.
- Velocity is stored normalized 0.0–1.0; converted to 0–127 at the RPC
  boundary (`src/engine/AudioEngineCommands_Midi.cpp:23,53`).
- No per-note channel — `midiChannel` is a track property
  (`ProjectModel.h:30`), applied in `RoutingManager.cpp:455-456`.
- Playback path: `MidiClipProcessor` (emits notes) → graph edge →
  `Track::processBlock` → hosted plugin instrument renders audio.
  **No internal synth** — MIDI is silent without a hosted instrument
  (that's Phase 4).
- Quantize is **frontend-only** (`NoteGrid.tsx:355-391`,
  `snapUtils.ts:1-10`); no engine-side quantize/swing/groove exists.
- MIDI FX insertion points: the graph edge at
  `RoutingManager.cpp:466-467` (most consistent) or inside
  `MidiClipProcessor::processBlock` (`MidiClipProcessor.h:58-127`,
  lowest friction).

---

## A. Complete the CC pipeline *(finish half-built work — do first)*

| # | Task | Where | Size |
|---|------|-------|------|
| A1 | **CC playback** — emit recorded `CC_LIST` events during playback (currently only CC7-from-gain) | `src/engine/MidiClipProcessor.h:58-127` | M |
| A2 | CC point drag/move/delete (currently click-add only) | `frontend/src/components/CCLane.tsx:91-108` | M |
| A3 | Multi-CC-lane display (currently single lane + selector) | `frontend/src/components/PianoRoll.tsx:300-315`, `CCLane.tsx` | S–M |
| A4 | **Complete CC Rec** — wire the stubbed callback → `addCcPoint` | `src/engine/AudioEngine.h:70-72` (no callers) | S |
| A5 | MCP CC tools (`add_cc_point`, `get_cc_points`) — frontend-only today | `src/mcp/McpTools_Project.cpp` | S |

## B. MIDI note recording *(missing entirely — high value)*

| # | Task | Where | Size |
|---|------|-------|------|
| B1 | Record incoming MIDI notes into a clip on armed tracks (noteOn/noteOff → notes). `isArm` gates audio only today | `src/engine/MidiInputManager.cpp:54-58`, `src/engine/MainAudioProcessor.cpp:157+`, `src/engine/AudioEngine.cpp:160-176` | L |

## C. Groove / swing engine *(foundation — reused Phase 2 & 7)*

| # | Task | Where | Size |
|---|------|-------|------|
| C1 | Groove template model + swing offset; decide frontend vs engine quantize | extend `frontend/src/components/NoteGrid.tsx:355-391`, `frontend/src/components/snapUtils.ts:1-10` | M–L |
| C2 | Quantize-to-groove (apply template to selected notes) | `frontend/src/components/NoteGrid.tsx` | S |

## D. MIDI effects rack *(headline feature)*

| # | Task | Where | Size |
|---|------|-------|------|
| D1 | **MIDI FX chain architecture** — ValueTree schema + insert MIDI-effect processor on the clip→track graph edge (or in `MidiClipProcessor::processBlock`) | `src/engine/RoutingManager.cpp:466-467`, `src/engine/MidiClipProcessor.h:58-127` | L |
| D2 | Arpeggiator (first FX — proves the architecture) | new | M |
| D3 | Chord / scale / note-length / velocity FX | new | M each |
| D4 | MIDI FX UI (frontend rack) | `frontend/src/components` | M |

## E. Step sequencer enhancements *(quick wins on existing component)*

| # | Task | Where | Size |
|---|------|-------|------|
| E1 | Per-step velocity editing | `frontend/src/components/StepSequencer.tsx` | S |
| E2 | Drum-map note naming | `frontend/src/components/StepSequencer.tsx` | S |
| E3 | Playhead highlight during playback | `frontend/src/components/StepSequencer.tsx` | S |
| E4 | Pattern length / presets / rotation | `frontend/src/components/StepSequencer.tsx` | S–M |

## F. Input quantize while recording *(depends on B1 + quantize logic)*

| # | Task | Where | Size |
|---|------|-------|------|
| F1 | Snap incoming notes to grid on record | `src/engine/MidiInputManager` + quantize util | S–M |

## G. Logical MIDI transforms

| # | Task | Where | Size |
|---|------|-------|------|
| G1 | Rule-based note transform (condition → action on pitch/velocity/position) | new engine command + UI | M–L |

## H. MPE / note expression *(epic — defer)*

| # | Task | Where | Size |
|---|------|-------|------|
| H1 | Per-note pitch/pressure/slide: data model + engine + UI | schema-wide | XL |

---

## Suggested build order

A1 → B1 → A2–A5 → C → D1 → D2 → E (interleaved) → D3/D4 → F → G → H.

Rationale: A1 fixes a real bug (recorded CC is currently dead). B1 makes
MIDI usable for recording at all. A2–A5 round out CC. C lays the groove
foundation reused by Phase 2 (audio quantize) and Phase 7 (tempo swing).
D1 is the architectural centerpiece; D2 proves it before building the
rest of the FX. E are cheap wins that can be interleaved anywhere.
