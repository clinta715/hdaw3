# MCP ↔ RPC parity retrofit (2026-09-21)

**Status:** in progress — slice 1 (PsyFm, the first confirmed semantic gap) shipped.
**Owner:** agent session 2026-09-21. **Risk:** low (no engine/DSP/render/playback code).
**Related:** `docs/plans/2026-09-21-device-param-map.md` (same parity rule, worked
example `src/common/DeviceParamMap.cpp`).

## Why

`AGENTS.md` → "Feature parity: MCP + RPC" states two parity contracts:

- **MCP parity** — anything a human can do in the UI must be reachable over MCP.
- **RPC parity** — *every MCP tool must also be reachable over the frontend JSON-RPC
  surface as `namespace.method`, dispatched by `frontend::dispatch`
  (`src/frontend/FrontendRouter.cpp`) into a `src/frontend/router/Router_<Domain>.cpp`
  handler, whether or not any UI control consumes it.*

The surfaces were built opportunistically (MCP first, RPC when a UI needed it), so the
second contract has drifted. Drift is invisible: an MCP tool that works, tests green, and
no UI path means nobody notices the missing RPC method — until an agent or the browser
frontend tries to call it.

## Audit method (semantic, not name-based)

A name comparison (`snake_case` MCP tool vs `camelCase` RPC method) is a *heuristic
screen only* and produces false positives: `add_track` vs `track.add` share no substring
yet map to the same command. The real join is the **shared command symbol**:

1. For each MCP tool, find the command-layer entry point it calls (e.g.
   `ProjectCommands::addTrack`, `AudioEngineCommands::setFxSlotPsyFmModRoute`).
2. For each `Router_*.cpp` handler, find the same.
3. A tool is a **genuine gap** when (a) no RPC method reaches its command entry point,
   and (b) it is not a UI-only affordance (browser/pool/job plumbing).

Heuristic screen (2026-09-21, `src/mcp/*` vs `src/frontend/router/*`): **286 MCP tools,
366 RPC methods, 152/286 without a name match** — mostly naming artifacts, so the screen
is only useful for prioritisation. The per-domain semantic pass is the actual work.

### Confirmed gaps found so far

| Domain | MCP tools | RPC methods | Gap |
| --- | --- | --- | --- |
| `psy_fm` | `psy_fm_set_mod_route`, `psy_fm_clear_mod_matrix`, `psy_fm_mod_matrix_debug`, `psy_fm_get_analysis`, `psy_fm_load_preset` | `getAnalysis`, `loadPreset` only | **3 tools unreachable** — fixed in this slice |

`Router_PsyFm` exposing 2 of 5 tools was the clearest case (a self-contained domain with a
single router file, and the missing methods are real editing operations, not plumbing).

### Full semantic audit (2026-09-21) — results

Every `src/mcp/McpTools_*.cpp` was joined against `src/frontend/router/Router_*.cpp` on the
**shared command symbol** (not names), and each candidate was then confirmed by reading the
tool body and the router handlers.

| Domain | Status | Evidence |
| --- | --- | --- |
| `device` | **clean** | slice 0 (`DeviceParamMap`) |
| `psy_fm` | **clean** | slice 1 |
| `matrix` | **clean** | slice 2 (`MatrixPresetService`) |
| `rave` (13 tools) | **clean** | 11 have direct `rave.*` routes; `rave_get_config`/`rave_set_config` are reachable as `settings.getRaveConfig`/`setRaveConfig` (`Router_Project.cpp`, sharing `RaveService::persistedConfigJson`) |
| `pool` | **clean** | `pool_list` maps to `pool.list`; the router even adds `pool.cleanup` (RPC is a superset) |
| fx capture | **partly** | `capture_fx_snapshot`/`swap_fx_snapshot` map to `audio.captureFxSnapshot`/`swapFxSnapshot`; `snapshot_project` maps to `read.snapshot`; **`get_fx_capture_status` has no route** |
| `song plan` / cells (18 tools) | **15 covered, 3 missing** | real gaps: the BATCH `set_cells` (`setCellRecipes` — the router had only the singular `setCellRecipe`), `audit_song_structure` (`auditSongStructure`), and `get_layer_handoffs` (the ledger was **write-only** over RPC: `project.setLayerHandoff` / `clearLayerHandoff` existed with no reader). `get_clip_provenance`, `set_layer_handoff` and `clear_layer_handoff` **do** have routes — see the correction note below |
| `tuning` (1 tool) | **missing** | `analyze_tuning` has no RPC route and there is no `tuning` namespace |

**Correction (same day):** the first pass of this table was produced with a *keyword* filter
over the `if (m == "..."` lines of the routers (`song|cell|reroll|brief|section`), which
produced false positives **exactly like the name screen this audit exists to replace**:
`getClipProvenance` contains none of those keywords, so it was reported missing when it has
a route, and the handoff *writes* live in `Router_Project.cpp`, not `Router_Composition.cpp`.
**Rule: join on the command symbol / method identifier**
(`rg 'setCellRecipes|setLayerHandoff|auditSongStructure' src/frontend`), **never on a keyword
filter.** The verified gap list before slice 3 was therefore 4 methods: `set_cells`,
`audit_song_structure`, `get_layer_handoffs`, `get_fx_capture_status` — all fixed in slice 3.

**Two corrections to this plan's own assumptions.** (a) The original candidate order named
`Router_Matrix` and `Router_Tuning` — **neither file exists**; this plan now uses the real
inventory. (b) The two named candidates `Rave` and `Pool` are **clean**: both were false
positives of the name screen, exactly the artefact class the audit method was written to
avoid. The real remaining gaps are **SongPlan (6)**, **`get_fx_capture_status` (1)** and
**Tuning (1)**.

**Implementation cost differs by gap** (this drives the slice order):

- **SongPlan — thin wiring.** All six missing methods' bodies already call `ProjectCommands`
directly (`setCellRecipes`, `getClipProvenance`, `setLayerHandoff`, `clearLayerHandoff`, plus
plan/cell getters), so the router methods need no extraction — only argument parsing +
payload shaping mirroring the MCP tools. **Highest value:** the missing batch call is
`set_cells`, the *batch* edit unit the performance rules tell agents to prefer over N
separate calls — currently unreachable from the browser frontend.
- **`get_fx_capture_status` — thin wiring**, but it is the *polling companion* of the
capture/matrix flows ("poll get_fx_capture_status to confirm"), so its absence leaves the
receipt contract half-exposed over RPC.
- **Tuning — needs extraction.** `analyze_tuning` is implemented as static helpers inside
`McpTools_Tuning.cpp` (`analyzeTuningText`/`analyzeTuningObject`, ~140 lines of spectral
analysis), not in the command layer, so parity-by-construction requires moving them to
`src/common/` first. It also participates in the async job registry (`McpJobs`, `wait:false`
+ `poll_job`) — and **`poll_job` is itself MCP-only**, so a Tuning slice must decide whether
to expose a generic `audio.pollJob` or document that each async domain has its own status
route (`rave.jobStatus`, `rave.trainingJobStatus`).

## Slice 1 — PsyFm (SHIPPED)

### Implementation

- **`src/common/PsyFmModMatrixView.{h,cpp}`** (new) — the canonical builder for the
  `mod_matrix_debug` payload, extracted from `McpTools_PsyFm.cpp`. Parity by
  construction: `McpTools_PsyFm.cpp` and `Router_PsyFm.cpp` now *both* call
  `HDAW::buildPsyFmModMatrixView(...)`, so the payload and its budget math cannot drift.
  Extracting (rather than re-implementing) was the point — `mod_matrix_debug` is ~150
  lines of budget/`apply()` simulation, exactly the kind of logic that diverges silently.
- **`src/frontend/router/Router_PsyFm.cpp`** — three methods added, matching the
  existing camelCase convention:
  - `psy_fm.setModRoute {trackIndex, slotIndex, source, dest, depth}` →
    `AudioEngineCommands::setFxSlotPsyFmModRoute`
  - `psy_fm.clearModMatrix {trackIndex, slotIndex}` →
    `AudioEngineCommands::clearFxSlotPsyFmModRoutes`
  - `psy_fm.modMatrixDebug {trackIndex, slotIndex}` → the shared view
- `src/CMakeLists.txt` / `tests/CMakeLists.txt` entries + `tests/unit/frontend/psy_fm_rpc_test.cpp`.

### Success gates

| # | Gate | Status |
| --- | --- | --- |
| G1 | `psy_fm.setModRoute` reaches the command layer (the debug view reports the route) | **PASS** |
| G2 | `psy_fm.clearModMatrix` drops every route | **PASS** |
| G3 | **Parity**: RPC and MCP payloads are byte-identical for the same state, *including* the `Op6Feedback` depth-budget scaling (`EXPECT_EQ(viaRpc, viaMcp)`) | **PASS** |
| G4 | Error surfaces mirror `getAnalysis`/`loadPreset`: unknown method, missing args, out-of-range slot, non-`psy_fm` slot | **PASS** |
| G5 | The extracted builder did not change MCP behaviour — `PsyFmModMatrixDebug.*` still green | **PASS** (7/7) |
| G6 | Registry/RPC-surface regression sweep unaffected | **PASS** (`ToolRegistry*:McpCoverage*:RpcSurface*:McpServer*:DeviceParams*:PsyFmRpcTest*` → 162/162, exit 0) |
| G7 | Build-graph correctness (suppressed regeneration) — `PsyFmModMatrixView` and `psy_fm_rpc_test` present in `build.ninja` after an explicit `cmake -S . -B build` | **PASS** |
| G8 | Blast radius: no `processBlock` / DSP / `RoutingManager` / render / playback / plugin-isolation file touched (only `src/common/`, `src/frontend/router/`, `tests/`, `CMakeLists.txt`) | **PASS** |

Evidence: `PsyFmRpcTest.*` 4/4 and `PsyFmModMatrixDebug.*` 7/7 (11/11 in 2 suites),
plus the 162-test sweep.

## Slice 2 — Matrix (SHIPPED 2026-09-21)

The Matrix domain had **no RPC route at all**: `list_matrix_presets` and
`apply_matrix_preset` lived entirely in `src/mcp/McpTools_Matrix.cpp` (751 lines of Qt
domain logic — sheet resolution + caching, morph chains, the four apply routes, the
override-ledger write, the deferred state capture) and `FrontendRpc.h` had no `matrix`
namespace. Confirmed by the semantic join (shared command symbols), not by name matching:
the only router touching matrix-shaped code was `Router_PsyFm`, which serves the psy_fm
mod matrix — a different domain.

### Implementation

- **`src/common/MatrixPresetService.{h,cpp}`** (new) — the extracted domain logic:
  presets-dir resolution + env-keyed cache, bounded sheet/morph/index-map parsing,
  `resolvePluginSlot`, parameter apply (`resolveLiveParamIndex` + 0..127 -> 0..1
  scaling), the deferred state capture, the `appliedParamOverrides` ledger write, and the
  loose `.syx`/`.mid` step-file route (validate every dump before queueing, then dumps +
  optional PC + a trailing CC125). Both surfaces call it, so payloads, routes, error text
  **and the JSON-RPC error class** are parity by construction.
- **`src/mcp/McpTools_Matrix.cpp`** — reduced to the two tool registrations (schemas and
  descriptions preserved) serializing the service payload as compact JSON.
- **`src/frontend/router/Router_Matrix.{h,cpp}`** (new) — `matrix.listPresets` /
  `matrix.applyPreset`, mirroring `Router_Device`'s shape.
- **`src/frontend/FrontendRpc.h`** — `method::Matrix = "matrix"` plus
  `frontend::allMethodNamespaces()` (the single source for the coverage gate below).
- `FrontendRouter.cpp` dispatch branch; `CMakeLists.txt` + `tests/CMakeLists.txt`
  entries; `tests/integration/mcp/matrix_presets_rpc_test.cpp` (new).

**Error-class rule** (documented in the service header, mirrored by both surfaces):
`-32602` invalid params — invalid engine id, **no sheet for a named engine**
(`Router_Device` precedent), unknown preset/morph-step id, unknown track/slot, non-plugin
slot, `appliesVia` with no parameter-level path, malformed preset SysEx, program out of
range; `-32603` environment — presets dir missing, sheet unreadable / invalid JSON /
unsupported schema, index-map unreadable, missing or invalid `.syx` step file,
engine-command failure.

### Success gates

| # | Gate | Status |
| --- | --- | --- |
| S1 | MCP behaviour unchanged by the extraction — `MatrixPresetsTest.*` | **PASS** (12/12) |
| S2 | `matrix.listPresets` payload byte-identical to the MCP payload (`EXPECT_EQ`) | **PASS** |
| S3 | `matrix.applyPreset` accounting payload byte-identical (applied/skipped/unmapped) | **PASS** |
| S4 | Failure text **and** JSON-RPC class match the MCP text (`-32602` argument; `-32603` environment incl. the resolved `3-4` pair path) | **PASS** |
| S5 | Unknown sub-method is a clean `-32601` | **PASS** |
| S6 | Namespace-coverage gate (backlog item 2) — every `method::` constant has a dispatch branch | **PASS** (2/2) |
| S7 | Registry/RPC-surface sweep unaffected | **PASS** |
| S8 | Build graph — the new files appear in `build.ninja` after an explicit reconfigure (suppressed regeneration) | **PASS** |
| S9 | Blast radius — no `processBlock` / DSP / `RoutingManager` / render / playback / plugin-isolation file touched | **PASS** |

Evidence: `MatrixPresetsTest.*` 12/12, `MatrixRpcParityTest.*` 5/5,
`RpcNamespaceCoverage.*` 2/2 — 19/19 in 3 suites.

Behavioural note: the `.syx` step-file route's **success** payload changed from the
`load_nord_bank` loader's prose line to the structured
`{queued, route:"file", bytes, program, capturedToTree, captureDeferred}` the domain's
other routes use — required for a surface-neutral payload. The error path is unchanged
(`MatrixPresetsTest.MorphSysexAndFileDispatch` still pins it). `mcp::runNordBankFile`
itself is untouched and still serves `load_nord_bank`; de-duplicating it with the
service's file route is item 4 below.

## Slice 3 — song plan / cells + capture receipt (SHIPPED 2026-09-21)

Four MCP tools had no RPC route (see the corrected audit table):

| New RPC method | MCP twin | Why it mattered |
| --- | --- | --- |
| `composition.setCellRecipes` | `set_cells` | The **batch** cell write: `ProjectCommands::setCellRecipes` = ONE command, one undo unit, one message-loop tick. The router had only the singular `setCellRecipe`, so the batch path the performance rules require was unreachable from the frontend |
| `composition.auditSongStructure` | `audit_song_structure` | The Mix Verifier's arrangement-variety gates (boredom spans, drop backbeats, first-drop motif) |
| `composition.getLayerHandoffs` | `get_layer_handoffs` | The ledger was **write-only** over RPC — `project.setLayerHandoff`/`clearLayerHandoff` existed but nothing could read the evidence back |
| `audio.getFxCaptureStatus` | `get_fx_capture_status` | The polling companion every capture/preset flow points at ("poll get_fx_capture_status to confirm") |

### Implementation

- **`src/common/SongPlanView.{h,cpp}`** (new) — the single shaping for
  `layerHandoffJson`, `layerHandoffsJson` (whole ledger or one track, `hasHandoff`
  detection, JSON-or-string `modulation`/`verify`) and `structureAuditJson`. The MCP
  side now calls these too, so payloads cannot drift; the previous `mcp::structureAuditJson`
  is gone (its unqualified call became **ambiguous via ADL** once the `HDAW::` twin
  existed — a real compile error the build caught, resolved by qualifying both call sites).
- **`src/common/FxCaptureStatus.{h,cpp}`** (new) — `readFxCaptureStatus(slotTree)`
  returns {status, stateBytes, capturedAtMs, hasPluginState}; the MCP tool keeps its
  exact prose (`status=… stateBytes=… capturedAtMs=… hasPluginState=…`, pinned by
  `MatrixPresetsTest`) while formatting it from the shared read, and the RPC returns the
  same four values as JSON.
- **`Router_Composition.cpp`** — `setCellRecipes` + `getLayerHandoffs` +
  `auditSongStructure`. **`Router_Audio.cpp`** — `getFxCaptureStatus`.
- Argument naming follows each namespace's existing convention (`composition` uses
  `trackId`, `project`/`audio` use `trackIndex`), which is why the error *text* differs
  between surfaces for an out-of-range track while the verdict does not.
- Documented asymmetry: the handoff **writes** stay in `project.*` (they need only
  commands), the **read** lives in `composition.getLayerHandoffs` (it needs the engine's
  track tree, and `dispatchProject` receives only `ProjectCommands&`).

### Success gates

| # | Gate | Status |
| --- | --- | --- |
| T1 | `composition.setCellRecipes` payload identical to `set_cells` (incl. a partial-failure batch: per-recipe `ok`/`error`, batch not aborted) | **PASS** |
| T2 | `composition.auditSongStructure` payload identical to `audit_song_structure`, with and without a plan | **PASS** |
| T3 | `composition.getLayerHandoffs` identical to `get_layer_handoffs` — empty ledger, after a real `project.setLayerHandoff` write (nested `modulation`/`verify` survive as JSON), and single-track vs all-tracks | **PASS** |
| T4 | `audio.getFxCaptureStatus` returns the same four values the MCP prose reports | **PASS** |
| T5 | Argument failures are clean `-32602` on both surfaces | **PASS** |
| T6 | MCP behaviour unchanged (prose pinned, shared builders used) — `McpCoverageTest`, `McpServer`, `MatrixPresetsTest`, `ApplyPreset*`, `workflow_pack*`, `SongPlan.*`, `SongStructureAudit.*` | **PASS** |
| T7 | Registry/RPC-surface sweep unaffected | **PASS** (216 tests / 12 suites in the combined run) |
| T8 | Build graph — the three new files present in `build.ninja` after an explicit reconfigure | **PASS** |
| T9 | Blast radius — no `processBlock` / DSP / `RoutingManager` / render / playback / plugin-isolation file touched | **PASS** |

Evidence: `SongPlanRpcTest.*` 6/6 (new), and the combined sweep 216/216 across
`ToolRegistry*`, `McpCoverage*`, `RpcSurface*`, `McpServer*`, `DeviceParams*`,
`PsyFmRpcTest*`, `MatrixPresetsTest*`, `ApplyPreset*`, `workflow_pack*`, `SongPlan*`,
`SongStructureAudit*`, `SongPlanRpcTest*`.

## Remaining backlog

1. **Semantic pass over the remaining domains.** For each `Router_*.cpp`, list its
   methods and the command entry points they reach; for each `src/mcp/McpTools_*.cpp`,
   list the tools and their entry points; report tools with no RPC route (excluding
   plumbing). **Order re-baselined 2026-09-21 against the actual router inventory** — the
   earlier list named `Router_Matrix` and `Router_Tuning`, **neither of which exists**
   (`src/frontend/router/`: Audio, AudioGraph, Composition, Device, Export, Library,
   Midi, Plugin, Pool, Preview, Project, PsyFm, Rave, Read, Sampler, Session,
   Transport). Confirmed status:
   * **Device** — done (slice 0, `src/common/DeviceParamMap.cpp`).
   * **PsyFm** — done (slice 1).
   * **Matrix** — done (slice 2, `src/common/MatrixPresetService`).
   * **Rave** — clean: its two config tools are reachable as
     `settings.getRaveConfig` / `settings.setRaveConfig` (sharing
     `RaveService::persistedConfigJson`). A false positive of the name screen.
   * **Pool** — clean: `pool_list` maps to `pool.list`, and the router even adds
     `pool.cleanup`, so the RPC surface is a superset. Also a false positive.
   * **Song plan / cells + fx capture receipt** — done (slice 3): the batch
     `set_cells`, `audit_song_structure`, `get_layer_handoffs` and
     `get_fx_capture_status` now have RPC routes.
   * **Tuning** — the one remaining confirmed gap: `analyze_tuning` has no route and
     there is no `tuning` namespace. It needs `analyzeTuningText` / `analyzeTuningObject`
     (~140 lines of spectral analysis) extracted from `McpTools_Tuning.cpp` into
     `src/common/`, and it participates in the async job registry (`wait:false` +
     `poll_job`) — so that slice must also decide whether to expose a generic
     `audio.pollJob` or to document that each async domain has its own status route
     (`rave.jobStatus`, `rave.trainingJobStatus`). `poll_job` is MCP-only today.
   * **Untouched domains** (no gap found by the symbol join, no separate slice needed):
     Clip/Note/Cc/Track/Transport/Read/Settings/Automation/Modulation/Send/Envelope/
     Sampler/FmSynth/MidiFx/FxSlot/FxChain/FxPreset/Library/Session/AudioRead/Arranger/
     Composition-* — their tools drive the same command entry points their routers do.
2. ~~**A namespace-coverage gate.**~~ **DONE (slice 2).**
   `frontend::allMethodNamespaces()` (src/frontend/FrontendRpc.h) is the single source and
   `tests/unit/frontend/rpc_namespace_coverage_test.cpp` asserts every namespace answers
   something other than `unknown method namespace` for an unknown sub-method (plus sentinel
   checks that the retrofit's namespaces are listed). This is the gate that would have
   caught the missing `matrix` namespace on day one.
3. **Ratchet the guard.** Once the backlog is cleared, add a CI-style check (like the
   device-map `--check`) that fails when an MCP tool has no corresponding RPC method, so
   the contract stops depending on discipline.
4. **De-duplicate the nord `.syx` file loader.** Slice 2 re-implemented the
   validate-then-queue sequence inside `MatrixPresetService` (the matrix tool's file route
   needed a structured payload), while `mcp::runNordBankFile` (`PresetRoute.h`) still
   serves `load_nord_bank` with the same parsing/validation. One of them should delegate:
   move the loader core next to the pure parser (`src/mcp/PresetFileParser.h` is
   engine-surface only) and let each caller format its own output.

## Deviation / process notes

- `hdaw-guard` mandates subagent delegation. Subagents are disabled by the user's global
  config (`Unknown agent: general`), so this slice was done inline; the same deviation is
  recorded in `docs/plans/2026-09-21-device-param-map.md`.
- `CMAKE_SUPPRESS_REGENERATION=ON` means `cmake --build` never re-runs CMake here, so a new
  `.cpp` plus its `CMakeLists.txt` entry would never compile (silent `LNK2001` at link).
  Every structural edit in this slice was paired with an explicit `cmake -S . -B build` and
  a `build.ninja` grep proving the new files are in the graph.
