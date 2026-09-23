# Plan: `set_bus_target` (re-parent a bus)

**Status:** approved 2026-09-23 ("when the suite lands go ahead with the set_bus_target work").
**Risk:** LOW–MEDIUM — one additive command; it changes *routing topology*, so the validation
must be right (a cycle would corrupt the graph), but nothing touches DSP.
**Why:** `busTarget` is set only by `createBus`, so a bus's parent can never change. That makes
the filter-bus HPF applicable **only** to buses created in the right order — the default
`Reverb` bus (busID 1, target 0) cannot be routed through a new HPF bus, and a mis-ordered
chain has to be torn down and recreated. With `set_bus_target` the chain becomes editable:
`reverb → HPF → master` by re-parenting, not by recreation.

## What already exists (verified 2026-09-23 — reuse, do not reinvent)

| Piece | Where | Note |
| --- | --- | --- |
| Bus model | `ROUTING_GRAPH/BUS_LIST/BUS` with `busID` / `busType` / `busTarget` / `fxType` / `param_N` | `busTarget` = the parent bus id; master is 0 with `busTarget == -1` |
| Creation validation | `AudioEngineCommands::createBus` (`AudioEngineCommands.cpp:244-285`) | the shape to mirror: busList valid → target exists → clear error strings |
| Parent resolution | `RoutingManager::connectBusToParent` | reads `busTarget`, connects to `busNodes[parentID]`, falls back to master when the id is unknown |
| The rebuild | `AudioEngineCommands::rebuildRoutingGraph()` | the pump-park-safe wrapper every structural command uses (pitfall 12) |
| The bus command home | `AudioEngineCommands.cpp` "Bus operations" (createBus/removeBus/setBusFxParam) | where the new command belongs |

## Success gates

- **G1 — it works and is live.** `set_bus_target {busID, busTarget}` re-parents the bus; after
  the rebuild the **live graph** has the new connection (assert on the graph, not the tree):
  `delay → HPF → master` re-parented so the delay feeds master directly, and back.
- **G2 — the HPF-on-an-existing-return recipe works.** The default `Reverb` bus can be
  re-parented behind a new filter bus, and a send into it comes out high-passed (A/B render:
  the low band drops with the filter in HP mode). This is the deliverable the gap was blocking.
- **G3 — cycles are impossible.** Reject: targeting itself; targeting a **descendant**
  (walk the proposed parent's chain — if it reaches this bus, refuse, however many hops);
  targeting a nonexistent bus; re-parenting the **master** bus. Each with a clear error and
  **no tree change**. This is the gate that matters: a cycle would corrupt the routing graph.
- **G4 — clamping/atomicity.** One undo unit per call; a rejected call leaves the tree byte-identical;
  one `rebuildRoutingGraph()` on success.
- **G5 — parity.** MCP `set_bus_target` ↔ RPC `project.setBusTarget`, same argument names
  (`busID`, `busTarget`) and the same failure text; ledger regenerated (+1 tool, +1 row) and
  the ratchet green.
- **G6 — blast radius.** No DSP change; the shipped bus/filter/delay suites stay green.

## Interface contract (frozen)

```cpp
// src/common/ProjectCommands.h — additive virtual, mirrors createBus's result style
struct BusRetargetResult { bool ok = false; std::string error; };
virtual BusRetargetResult setBusTarget(int busID, int busTarget) = 0;
```
Surfaces: MCP `set_bus_target {busID:int, busTarget:int}` → RPC `project.setBusTarget`
(name-derived camelCase). Both return `{"ok":true}` / `text(error, true)`.

## Steps

1. **Slice F (code, subagent):** the command (validation incl. the transitive cycle walk + the
   master guard), the MCP tool, the RPC twin, tests for G1/G3/G4, ledger regen.
2. **Orchestrator:** the tree/docs content — the `shared-return` recipe gains the
   re-parent variant (no longer "create in the right order"), and the
   `return-hpf-needs-a-chained-filter-bus` trap's "no re-parenting" clause is retired.
3. **Verification:** build, run the bus/filter suites, then the live G2 — re-parent the default
   `Reverb` return behind a new HPF bus on `aether_dub_returns` and show the low band drop.

## Evidence log

- 2026-09-23: recon. `createBus` is the validation shape; `connectBusToParent` already resolves
  an arbitrary parent (so only the *editing* is missing). The cycle walk is the one piece with
  no precedent — `createBus` cannot create a cycle (a new bus has no children), but re-parenting
  can.
