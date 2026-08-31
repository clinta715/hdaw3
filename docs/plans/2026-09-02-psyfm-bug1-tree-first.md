# Plan: Bug 1 fix — psy_fm MCP/frontend tools tree-first + mod matrix persistence

> Handoff: `docs/handoffs/2026-09-01-psyfm-bugs-handoff.md` Bug 1 (HIGH).
> Loaded skill: hdaw-guard. Environment note: no subagent task tool in this
> session — orchestrator implements directly with full gate discipline.

## Goal

Make the psy_fm MCP tools and frontend router work in ANY engine state
(deviceless headless included) by routing configuration through the ValueTree
command layer (the `set_internal_fx_param` pattern), and persist the PsyFm
modulation matrix in the FX-slot tree so it survives `rebuildRoutingGraph()`.

## Root cause (verified in code, 2026-09-02 session)

- `MainAudioProcessor::routingManager` is created **only in `prepareToPlay`**
  (`MainAudioProcessor.cpp:138`) and nulled in `releaseResources()` (`:180`).
  Deviceless engine (no audio device, or device lost mid-session) ⇒ null ⇒
  every `getMainProcessor()->getTrack(ti)` consumer fails "track not found".
- The 4 psy_fm MCP tools + `Router_PsyFm.cpp` + `McpTools_FmSynth.cpp` +
  Sampler/MidiFx/Automation/FxPreset/Send/Settings/ProjectSaveLoad/ExportTool
  MCP files all use that live path. Exports are immune (ExportManager builds
  its own `renderGraph`, `ExportManager.cpp:225`).
- Additionally: preset/matrix mutations were **engine-RAM-only** — lost on any
  rebuild (Gate 1/10 violation), and `PsyFmEngine::setModMatrix` assigns
  `matrix_` (a vector-holding object) with no lock while the audio thread
  reads it in `render()` (`PsyFmEngine.cpp:231`) — Gate 3/13 race.
- Non-repro evidence: this session's engine HAS a working output-only device
  (lesson 17 retry), so routingManager exists and the tools "work" — the
  2026-09-01 session's engine was deviceless. The fix removes the dependency
  on device state entirely.

## Success Gates (all must pass with evidence)

- [x] G1: Preset/mod-route writes go through the ValueTree command layer and
      succeed with NO live processor (deviceless path). Test: command-layer
      calls with no prepareToPlay → FX-slot tree carries `param_N` +
      `psyFmMatrix` properties.
- [x] G2 (Gate 1/10): matrix restore on rebuild — build Track FX chain from a
      slot tree carrying `psyFmMatrix`, assert the LIVE `TrackFXSlot`'s
      `psyFmEngine()->getModMatrix().getRoutes()` matches (live processor, not
      ReadModel).
- [x] G3 (Gate 3/13): matrix swap is lock-guarded; stress test renders blocks
      in a loop while swapping matrices concurrently — no crash, no torn read
      (render uses ScopedTryLock, skips matrix update on contention).
- [x] G4: `psy_fm_get_analysis` returns `{"live":false,...}` honestly when no
      live engine; live data when present.
- [x] G5: `psy_fm_set_mod_route` upserts (add-or-update contract) instead of
      silently replacing the whole matrix.
- [x] G6: All existing PsyFm tests pass (25) + new tests pass; full Debug
      build succeeds; binary freshness verified (Gate 15 — .obj timestamp).
- [x] G7: Docs updated (handoff Bug 1 → FIXED, pitfalls entry), and
      codebase-memory index refreshed (structural change: new header, new
      commands, new tree properties).

## Dependency Map

- Upstream callers of the changed surface: `McpTools_PsyFm.cpp` (4 tools),
  `Router_PsyFm.cpp` (getAnalysis/loadPreset), `AudioEngine.cpp`
  FX_SLOT property listener (high-degree hub — surgical branch only).
- Downstream consumers: `PsyFmEngine::render` (reads matrix), saved `.hdaw`
  files (new properties must be optional — absent = empty matrix, backward
  compatible), frontend `PsyFmEditor.tsx` (calls `psy_fm.getAnalysis` /
  `psy_fm.loadPreset` — schemas unchanged).
- Projections: ReadModel untouched (raw FX-slot properties are not projected;
  FX-slot changes already fullSync per project conventions).
- SPSC / cross-thread: matrix crosses message→audio thread ⇒ Gate 3/13 lock.
- God nodes touched: `AudioEngine::valueTreePropertyChanged` (add one branch,
  mirror the existing `param_` structure), `Track::rebuildFXChain` (one extra
  restore call in the generic slot branch).
- Shared duplication removed: preset tables currently duplicated verbatim in
  `McpTools_PsyFm.cpp` and `Router_PsyFm.cpp` → move to one engine header.

## Pitfall Gates Triggered

| Gate | Addressing |
|------|-----------|
| 1/10 State-restore on rebuild | `loadPsyFmStateFromTree` in `Track::rebuildFXChain` generic branch; live-processor test (G2) |
| 2 Silent no-op path | listener branch for `psyFmMatrix`/`psyFmSweepRate` forwards to `Track::setFxSlotPsyFm*`; covered by tests |
| 3/13 Audio-thread safety | `juce::SpinLock matrixLock_` in PsyFmEngine; render reads under ScopedTryLock (skip on contention — base params still valid); writes (message thread) under ScopedLock. Track setters hold `stateLock` (mirror `Track::setFxSlotInternalParam`, lesson 13) |
| 9 Null guards | All new paths guard slot/track/engine pointers; tool layer keeps ReadModel slot validation |
| 15 Stale binaries | verify .obj timestamps after build; run the built binary's tests |
| Lesson 2 (`setProperty` no-op on unchanged) | set_mod_route writes the merged route string; when the string is unchanged the tree is already correct — live engine can only be stale if it missed the earlier change, in which case prepareToPlay/rebuild re-reads the tree (acceptable; noted in code) |

## Tree representation

- `psyFmMatrix` (string): `;`-separated `source:dest:depth` routes. Names match
  the MCP enum strings; dest also allows `ratioSweepRate` (used by riser).
  Absent/empty = no routes. Stable text format — no enum-ordinal coupling.
- `psyFmSweepRate` (double): `PsyFmModSourcePool::ratioSweepLFORateHz`.
- Params 0–32 continue to use `param_N` (existing restore via
  `loadParamsFromTree` — envelope layout `7 + op*4 + {A,D,S,R}` confirmed).

## Steps

1. `src/engine/PsyFmState.h` (NEW, header-only): route codec
   (`encodeRoutes`/`decodeRoutes`) + preset table (`findPreset`, per-preset
   ratios/feedback/env[6][4]/level/algorithm/sweep/matrix).
2. `src/engine/PsyFmEngine.{h,cpp}`: `matrixLock_` SpinLock; `setModMatrix`
   takes it; render applies under ScopedTryLock with base-param fallback.
3. `src/engine/TrackFXSlot.h`: `applyModMatrixFromString`,
   `applySweepRate`, `loadPsyFmStateFromTree`.
4. `src/engine/Track.{h,cpp}`: `setFxSlotPsyFmMatrix`, `setFxSlotPsyFmSweepRate`
   (stateLock), restore call in `rebuildFXChain` generic branch.
5. `src/engine/AudioEngineCommands.{h, _Fx.cpp}`: `setFxSlotPsyFmPreset`,
   `setFxSlotPsyFmModRoutes`, `clearFxSlotPsyFmModRoutes` (undo-managed tree
   writes, no rebuildTrackFX — listener pushes live state).
6. `src/engine/AudioEngine.cpp`: FX_SLOT listener branch for the two new
   properties (mirrors `param_` resolution).
7. `src/mcp/McpTools_PsyFm.cpp`: rewrite all 4 tools tree-first;
   `get_analysis` gains `live` flag + honest zeros.
8. `src/frontend/router/Router_PsyFm.cpp`: loadPreset/getAnalysis via the same
   command layer / live-optional analysis.
9. Tests in `tests/unit/engine/psyfm_test.cpp`: codec round-trip; preset table
   sanity; live-matrix restore after rebuild (G2); concurrent
   render+swap stress (G3); upsert semantics (G5).
10. Build (`cmake --build build --config Debug`), run `PsyFm*` + adjacent
    suites, verify binary freshness, update handoff/pitfalls, refresh
    codebase-memory index.
