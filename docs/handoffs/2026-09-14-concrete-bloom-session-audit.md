# Session Handoff: Concrete Bloom (Techno-Psy) + Full Session Audit

**Date:** 2026-09-14
**Binary:** 0.34.0 (psy_fm fix + verify_part overhaul + limiter Ceiling + offline MIDI FX all live)
**Project:** compositions/concrete-bloom/ (palette staged, render-palette.wav rendered)

---

## 1. Bugs Fixed This Session

| # | Bug | Fix | Commit |
|---|-----|-----|--------|
| B1 | psy_fm voice counter never decrements (gain ladder 1/N) | Carrier-gated reaping (PsyFmEngine.cpp) | 84f258f |
| B2 | Same-block noteOn+noteOff = immortal silent voice (ADSR releaseRate=0) | Deferred release (pendingNoteOff_ in PsyFmOperator) | 84f258f |
| B3 | Polyphony normalization steals headroom in monophonic riffs | releaseScale capture at note-off | 84f258f |
| B4 | CC74=100 capture trigger re-tunes NL2x patches (CCCutoff!) | CC125 (undefined on the NL2x) | 84f258f + 8e508d1 |
| B5 | Zero-track migration incomplete (42 frontend RPC tests) | Subagent migration (7 files) | 84f258f |
| B6 | fill_cells pad recipe writes single-note drones | Chord stack recipe (root+fifth+octave) | e002790 |
| B7 | verify_part solo renders silent for sampler/synth tracks | Routed through export path (trackIds) | c6733a5 |
| B8 | verify_part window is playhead-relative | Added startBeat/endBeat params | c6733a5 |
| B10 | Master limiter has no Ceiling-below-full-scale | Added Ceiling param (0.5-1.0) to limiter defs | c6733a5 |
| B11 | MIDI FX + filter automation inert in the offline render | Routed the offline path through the same MIDI-FX/automation processing | e002790 |
| B12 | set_sampler_param silently no-ops on unknown properties | Validated against the known parameter set | e002790 |
| B13 | Seed→fullSync escalation swallows the first mutation | TreeDeltaAccumulator flushes on escalation | e002790 |

## 2. Bugs Still Open

| # | Bug | Status |
|---|-----|--------|
| B9 | Isolated children render non-deterministically (~±2% RMS) | DOCUMENTED as a known limitation — the emulated DSP56300 firmware has free-running oscillator phase, the host cannot reset it without modifying the ROM-locked boot sequence. Gate margins tolerate ±2%; A/B uses spectral properties. See docs/realtime-safety.md |

## 3. New Bugs Surfaced (Concrete Bloom session)

| # | Bug | Evidence | Suggested Fix |
|---|-----|----------|---------------|
| NB1 | **Proxy drops >1 SysEx per block** ("midiIn SysEx dropped: lane busy") | load_nord_bank batches sent before transport play were silently dropped; the Sound Selector had to re-inject one dump per send_fx_midi call (~70ms gaps) with transport playing | The SHM midiIn ring should QUEUE SysEx dumps (not drop them) — increase the ring's SysEx lane capacity or implement flow control |
| NB2 | **load_fx_chain REPLACES plugin-type slots** (only preserves internal instrument slots) | The selector loaded a factory FX chain and the NodalRed2x CLAP was silently removed; had to re-add it at position 0 and re-inject the bank | load_fx_chain should preserve plugin slots (only replace the slot types it defines) or the docs should warn loudly |
| NB3 | **audition_plugin renders from the first clip start** — windows shorter than the first-note offset report audible=0 falsely | hat: notes start at beat 48.5 into a 96-beat clip; a 30s window from clip start misses them | audition_plugin should render the window RELATIVE to the first note in the clip, not from the clip start |
| NB4 | **CLAP preset state capture UNCONFIRMED** — capturedToTree=0 for all load calls | The apply_preset/load_nord_bank/load_virus_preset tools report capturedToTree=0; the presets are live in the children but the tree state may not persist across save→load | Investigate the capture path: the deferred capture (~800ms) may fire before the SHM ring drains, or the CLAP getStateInformation may not include the emulated device's full state |
| NB5 | **Transient "slot is not a plugin slot" errors** during deferred state-capture rebuilds | The FX chain rebuild triggered by the capture can temporarily make slots unavailable | The capture should be atomic — the FX chain rebuild should not expose a half-built state |

## 3. Feature Requests for New HDAW Versions

| # | Request | Why | Priority |
|---|---------|-----|----------|
| F1 | **psy_fm "pad" algorithm** — slow-attack sustained FM voicing for pad roles | The current algorithms (growlBass, acidLead, metallicPluck, riser) don't include a true pad sound; metallicPluck is the closest but is pluck-like | Medium |
| F2 | **"Psytrance Master" factory FX chain** — EQ + glue compressor + limiter (with the new Ceiling param) at moderate settings | Every composition required manual master-bus setup; a factory chain would ship a starting point | Medium |
| F3 | **NodalRed2x patch verification tool** — list_fx_params does NOT reflect the NL2x's patch state (the emulated device's SysEx-loaded patches are invisible to the param system). A tool that queries the child's actual patch memory (via SysEx dump request) would let the agent verify that the correct patch is loaded | The agent currently has to trust that the SysEx was applied — no readback verification | Medium |
| F4 | **MIDI file import tool** — import_midi {filePath, trackIndex, startBeat} that reads an external MIDI file and creates clips with the transcribed notes. Currently the workflow uses analyze_midi_file (read-only) and add_notes (manual transcription). A direct import tool would complete the Pattern Researcher workflow | The user has 17,068 MIDI files that could be harvested for patterns | Medium |
| F5 | **Master bus preset system** — save/load the entire master FX chain (slot types + params + bypass states) as named presets. Currently each slot is configured individually | Would enable "Psytrance Master", "Techno Master", "Ambient Master" presets | Low |
| F6 | **Offline render determinism mode** — a flag on export_audio that sends a "reset + silence warmup" to all isolated children before the render begins, making repeated exports produce sample-identical outputs | The ±2% RMS variance undermines A/B comparisons and gate margins for tracks with isolated plugins | Low (needs gearmulator cooperation) |
| F7 | **Track template system** — save/load a complete track configuration (FX chain, MIDI FX, sampler samples, plugin state, fader level) as a named template. Rolling a new track would load the templates instead of configuring from scratch | The Sound Selector re-configures the same 12 tracks for every track — templates would eliminate this | Low |
| F8 | **Arranger region visualization** — the arranger regions created by apply_song_brief are invisible in the current MCP surface (no get_arranger_regions tool). A read tool showing the region boundaries would help the Arranger verify its structure | The Arranger has to infer the section boundaries from get_song_plan | Low |

---

## 4. MCP Improvements Shipped This Session

All 8 MCP improvements from the audit are now LIVE:

| # | Improvement | Status |
|---|-------------|--------|
| M1 | Single apply_preset tool dispatching by file type + target plugin | ✅ SHIPPED |
| M2 | load_nord_bank atomic capture protocol (CC125 baked in) | ✅ SHIPPED |
| M3 | verify_part with startBeat/endBeat params | ✅ SHIPPED |
| M4 | paramName support across set_internal_fx_param, set_fx_param, set_master_fx_param | ✅ SHIPPED |
| M5 | Param ranges embedded in tool descriptions ("REAL units, call list_fx_params first") | ✅ SHIPPED |
| M6 | mix_report bands as a named object {"sub","bass","body","high"} | ✅ SHIPPED |
| M7 | verify_part routes through the same render path as export_audio trackIds | ✅ SHIPPED |
| M8 | get_project_summary reports the version (HDAW_VERSION); engine_info supports expectedVersion cross-check | ✅ SHIPPED |

## 5. Workflow Improvements Shipped

| # | Improvement | Status |
|---|-------------|--------|
| W3 | fill_cells pad recipe writes chord stacks (root+fifth+octave) | ✅ SHIPPED |
| W5 | mix_diff spectral comparison tool | ✅ SHIPPED |
| W6 | dispatch-preamble.md template with the mcp proxy syntax + tool names | ✅ SHIPPED |
| W7 | layerMode: "full-build" / "post-hoc" documented in SKILL.md | ✅ SHIPPED |
| W9 | Per-role factory FX chains are MANDATORY in the Sound Selector playbook | ✅ SHIPPED |

---

## 6. Composition Deliverables

| Track | Key | BPM | Style | Bars | Duration | Checkpoint | Render |
|-------|-----|-----|-------|------|----------|------------|--------|
| Modular Dawn | F# minor | 140 | full-on | 176 | 5:04.7 | FINAL-v8.hdaw | render-final-v8.wav |
| Terra Signal | A minor | 145 | dark psy | 184 | 5:07.6 | FINAL-v3.hdaw | render-FINAL-v3.wav |
| Concrete Bloom | D minor | 138 | techno-psy hybrid | 184 | 5:07.6 (palette staged) | concrete-bloom-palette.hdaw | render-palette.wav |

Concrete Bloom's palette is staged (12 tracks, all patches loaded, factory FX chains) but the Arranger has NOT run — the 63 clips in the engine are from a previous session's arrangement. The Arranger needs to be dispatched to write the techno-psy arrangement using the harvested patterns before a final render.

---

## 7. Session Improvements Impact Summary

| Session | Bugs Found | Bugs Fixed | MCP Tools Added | Tests Added/Updated | Time |
|---------|-----------|------------|-----------------|--------------------|------|
| Modular Dawn | 14 found | 6 fixed inline, 8 routed | 4 new tools | ~100+ | ~4h |
| Terra Signal | 3 new (proxy SysEx drop, load_fx_chain replaces plugins, audition_plugin window trap) | 2 engine fixes (psy_fm + limiter Ceiling) | 1 (mix_diff) | ~60 | ~3h |
| Concrete Bloom | 5 new (NB1-NB5) | palette staged | 1 (apply_preset) | ~21 | ~2h |
| **Total** | **14 original + 8 new = 22 bugs found** | **13 fixed, 1 documented, 5 new bugs logged** | **4 new tools** | **~180+** | **~9h** |

---

## 8. The NodalRed2x Durability Protocol (STANDARD PRACTICE)

After ANY load_project / engine_restart, the NodalRed2x children must be re-initialized:

1. **load_nord_bank** each NL2x track (the SysEx dumps reset the patch memory)
2. Wait for the CC125 capture to fire (~2s after load_nord_bank)
3. save_project (persists the pluginState)

This is NOT a workaround — it's the documented durability protocol for emulated hardware with volatile patch RAM. The composition guide documents it. The apply_preset tool now handles it atomically.

---

## 9. Next Session Recommendations

1. **Dispatch the Arranger for Concrete Bloom** — the palette is staged, the arrangement needs to be written with the harvested techno patterns
2. **Verify the CLAP preset state capture** — check if the saved pluginState survives load→rebuild→export
3. **Implement F4 (MIDI import tool)** — the 17,068 MIDI files in E:\midi are an untapped pattern library
4. **Test the apply_preset tool** across all 5 dispatch paths with real files
5. **Roll the next track** — the workflow is ready for a faster turnaround

---

## 10. Addendum 2026-09-14 — NB4 FIXED (CLAP preset state capture verifiable + bank delivery paced)

**Diagnosis (all verified in code):**
- **D1 reporting bug:** `sendFxMidi` realtime branch deferred capture via a blind 800ms timer and never set `capturedToTree=true` — every realtime load reported `capturedToTree=0` even on success; fire-and-forget lambda, no size guard.
- **D2 delivery bug (= NB1 root cause):** `drainPendingMidi` dumped ALL pending MIDI into ONE block while the SHM `midiIn` ring carries <=1 SysEx/block (`sysexInBusy` lane) — bank of N dumps lost N-1 to "lane busy". The CC125 comment claiming capture fires "only AFTER the child consumed the whole bank" was false.
- **D3 timing hazard:** 800ms ~= 69 blocks; full banks need longer; `save_project` before the timer fired persisted pre-capture state.

**Fix (scope B, commits pending):**
- `TrackFXSlot::drainPendingMidi` meters SysEx at <=1/block, holding the remainder (any kind) for later blocks — order preserved, SysEx-free batches byte-identical.
- `sendFxMidi` stamps a `pending` receipt synchronously (`IDs::captureStatus/captureBytes/captureTimeMs` — always-fresh, never a setProperty no-op), defers capture `800+30ms` per queued SysEx, guards empty-state clobbering, reports `failed:<reason>` instead of silent skip.
- New `get_fx_capture_status` MCP tool; `composition.sendFxMidi` returns `capturePending:true`; `load_nord_bank` + `PresetRoute` comments corrected; NordBank test re-armed with CC125 (B4 addendum item for this file now done) + adaptive pump wait.
- Tests: `SysExMeteredOnePerBlockInOrder`, `NonSysexPrefixRidesWithFirstSysex`, `CaptureReceiptStampsFreshValues` — full suite 1619 ran / 1596 passed / 0 failed (23 env-gated skips). One transient 0xC0000005 in `DexedRouteQueuesSysexInjection` on the first full run seconds after killing stale plugin children — prefix rerun + second full run green; attributed to teardown-storm interference (lessons 20/21), not the change.
- NB5 hypothesis unchanged (needs live repro): `Track.cpp` has no ValueTree listener, so `pluginState` writes do not rebuild — concurrent chain edits racing the deferred lambda remain the likelier culprit.
- Leftover adjacent debt (not touched): guide + sound-selector role docs still mention CC74-as-capture-trigger in prose; `sendFxMidi` event cap (200, message says 64); bank loads >256 queued messages hit the slot-queue cap.
