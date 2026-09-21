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

## Remaining backlog

1. **Semantic pass over the remaining domains.** For each `Router_*.cpp`, list its
   methods and the command entry points they reach; for each `src/mcp/McpTools_*.cpp`,
   list the tools and their entry points; report tools with no RPC route (excluding
   plumbing). Suggested order — self-contained, editing-heavy, UI-less domains first
   (the PsyFm shape): `Router_Matrix`, `Router_Device` (done), `Router_Rave`,
   `Router_Pool`, `Router_Tuning`.
2. **A namespace-coverage gate.** `method::` constants in `src/frontend/FrontendRpc.h`
   are a hand-maintained list with no test that each has a `dispatch` branch. Add a gate
   that iterates the namespaces and asserts `dispatch(engine, "<ns>.probe", {})` does not
   answer `unknown method namespace`. To be drift-proof the list must be derived from a
   single source (add `frontend::allMethodNamespaces()` next to the constants) rather than
   copied into the test.
3. **Ratchet the guard.** Once the backlog is cleared, add a CI-style check (like the
   device-map `--check`) that fails when an MCP tool has no corresponding RPC method, so
   the contract stops depending on discipline.

## Deviation / process notes

- `hdaw-guard` mandates subagent delegation. Subagents are disabled by the user's global
  config (`Unknown agent: general`), so this slice was done inline; the same deviation is
  recorded in `docs/plans/2026-09-21-device-param-map.md`.
- `CMAKE_SUPPRESS_REGENERATION=ON` means `cmake --build` never re-runs CMake here, so a new
  `.cpp` plus its `CMakeLists.txt` entry would never compile (silent `LNK2001` at link).
  Every structural edit in this slice was paired with an explicit `cmake -S . -B build` and
  a `build.ninja` grep proving the new files are in the graph.
