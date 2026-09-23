# Plan: bus + send creation surface (MCP + RPC)

**Status:** planned. Owner: agent session 2026-09-22. **Risk:** LOW–MEDIUM — additive
commands; the routing graph is *read* by the new code, never mutated directly.
**Related:** `docs/composition-toolkit.md` ("Known capability gap"), `docs/paths/dub_electro.json`
(the `space` node's per-track workaround this plan obsoletes), `docs/plans/2026-09-21-rpc-parity-retrofit.md`.

## Goal

Make the mixer's bus/send architecture reachable from the agent surfaces, so the dub /
psybient production idiom — shared delay + reverb returns ridden by per-phrase send
automation — becomes buildable via MCP and RPC.

## What already exists (verified 2026-09-22, do not rebuild)

| Piece | Where | State |
| --- | --- | --- |
| Default buses | `ProjectModel::createDefaultProject` (`ProjectModel.cpp:378,386`) | master (`busID 0`) + ONE fx bus `"Reverb"` (`busID 1`, `busType "fx"`, `busTarget 0`, `fxType "reverb"`) |
| Bus build/teardown | `RoutingManager::addBus` (`RoutingManager.cpp:358-375`: `GroupBusProcessor` / `FxBusProcessor`), `removeBus`, `connectBusToParent` | works, driven ONLY by `rebuildFromValueTree` (`RoutingManager.cpp:116`) |
| Send build/teardown | `RoutingManager::addSend` (`:442-467`, reads `sendTarget`/`sendLevel`), `removeSend`; sends live at `track → SEND_LIST → SEND` (`RoutingManager.cpp:250-260`) | works; nothing creates SEND nodes |
| Send mutation tools | `McpTools_Send.cpp` — `get_track_sends`, `set_track_send_level`, `set_track_send_mode`, `set_track_send_bypassed` | registered + parity-mapped (`read.getTrackSends`, `project.setTrackSend*`) but **inert on a fresh project** (no sends exist to set) |
| Render path | `ExportManager::renderThreadFunc → rebuildFromValueTree` (`ExportManager.cpp:420`) | a tree-based bus/send is automatically present in exports, no export-side change |

**The gap is exactly two creators** (plus their removers): nothing appends a `BUS` node
to `busList`, and nothing appends a `SEND` node to a track's `SEND_LIST`.

## Success gates (all must pass with evidence)

- **G1 — bus create reaches the LIVE graph.** `add_bus {busType:"fx", fxType:"delay", name:"Dub Delay"}`
  appends a `BUS` node with a unique `busID`, and after the rebuild the live
  `RoutingManager` holds an `FxBusProcessor` for that id (`fxBusProcessors`) connected to its
  parent — asserted on the **live processor**, never the ReadModel (Gates 1/6/10).
- **G2 — send create reaches the LIVE graph.** `add_send {trackId, busTarget:<that bus>, level:0.5}`
  appends a `SEND` under the track's `SEND_LIST`; after rebuild the live graph has the send
  connection (`sendConnections[{trackIndex, sendIndex}]`) and `get_track_sends` lists it.
- **G3 — end-to-end AUDIO proof (the real acceptance).** With the send feeding a delay bus,
  a rendered window differs **provably** from the identical render with `level 0`:
  `mix_report`/`verify_part` on the same window must show a measurable rms/band delta (the
  `dialect`/`delayThrow` provability rule). A silent or identical result fails this gate.
- **G4 — cascade + teardown.** `remove_bus` on a bus that has sends: the sends are removed
  (count reported) and the live graph drops the bus node; no dangling `sendTarget`.
- **G5 — ONE undo unit.** A single `undo` after `add_bus` + `add_send` removes both
  (transaction, not two undo entries).
- **G6 — Gate 2 path trace.** MCP tool and RPC route both reach the *same* command symbol;
  a twin test asserts identical payloads **and** identical failure text/class for a bad
  `busTarget`, a bad track index, and a non-existent bus id.
- **G7 — parity mechanics.** `node tools/rpc_parity_map.mjs` regenerated; `RpcParityRatchet.*`
  and `RpcNamespaceCoverage.*` green. No new namespace (`project`/`send` already exist) ⇒ no
  `FrontendRpc.h` change; the new tools must not land as `unresolved`.
- **G8 — blast radius.** No `processBlock`, DSP, `RoutingManager` internals, export, playback
  or plugin-isolation file modified. `mix_verdict` on an existing project still passes all
  six gates after the change.
- **G9 — build + suites.** Clean build; targeted gtest suites (new engine test + parity +
  the existing send/registry sweeps) green.

## Dependency map (graphify, 2026-09-22)

- **Blast radius:** the `RoutingManager` community (`addBus`, `connectBusToParent`,
  `rebuildFromValueTree`, `isFolderTrack`) — *read-only* for this change.
- **Upstream:** `rebuildFromValueTree` is the only caller of `addBus`; `renderThreadFunc`
  is the only caller of `rebuildFromValueTree` besides the live graph.
- **Downstream consumers:** `ReadModelImpl::getTrackSends` (`ReadModelImpl.cpp:922`) reads
  `SEND_LIST` children; `RoutingManager::addSend` reads `sendTarget`/`sendLevel`.
- **God nodes in scope:** `ProjectCommands` (254 edges) and `AudioEngineCommands` (308) —
  additive virtuals only, mirroring `auditionPlugin` / `verifyParamSweep`.
- **Projections:** ReadModel gains rows only because the tree changed (no new ReadModel API).
- **SPSC paths:** none added — the send/bus processors already exist.
- **Community boundaries crossed:** model → engine → MCP/RPC (the standard seam; the parity
  contract is the interface).

## Pitfall gates

| Gate | Applies | How it is addressed |
| --- | --- | --- |
| 1 / 6 / 10 (state not restored on rebuild) | **YES** | The created node must survive `rebuildRoutingGraph()`: G1/G2 assert on the live processor *after* a rebuild, not before. |
| 2 (unimplemented path) | **YES** | G6 traces RPC→command→tree→rebuild→live slot; G3 proves the audio effect. |
| 3 (audio-thread safety) | no | No audio-thread code; the bus/send processors exist. |
| 9 (ID namespaces) | **YES** | New `ProjectModel::allocateBusID()` = `max(busID)+1`, never reused; validate `busTarget` exists and `trackIndex` in range; validate `busType ∈ {group,fx}` and, for fx, `fxType` against what `FxBusProcessor` accepts. |
| 12 (graph mutation off the message thread) | **YES** | Never touch `graph` directly: mutate the ValueTree inside one transaction and call the EXISTING rebuild (which owns the pump-park idiom). |
| 13 (stateLock) | no | No DSP object writes. |
| 4 (stale build) | **YES** | After CMakeLists changes: explicit `cmake -S . -B build`; verify the new TU/regs are in `build.ninja`. |
| 23 (clamping) | n/a | `level` is a send gain in 0..1+ — clamp and document exactly as `set_track_send_level` does. |

**Anti-patterns to avoid:** N RPC calls in a loop (one command per mutation, batched by the
caller); full-tree walks (use `getChildWithName(IDs::SEND_LIST)`); `setProperty` + relying on
a listener for the side effect (drive the manager/rebuild explicitly).

## Interface contract (frozen — both slices implement against this)

```cpp
// src/common/ProjectCommands.h  (additive virtuals, one transaction + one rebuild each)
struct BusCreateResult { bool ok = false; std::string error; int busID = -1; };

BusCreateResult createBus(const std::string& busType,   // "fx" | "group"
                          const std::string& name,
                          const std::string& fxType,    // fx only; ignored for group
                          int busTarget /*= 0 (master)*/);

bool removeBus(int busID, std::string& error);           // cascades: removes sends targeting it

struct SendCreateResult { bool ok = false; std::string error; int sendIndex = -1; };
SendCreateResult createSend(int trackIndex, int busTarget, float level, bool isPreFader);

bool removeSend(int trackIndex, int sendIndex, std::string& error);
```

Property names, types and default values must be read from the two existing consumers
(`RoutingManager::addSend`, `ReadModelImpl::getTrackSends`) and matched exactly — a
mismatched property is a silent no-op graph node.

Surfaces: MCP `add_bus` / `remove_bus` / `add_send` / `remove_send` (registrar
`registerSendTools` in `src/mcp/McpTools_Send.cpp`); RPC `project.addBus` /
`project.removeBus` / `project.addSend` / `project.removeSend` (`Router_Project.cpp`).
Names must be the camelCase of the MCP tool names — the parity ratchet matches by
name-derived camelCase, so `createBus`/`createSend` on the RPC surface would land as
`unresolved`. No new namespace (`project` exists) ⇒ no `FrontendRpc.h` change.

**Accepted bus `fxType` values (verified `src/engine/FxBusProcessor.h` ~L83-105):**
`reverb`, `delay`, `eq`, `compressor` — there is no `filter` type. `createBus` must
validate against that set.

## Non-goals

- **Bus FX parameters.** A bus carries an `fxType` string only; `FxBusProcessor` exposes no
  param surface. Setting a bus delay's time/feedback is a follow-up (needs the processor to
  expose params + its own parity work). This slice creates buses with a chosen `fxType`.
- **Stable send ids** (sends stay positional like tracks), **GUI parity** (not required),
  **group-bus routing UI**, **more than the two bus types**.

## Steps

1. **Slice A (engine, subagent):** `allocateBusID` + the four commands + engine gtest.
   Files: `src/common/ProjectCommands.h`, `src/engine/AudioEngineCommands*.cpp`,
   `src/model/ProjectModel.{h,cpp}`, `tests/unit/engine/*`, `CMakeLists.txt`/`tests/CMakeLists.txt`.
   Verify: build + the new test green (G1, G2, G4, G5).
2. **Slice B (surfaces, subagent, after A):** the four MCP tools + four RPC routes + twin test
   + ledger regen. Verify: G6, G7.
3. **Orchestrator acceptance:** run the live end-to-end on the running engine — create a delay
   bus, send a track to it, render the window with `level 0.5` vs `level 0`, and prove the
   delta (G3); then re-run `mix_verdict` on the existing project (G8) and record the result.
4. **Docs:** flip the `composition-toolkit.md` capability gap to "supported" with the recipe;
   add the bus-based throw branch to `docs/paths/dub_electro.json` `space` and a note in the
   FX role file.

## Evidence log

- 2026-09-22: recon completed (bus/send model, the two missing creators, the four inert send
  tools, the parity rows, `FxBusProcessor::setFxType`). Graph queried; blast radius = the
  `RoutingManager` community, read-only.
- 2026-09-22 **slice A** (engine): `allocateBusID` + `createBus`/`removeBus`/`createSend`/
  `removeSend`, one undo unit + ONE `rebuildRoutingGraph()` each (never a direct graph
  mutation). 5 new tests in `tests/unit/engine/send_test.cpp` — **5/5 pass**, asserting on
  the LIVE graph and re-asserting after a second rebuild (survival).
- 2026-09-22 **slice B** (surfaces): MCP `add_bus`/`remove_bus`/`add_send`/`remove_send` +
  RPC `project.addBus`/`removeBus`/`addSend`/`removeSend`; ledger regenerated (tools
  296 → 300, mapped 188 → 192, all four rows `mapped`); 13 twin tests —
  **13/13 pass**. Independently re-run by the orchestrator: **18/18**.
- 2026-09-22 **orchestrator acceptance on the live engine** (relaunched onto the fresh
  binary; `engine_info` → `stale:false`, 300 tools):
  - G1/G2: `add_bus {fx, delay, "Dub Delay"}` → `busID 2` in `BUS_LIST`; `add_send
    {trackId:11, busTarget:2, level:0.5}` → `sendIndex 0`, listed by `get_track_sends`.
  - **G3 (audio proof)**: same 16 s drop1 window rendered at send 1.0 vs 0.0 →
    **rms 0.0832 → 0.1156 (+39%, +2.85 dB), bass 14 094 → 28 662 (+103%), body
    3 905 → 8 715 (+123%)**. The return reaches the master.
  - G4: `remove_bus 2` cascaded its send; the bus-3 send shifted index 1 → 0 (positional).
  - G5: one `undo` restored the bus AND the cascaded send.
  - G6/G7: twin test + ledger rows `mapped` (above).
  - G8: existing `aether_dub_v3.wav` still passes all six `mix_verdict` gates.
- **Findings raised by the acceptance (both recorded in the trees/docs):**
  1. **`add_bus` drops its HTTP response** (reproduced 2/2) while the bus lands in the
     tree — `add_send`/`remove_bus` return normally. The new id is deterministic
     (`max(busID)+1`), so nothing is lost, but a caller must not blind-retry.
  2. **No tool lists buses** — `list_buses` (MCP + RPC) is the obvious companion and
     would also remove finding 1's practical sting.
  3. `export_audio`'s `start`/`end` are **seconds** (16.0 s file confirmed), while
     `verify_part` uses `startBeat`/`endBeat`.
  4. The legacy send routes take `trackIndex` on RPC but `trackId` on MCP (pre-existing);
     the new routes use `trackId` on both surfaces.
  5. A subagent's "these 4 failures are pre-existing/environmental" claim was **false** —
     all four pass alone and mixed in the current binary; they were artifacts of that
     agent's own transient bug plus a build-dir race. Verified rather than accepted.
- 2026-09-22 **full suite** (`build/hdaw_tests.exe`, 2723 s): **4 failures, 0 regressions.**
  All four are explained and none touches this change's code:
  1. `McpServer.HttpRoundTrip` + 2. `McpServer.DiagnosticClapExportMatrix` — the suite ran
     while the acceptance engine still held **port 18765**, which `mcp_server_test.cpp:115`
     hardcodes; both **pass** in a clean filtered re-run (and the file already documents a
     separate pre-existing order-sensitivity).
  3. `PluginIsolation.LargeStateRoundTripThroughProxy` — the documented flake, **passes**.
  4. `RespawnPath.RealPathPassesThrough` — deterministic pre-existing failure in
     `tests/unit/proxy/crash_recovery_test.cpp:655` (last touched 2026-08-20, not in this
     diff): it asserts a Unix-style path survives `PluginManager::resolveRespawnPath`
     unchanged and fails on Windows. Unrelated to any command/model code.
  Recorded in `docs/testing-mcp.md`; the `AGENTS.md` "1 flake" baseline is stale.
- **Process note (mine):** running the suite concurrently with a live engine and killing
  plugin hosts mid-run confounded the first result. Re-run the failures clean before
  attributing them.
