# Plan: Jungle/DnB generation feature gaps — P1 (software-defined sounds)

Derived from the 2026-08-29 jungle/dnb session (handoff §2). Goal: remove the
manual note/automation placement we hand-rolled in every render. This plan
covers P1 only; P2/P3 are a backlog at the bottom (each becomes its own plan
when picked up).

## P1-1: MCP LFO tools (add_lfo / set_lfo_param) — ✅ LANDED 2026-08-29 (4 tools; also fixed a real Gate 1/10 bug: addTrack never restored LFOs on routing rebuilds)
**Goal:** Expose the engine's per-track LFO system over MCP so per-beat pumps
and wobbles are one command, not ~150 computed automation points.
**Context:** engine commands exist (`addLfo`, `setLfoParam` per track
MODULATION_LIST; LFO params: waveform 0=sin/1=tri/2=saw, rateSync, rate,
depth, bipolar, phaseOffset, targetParamID; target 1=Volume, 200+ = FX param
IDs). Only the command/RPC layer has them today — 193 MCP tools, zero lfo.
**Success gates:**
- G1: `add_lfo {trackId}` + `set_lfo_param {trackId, lfoIndex, param,
  value}` exist in MCP (McpTools_* new file or extension), documented.
- G2: gtest `McpServer.AddLfoParam`-style test: add_lfo → set params →
  live processor LFO list reflects them after a routing rebuild
  (Gate 1/10 discipline: assert live processor, not just ReadModel).
- G3: Full engine suite passes.
**Pitfall gates:** 1/6/10 (rebuild restore: LFOs must survive
rebuildRoutingGraph — verify the existing restore path covers MCP-created
LFOs), 2 (full path RPC→tree→processor→audio), 13 (param writes hold
stateLock), 15 (verify binary).

## P1-2: Internal filter types (low/high-pass) + honest cutoff sweeps — ✅ LANDED 2026-08-29 (fxType 'filter', StateVariableTPT, 36.9 dB LP attenuation measured; B3 fixed)
**Goal:** Add lowpass/highpass/bandpass modes so "filtered break" and LP
sweeps actually attenuate. Today the internal `eq` is a peak filter
(Frequency/Q/Gain dB) — automating Frequency moves a boost/cut center; it
does not mute a band (measured: mini-break stayed loud).
**Design options:** (a) `fxType:"filter"` = new internal FX (cutoff,
mode enum, resonance) with automatable cutoff param; (b) add a `mode` param
to `eq`. Prefer (a): separate, one automatable `Cutoff` param.
**Success gates:**
- G1: `add_fx {fxType:"filter"}` yields 3 params (Cutoff Hz, Mode, Res) with
  real units via `list_fx_params` + `set_internal_fx_param`.
- G2: gtest `InternalFx.LowpassSweepAttenuatesAboveCutoff`: sine-dominated
  material above cutoff is attenuated ≥ 12 dB vs peak; lowpass @ 200 Hz vs
  20 kHz render comparison.
- G3: automation lane on filter cutoff (paramID 100+slot*100+0) drives a
  sweep end-to-end (gtest `InternalFx.FilterCutoffAutomationSweeps`).
- G4: full suite passes; latency/quality note in plan (AGENTS.md lessons 7/8).
**Pitfall gates:** 1/6/10 (restore on rebuild), 3 (audio-thread DSP changes →
stateLock + no alloc in processBlock), 13 (stateLock writes), 7/8 (latency +
fidelity evaluation explicit).

## P1-3: Tempo-synced delay divisions — ✅ LANDED 2026-08-29 (SyncToTempo + Division enums; ratio 1.45843 measured) — **P1 BACKLOG COMPLETE**
**Goal:** one `sync` + `division` on delay (dotted-8th etc.) instead of
hand-computing seconds per tempo.
**Design:** add `syncToTempo(bool)` + `division` (selected from 1/2, 1/4,
1/8, 1/16, dotted variants, triplet variants) to internal delay; `Delay Time`
becomes the division's seconds at the current tempo (recomputed on tempo
changes + rebuild).
**Success gates:**
- G1: delay params include sync + division; a dotted-8th at 175 BPM reads
  ≈ 0.1286 s in `list_fx_params` (`getValueForAutomation`/snapshot path).
- G2: gtest `InternalFx.DelaySyncDivisionTracksTempo`: render at 120 vs 175,
  measured first-tap spacing scales by 120/175.
- G3: full suite passes.
**Pitfall gates:** 1/6/10 (restore), 3/13 (DSP + stateLock), 7/8 (latency/
fidelity).

## P1-4: `add_notes` absolute-beat mode — ✅ LANDED 2026-08-29 (`relative:false`, McpCoverageTest.AddNotes* trio green)
**Goal:** accept timeline-absolute note starts without the clip-local
conversion trap (startBeat is clip-relative today).
**Design:** optional `{relative: false}` (default current behavior): subtract
the clip's startTime before writing startBeat. Clip start known from the tree.
**Success gates:**
- G1: gtest `McpServer.AddNotesAbsoluteBeatsConvertsToClipLocal`: clip at
  start=32 fits; notes with absolute starts ≥ clip start become clip-local;
  absolute start < clip start → error.
- G2: description documents both modes; full suite passes.
**Pitfall gates:** 2 (path + handler exists), 9 (parse/validate), 15 (verify
binary), 1 (unit conversion correctness — beats everywhere).

## P2/P3 backlog (each gets its own plan when picked up)
- P2-1 Break chopper/composer: slice-ORDER pattern generation on top of  ✅ LANDED 2026-08-29 (`generate_chopped_break`, BreakPatternGenerator, 13 tests)
  `detect_sampler_slices` + `trigger_sampler_slice` (amen/2step/halftime
  grids, ghost fills, drop-the-first-beat edit).
- P2-2 Pattern-placement helper: tile `analyze_midi_file` patterns across a  ✅ LANDED 2026-08-29 (`place_patterns`, octave/velocity/retrograde transforms, 8 tests)
  beat range with octave/rotation/velocity params.
- P2-3 Section-aware arrangement + automation preset bank (pump, macro sweep, ✅ LANDED 2026-08-29 (`automation_preset`, 6 presets + sections[], 9 tests)
  breakdown open/close, riser presets).
- P2-4 Key/scale degree helper + `analyze_midi_file` returns key/scale/bpm + pattern ids.  ✅ LANDED 2026-08-29 (`scale_note` + B5 fixed)
- P2-5 Server-side mix report: RMS arc / bands / pump depth / kick prominence per section.  ✅ LANDED 2026-08-29 (`mix_report`) — **P2 BACKLOG COMPLETE**
- P3-1 `add_notes` duplicate guard.  ✅ LANDED 2026-08-29 (`duplicatesSkipped`, batch-internal exact triples)
- P3-2 `add_track` returns JSON `{trackId, routed}`.  ✅ LANDED 2026-08-29
- P3-3 `related_samples` includes `libraryId`.  ✅ LANDED 2026-08-29
- P3-4 Sidecar re-ingest robustness.  ✅ DONE via B2 (c99b7a0); regression pair re-verified 2026-08-29 — **P3 BACKLOG COMPLETE; P1+P2+P3 DONE**

## Sequencing
P1-1 (MCP LFO) is the highest-leverage for jungle/dnb generation; P1-2 is
needed for honest filter sweeps; P1-3 for dub timing; P1-4 removes the
biggest data-convention trap. Suggested order: P1-4 → P1-1 → P1-2 → P1-3 (P1-4 done; next: P1-1 MCP LFO tools)
(smallest blast radius first). Each lands with its gates + tests in the same
change (hdaw-guard completion contract: test discipline, MCP parity, graph
refresh after new files/RPC methods).
