# Plan: `acid_step` MIDI FX — the first composable component (303, slice 1)

**Date:** 2026-10-04 · **Status:** approved scope, implementation dispatched
**Predecessor discussion:** componentization grain for "I want a 303"
(monolithic engines vs. composed components). User decisions recorded below.

## Goal

Add a 16-step acid sequencer as a **MIDI FX component** (`acid_step`) that emits
notes plus per-step velocity and polyphonic-aftertouch accent, with legato/tie
slide pairing. **No engine/DSP change** — no `processBlock` edit, no change to
`SubtractiveSynthEngine` / `TrackFXSlot` / any filter.

## Locked design decisions (user-approved 2026-10-04)

1. **Slice 1 = MIDI FX only.** No `SubtractiveSynthEngine` / DSP /
   `processBlock` change. The accent becomes *audible* in slice 2; slice 1
   proves the note contract.
2. **Per-note accent channel = velocity + polyphonic aftertouch**
   (`juce::MidiMessage::aftertouchChange(channel, note, value)`). Poly aftertouch
   is per-note-number, needs no new plumbing, and is the interim **L0
   per-note-modulation contract** the eventual polyphonic/section voice target
   needs (an envelope modulator is a per-voice source whose amount is per-note).
3. **The step pattern is expressed as PARAMS, not a data blob.** The arithmetic
   that decides this: the automation pid for a MIDI-FX param is
   `1000 + slotIndex*100 + paramIndex` (`docs/adr-automation-model.md:67`,
   `ReadModelImpl.cpp:888`), so `paramIndex <= 99` is the hard limit (and there
   are at most 10 midi-FX slots per track). A 16-step x 4-attribute table is
   64 params; with 12 globals that is **76 params (indices 0..75)** — it fits,
   with 23 indices of headroom. A 32-step variant would need the data-blob
   route; that is explicitly out of scope.
   Benefit of params over a blob: no new MCP tool and no new RPC method (so no
   parity-ledger regeneration), the pattern is discoverable through
   `list_midi_fx_params`, and every step is automatable — and it demonstrates
   the componentization thesis directly ("a component = a deterministic flat
   param table + semantics").
4. **Grain chosen for the long term** (from the same discussion): eventually
   construct complex devices — polyphonic voices + envelope modulators — from
   per-voice *sections* with typed mod input ports, and a **flat param table
   derived from a section manifest** rather than hand-written per fxType. Slice 1
   deliberately probes only L0 (per-note modulation), the layer that design
   depends on.

## Param table (indices are the contract)

Globals, indices 0..11:

| idx | name | range | default | meaning |
|---|---|---|---|---|
| 0 | `rate` | 0.01..2.0 beats | 0.25 | step length (1/16) |
| 1 | `steps` | 1..16 | 16 | pattern length |
| 2 | `octave` | -2..2 | 0 | global transpose in octaves |
| 3 | `gate` | 0.05..1.0 | 0.5 | note length as a fraction of rate |
| 4 | `velocity` | 1..127 | 96 | velocity of non-accented steps |
| 5 | `accentVelocity` | 1..127 | 118 | velocity of accented steps |
| 6 | `accentAmount` | 0..127 | 110 | poly-aftertouch value emitted on accented steps |
| 7 | `swing` | 0..0.6 | 0 | odd-step delay as a fraction of rate |
| 8 | `slideMode` | 0..1 | 0 | 0 = legato (off/on at one sample), 1 = tie (overlap) |
| 9 | `direction` | 0..2 | 0 | 0 = forward, 1 = reverse, 2 = ping-pong |
| 10 | `latch` | 0..1 | 0 | 0 = play while transport runs |
| 11 | `baseNote` | 0..127 | 36 | MIDI note for a step value of 0 |

Per step `N` = 1..16, four params at `12 + (N-1)*4 + {0,1,2,3}`:
`step{N}Note` (-24..24 semitones, default 0), `step{N}Accent` (0..1),
`step{N}Slide` (0..1), `step{N}Rest` (0..1). Total 76 params, indices 0..75.

## Four per-type dispatch sites (a new MidiEffect must be wired into ALL of them)

Missing any one produces a silently inert component or silently inert params
(lesson 38 class):

1. `src/engine/MidiFx.cpp` — `getMidiFxParamDefs(type)` (+ `getMidiFxParamCount`).
2. `src/engine/Track.cpp:314+` — `Track::rebuildMidiFXChain` factory (the only
   place the tree values reach the effect's fields at load).
3. `src/engine/AudioEngineCommands_Fx.cpp:147` — `addMidiFxSlot` default writes.
4. `src/engine/MidiFx.cpp:154` — `MidiFxSlot::applyToEffect` (the automation /
   live-write path; without a case here every param write is dropped).

This four-site duplication is itself the argument for the manifest/registry
direction in decision 4 — do NOT refactor it in this slice.

## Success gates (all must pass; evidence required)

- **G1 Build:** `./build-fast.sh test` completes; `build/hdaw_tests_engine` relinks.
- **G2 New gtest suite `AcidStep.*`** in the **already-registered**
  `tests/unit/engine/midi_fx_test.cpp` (do NOT edit `tests/CMakeLists.txt` —
  `build-fast.sh` only reconfigures when the ROOT `CMakeLists.txt` changes, so a
  test-list edit would silently not take effect). Asserts exact MidiBuffer output:
  - step onsets derived from `PositionInfo` at rate 1/16 (and 1/8, 1/4), sample-accurate;
  - per-step note + `octave` + `baseNote` applied; `step{N}Rest` emits nothing; the pattern wraps after `steps`;
  - an accented step emits `step{N}`-scaled `accentVelocity` **and** a matching `aftertouchChange(channel, note, accentAmount)` at the same sample;
  - a slide step emits the previous note-off and the next note-on at the **same** sample (legato), and `slideMode:1` overlaps instead;
  - **no stranded note-offs:** stop / `steps` change / `rate` change mid-pattern emits the pending note-off exactly once; a backwards playhead jump resets step state;
  - **determinism:** two runs over the same playhead sequence produce byte-identical MidiBuffers;
  - **flag values are honoured, not just parsed** (lesson 34/38): a value of 1 in
    `step{N}Accent`/`Slide`/`Rest` must change the emitted buffer relative to 0.
- **G3 No regression:** `./build/hdaw_tests_engine --gtest_filter='*MidiFx*:*Automation*:*Commands*'`
  passes, plus the midi-fx suites in the `_mcp` / `_frontend` binaries.
- **G4 Scope proof:** `git diff --stat` touches nothing under `src/engine/SubtractiveSynthEngine*`,
  `src/engine/TrackFXSlot.*`, `src/engine/DrumSynth*`, `src/engine/InternalFilter.h`.
  No DSP change ⇒ no bit-identity exposure ⇒ no render A/B required.
- **G5 Parity:** `add_midi_fx {type:"acid_step"}` accepted via the MCP tool AND the
  `project.addMidiFxSlot` route; `list_midi_fx_params` returns 76 params with pids
  `1000 + slot*100 + index`, all < 1100. No new tool/method ⇒ no
  `tools/rpc_parity_map.mjs` regeneration.
- **G6 Not-silently-dropped:** a downstream consumer in-test observes the aftertouch
  event (proves the chain forwards it; `CLAPPluginInstance.cpp:781` already passes
  aftertouch through to plugins).

## Dependency map

Source: `graphify query "MidiEffect MidiFxSlot addMidiFxSlot getMidiFxParamDefs"`
(24160-node graph; 183-node BFS; community `MidiFxSlot`).

- **Blast radius:** the `MidiEffect` / `MidiFxSlot` community only. No audio DSP community touched.
- **Upstream (callers):** `Track::rebuildMidiFXChain` (`Track.cpp:314`);
  `AudioEngineCommands::addMidiFxSlot` (`AudioEngineCommands_Fx.cpp:147`);
  `ReadModelImpl::getAutomatableParams` (`ReadModelImpl.cpp:888`).
- **Downstream (consumers):** the MIDI buffer handed to the instrument slot;
  `MidiFxSlot::loadParamsFromTree` (`MidiFx.cpp:244`, reads **by param name**);
  `MidiFxSlot::applyToEffect` (`MidiFx.cpp:154`).
- **Surfaces:** MCP `add_midi_fx` enum (`src/mcp/McpTools_MidiFx.cpp:34`) and RPC
  `project.addMidiFxSlot` (`src/frontend/router/Router_Project.cpp:589`).
- **God nodes in scope:** none.
- **Projections affected:** ReadModel automatable-param list (pids only).
- **Realtime paths touched:** the MIDI buffer inside the track's MIDI-FX chain, per
  block on the audio thread ⇒ `process()` obeys realtime rules (no alloc, no lock, no IO).

## Pitfall gates triggered

1. **Realtime safety.** `AcidStep::process` runs on the audio thread. Follow the
   existing sibling convention: plain public POD fields written from the message
   thread and read on the audio thread (benign tear — the `Arpeggiator` /
   `InternalFilter` precedent), no allocation, no locks, no logging.
2. **Automation pid space.** 76 params, indices 0..75 ≤ 99. Do not exceed 99; a
   32-step variant would need the data-blob route (out of scope).
3. **Silent acceptance (lesson 38).** Three pre-existing silent paths found in
   recon, none to be fixed here but all to be gated against:
   `setMidiFxSlotParam` stores ANY property name and only forwards it when it
   matches a param def name (a typo is a silent no-op);
   `addMidiFxSlot` validates no type string at all (a typo yields a dead slot;
   only the MCP enum gates it); a missing `applyToEffect` case drops every write.
   G2/G5 must therefore assert the OBSERVABLE effect, never the parse.
4. **Determinism (lesson 21 analog).** `reset()` clears step index and pending
   note state; all timing comes from `PositionInfo` (never wall clock) so offline
   render equals live.
5. **Transport stop (lesson 3 analog).** Flush pending note-offs on stop/relocate.
6. **Naming-convention mismatch (found in recon).** `addMidiFxSlot` writes
   `arpRate`/`arpPattern`, but `loadParamsFromTree` reads `defs[i].name`
   (`rate`/`pattern`) — arp non-default values written at add time can be
   dropped, masked by matching defaults. The new type must use ONE convention
   (the param-def names) for all four sites. The arp mismatch is documented, not fixed.
7. **No DSP touch** ⇒ bit-identity / render-variance gates deliberately not required.

## Steps

1. `src/engine/MidiFx.h` — add `class AcidStep : public MidiEffect` beside its
   siblings: public POD fields for the 12 globals, a POD `struct Step { int note;
   float accent, slide, rest; }` array of 16, `reset()`, `process()`.
2. `src/engine/MidiFx.cpp` — `acidStepParams[]` (76 entries, indices as above) +
   dispatch in `getMidiFxParamDefs` / `getMidiFxParamCount` + the `applyToEffect`
   `dynamic_cast<AcidStep*>` case mapping all 76 indices.
3. `src/engine/Track.cpp` `rebuildMidiFXChain` — `else if (type == "acid_step")`
   branch constructing `AcidStep` from the tree using the param-def names.
4. `src/engine/AudioEngineCommands_Fx.cpp` `addMidiFxSlot` — defaults branch for
   `acid_step` using the SAME param-def names.
5. `src/mcp/McpTools_MidiFx.cpp:34` — enum += `"acid_step"`.
6. `tests/unit/engine/midi_fx_test.cpp` — the `AcidStep.*` suite (G2). No CMake edit.
7. Run G1–G6 and report evidence (files changed, commands, output, gate results).

## Out of scope (slice 2+)

- Accent coupling in `SubtractiveSynthEngine` (velocity/aftertouch → filter env amount + resonance).
- Any internal engine reading poly aftertouch.
- Filter component unification; voice section templates; manifest-derived param tables;
  collapsing the four per-type dispatch sites into a registry.
