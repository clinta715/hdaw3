---
name: hdaw-guard
description: ALWAYS load before any code change in this project. Guards against the 9 recurring pitfalls, enforces plan-first development with success gates, requires dependency/consequence analysis before mutations, and alerts on anti-patterns. Non-negotiable for every task.
---

# HDAW Guard

## Iron Law

```
NO CODE WITHOUT A PLAN. NO PLAN WITHOUT SUCCESS GATES.
NO CHANGE WITHOUT CONSEQUENCE ANALYSIS. NO ASSUMPTIONS.
ALL IMPLEMENTATION RUNS IN SUBAGENT TASKS.
```

If you have not completed the Pre-Flight Checklist, you cannot write code.
If the work involves writing or editing code, you must dispatch it to a subagent.

---

## Pre-Flight Checklist (mandatory before every change)

1. **PLAN** — Write a plan with explicit success-gating criteria (see §Planning).
2. **GRAPH QUERY** — Consult the knowledge graph (`graphify-out/graph.json`) to map the blast radius. Use `graphify query`, `graphify path`, and `graphify explain` (see §Graph-Based Analysis). Do not assume relationships — verify them.
3. **DEPENDENCIES** — Identify all upstream callers and downstream consumers of the code you will touch. Use `trace_path`, `search_graph`, graphify traversal, grep. Do not assume.
4. **PITFALL SCAN** — Check your change against the 9 Recurring Pitfalls (§Pitfall Gates). If any gate triggers, address it in the plan.
5. **ANTI-PATTERN SCAN** — Check your change against the Anti-Pattern Alerts (§Anti-Patterns). If any alert fires, stop and redesign.
6. **VERIFY** — After implementation, run the success gates from the plan. Evidence before claims.

---

## §Execution Model: Subagent Tasks

**All implementation work MUST be dispatched to subagent tasks.** The orchestrating
session plans, analyzes, and verifies — subagents write code.

### Division of labor

| Orchestrator (this session) | Subagent (task tool) |
|---|---|
| Pre-flight checklist | Code implementation |
| Graph queries / dependency analysis | File edits (write/edit) |
| Plan authoring with success gates | Running build/test commands |
| Pitfall + anti-pattern scanning | Reporting results back |
| Reviewing subagent output | |
| Final verification against gates | |

### Dispatch rules

1. **One task per coherent unit of work.** A subagent gets a single, well-scoped
   implementation task — not "do everything." Split multi-file changes into
   logical units (e.g., "add the C++ command + test" and "wire the frontend
   store + component" are separate tasks if they don't share state).

2. **The subagent prompt MUST contain:**
   - The full plan (goal, success gates, steps)
   - The dependency map (upstream, downstream, projections, SPSC paths)
   - Which pitfall gates apply and how to address them
   - Relevant anti-patterns to avoid
   - Exact file paths to modify
   - The verification commands to run before reporting back
   - Instruction to report: files changed, tests run + output, any gates that failed

3. **Parallel dispatch when independent.** If two tasks don't share files or
   state, dispatch them in the same message (multiple task tool calls). Follow
   the `dispatching-parallel-agents` skill for coordination.

4. **Never dispatch without a plan.** The subagent receives a finished plan —
   it does not do its own dependency analysis or pitfall scan. That work is
   done here, in the orchestrator, before dispatch.

5. **Review before accepting.** When a subagent returns, the orchestrator must:
   - Confirm all success gates passed (read the evidence)
   - Scan the diff for anti-patterns
   - Verify no pitfall gates were violated
   - Run the completion contract checks
   Only then is the work accepted.

### What stays in the orchestrator

- Reading files for analysis (graph queries, grep, trace_path)
- Writing the plan
- Dispatching and reviewing subagents
- Running final verification (build, tests) if the subagent's evidence is insufficient
- Communicating with the user

### Subagent type selection

- **`general`** — for implementation tasks (writes code, runs commands, edits files)
- **`explore`** — for read-only investigation (finding files, tracing callers, reading docs). Use when the orchestrator needs more information before planning.

Never use `explore` for implementation — it cannot write files.

---

## §Planning

Every task gets a plan BEFORE code. The plan must contain:

```
## Goal
<one sentence>

## Success Gates (all must pass to declare done)
- [ ] Gate 1: <measurable criterion, e.g. "gtest suite X passes">
- [ ] Gate 2: <e.g. "live processor state asserted after rebuild">
- [ ] Gate 3: <e.g. "no new raw hex in CSS">
- ...

## Dependency Map (from graphify + trace_path)
- Blast radius: <graphify query result summary — communities touched>
- Upstream: <who calls/reads this?>
- Downstream: <who consumes the output?>
- God nodes in scope: <high-degree hubs being modified, or "none">
- Community boundaries crossed: <list, or "none">
- Projections affected: ReadModel? Audio graph? Frontend snapshot?
- SPSC paths touched: <list or "none">
- Path integrity: <graphify path verified? gaps found?>

## Pitfall Gates Triggered
- <which of the 9 pitfalls apply, and how you address each>

## Steps
1. ...
2. ...
```

**Success gates are the completion contract.** Work is not done until every gate passes with evidence (test output, grep result, build log). "Should work" is not a gate.

---

## §Pitfall Gates

Check EVERY change against these. They are ranked by historical frequency.

### Gate 1: State Not Restored on Rebuild / Projection Seam

**Trigger:** Any change that writes state to a processor, adds a new processor property, or modifies `RoutingManager`/`rebuildRoutingGraph`.

**Rule:** Any state that reaches a processor via SPSC must ALSO be restored in the rebuild path (`RoutingManager::addTrack` / `restoreMixerState` or equivalent).

**Verification:**
- Test that: (1) mutates state, (2) calls `rebuildRoutingGraph()`, (3) asserts on the LIVE processor (`getMainProcessor()->getTrack(idx)`), not the ReadModel.
- A no-crash smoke test is NOT sufficient.

**Alert if:** You see a new `setProperty` on a track/clip ValueTree that has no corresponding restore in the rebuild path.

### Gate 2: Unimplemented Code Path Silently Failing

**Trigger:** Any new command, property, RPC method, or message.

**Rule:** Trace the FULL path: RPC → ValueTree → listener → processor → observable effect. If ANY link is a no-op or placeholder, the feature is unimplemented.

**Verification:**
- Integration test asserting on the live processor output or frontend DOM state.
- Grep for the property/message on the receiving side — confirm a handler exists.

**Alert if:** You write a property or send a message and cannot point to the code that reads/handles it.

### Gate 3: Audio-Thread Safety Violations

**Trigger:** Any change to `processBlock`, audio callbacks, or code reachable from the audio thread.

**Forbidden on the audio thread:**
- Heap allocation (`new`, `malloc`, `String`, `std::string`, `Array::add`)
- Mutex lock (`CriticalSection::enter`, `std::mutex::lock`)
- File/network I/O (`WriteFile`, `File::`, sockets)
- `String` formatting / `DBG` / logging
- O(n) scans over unbounded collections

**Required patterns:** atomic flags, SPSC ring buffers, `SpinLock` with `tryEnter`, pre-allocated buffers, message-thread servicing.

**Alert if:** You see allocation, locking, I/O, or string ops inside `processBlock` or anything it calls.

### Gate 4: Build / Packaging Stale Binaries

**Trigger:** Adding new executables, changing build config, or testing after C++ changes.

**Rule:**
- After C++ changes, verify the binary under test is the one just built (check timestamp/size).
- New executables must appear in `electron-builder.yml` `extraResources`.
- `frontend\build.bat` is the canonical full pipeline.
- NEVER test against `build/Release/HDAW.exe` (stale).

**Alert if:** A new `.exe` is produced by CMake but not listed in `electron-builder.yml`.

### Gate 5: Frontend Stale Closures / Missing Hook Deps

**Trigger:** Any `useMemo`, `useCallback`, `useEffect`, or event handler in React/TS code.

**Rule:**
- Every value used inside a memo/callback body MUST appear in its dependency array.
- After any `await`, read from `useProjectStore.getState()`, NEVER from closure props.
- Window-level listener callbacks read from store, not closure.

**Alert if:** A `useMemo`/`useCallback` body references a variable not in its deps array. A post-`await` expression reads `clips`/`tracks` from a prop.

### Gate 6: Day-One Bugs Masked by Live SPSC Path

**Trigger:** Same as Gate 1, but specifically: feature "works" interactively yet has no restore path.

**Rule:** Interactive correctness ≠ rebuild correctness. Every stateful processor needs a restore path exercised by a test.

**Alert if:** You demonstrate a feature working live but cannot show a test that survives `rebuildRoutingGraph()`.

### Gate 7: Window Management / Z-Order / Sizing

**Trigger:** Spawning native windows (plugin editors, child-process UI).

**Rule:**
- Use `toFront(true)` / `SetForegroundWindow` for child-process windows.
- Account for window decorations — use `getBorderSize()` or content-size APIs.

### Gate 8: CSS Design-Token Violations

**Trigger:** Any CSS/SCSS change or new component with styling.

**Rule:**
- ALL colors must reference `--var` tokens from the theme. No raw hex.
- Respect `prefers-reduced-motion`.
- No hardcoded spacing that bypasses the token system.

**Verification:** `grep -rn "#[0-9a-fA-F]\{3,8\}" frontend/src/**/*.css` returns zero hits in new code.

**Alert if:** You see a raw hex color, an undefined CSS variable, or a missing reduced-motion query.

### Gate 9: ID Namespace Collisions / Missing Validation

**Trigger:** Allocating IDs, parsing string→int, or accessing optional pointers.

**Rule:**
- Each entity type has its own ID allocator (`allocateClipID`, `allocateNoteID`, etc.). Never cross-allocate.
- Guard all pointer access on optional paths (`getMainProcessor()` can be null).
- Validate `std::stoi` / `parseInt` at trust boundaries.

**Alert if:** You see `allocateClipID` used for notes, `getMainProcessor()` without a null check, or unguarded `stoi`.

---

## §Anti-Patterns (auto-alert)

If you observe any of these in code you are writing or reviewing, STOP and flag:

| Anti-Pattern | Correct Pattern |
|---|---|
| N separate `await rpc.call()` in a loop | Single batch RPC (`addClips`, `moveClips`, `removeClips`, `duplicateClips`, `paintClips`) or `beginTransaction`/`endTransaction` |
| `syncSnapshot` after unverified RPC | Verify RPC succeeded, then sync — or skip sync and keep optimistic state |
| Optimistic placement during continuous drag | Place only on mouseup; use drag preview for feedback |
| Reading `clips` prop after `await` | `useProjectStore.getState().snapshot?.clips` |
| `setProperty` relying on listener for side-effect at fixed value | Drive the manager directly OR nudge the value |
| Full-tree walk to touch one node | Indexed access / `getChildWithProperty` / held references |
| `rebuildRoutingGraph()` per-clip in a loop | Slice at model level (`ProjectModel::sliceClipAtTimes`), rebuild once at end |
| Test asserting only ReadModel after rebuild | Assert on live processor (`getMainProcessor()->getTrack(idx)`) |
| Raw hex color in CSS | Theme token `var(--token)` |
| `DBG(...)` macro | `HDAW_LOG(tag, msg)` from `DebugLog.h` |
| New `.cpp` not added to `CMakeLists.txt` source list | Always add to `add_executable` |
| `direction: reverse` on range input | `transform: scaleY(-1)` |

---

## §Graph-Based Analysis (graphify)

This project maintains a knowledge graph at `graphify-out/graph.json`. Use it as
the FIRST tool for understanding blast radius, tracing paths, and verifying
assumptions. The graph is persistent across sessions — query it, don't rebuild it.

### When to use which traversal

| Question shape | Tool | Mode |
|---|---|---|
| "What does X touch / affect?" | `graphify query "What calls X and what does X call?"` | BFS (broad context) |
| "How does state flow from A to B?" | `graphify query "trace data flow from A to B" --dfs` | DFS (specific path) |
| "Are A and B connected?" | `graphify path "A" "B"` | Shortest path |
| "What is X and what does it do?" | `graphify explain "X"` | Node explanation |
| "What are the risky hub nodes?" | Read `graphify-out/GRAPH_REPORT.md` → God Nodes | Pre-computed |

### Mandatory graph queries before code changes

1. **Blast radius (BFS):** Before modifying any function/class, run:
   ```
   graphify query "What depends on <target> and what does <target> depend on?"
   ```
   This surfaces upstream callers AND downstream consumers in one pass. If the
   result crosses multiple communities, the change is high-risk — flag it in the plan.

2. **Path verification (DFS):** When wiring a new command/property (Gate 2), verify
   the full chain exists:
   ```
   graphify query "trace the path from RPC handler <name> to the audio processor" --dfs
   ```
   If the path has a gap (no edge between listener and processor), the feature is
   unimplemented. Do NOT assume the connection exists.

3. **God node check:** Before modifying any node that appears in the God Nodes
   section of `GRAPH_REPORT.md`, escalate: these are high-degree hubs where a
   mistake cascades. Add an explicit "God node risk" line to the plan with the
   node's degree and the communities it bridges.

4. **Community boundary check:** If your change touches nodes in more than one
   community (from `GRAPH_REPORT.md`), you are crossing an architectural seam.
   Document which communities are affected and verify the interface contract
   between them (RPC boundary, SPSC bridge, ValueTree listener).

### Graph honesty rules (from graphify)

- **Never invent an edge.** If the graph doesn't show a connection, it doesn't
  exist. Verify with grep/read before assuming.
- **Never assume the graph is complete.** It is a snapshot. If code was added
  since the last build, the graph may be stale. Cross-check critical paths with
  `trace_path` or grep.
- **Mark uncertainty.** If a relationship is unclear, say AMBIGUOUS and
  investigate — do not proceed on a guess.
- **Cite source_location.** When referencing a graph finding in the plan, note
  the file/line so it can be verified.

### Keeping the graph current

After significant structural changes (new files, new RPC methods, new classes),
the graph should be refreshed:
```
graphify . --update
```
This is incremental (only re-extracts changed files). Run it before the final
verification pass so the completion contract checks against current topology.

---

## §Dependency Analysis Protocol

Before modifying ANY function, class, property, or RPC method:

1. **Graph first:** Run `graphify query` (BFS) on the target. Read the blast radius.
2. **Upstream:** Who calls this? Confirm with `trace_path` (direction: inbound) or grep.
3. **Downstream:** What consumes the output? Confirm with `trace_path` (direction: outbound).
4. **Path integrity:** For new wiring, run `graphify path "source" "sink"` — if no path exists, the feature is unimplemented (Gate 2).
5. **Projections:** Does this affect ReadModel? Audio graph? Frontend snapshot? All three?
6. **SPSC bridge:** Does state cross the message-thread → audio-thread boundary? If yes, Gate 1 + Gate 3 apply.
7. **Delta vs fullSync:** Will this change express as an incremental delta or require fullSync? (Clip/track property = delta. Tree restructure / non-clip entity = fullSync.)
8. **Undo:** Is this wrapped in an undo transaction? Batch ops = one undo unit.
9. **Community boundaries:** Does the change cross graph communities? If yes, document the interface contract.

**No assumptions.** If you cannot trace a path in the graph OR in the code, say so and investigate before proceeding.

---

## §Project Conventions (quick reference)

- **Beats vs seconds:** Frontend speaks beats; clip ValueTree and processors speak seconds. Convert at boundaries.
- **Batch RPCs:** One batch call, not N loops. Coalesces into one delta + one rebuild + one undo unit.
- **Incremental deltas:** Express changes as clip/track deltas. Reserve fullSync for restructures.
- **Test discipline:** Engine change → identify affected gtest suites → update/add tests → run `hdaw_tests.exe`. UI change → identify Vitest/Playwright tests → update/add → run.
- **MCP parity:** Any user-facing feature must also be an MCP tool.
- **Generative toolkit:** Ask whether PhraseGenerator / humanize / modulation applies.
- **UI idiom:** Bitwig Arranger + Ableton fixed-tile flow. Spatial stability is sacred. No floating windows. Bottom panel tabs for detail views.
- **Version sync:** `CMakeLists.txt` and `frontend/package.json` must match.

---

## §Completion Contract

Work is complete ONLY when:

1. All success gates from the plan pass with evidence.
2. Relevant test suites run and pass (`hdaw_tests.exe`, `npm test`, `npm run test:e2e` as applicable).
3. No new anti-patterns introduced (scan your diff).
4. Dependency map confirmed — no silent breakage upstream/downstream.
5. If C++ changed: `cmake --build build --config Debug` succeeds.
6. If frontend changed: `cd frontend && npm run build` succeeds.
7. If new RPC/command: MCP tool exists (parity rule).
8. If structural change (new files/classes/RPC methods): `graphify . --update` run so the knowledge graph stays current.
9. Graph path integrity re-verified for any new wiring (`graphify path "source" "sink"` shows a complete chain).
