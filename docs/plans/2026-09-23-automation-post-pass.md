# Plan: the post-arrangement (timeline-final) pass

**Status:** approved 2026-09-23. **Risk:** LOW — one additive flag on an existing command plus
path-tree content; no DSP, no export, no routing.
**Question that produced it:** *"should automation, which sometimes needs to progress for
several bars, be handled in a separate pass after the entire track length has been
constructed — and would after-track-made be useful for other processes?"* Answer: yes for
**timeline-dependent** automation, no for **material-bearing** automation; and the phase
generalises to every process that is a function of the finished timeline.

## What already exists (verified 2026-09-23 — this corrects the original proposal)

| Piece | Where | State |
| --- | --- | --- |
| Window-level idempotency | `automation_preset` has **`clear: true`** — "removes existing points inside each window before writing" (`McpTools_Automation.cpp:201+`) | **already idempotent per window** |
| Lane idempotency | `addAutomationLane` (`AudioEngineCommands_Automation.cpp`): same name + same `paramID` → **idempotent no-op**; a *different* name for an already-bound `paramID` → **conflict** (deliberate: "two lanes can't drive the same plugin parameter") | create-only; no upsert |
| Lane lookup by paramID | `findLane(engine, trackId, ref)` (`McpTools_Automation.cpp`) accepts an integer paramID or a name; `remove_automation_lane` already uses it | reusable |
| Clip-scoped vs lane-scoped split | `generate_clip_gain_envelope` / `generate_clip_cc_lane` (material) vs `add_automation_lane` + `automation_preset` (arcs) | the split the phase needs, already in the model |

**The real gap is lane *identity*, not points.** A re-runnable post-pass must be able to say
"the lane bound to paramID N is mine, named X" in one call. Today it can't: the pass's
`add_automation_lane` trips the conflict guard, and the caller then aborts *before* the
preset — which is exactly the documented `one-lane-per-param` failure mode ("already exists"
followed by "lane not found").

## Success gates

- **G1 — upsert by paramID.** `add_automation_lane {trackId, laneName:"DubThrow",
  paramID:139, replace:true}` succeeds when paramID 139 is already bound to a lane named
  something else, and the lane is afterwards named `DubThrow`, still bound to 139, **with its
  existing points intact**. Assert on the tree.
- **G2 — default behaviour unchanged.** Without `replace` (or with `replace:false`), the
  conflict guard still fires and the error text is unchanged on both surfaces.
- **G3 — the pass is re-runnable end to end.** Run the same `add_automation_lane(replace:true)`
  + `automation_preset {lane:<paramID>, clear:true, sections:[...]}` sequence **twice**;
  the second run must succeed and produce the **same point count and values** as the first
  (deterministic seed). This is the gate that makes the phase safe.
- **G4 — name-collision safety.** `replace:true` with a `laneName` already bound to a
  *different* paramID still fails (no silent steal), with a clear error.
- **G5 — parity.** MCP `add_automation_lane` and RPC `project.addAutomationLane` accept the
  same `replace` key with the same failure text; `node tools/rpc_parity_map.mjs` unchanged
  (no new tool).
- **G6 — blast radius.** No change to `automation_preset`'s existing semantics, the
  automation cache, DSP, export or routing.

## Interface contract (frozen)

```cpp
// src/common/ProjectCommands.h — additive defaulted parameter
virtual bool addAutomationLane(int trackIndex, const std::string& laneName,
                               int paramID, bool replace = false) = 0;
```
Semantics when `replace == true` **and** `paramID != 0`: if a lane is already bound to
`paramID`, **rename it to `laneName`** (points preserved) and return true. If `laneName` is
already taken by a *different* `paramID`, return false. `replace == true` with `paramID == 0`
falls back to today's behaviour (0 = "unbound, matches any"). `replace == false` is byte-for-byte
today's behaviour.

## The phase (tree content)

**Walk order (psydub) becomes:** `foundation → palette_theme → rhythm_feel → bass_character →
**arrangement** → space → texture → movement → **rides** → mix`.

- **`space` moves after `arrangement`** (was before it): its options are *section-scoped*
  gestures (`delayThrow` "last 2 beats of every 8-bar group", `openClose` on `breakdown`,
  `steppedGate` on `drop2`), so every section reference was being resolved against a
  provisional timeline.
- **New `rides` node** (between `arrangement` and `mix`) holds the timeline-final writers that
  have no other home: whole-track arcs, **send/bus return rides** (newly possible — the dub
  throw as a send ride), and per-section gain staging; its gate is "nothing writes after
  measurement".
- **`_readme` gains a `phases` block** declaring which nodes are timeline-final and the rule
  *timeline-shape writers → timeline-dependent writers → readers (measurement last)*.
- `dub_electro.json` needs no reorder (it has no `space` node; its `arrangement` already
  precedes `movement`/`mix`) — but it should gain the same `phases` declaration.

## Steps

1. **Slice D1 (code, subagent):** the `replace` flag through `ProjectCommands` →
   `AudioEngineCommands_Automation` → `McpTools_Automation` → `Router_Project`, plus tests for
   G1–G4.
2. **Orchestrator:** the tree content (walk reorder, the `rides` node with measured recipes,
   the `phases` declaration in both trees, and the re-run recipe documented where the trap is
   recorded).
3. **Verification:** build, run the automation suites + the new tests, then a live re-run of
   the same pass twice to prove G3 on a real project.

## Evidence log

- 2026-09-23: recon. `clear:true` and the smart-create semantics found, which shrank item 1
  from "add a rewrite primitive" to "add lane upsert by paramID" — the original proposal
  assumed no idempotency existed. Tree orders read for both paths; `dub_electro` has no
  `space` node.
- 2026-09-23 **items 2 and 3 landed (tree content, orchestrator).**
  - `docs/paths/psydub.json` node order is now
    `foundation → palette_theme → rhythm_feel → bass_character → arrangement → space →
    texture → movement → rides → mix` — `space` moved after `arrangement` because its options
    are section-scoped gestures (`delayThrow` on "last 2 beats of every 8-bar group",
    `openClose` on `breakdown`, `steppedGate` on `drop2`) that were being resolved against a
    provisional timeline.
  - New **`rides`** node (3 options, all `measured`): `arc-pass` (whole-track arcs, paramID-
    addressed + `clear:true` + `replace:true` = the idempotent re-run), `return-ride` (the dub
    throw as a *send* ride, with the Mix-1.0/Feedback setup and the measured +2.85 dB gate),
    `staging-pass` (per-section staging with the `faderOverriddenIds` authority trap). Its note
    carries the phase's terminal rule: nothing writes after measurement.
  - **`_readme.phases`** declared in BOTH trees (`rule` / `arrange` / `timeline-final` /
    `measure` / `why` / `rerun`); `dub_electro` cross-references psydub's `rides` for the
    recipes rather than duplicating them (its `movement` node already cross-references psydub).
  - The `mutation-order` trap now points at the declared phase instead of only naming
    `movement`.
  - **Blast radius checked:** no code reads the path trees (`FileLibraryManager`'s
    `patch_selection_ledger.json` is a different ledger) — the reorder is agent-workflow only.
- 2026-09-23 **item 1 landed (slice D1).** `addAutomationLane` gained an opt-in `replace` flag
  implemented as a **two-pass upsert**: pass 1 enforces *name* ownership (a `laneName` held by
  a different `paramID` still fails — no silent steal), pass 2 renames the lane bound to
  `paramID` **in place** (`setProperty(IDs::name, …)` + `rebuildAutomationCache`) so its points
  survive. Every pre-existing branch is byte-for-byte unchanged (the old loop only moved down),
  so `replace=false` is today's behaviour exactly. MCP `add_automation_lane` and RPC
  `project.addAutomationLane` both take `replace` with the unchanged failure text; the UI's
  calls keep the default (deliberately: the UI must not steal a lane).
  - **No test doubles existed** for `ProjectCommands` (enumerated by grep), so the 4th
    parameter cost one declaration + one definition.
  - **Evidence:** 44/44 tests across 8 suites (`Automation` 14, `AutomationPreset` 9,
    `AutomationMode` 4, `AutomationUnits` 3, `AutomationPidRouting` 3, `GainEnvelopeUnits` 2,
    `EnvelopeGenerationRpc` 6, `McpCoverageTest` 3). G1 (rename + points preserved + re-run is
    a no-op), G2/G2b (default unchanged), G4 (no silent steal) are engine tests; **G3 is proven
    through the REAL MCP tools** — `McpCoverageTest.AutomationLaneReplaceUpsertIsRerunnable`
    runs `add_automation_lane{replace:true}` + `automation_preset{lane:<paramID>, clear:true,
    seed:12345}` **twice** and asserts identical point count and values. That is a stronger G3
    than a live HTTP re-run would be (same command path, no transport in the way), so no
    separate live pass was run.
  - **G5:** parity unchanged — 303 tools / 391 RPC / 195 mapped, and the ledger diff contains
    only the 7 rows from the two earlier slices (a flag on an existing tool adds no row).

