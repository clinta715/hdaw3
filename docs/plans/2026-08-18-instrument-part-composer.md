# Plan: `composition.addInstrumentPart` + `autoGainToTarget` — the track-composer tool

## Goal

Ship the composite "make me an instrument part" tool from the handoff agenda
item #1: one command that does what LLM hand-coding did today — pick instrument →
add track → add instrument FX → generate a phrase → paint it across the
arrangement → gain-stage to a target RMS — plus a reusable standalone gain-stage
primitive. Engine command first (scriptable + testable, Q5 decision), RPC + MCP
in the same change, UI wraps later (PhraseGeneratorDialog pattern).

## Success Gates (all must pass with evidence)

- [ ] G1: New gtest suite `InstrumentPart.*` passes: the composite creates
      track + instrument FX slot + phrase clip(s) in **one undo unit** (a single
      `undo()` removes the whole part) and the **live processor** shows the slot
      after a rebuild (Gate 1/10 discipline — assert
      `getMainProcessor()->getTrack(trackIndex)`, not just the ReadModel).
- [ ] G2: New gtest suite `AutoGainToTarget.*` passes with a **deterministic
      source** (sine WAV, known RMS): fader ≈ target/knownRms; the `verify`
      re-render reports measured RMS ≈ target ±10%; a too-loud target clamps at
      fader 1.0 with `clamped=true`; a silent track returns an error, not a
      divide-by-zero/NaN.
- [ ] G3: MCP server suite passes, including the new `add_instrument_part` and
      `auto_gain_to_target` tool tests (same command path as RPC).
- [ ] G4: FrontendServer RPC test: `composition.addInstrumentPart` round-trips
      over WebSocket and returns `{trackIndex, clipIds, noteCount}`.
- [ ] G5: Full engine suite passes (exit 0; count grows from 908).
- [ ] G6: Runtime (Gate 4/15 — verify the **binary**, not source): fresh
      `build/Debug/HDAW.exe`, default project, `add_instrument_part` → audible
      part, `auto_gain_to_target` → fader set on the ReadModel, full export
      still works. Clean up all HDAW/plugin-host processes afterwards.
- [ ] G7: Diff scan — no anti-patterns: ONE command path shared by RPC+MCP, one
      `rebuildRoutingGraph()` per composite (never per clip), no new processor
      state lacking a restore path, no raw `DBG`, new `.cpp` present in CMake.

## Design

### Command 1 — `ProjectCommands::addInstrumentPart` (the composite)

```
addInstrumentPart {
  trackName: string,
  style: "Standard"|"Arpeggio"|"BassLine"|"ChordStab"|"Pad"|"Lead"|"RandomWalk"|"Buildup"|"Euclidean",
  pluginId?: string,        // empty → internal "fm_synth" slot
  lengthBeats?: number,     // default 4.0
  placement?: "wholeSong"|"region",   // default "region"
  startBeat?: number,       // default 0.0
  count?: number,           // "region": copies to paint (default 1)
  scaleRoot?: int,          // default -1 → project scale
  scaleMode?: int,          // default -1 → project scale
  density?: int,            // default 8
  noteDuration?: number,    // default 0.5
  lowNote?: int, highNote?: int, minVelocity?: int, maxVelocity?: int,
  seed?: int,
  targetRms?: number,       // >0 → run autoGainToTarget after building
  windowSeconds?: number,   // gain-staging window, default 4.0
  verify?: bool             // gain-staging verify re-render, default false
}
→ { trackIndex, clipIds[], noteCount, gain?: GainStageResult, error? }
```

Implementation (new file `src/engine/AudioEngineCommands_Composition.cpp`,
declared in `AudioEngineCommands.h`, added to `CMakeLists.txt`):

1. Validate: style name → `PhraseGenerator::Style` (unknown → error);
   `placement` ∈ {wholeSong, region}; bounds for count/beats.
2. `beginTransaction("Add instrument part")`.
3. `addTrack(trackName, -1, -1, /*trackType*/ 0)` (single implementation;
   MCP `add_track`/engine `addTrack` divergence is NOT reproduced here).
4. Add the FX slot WITHOUT the per-op `rebuildTrackFX`: `addFxSlot` (string
   overload, `AudioEngineCommands_Fx.cpp:62-95`) ends with
   `proc->rebuildTrackFX(trackIndex)`. Extract its slot-building body into a
   private `AudioEngineCommands::addFxSlotInternal(trackIndex, type, position,
   pluginId)` (no rebuild); `addFxSlot` calls it then rebuilds; the composite
   calls `addFxSlotInternal`. Keeps ONE implementation + honors the single-rebuild
   gate. (The new track isn't in the routing graph yet, so the per-op rebuild
   would be a no-op — but not calling it at all is the correct shape.)
5. `PhraseGenerator::generatePhrase` (beats end-to-end; convert to seconds only
   inside `addMidiClip`/`addNote`, which already do — lesson 1).
6. `addMidiClip` + `addNote` for each note.
7. Placement, **inline within the same transaction** (do NOT call `paintClips`
   — it opens its own `beginTransaction`, which would split this into two undo
   units):
   - `region`: `count-1` additional ghost copies at `startBeat + i*lengthBeats`
     (mirror the copy/offset/id-allocation logic in
     `AudioEngineCommands_GhostPaint.cpp` `paintClips`, minus the transaction).
   - `wholeSong`: project duration in beats =
     `ExportManager::calculateProjectDuration(model) * bpm/60`; `count =
     max(1, ceil(durationBeats / lengthBeats))`; copies from `startBeat`
     (default 0) at `lengthBeats` spacing so the part covers the song.
8. ONE `rebuildRoutingGraph()` (inside the transaction, after the paint loop).
9. `endTransaction()`.
10. If `targetRms > 0`: call `autoGainToTarget` (see below) — its fader write is
    a **separate undo unit** ("Auto gain stage"), so undo #1 removes the part,
    undo #2 removes the fader. Documented in the result.

### Command 2 — `ProjectCommands::autoGainToTarget` (the gain-staging primitive)

```
autoGainToTarget { trackIndex, targetRms, windowSeconds?, verify? }
→ { ok, fader, measuredRms, peak, clamped, error? }
```

Closed-form loop (the handoff's mechanical step, automated):

1. Validate trackIndex in range, `targetRms > 0`, `windowSeconds > 0`.
2. Guard: `getMainProcessor()->getExportManager().isExporting()` → error
   "export already in progress" (never clobber a live export's callbacks).
3. Build a **solo tree copy**: `model.getTree().createCopy()`, then in the copy
   set `IDs::isMuted=true` on every track except target and `IDs::isSoloed=false`
   on ALL tracks (guarantees only the target renders regardless of live solo
   state; the copy is never written back).
4. Window start = target track's earliest clip `startTime` (seconds); if the
   track has no clips → error "track has no clips". Duration = windowSeconds.
5. Temp WAV in `juce::File::getSpecialLocation(tempDirectory)`
   (`hdaw_gainstage_<track>.wav`), WAV 24-bit, 48 kHz (deterministic).
6. `em.startExport(copy, engine_.getProjectPool().getFormatManager(),
   &engine_.getPluginManager(), tempFile, 48000, windowStart, windowSeconds,
   WAV, 24)`. Block-wait `while (em.isExporting()) sleep(10)` with a deadline =
   `computeBakeWaitMs(treeCopy) + windowSeconds*1000 + 5000` (message pump is a
   separate thread — the bake still arrives; same pattern as the tests'
   `waitForExport`). On timeout → error.
7. Read the WAV with `juce::AudioFormatReader`; compute RMS + peak over the
   whole window. Measured RMS ≈ 0 → error "track is silent".
8. `fader = targetRms / measuredRms` (linear amplitude). `clamped = fader > 1.0`
   → clamp to 1.0. This is the FM-at-fader-1.0/quiet-plugin edge, reported, not
   hidden; global-scale + post-scale (master gain) stays a documented follow-up
   (per-part solo gain staging cannot fix a dense full-mix clipping).
9. `setTrackVolume(trackIndex, fader)` under `beginTransaction("Auto gain
   stage")`/`endTransaction`. Volume is restored on rebuild via the existing
   `Track::restoreMixerState` (already covered by `track_mixer_state_test`).
10. If `verify`: set the fader in a fresh tree copy, re-render the same window,
    re-measure → `measuredRms` is the verified value. Delete temp file(s).
11. Return result.

### RPC surface (`src/frontend/router/Router_Composition.cpp`, `dispatchComposition`)

- `composition.addInstrumentPart` → calls `engine.getProjectCommands().
  addInstrumentPart(params)`; maps the result struct to JSON (beats are
  accepted at the RPC boundary, matching every other composition RPC).
- `composition.autoGainToTarget` → same, maps `GainStageResult` to JSON.
- Both are synchronous RPCs that block the channel for the render duration
  (windowSeconds + bake) — acceptable for a tool, same shape as `export.audio`
  which also blocks; document in the dispatch comment.

### MCP surface (`src/mcp/McpTools_Project.cpp`, `registerCompositionTools` ~line 725)

- `add_instrument_part` — full param schema mirroring the RPC; calls the SAME
  `e->getProjectCommands().addInstrumentPart(...)` (parity rule — one command
  path, no independent reimplementation).
- `auto_gain_to_target` — `{trackId, targetRms, windowSeconds?, verify?}` →
  `e->getProjectCommands().autoGainToTarget(...)`.
- Both return text/JSON of the result.

### Out of scope (documented follow-ups)

- Frontend UI (Q5 says scripting/testing wins; UI wraps later).
- Global-scale + post-scale gain staging for dense mixes.
- Part templates (Q6), `compose.verify` companion (Q7), typed track presets (Q8),
  `pluginFormat` in the snapshot (agenda #4).

## Dependency Map (from trace_path + grep, verified)

- **Blast radius:** `ProjectCommands.h` (interface, +2 virtuals — every
  implementer compiles: `AudioEngineCommands` only, per grep), new
  `AudioEngineCommands_Composition.cpp` (new file → `CMakeLists.txt`),
  `Router_Composition.cpp` (dispatch cases), `McpTools_Project.cpp`
  (registerCompositionTools), tests. No changes to `RoutingManager`,
  `Track`, `ExportManager`, or `ReadModelImpl`.
- **Upstream callers (unchanged):** `Router_Composition` and MCP
  `registerCompositionTools` are the only composition consumers; existing
  `generate*` RPCs untouched.
- **Downstream (unchanged):** `addTrack`/`addFxSlot`/`addMidiClip`/`addNote`
  keep their signatures; the composite calls them. `ExportManager::startExport`
  unchanged (render path verified by `export_bake_timeout_test`).
- **God nodes in scope:** none — no hub (project model, routing manager,
  transport) is modified.
- **Projections:** ReadModel (new track/clips → existing delta/fullSync paths;
  track add = fullSync, clip add = delta — automatic, no wiring needed). Audio
  graph via the composite's single `rebuildRoutingGraph()` (existing park
  pattern, Gate 12).
- **SPSC paths:** track volume fader write (existing SPSC bridge + restore path).
- **Delta vs fullSync:** composite adds a track → fullSync (correct); the fader
  set is a track property → delta. Both automatic.
- **Undo:** composite = one undo unit (inline paint, single transaction);
  gain staging = one undo unit.
- **Path integrity:** RPC → ProjectCommands → addTrack/addFxSlot/addMidiClip/
  addNote → ValueTree → rebuild → ReadModel + live processor: every link exists
  today (verified by grep of the four command implementations and
  `generateArrangement`'s identical pattern at `AudioEngineCommands_Clips.cpp:728`).
  Gain staging path: startExport → render → WAV → AudioFormatReader → fader:
  verified against `export_bake_timeout_test.cpp` and the MCP export tool.

## Pitfall Gates Triggered

- **Gate 1/10 (state restore on rebuild):** the composite adds no NEW processor
  state (track/fx/clips are existing machinery); the fader is existing track
  volume with an existing restore path. Still: G1 asserts the LIVE processor
  after `rebuildRoutingGraph()` — never just the ReadModel.
- **Gate 2 (unimplemented path):** G2's verify re-render and G6's runtime render
  exercise the full RPC → tree → graph → render → measure → fader chain.
- **Gate 3 (audio-thread safety):** no new audio-thread code. Gain staging is a
  command-thread render; the measurement reads a file off the audio thread.
- **Gate 4/15 (stale binary):** G6 verifies the BINARY (timestamp probe) and
  cleans up processes.
- **Gate 9 (ID namespace):** uses `addMidiClip`/`addNote` and the paint copy
  logic, which call `allocateClipID`/`allocateNoteID` correctly. Bounds-check
  every param at the command boundary.
- **Gate 11 (message pump):** tests already start `MessagePumpThread`;
  `waitForExport`-style blocking is the proven pattern.
- **Gate 12 (graph mutation off-message-thread):** the composite's ONE
  `rebuildRoutingGraph()` uses the existing park; `autoGainToTarget` never
  touches the live graph (renders a copy). No new graph mutation sites.
- **Lesson 1 (beats vs seconds):** placement/notes in beats; render window in
  seconds; `wholeSong` duration converts project seconds → beats at the command
  boundary.
- **Lesson 6 (batched routing):** ONE rebuild per composite, never per clip.
- **Lesson 9 (default project):** default project ships 3 empty tracks — count
  assertions scope to the newly created track index, never a hard-coded
  absolute clip count.
- **Lesson 2 (setProperty no-op):** the fader write only happens when it changes
  the value; no side-effect relies on a listener firing at an unchanged value.
- **MCP parity:** both RPC methods get MCP twins sharing the one command path.

## Anti-patterns to avoid

- No per-clip `rebuildRoutingGraph()` in a loop (the composite's paint is inline
  and one rebuild runs at the end).
- No independent MCP reimplementation of track building (route through
  `ProjectCommands::addInstrumentPart`).
- No `DBG(...)` — use `HDAW_LOG` if logging is added.
- New `.cpp` (`AudioEngineCommands_Composition.cpp`) must appear in
  `CMakeLists.txt`; new test `.cpp` in `tests/CMakeLists.txt`.
- No raw hex CSS / no frontend changes at all in this plan.

## Files

1. `src/common/ProjectCommands.h` — add `InstrumentPartParams`,
   `InstrumentPartResult`, `GainStageParams`, `GainStageResult` structs + two
   pure virtuals (`addInstrumentPart`, `autoGainToTarget`).
2. `src/engine/AudioEngineCommands.h` — declare the two overrides + private
   `addFxSlotInternal`.
3. `src/engine/AudioEngineCommands_Composition.cpp` — **NEW**: implement both
   commands + a small WAV-measure helper (file-local static). Add to
   `CMakeLists.txt`.
4. `src/engine/AudioEngineCommands_Fx.cpp` — extract `addFxSlotInternal` (slot
   body only, no `rebuildTrackFX`); `addFxSlot` string overload calls it.
5. `src/frontend/router/Router_Composition.cpp` — add `addInstrumentPart` and
   `autoGainToTarget` dispatch cases + a `styleName→Style` helper (reuse the
   existing mapping pattern at lines 97–105).
6. `src/mcp/McpTools_Project.cpp` — register `add_instrument_part` and
   `auto_gain_to_target` in `registerCompositionTools`.
7. `tests/unit/engine/instrument_part_test.cpp` — **NEW**: G1 + G2 suites
   (deterministic sine-WAV gain staging via the existing `writeSineWav` helper
   pattern from `audioengine_read_facade_test.cpp:75`).
8. `tests/integration/mcp/mcp_server_test.cpp` — G3 tool tests.
9. `tests/unit/frontend/frontend_server_test.cpp` — G4 RPC round-trip.
10. `tests/CMakeLists.txt` — add the new test file.
11. `docs/handoffs/` — follow-up handoff line or next-steps update (optional).

## Steps (hdaw-guard subagent dispatch)

1. **Task A (engine core + unit tests):** implement files 1, 2, 3, 4, 7, 10
   (interface + implementation + addFxSlotInternal extraction + gtest). Success
   gates G1, G2, and the `*InstrumentPart*`/`*AutoGain*` filter runs green
   before reporting.
2. **Task B (RPC + MCP + their tests):** implement files 5, 6, 8, 9, calling
   the now-existing engine commands. Success gates G3, G4.
3. **Verification (orchestrator):** full `hdaw_tests.exe` (G5), diff scan (G7),
   runtime G6 on a fresh binary, process cleanup, knowledge-graph refresh
   (`codebase-memory` `index_repository` — new RPC methods + new files).