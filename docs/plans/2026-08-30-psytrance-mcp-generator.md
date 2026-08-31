# Plan: P1 — Psytrance MCP generator + multi-sampler key routing + FX param read-back

Source: `docs/handoffs/2026-08-30-pi-mcp-compose-session.md` §3 (P1.1–P1.3).
The session composed a full F-minor psytrance track over MCP by hand-computing
2,686 notes client-side; this plan removes that manual work and the two
contract gaps that blocked the "riser + downlifter on one track" and
"verify FX state after set" workflows.

## Scope decision (what this plan does NOT do)

- `generate_psytrance` composes the **score only** (guide §4 grammar: notes
  into clips on the caller's palette tracks). It does NOT build the kit
  (sample selection/loading) or the production stack (FX chains, LFOs,
  automation — guide §5). Those stay deliberate MCP steps per palette; score
  generation is the "1–2 calls instead of a JS scoring layer" item from the
  handoff.
- The P2.4 contract-consistency pass (`laneName`/`lane`, `time`/`beat`) is
  explicitly OUT of scope (own plan; small and shippable alone).
- New automation-lane default, `render_and_autoscale`, `prune_empty_clips`,
  cluster-lib defaults, MCP cheat sheet = P2/P3 backlog, not here.

## Workstreams

- **W1**: Psytrance score generator — engine class + command + RPC + MCP tool
  (`generate_psytrance`). Largest; includes a tiny shared scale-helper
  extraction.
- **W2**: Multi-sampler note routing (per-slot key range on one track) —
  audio-engine change in `TrackFXSlot`/`Track` + command + MCP tools.
- **W3**: `get_internal_fx_param` read-back — MCP tool only (reads the
  existing ValueTree param_N storage; no engine change).

W1/W2/W3 touch disjoint files (W1: new engine files + registerGenerateTools +
RPC router; W2: `TrackFXSlot.h`/`Track.cpp` + AudioEngineCommands_Fx +
McpTools_Sampler; W3: McpTools_FxSlot only). No shared headers besides the
`IDs` namespace (2 new ids) and `McpTools_Private.h` (no-op). They can be
implemented in any order; W3 first (smallest) is a good confidence task.

---

## Goal

Ship the three P1 workflow items: one MCP call that writes the complete
key-disciplined psytrance score (guide §4 grammar) onto the caller's palette
tracks; per-slot sampler key ranges so riser+downlifter (and any FX bank)
share one track; and FX param read-back to verify `set_internal_fx_param`
state without a render.

## Success Gates (all must pass to declare done)

### W1 — generate_psytrance
- [ ] **G1.1 Note grammar unit test** (`PsytranceGeneratorTests` suite, new
      `tests/unit/engine/psytrance_generator_test.cpp`): for an F-minor run
      with the canonical 7-section layout, every generated note's pitch class
      is in {F,G,Ab,Bb,C,Db,Eb} (key discipline); kick present from `build`
      onward and absent inside `mini`/`breakdown`; bass notes sit at
      `beat+0.5` (offbeat, swing backbeat) with octave +12 only in
      `mainB`/`finale`; arp 16ths with the +12 glint on the last 16th of each
      beat and only in `mainA`/`mainB`/`finale`; stabs at `bar*4+1` in
      `mainA`+; pads every bar across the whole arrangement; breakdown melody
      (durations ≥ 3 beats) inside `breakdown`; riser notes confined to the
      8 beats before a section boundary; downlifter within 4–8 beats before
      each drop. All pitches computed via the shared scale helper (gate below).
- [ ] **G1.2 Determinism + seed**: same params + same seed ⇒ byte-identical
      note sets; different seeds ⇒ different (with density>0 rolls present).
- [ ] **G1.3 Command round-trip** (`PsytranceGeneratorCommand` engine test):
      `AudioEngineCommands::generatePsytrance` on a 5–8 track palette writes
      one clip per mapped role on the RIGHT track (clip start 0, length =
      totalBeats), note starts are clip-local (no absolute-beat misplay —
      guide trap §9.1), all inside ONE undo transaction (`undo()` once
      removes every clip/note it created), and returns the compact summary
      (role/trackId/clipId/noteCount — no note payloads).
- [ ] **G1.4 MCP loopback** (`McpCoverageTest`-style, in
      `tests/integration/mcp/mcp_coverage_test.cpp` or a new suite):
      `generate_psytrance` round-trips over `TransportLoopback`; un-mapped
      optional roles (riser/down) are reported as skipped, not errors; bad
      `paletteTrackIds` (track id out of range / non-numeric) returns a
      tool-named error (`isError=true`); response text is small (< ~4 KB —
      output-guard discipline, B6).
- [ ] **G1.5 RPC parity**: `composition.generatePsytrance` exists in
      `src/frontend/router/Router_Composition.cpp` (pattern:
      `generatePhrase`) and one RPC-router test round-trips it (may live in
      `rpc_surface_test.cpp` or the generator test).
- [ ] **G1.6 Shared scale helper**: `PhraseGenerator::scaleDegreeToPitch(
      rootMidi, scaleModeIndex, degree, octave)` exists and the MCP
      `scale_note` tool now calls it (identical behavior — the current tool
      inlines the same math); the PsytranceGenerator uses it too. No
      duplicated interval math survives (grep proves single implementation).
- [ ] **G1.7 Build + suite**: `cmake --build build --config Debug` succeeds;
      `build/Debug/hdaw_tests.exe` runs with the new suites green and no
      regressions in `PsytranceComposition.*`, `InternalFx.*`,
      `McpCoverageTest.*`.

### W2 — multi-sampler key-range routing
- [ ] **G2.1 Two-sampler partition render test**: one track, slot0 sampler
      key range [36,50], slot1 sampler key range [51,80], two samples with
      distinct spectral content, two clips each holding notes only in one
      range → a short offline render (the stress-test render/measure pattern)
      proves BOTH sounds are present (per-slot isolation render or carved
      frequency bands: RMS in the other slot's occupied band non-silent).
      Baseline: before this change slot1 is dead (first sampler clears the
      MIDI buffer + `buffer.clear()`).
- [ ] **G2.2 Rebuild restore (Gate 1/10)**: set key ranges → `drainPendingRoutingRebuild()`
      (or `rebuildRoutingGraph`) → assert on the LIVE processor
      (`getMainProcessor()->getTrack(t)->getFXChain()[slot]` sampler) that
      the ranges are still applied and the partition still renders both
      sounds. Live-processor assert, not ReadModel-only.
- [ ] **G2.3 Regression**: a single full-range sampler on a track (today's
      only supported config) behaves byte-identically — the existing sampler
      suite (`InternalFx.*`, sampler tests) stays green with no changes to
      their expectations.
- [ ] **G2.4 Audio-thread safety (Gate 3)**: the new sampler process path
      performs no heap allocation, mutex, or I/O (scratch `MidiBuffer`/
      `AudioBuffer` are members sized in `prepare()`); reviewed in the diff
      AND exercised by the render test under repeated blocks.
- [ ] **G2.5 MCP surface**: `set_sampler_key_range {trackId, slotIndex,
      keyLow, keyHigh}` validates (0..127 or -1; errors name the tool),
      round-trips via loopback, and `sampler_get_state` reports
      `keyRangeLow`/`keyRangeHigh`. RPC twin `sampler.setKeyRange` in
      `Router_Sampler.cpp` (pattern: the existing sampler routing there) with
      a router test.
- [ ] **G2.6 Build + suite**: Debug build succeeds; new + affected suites
      green (mcp sampler/fx, existing sampler engine suites).

### W3 — get_internal_fx_param
- [x] **G3.1 Round-trip test** (`McpCoverageTest` addition): after
      `set_internal_fx_param` (eq slot: freq 3600; compressor: threshold −18,
      ratio 4) `get_internal_fx_param` returns those exact real-unit values;
      an untouched param returns `defaultValue` (from the defs table); works
      for sampler env params (param_0..3 read back).
      **PASS** — `McpCoverageTest.GetInternalFxParamReadsBackRealUnits` green.
      Implementation note: read path is `ReadModelImpl::getInternalFxParams`
      (existing canonical tree read — no new parsing, no DSP access); the
      tool error text names the tool (handoff B-note).
- [x] **G3.2 Validation**: out-of-range `paramIndex` and non-FX slot return
      tool-named errors (Gate 9).
      **PASS** — `McpCoverageTest.GetInternalFxParamValidation` green; the
      validator rejects empty/unknown slots via an empty defs table (covers
      plugin/none/untyped slots uniformly).
- [x] **G3.3 `list_fx_params` unchanged; `set_fx_param`/`set_internal_fx_param`
      behavior unchanged** (existing fx tests stay green); build succeeds.
      **PASS** — internal branch of `list_fx_params` now ALSO returns the
      current `value` (additive; symmetric with its plugin branch — the one
      intentional delta, closing the metadata-only gap at the discovery call
      itself); existing `FilterFxParamsRealUnitsAndRebuildSurvival` untouched
      and green. Full `McpCoverageTest.*` + `InternalFx.*` = 70/70 green on
      the fast build (below).

## Dependency Map

- **Blast radius (from codebase-memory + grep; graph gaps noted):**
  - `TrackFXSlot::process` (W2): graph shows 0 callers — the edges are hidden
    by the `slot->process(...)` indirect call; grep confirms the single caller
    `Track::processBlock` (`src/engine/Track.cpp:536`). Audio-thread hot path.
  - `TrackFXSlot::loadSamplerState` (W2 restore path): graph shows 0 callers;
    grep confirms `Track.cpp:226` (`addFxSlot` during rebuild) + `prepare`.
    Graph index is stale for these edges → treated as grep-verified.
  - `AudioEngineCommands::setFxSlotParam` (W3 read source): graph shows
    inbound = McpTools_FxSlot; grep adds Router_* callers. The param values
    live as `param_<idx>` ValueTree properties on the FX_SLOT
    (`AudioEngineCommands_Fx.cpp:285`, read by `TrackFXSlot::loadParamsFromTree`
    and `McpTools_Sampler.cpp` sampler_get_state env read).
- **Upstream (callers):** W1 command ← Router_Composition (new RPC) + MCP
  tool (new); W2 command ← McpTools_Sampler (new tool) + Router_Sampler (new
  RPC); W3 ← McpTools_FxSlot (new tool).
- **Downstream (consumers):** W2 audio: `Track::processBlock` → graph →
  mix; W3: MCP clients; W1: MCP composer + frontend RPC clients.
- **God nodes in scope:** `TrackFXSlot` is the W2 hub (process/prepare/
  loadSamplerState/LoadParamsFromTree all in one header); change is confined
  to the sampler branch of `process()` + two copied members + one tree read —
  no signature changes, so external callers are unaffected. `PhraseGenerator`
  gains one small static (scaleDegreeToPitch) — additive.
- **Community boundaries crossed:** engine (W1/W2), MCP layer (all), frontend
  RPC router (W1/W2). Contract across each: RPC router ↔ MCP tool both call
  the same AudioEngineCommands entry point (parity rule).
- **Projections affected:** W1: ReadModel (clips appear — normal delta path,
  clip add = incremental delta, no fullSync); audio graph (clip add →
  incremental routing path, NOT `rebuildRoutingGraph` per clip — one clip per
  role, 8 max, single batch-style transaction). W2: ReadModel
  (`FxSlotSnapshot` field? — sampler state reads come from the ValueTree;
  getFxSlots shape unchanged); audio graph (rebuild path must restore).
  W3: none (read-only).
- **SPSC paths touched:** W2 only — the sampler MIDI partition runs on the
  audio thread; new members are written only on the message thread via
  prepare/rebuild and read on the audio thread (same discipline as existing
  sampler state; see Pitfall Gate 3/13).
- **Path integrity:** W1 note→clip→ReadModel path and W2 command→tree→
  listener→rebuild→processor path were verified in existing code
  (`addMidiClip`/`setSamplerMode`); new wiring gets a loopback test (gates).

## Pitfall Gates Triggered

| Gate | Trigger | How addressed |
|---|---|---|
| **1/10** Rebuild restore | W2 adds sampler state (`keyRangeLow/High`) | Written as ValueTree props, read in the rebuild path where sampler state is restored (`loadSamplerState`/`prepare`); G2.2 asserts the LIVE processor after rebuild, not the ReadModel. |
| **3** Audio-thread safety | W2 modifies `TrackFXSlot::process` (audio thread) | No allocation/lock/I-O in the new path; scratch `MidiBuffer`/`AudioBuffer` are class members sized in `prepare()`; note partitioning is a single pre-allocated pass. G2.4 reviews + stress-renders. |
| **9** ID/validation | New props + new MCP params | `keyLow/keyHigh` validated 0..127 or −1; `paramIndex` validated against the defs table (existing `set_internal_fx_param` pattern); W1 role/track maps validated with tool-named errors. |
| **13** DSP-state writes | W3 reads FX param state | Reads the ValueTree `param_<idx>` (source of truth) — no DSP access, no lock needed. W2 key ranges are NOT live-mutated DSP state (write via property + rebuild, like `setSamplerMode`); no automation target added. |
| **2** Unimplemented path | Three new tools | Every tool gets a loopback test asserting the observable effect (clips on right tracks / both sounds audible / param value round-trip). |
| **15** Stale binaries | C++ changes | Gate keeps: verify `build/Debug/hdaw_tests.exe` timestamp after build; never trust `build/Release`. |
| **11/12** Pump / graph mutation | W1 writes clips, W2 rebuilds FX | Same as existing clip-add/`setSamplerMode` paths (message-thread or guarded rebuild); no new entry points, no direct graph mutation added. `drainPendingRoutingRebuild()` used in tests. |

Not triggered: 4 (no new executables/config — new .cpp added to CMake engine
source list only, W1), 5/8 (no frontend UI), 6 (no new interactive-only
behavior), 7 (no windows), 14/16 (no proxy/plugin lifecycle changes).

## Steps (subagent dispatch units — one task per unit)

### Unit A (W3 — smallest, do first, confidence task)
1. `src/mcp/McpTools_FxSlot.cpp`: add `get_internal_fx_param {trackId,
   slotIndex}` — validate slot exists and is internal FX (same guards as
   `set_internal_fx_param`); for each def in
   `HDAW::TrackFXSlot::getParamDefsForType(type)` read
   `slotTree.getProperty("param_<idx>", defaultValue)` from the FX_SLOT tree
   (same tree access `set_internal_fx_param`→`setFxSlotParam` writes);
   return `{params:[{index,name,value,min,max,defaultValue}]}` compact JSON.
   No engine change.
2. Add `GetInternalFxParam` coverage test (G3.1/G3.2) to
   `tests/integration/mcp/mcp_coverage_test.cpp`.
3. Run `cmake --build build --config Debug`; run the mcp coverage + fx tests.

### Unit B (W1 — engine generator + extraction)
1. Extract `PhraseGenerator::scaleDegreeToPitch(int rootMidi, int
   scaleModeIndex, int degree, int octave)` from the inline math currently in
   `McpTools_CompositionPattern.cpp scale_note` (keep semantics identical: the
   wrapped-degree + floor octave-shift math); update `scale_note` to call it.
2. New `src/engine/PsytranceGenerator.{h,cpp}` — pure note generation:
   - Params: `{paletteTrackIds{role→trackId}, sections[{name,start,end}],
     keyRoot, scaleMode, density(0..1), seed}`;
     roles = kick, bass, hat, arp, stab, pad, riser, down (+ clap defaults to
     the hat track if not mapped; melody handled inside the arp clip).
   - Output: per-role `{role, notes[{startBeat(absolute), pitch, velocity,
     durationBeats}]}` derived from guide §4 templates + section schedule
     (see G1.1 for the exact rules; progressions default i–VII–VI–VII (A) /
     VI–VII–i–i (B) in degrees, dark-first per DarkForestV5; optional
     `progressionA/B` degree overrides).
   - All pitches via `scaleDegreeToPitch` (key discipline, guide §9.7).
   - Seeded RNG (`std::mt19937`), deterministic; density scales the extra
     16th rolls / 8-bar roll placement.
3. `AudioEngineCommands::generatePsytrance(PsytranceParams) → Result` in
   `src/engine/AudioEngineCommands_Composition.cpp` (pattern:
   `addInstrumentPart`): validate track ids; ONE `beginTransaction("Generate
   psytrance")`/`endTransaction()`; one clip per mapped role at start 0,
   length = totalBeats (from last section end), notes written with
   **clip-local** starts (= absolute beats, clip at 0), undo-capable, one
   `drainPendingRoutingRebuild`.
4. Add `PsytranceGeneratorTests` (G1.1/G1.2) + `PsytranceGeneratorCommand`
   (G1.3); CMake: add `src/engine/PsytranceGenerator.cpp` + the new test file
   (both source lists).
5. RPC twin `composition.generatePsytrance` in
   `src/frontend/router/Router_Composition.cpp` + router test (G1.5).
6. MCP tool `generate_psytrance` in `src/mcp/McpTools_CompositionGenerate.cpp`
   (compaction: return summary only); loopback test (G1.4).
7. Build + run: generator suites, `PsytranceComposition.*`, mcp coverage.

### Unit C (W2 — multi-sampler key-range routing)
1. `src/model/ProjectModel.h` IDs: `DECLARE_ID(keyRangeLow)`,
   `DECLARE_ID(keyRangeHigh)`.
2. `src/engine/AudioEngineCommands_Fx.cpp`: `setSamplerKeyRange(track, slot,
   low, high)` — validate slot is sampler, write props under undo, rebuild
   track FX (follow `setSamplerMode`/`setSamplerProperty` rebuild pattern).
3. `src/engine/TrackFXSlot.h`:
   - Copy `keyRangeLow_/keyRangeHigh_` (defaults −1) from the slot tree in
     the sampler restore path (`loadSamplerState` and/or `prepare`, wherever
     sample state is staged — the same place `transpose`/`mode` are read) so
     rebuild restores them (Gate 1/10).
   - `process()` sampler branch: partial = range configured. Partial: partition
     `midiMessages` into in-range/out-of-range via member scratch buffers
     (sized in `prepare()`); render in-range into a member scratch
     `AudioBuffer`; accumulate into the output buffer; replace `midiMessages`
     with the out-of-range remainder (later slots see the notes). Full-range:
     EXACT current behavior (`buffer.clear()` + render + `midiMessages.clear()`).
   - Document contract in a code comment: at most one full-range sampler per
     track, and it must be the FIRST sampler slot; partial slots after it
     accumulate; a track of all-partial samplers needs the buffer cleared
     once before the chain (Track-level, see next).
4. `src/engine/Track.cpp` `processBlock`: when the chain's sampler
   configuration is non-trivial (≥2 sampler slots OR any partial slot),
   clear the buffer once before the FX-chain loop so partial slots start from
   a clean slate; leave today's single-full-range-sampler topology untouched
   (slot clears internally as today — no behavior change).
5. Tests (G2.1–G2.4): new suite `tests/unit/engine/sampler_key_range_test.cpp`
   — partition render (both sounds), rebuild restore asserting the LIVE
   processor, regression (single full-range sampler unchanged), repeated-block
   render for audio-thread safety. Use the stress-test engine+render pattern.
6. MCP: `set_sampler_key_range` in `src/mcp/McpTools_Sampler.cpp` +
   `keyRangeLow/High` in `sampler_get_state`; RPC twin in
   `src/frontend/router/Router_Sampler.cpp`; loopback test (G2.5).
7. Build + run: new suite, `InternalFx.*`, sampler suites, mcp sampler/fx.

### Unit D (final verification — orchestrator, not subagent)
1. Build Debug; run `build/Debug/hdaw_tests.exe` fully (or the union of
   affected suites + mcp) — gate evidence.
2. Grep the diff: no duplicated interval math, no raw hex, no `DBG`, no new
   rebuild per-clip loops, outputs compact (B6).
3. Refresh the knowledge graph (`index_repository` fast) since W1 adds new
   files/classes/RPC methods (completion contract §8).
4. Update `docs/psytrance-composition-guide.md` §4 with the
   `generate_psytrance` one-call alternative (guide fidelity) and note the
   launcher fix reference per handoff §6.3 — deferred to the same PR if the
   user prefers prose-only.

## Risks / notes

- **W2 semantics are the riskiest change** (audio-thread hot path). The
  backward-compat gate (G2.3) is the shield: single sampler behavior must be
  byte-identical. The mixed full-range-after-partial topology is documented
  as unsupported (comment + test coverage of supported ones) rather than
  engineered against.
- The graph index is stale on `TrackFXSlot` edges (indirect calls) — plan and
  tests use grep-verified call sites; do NOT rely on graph "no callers" to
  conclude anything.
- W1 output discipline: never return note arrays from the tool (B6); the
  composer reads detail on demand with `get_clip`.
- Clip note ceiling 8192 (`MidiClipProcessor`): the generator must stay well
  under per clip (2,686 total in the reference track across 8 clips), and the
  command reports `skipped` if a ceiling is hit (guide §9.1).

## Build-infrastructure session note (2026-08-30, Unit A)

The slow `cmake --build build --config Debug` (VS generator) is replaced for
iteration by a dedicated Ninja dir. Recorded here so later units use it:

- **`build-ninja/`** — Ninja single-config **Release** + sccache + PCH, used
  for the gtest loop. Configure once (reuses the FetchContent clones in
  `build/` — no network):
  `cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64 -DHDAW_BUILD_TESTS=ON
  ... -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
  -DFETCHCONTENT_SOURCE_DIR_{JUCE,CLAP-JUCE-EXTENSIONS,AUBIO,SOUNDTOUCH,GOOGLETEST}=<existing build/ clones>`.
  Full fresh build ≈ 10 min; incremental compiles are a few seconds.
  Binary: `build-ninja/hdaw_tests.exe`.
- **`build-fast.bat` patched** (3 small optimizations): (a) auto-bootstraps
  vcvars64 when `cl` isn't on PATH (probes VS 18/2022 roots; note `%VSB%`
  needs **delayed expansion** `!VSB!` inside the `if(...)` block or it
  expands empty at parse time — that bug cost one cycle); (b) `HDAW_BUILD_DIR`
  env override; (c) generator-aware build step — Ninja omits the msbuild
  `-- /m /v:minimal` tail. `build/` (VS) is untouched: E2E depends on
  `build\Debug\HDAW.exe` (playwright.config.ts) so it stays the canonical
  multi-config dir. `frontend/build.bat` will refresh `build/Debug` (now
  stale vs the McpTools_FxSlot change) on its next run.
- **CMakeLists.txt** (2 small, safe edits): `LANGUAGES C CXX` (aubio needs
  the C rule materialized at project-init under the Ninja generator — C was
  previously enabled lazily by FindOpenMP and the CMake 4.3 Ninja generator
  failed generate with `CMAKE_C_COMPILE_OBJECT` unset); version-guarded
  `cmake_policy(SET CMP0141 NEW)` so `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`
  is honored. VS behavior unchanged (var unset there → default
  ProgramDatabase). "Use Release not RelWithDebInfo" was the empirical
  resolution of sccache C1041 //Fd pdb contention: RelWithDebInfo leaves
  per-target pdb paths in every compile command which sccache chokes on
  (C1041 across parallel aubio .c compiles, /Fd-open failures in fetched
  subprojects); Release has not been observed to fail (477 targets, clean).
- **.gitignore**: added `build-ninja/`.