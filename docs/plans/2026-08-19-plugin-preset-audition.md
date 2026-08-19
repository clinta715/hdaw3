# Plan: Plugin preset selection + audition for the instrument-part composer (handoff #1 + #5)

## Goal

Make plugin-backed composition trustworthy and scriptable: (a) accept a
`programIndex` in `composition.addInstrumentPart` so a compose call can pick a
specific plugin preset, and (b) add `composition.auditionPlugin` — a fast probe
that solo-renders a plugin (temp track or existing slot) and reports
`{peak, rms, durationSeconds, audible}` so silent-at-default plugins stop being
a blocker. Prerequisite: extract the shared solo-render+measure loop from
`autoGainToTarget` into one helper (handoff #5) so audition, gain staging, and
verify share exactly one render implementation.

## Success Gates (all must pass with evidence)

- [ ] G1 (refactor): `--gtest_filter=InstrumentPart.*:AutoGain.*` passes
      UNCHANGED after the `renderTrackWindow` extraction (behavior-preserving
      refactor — no test edits for the existing suites).
- [ ] G2 (composer programIndex): new gtest `InstrumentPart.ProgramIndex*`:
      (a) programIndex with empty pluginId → error "programIndex requires a
      pluginId"; (b) programIndex out of range (fake plugin with no programs) →
      error, no crash; (c) env-guarded real plugin (TyrellN6): after
      addInstrumentPart{pluginId, programIndex:N} the LIVE slot reports
      `getCurrentProgram()==N` AND the tree slot's `pluginState` is non-empty
      (the snapshot that makes tree-copy renders + save/load capture the pick).
- [ ] G3 (audition): new `Audition.*` suite: (a) temp-probe mode leaves the
      project untouched (track count restored) unless `keepTrack=true`; (b)
      env-guarded TyrellN6: the audition finds an AUDIBLE plugin (peak/rms above
      threshold) and reports the program count/name — per-program AUDIO
      DIFFERENCES are NOT asserted (verified 2026-08-19: JUCE's setCurrentProgram
      is cosmetic for the installed VST3 synths — it changes the reported index
      and the captured state blob, but the rendered audio is byte-identical
      across programs); (c) invalid params (bad pluginId, programIndex out of
      range, trackIndex out of range) return an error, never a crash.
- [ ] G4 (MCP parity): `McpServer.AuditionPluginTool` + an extended
      `add_instrument_part` test passing `programIndex` — both call the SAME
      engine commands as the RPC.
- [ ] G5 (RPC): `FrontendServer.AuditionPluginRpc` round-trips over WebSocket;
      `AddInstrumentPartRpc` extended to pass `programIndex`.
- [ ] G6 (full suite): `build/Debug/hdaw_tests.exe` full run exits 0; test
      count grows by the new cases.
- [ ] G7 (runtime, Gate 4/15): fresh `build/Debug/HDAW.exe` (verify binary
      timestamp): (a) `audition_plugin` on TyrellN6 finds an audible program
      and reports peak/rms; (b) `add_instrument_part` with a real pluginId +
      programIndex + targetRms produces an audible, non-clipped part. Kill all
      HDAW/plugin-host processes afterwards.
- [ ] G8 (diff scan): ONE render-loop implementation (`renderTrackWindow`); the
      program-state snapshot uses the nullptr-undo pattern (mirrors
      `Track::rebuildFXChain`); no new processor state without a restore path;
      no raw `DBG`; new `.cpp` test file present in `tests/CMakeLists.txt`;
      MCP tools call the engine commands (no independent reimplementation).

## Design

### Shared render helper (step 0 / #5) — `AudioEngineCommands_Composition.cpp`

File-local (anonymous-namespace) helper extracted verbatim from the
`autoGainToTarget` body:

```cpp
struct RenderWindowResult { juce::File wavPath; float rms=0.0f; float peak=0.0f; std::string error; };
RenderWindowResult renderTrackWindow(AudioEngine& engine, int trackIndex,
                                     double windowSeconds, float fader, bool applyFader);
```

- Window start = target track's earliest clip `startTime` (seconds); no clips →
  error "track has no clips".
- Solo tree copy (`model.getTree().createCopy()`, mute all but target,
  un-solo all; apply `fader` to target only when `applyFader`).
- `startExport(..., 48000, windowStart, windowSeconds, WAV, 24)` + block-wait
  (`computeBakeWaitMs + windowSeconds*1000 + 5000`) + `measureWav`.
- Unique temp name `hdaw_render_<track>_<counter>.wav` (static atomic counter);
  the helper does NOT delete it — the caller owns cleanup via `wavPath`.
- `autoGainToTarget` refactors onto it: raw render = `(1.0f, false)`, verify
  render = `(fader, true)`. The "track is silent" check (`rms <= 1e-6`) and the
  clamped-at-1.0 behavior stay in `autoGainToTarget` verbatim.

### Program apply helper (shared by composer + audition)

```cpp
// File-local. Sets a plugin program on a LIVE slot and snapshots its state back
// into the ValueTree so tree-copy renders (gain stage, audition, export) and
// save/load capture the selection. The write uses nullptr undo — plugin state is
// volatile cache, not a user edit (mirrors Track::rebuildFXChain).
bool applyPluginProgram(AudioEngine& engine, int trackIndex, int slotIndex,
                        int programIndex, std::string& error);
```

- Requires a live `TrackFXSlot` with `isPlugin()` and a non-null
  `getPluginInstance()` (via `getMainProcessor()->getTrack(trackIndex)`).
- Bounds-check `programIndex` against `slot->getNumPrograms()` (Gate 9).
- `slot->setCurrentProgram(programIndex)` (same thread contract as the proven
  MCP `load_plugin_preset` path — command/MCP thread, bounded IPC for isolated
  plugins).
- `instance->getStateInformation(state)`; write
  `fxChainTree.getChild(slotIndex).setProperty(IDs::pluginState,
  state.toBase64Encoding(), nullptr)`. Empty state → error.

### `addInstrumentPart` — `programIndex` param

- `InstrumentPartParams` gains `int programIndex = -1;` (default = none).
- Validation at the command boundary: `programIndex >= 0 && pluginId.empty()`
  → error "programIndex requires a pluginId".
- Application point: AFTER the in-transaction `rebuildRoutingGraph()` (the
  plugin instance exists then), BEFORE `endTransaction` — so the pick rides the
  SAME undo unit as the composite. `applyPluginProgram` failure → endTransaction
  + error (composite rolled back by the caller's existing error path).
- Gain staging (targetRms) then sees the program's state via the tree copy.

### `auditionPlugin` command

```
ProjectCommands::AuditionParams {
  pluginId,            // required when trackIndex < 0
  programIndex = -1,   // -1 = current/default
  trackIndex = -1,     // <0 = temp probe track
  slotIndex = 0,       // with trackIndex
  style="Arpeggio", lengthBeats=4.0, density=8, noteDuration=0.5,
  lowNote=48, highNote=84, minVelocity=60, maxVelocity=110, seed=0,
  windowSeconds=4.0, keepTrack=false
}
ProjectCommands::AuditionResult {
  ok, trackIndex(=-1 when probe removed), slotIndex, programIndex, programName,
  numPrograms, rms, peak, durationSeconds, audible, error
}
```

- **Temp probe mode** (`trackIndex < 0`): one transaction "Audition probe" →
  `addTrack("Audition")` + `addFxSlotInternal("plugin", pluginId)` + a generated
  phrase clip (`PhraseGenerator::generatePhrase`, Arpeggio default) +
  `rebuildRoutingGraph()` → endTransaction. Apply program + snapshot. Render via
  `renderTrackWindow(..., 1.0f, false)`. Cleanup: if `!keepTrack`, `undo()` (one
  step reverts the whole probe) + `rebuildRoutingGraph()`; report
  `trackIndex=-1`.
- **Existing-slot mode** (`trackIndex >= 0`): validate the slot is a live plugin
  slot with clips on the track (no clips → error). No rebuild, no undo. Program
  set + snapshot persist (documented; same semantics as `load_plugin_preset`).
- `audible = peak > 1e-4f` (≈ −80 dBFS), reported alongside rms/peak/duration.
- Failures (plugin instantiation → "none" slot, out-of-range program) roll back
  the probe track when in probe mode, then return the error.

### RPC (`Router_Composition.cpp` `dispatchComposition`)

- `composition.auditionPlugin` → parse `AuditionParams` (optInt/optString/optBool
  helpers) → `c.auditionPlugin(p)` → map `AuditionResult` to JSON.
- `composition.addInstrumentPart`: add `p.programIndex = optInt(o, "programIndex", -1)`.

### MCP (`McpTools_Project.cpp` `registerCompositionTools`)

- `audition_plugin` — schema mirroring the RPC params; calls the SAME
  `e->getProjectCommands().auditionPlugin(...)`; returns text of the result
  (parity rule — one command path).
- `add_instrument_part` — add `programIndex` to the schema + pass it through.

## Dependency Map

- **Blast radius:** `ProjectCommands.h` (+1 virtual `auditionPlugin`, +2 structs,
  +1 field on `InstrumentPartParams` — implementer `AudioEngineCommands` only),
  `AudioEngineCommands_Composition.cpp` (helper extraction + 2 commands),
  `Router_Composition.cpp` (dispatch cases), `McpTools_Project.cpp`
  (registerCompositionTools), tests. No changes to `Track`, `RoutingManager`,
  `ExportManager`, `PluginParamServiceImpl`, or `ReadModelImpl`.
- **Upstream callers:** `Router_Composition` + MCP `registerCompositionTools`
  are the only composition consumers. Existing `generate*`/`addInstrumentPart`/
  `autoGainToTarget` RPCs keep their shapes (new optional param only).
- **Downstream:** `addTrack`/`addFxSlotInternal`/`addMidiClip`/`addNote`/
  `rebuildRoutingGraph` unchanged. `ExportManager::startExport` unchanged.
- **God nodes in scope:** none (no hub modified).
- **Projections:** ReadModel — track add = fullSync, clip add = delta (automatic);
  pluginState write is a property on an existing FX_SLOT (delta path, but
  pluginState is excluded from snapshots by design — no read-model impact). Audio
  graph via the existing single `rebuildRoutingGraph()` per composite (Gate 12).
- **SPSC paths:** none new. Program set + state snapshot are command-thread
  plugin calls (same thread as the proven `load_plugin_preset` MCP tool).
- **Undo:** composer programIndex rides the existing composite unit; audition
  probe = one self-reverting unit; gain stage unchanged (separate unit).
- **Path integrity:** RPC/MCP → ProjectCommands::addInstrumentPart/auditionPlugin
  → addTrack/addFxSlotInternal/rebuildRoutingGraph → live slot
  setCurrentProgram + state snapshot → renderTrackWindow (tree copy) →
  measureWav → fader/result. Every link exists or is added by this plan.
- **Real-plugin test dependency:** TyrellN6 at
  `C:\Program Files\Common Files\VST3\TyrellN6(x64).vst3` is installed and
  present in `%APPDATA%\HDAW\plugin_cache.xml` (128 programs). Env-guard with
  `HDAW_REAL_PLUGIN_TESTS=1` + `juce::File(path).existsAsFile()` + cache lookup;
  otherwise `GTEST_SKIP()` (pattern from `export_volume_bypass_test.cpp`).

## Pitfall Gates

- **Gate 1/10 (state restore):** the program pick persists ONLY via the
  pluginState snapshot (the existing restore path in `Track::rebuildFXChain`).
  The G2 test asserts the LIVE processor (`getCurrentProgram()`) AND the tree
  snapshot — never just the read model.
- **Gate 2 (unimplemented path):** G3/G5/G7 exercise RPC/MCP → command → live
  plugin → render → measure → report end-to-end.
- **Gate 9 (bounds):** validate programIndex against `getNumPrograms()` at the
  command boundary; pluginId-vs-trackIndex exclusivity; style re-validation.
- **Gate 11/12 (pump/rebuild):** no new entry points; the composer/audition use
  the existing `rebuildRoutingGraph()` park. `renderTrackWindow` renders a COPY —
  never mutates the live graph.
- **Gate 16 (plugin lifecycle):** program/state calls run on the command/MCP
  thread — identical to the existing `load_plugin_preset`/`ProjectSerializer`
  paths (bounded IPC for isolated plugins). No new control-loop lifecycle calls.
- **Lesson 1 (beats vs seconds):** phrase params stay in beats at the boundary;
  the render window is seconds (existing convention).
- **Lesson 20 (proxy slot collision):** real-plugin tests are env-guarded and
  use a unique temp-track flow; they do not collide with the CrashRecovery
  proxy-slot tests.
- **MCP parity:** `audition_plugin` + extended `add_instrument_part` share the
  engine command path.

## Verified finding (2026-08-19, drives test expectations)

Live probes on this machine established a hard limit of the JUCE program API for
the installed VST3 synths (TyrellN6/TripleCheese/Podolski/Zebralette3):

- `setCurrentProgram(i)` on a fresh live instance DOES change the reported
  program index (`getCurrentProgram()==i`) and the captured `getStateInformation`
  blob (all pairwise state comparisons DIFFER).
- The state round-trip is LOSSLESS: `setStateInformation` + `prepareToPlay` into
  a fresh instance reproduces the captured bytes exactly (MATCH).
- Yet the RENDERED AUDIO is byte-identical across programs (exact-0 peak
  differences for every pair) — both in the tree-copy render path AND when
  processing the live plugin instance directly with a held note.

Conclusion: for these plugins the JUCE program API is COSMETIC — the state blob
does not carry the audio params of the selected program, so offline renders
cannot reproduce program-specific audio. The audition tool still delivers its
headline value (finding AUDIBLE plugins/presets by rendering the plugin's actual
output), and the `programIndex` host-side mechanism is correct for plugins that
honor the API and errors cleanly for those that don't. Tests assert the verifiable
contract (live index changes, snapshot written, audibility signal) — NOT
per-program audio differences.

## Files

1. `src/common/ProjectCommands.h` — `InstrumentPartParams::programIndex`,
   `AuditionParams`, `AuditionResult`, virtual `auditionPlugin`.
2. `src/engine/AudioEngineCommands.h` — declare `auditionPlugin` override.
3. `src/engine/AudioEngineCommands_Composition.cpp` — `renderTrackWindow`
   extraction + `applyPluginProgram` + programIndex in addInstrumentPart +
   `auditionPlugin` impl.
4. `src/frontend/router/Router_Composition.cpp` — auditionPlugin dispatch +
   programIndex in addInstrumentPart.
5. `src/mcp/McpTools_Project.cpp` — `audition_plugin` tool + programIndex in
   add_instrument_part.
6. `tests/unit/engine/instrument_part_test.cpp` — G2 tests (env-guarded).
7. `tests/unit/engine/audition_test.cpp` — NEW: `Audition.*` suite (G3) +
   env-guarded real-plugin cases. Add to `tests/CMakeLists.txt`.
8. `tests/integration/mcp/mcp_server_test.cpp` — G4 tool tests.
9. `tests/unit/frontend/frontend_server_test.cpp` — G5 RPC round-trips.

## Steps (hdaw-guard subagent dispatch)

1. **Task A (engine core + unit tests):** files 1, 2, 3, 6, 7 +
   `tests/CMakeLists.txt`. Success gates G1, G2, G3 (filtered runs) green
   before reporting.
2. **Task B (RPC + MCP + tests):** files 4, 5, 8, 9. Success gates G4, G5.
3. **Verification (orchestrator):** full `hdaw_tests.exe` (G6), diff scan (G8),
   runtime G7 on a fresh binary, process cleanup, knowledge-graph refresh
   (`codebase-memory` `index_repository` — new RPC method + new file).