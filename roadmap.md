# HDAW Feature Parity Roadmap

Sequenced roadmap for closing the gaps in [`feature_parity.md`](feature_parity.md),
ordered by domain priority:

1. Arrangement & timeline
2. MIDI editing
3. Audio recording & editing
4. Instruments & sound generation
5. FX & processing
6. Automation
7. Tempo & time
8. Routing & I/O
9. Mixing

**Epics** are large architectural efforts. Foundation items pay off
multiple times across phases.

## Cross-cutting dependencies

- **Typed tracks** (Phase 1) is a prerequisite for instruments
  (Phase 4). The priority order already respects this.
- **Groove engine** (built in Phase 2) is reused by audio quantize
  (Phase 3) and tempo swing (Phase 7) — build it once.
- **Sidechain routing** (Phase 8) is shared by Routing and Mixing —
  do the routing engine once, the mixer UI consumes it.
- **Bounce/render engine** (Phase 3 freeze) is reused by
  instrument-to-audio (Phase 4).

---

## Phase 1 — Arrangement & timeline
1. ~~**Typed tracks** (audio/instrument/bus/folder/group)~~ ✅ *(foundation — unlocks Phase 4)*
2. ~~Track folders / grouping~~ ✅
3. ~~Track colors, height, show/hide~~ ✅
4. ~~Ripple edit~~ ✅
5. ~~**Clip-launch / session view**~~ ✅ *(8 scenes × N tracks, scene buttons, clip slots, quantized launch)*

## Phase 2 — MIDI editing
1. Finish CC controller lanes (complete the partial)
2. ~~Step sequencer / drum pattern editor~~ ✅
3. **MIDI effects rack** — arpeggiator, chord, scale, velocity, note-length (needs a MIDI FX chain)
4. Input quantize while recording
5. **Groove/swing engine** *(foundation — reused Phase 3 & 7)*
6. Logical MIDI transforms (rule-based note ops)
7. **MPE / note expression** *(epic — per-note pitch/pressure/slide)*

## Phase 3 — Audio recording & editing
1. ~~Auto crossfades between adjacent clips~~ ✅
2. ~~Comping / take lanes~~ ✅ (basic: multiple takes, switch via context menu)
3. ~~Punch in/out~~ ✅ (uses loop region as boundaries)
4. **Audio warp / elastic markers** *(epic — transient detection + warp engine; consumes groove engine)*
5. Audio quantize to groove *(consumes groove engine)*
6. Freeze / bounce-in-place *(foundation — reused Phase 4)*
7. Offline FX processing chain

## Phase 4 — Instruments & sound generation
1. Built-in synth *(needs instrument track type from Phase 1)*
2. Sampler / multisample instrument
3. Drum machine / drum rack
4. Stock instrument library
5. Render MIDI+instrument to audio *(consumes bounce engine from Phase 3)*

## Phase 5 — FX & processing
1. ~~Modulation FX — chorus/flanger/phaser/tremolo~~ ✅ *(chorus/flanger/phaser done; tremolo remaining)*
2. Distortion/saturation, filters, limiter/maximizer, tuner
3. Spectrum analyzer / oscilloscope
4. ~~FX presets & preset browser~~ ✅ (plugin factory and FX chain presets done; RPC: `project.saveFxChainPreset`, `project.listFxChainPresets`, `project.loadFxChainPreset`, `project.deleteFxChainPreset`; MCP: `save_fx_chain`, `list_fx_chains`, `load_fx_chain`, `delete_fx_chain`)
5. Per-slot wet/dry knob, oversampling

## Phase 6 — Automation
1. ~~Automation modes (Read/Write/Touch/Latch)~~ ✅
2. Bezier/curve automation shapes
3. Clip-based automation
4. Relative/trim automation

## Phase 7 — Tempo & time
1. ~~Time-signature track (multiple sig changes)~~ ✅
2. Tempo detection/mapping from audio
3. Swing / global groove quantize *(consumes groove engine)*

## Phase 8 — Routing & I/O
1. Flexible I/O routing matrix
2. **Sidechain routing engine** *(foundation — consumed by Phase 9 mixer UI)*
3. External hardware insert
4. Multi-out instruments, CV/gate

## Phase 9 — Mixing
1. ~~Loudness metering (LUFS/RMS/true-peak)~~ ✅ *(RMS + LUFS momentary done; true-peak remaining)*
2. VCA / group faders
3. Sidechain routing UI *(consumes Phase 8 engine)*
4. Per-channel input trim & phase invert
5. Monitor section / talkback / dim / mono
6. **Surround / spatial / Atmos** *(epic — defer)*
