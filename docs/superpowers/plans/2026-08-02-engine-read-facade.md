# Engine Read/Write Facade Extraction Implementation Plan (0.15.0)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `McpTools_*` and `FrontendRouter` from reaching into the realtime `MainAudioProcessor`/`RoutingManager`/`ProjectModel` god-object directly, by rerouting all read access through the existing `ReadModel` snapshot projection and all write access through the `ProjectCommands`/`AudioGraphCommands` command layer.

**Architecture:** Two-part cleanup of the "god-object `AudioEngine`" (comm 12) read/write leaks we traced. Part A (read) re-points every MCP/`FrontendRouter` read CALL at the already-existing `ReadModel` (comm 44) snapshot interface, adding a small number of missing snapshot methods (program lists, live FX params, meter reads already exist). Part B (write) re-points the direct `getMainProcessor()->getTrack(i)->{setFXBypassed, addFXSlotAt, ...}` calls onto `ProjectCommands`/`AudioGraphCommands` (which already own every needed FX write; the read side has one gap: FX program lists). No new subsystem; the facade is `ReadModel` + the command layer, which both already exist and are the intended seams.

**Scope decision:** FULL facade — all consumers (`McpTools.cpp`, `McpTools_Project.cpp`, `McpTools_Audio.cpp`, `McpTools_Transport.cpp`, `McpTools_Session.cpp`, `FrontendRouter.cpp`) are migrated in one coordinated change, because they share the same two seams and leaving any one uncuts keeps `AudioEngine` welded to that community.

**Tech Stack:** C++17 / JUCE 8, QT (MCP server uses `QJson*`), GTest (`build/Debug/hdaw_tests.exe`).

---

## Context from the knowledge graph (graphify-out)

- God node risk: `AudioEngine` (comm 12), degree ~28 consumers / 83 edges. `AudioEngineCommands` (comm 0, 180 edges). `MainAudioProcessor` (comm 5/141) and `RoutingManager` (comm 26) are the realtime cores being leaked to.
- Communities crossed: server (17/18), router (122), MCP-project (43), MCP-session/transport (79), engine core (12), realtime graph (5/26/141).
- Existing safe seams: `ReadModel` (comm 44) already exposes `getTransport().bpm`, `getScaleRoot/Mode()`, `getFxSlots()`, `getInternalFxParams()`, `getTrack()`, `getAutomatableParams()`. Command layer (`ProjectCommands` 1, `TransportCommands` 88, `AudioGraphCommands` 157, `AudioEngineCommands` 0) owns writes, including all the FX writes currently bypassed by MCP.
- NO new node invented: every reroute below targets a method that already exists or a snapshot struct already in `ReadModel.h`. No new edges assumed — each verified against the `.cpp` at the cited line.

---

## Dependency Map

- Blast radius: `McpTools_Project.cpp`, `McpTools_Audio.cpp`, `McpTools.cpp`, `FrontendRouter.cpp`, `ReadModelImpl.{h,cpp}`, `AudioEngine.h`, `ProjectCommands.cpp`/impl, `AudioGraphCommands` impl, `MainAudioProcessor.h`.
- Upstream (callers needing reroute): MCP tools + `FrontendServer::dispatch` → `FrontendRouter::dispatch*` facades (lines: McpTools_Project 30,57,82,113,119,166,180,182,195,217,259,280,304,326,328,349,353,375,389,395,397,465,487,495,819,860-972; McpTools_Audio 26,48,147,166,181,205,221,231,266,307,323,370; McpTools 24,40,60,80; McpTools_Transport 13; McpTools_Session 11; FrontendRouter 791,921,942,1018,1100,1105,1330,1386,1413).
- Downstream (consumers): MCP tool responses (`McpToolResult`), `FrontendRouter.dispatch()` return `DispatchResult`, frontend JSON-RPC.
- Projections affected: **ReadModel snapshot** (safe — already the frontend contract). Live audio graph (`MainAudioProcessor`/`RoutingManager`) — only the writes re-point to commands; **no new projection**.
- SPSC paths touched: read side writes DO travel SPSC for FX param/automation (`setAutomationParam`). Gate 1 + Gate 6 apply there — the command layer already restores mix state on rebuild, so keep writes on commands (which are the audited path) rather than direct.
- Path integrity: verified each reroute involves a real existing edge (grep above). God nodes modified: `AudioEngine` (read side only, no new god). The only genuinely new read surface is ONE small typed method added to `AudioEngine` (`getFxProgramList`) for the plugin-instance reads that the persisted `ReadModel` cannot provide.
- Undo grouping: writes go through `ProjectCommands`/`AudioGraphCommands` (already undo-managed).

---

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** every rerouted write must land on a real command. Verified immediately: `McpTools_Audio` calls `tr->setFXBypassed()` (live processor) but `ProjectCommands` already has `setFxSlotBypassed` (and `addFxSlot`/`setFxSlotPlugin`/`removeFxSlot`/`setFxSlotParam`). The plan adds NO new write command — it only reroutes the caller to the existing command.
- **Gate 6 (day-one masked by SPSC):** the writes via `MainProcessor` were "working live" but bypass the audited restore path. Moving them onto commands exercises the restore. Test asserts live processor after `rebuildRoutingGraph()`.
- **Gate 3 (audio-thread safety):** the facade is entirely on the message/command thread; no new `processBlock` code. Read via `ReadModel` snapshot never touches the audio thread. Writes remain on the command (message) thread via commands — no new SPSC on the render thread.
- **Gate 9 (null guards):** `getMainProcessor()` returns null before init. Every facade caller guards (`if (!proc) return ...`). Preserve existing guards; `getTrack(i)` can return null on a bad index — guard like the current code.

No CSS/JS (frontend unchanged except `FrontendRouter.cpp` C++), no build config changes, no new executables. Version bump in task 0.

---

## File Structure

- Modify: `src/mcp/McpTools.cpp` — read helpers `findClip/findNote/findCc/findLane` rerouted to `ReadModel` snapshots (drop `AudioEngine*` param where reads-only).
- Modify: `src/mcp/McpTools_Project.cpp` — read-only tools use `getReadModel()`; write tools use `e->getProjectCommands()` (commands already exist) — fix remaining `getMainProcessor()->getRoutingManager()` and `getTrackModel()` reads.
- Modify: `src/mcp/McpTools_Audio.cpp` — read tools via `getReadModel()`; FX write tools (`setFXBypassed`, `addFXSlotAt`, `setFXSlotPlugin`, `removeFXSlot`, `setAutomationParam`) — add/re-point onto commands.
- Modify: `src/mcp/McpTools_Transport.cpp`, `McpTools_Session.cpp` — remove `AudioEngine*` reads (use `ReadModel::getTransport()`).
- Modify: `src/frontend/FrontendRouter.cpp` — `dispatchAudio`'s `getDeviceManager()` and `dispatchRead`'s `getWaveformPeaks` (which pokes `getProjectModel()`/`getProjectPool()`) moved behind facade-verified helpers.
- Modify: `src/engine/AudioEngine.h` + `src/engine/AudioEngine.cpp` — add ONE missing typed read (`getFxProgramList`, returns `FxProgramListEntry` data) for plugin-instance reads that `McpTools_Audio` needs; the persistent `ReadModel` cannot supply them.
- Note: FX write commands already exist in `ProjectCommands` (`setFxSlotBypassed`, `addFxSlot`, `setFxSlotPlugin`, `removeFxSlot`, `setFxSlotParam`) — no new write commands are needed; the work is rerouting MCP callers off the live processor onto them.
- Modify: `src/engine/AudioEngine.h` — only if a non-leaky read accessor is needed for `FrontendRouter::dispatchAudio` device reads; if the existing `getDeviceManager()` suffices behind a null-guard, leave as-is. Keep changes minimal.

Tests:
- `tests/unit/engine/mcp_tools_read_reroute_test.cpp` (gtest) — asserts MCP read tools return exactly what `ReadModel` exposes (live processor NOT consulted).
- `tests/unit/engine/mcp_fx_write_command_test.cpp` (gtest) — asserts FX write via MCP lands in ValueTree AND survives `rebuildRoutingGraph()` (Gate 6); asserts on live `getMainProcessor()->getTrack(idx)`.
- `tests/unit/frontend/rpc_read_facade_test.cpp` (gtest) — `FrontendRouter` `getWavefieldPeaks` returns peaks without reaching `getProjectModel()` interior (verify the existing seam in `rpc_surface_test.cpp`).

---

## Tasks

### Task 0: Version bump + baseline build + graph refresh

**Files:**
- Modify: `CMakeLists.txt` (VERSION 0.14.1 → 0.15.0)
- Modify: `frontend/package.json` (version 0.14.1 → 0.15.0)

- [ ] **Step 1: Bump both versions**

`CMakeLists.txt` line: `project(HDAW VERSION 0.14.1 ...)`. Verify `frontend/package.json` `"version": "0.14.1"`. Use the version-management doc in `docs/architecture.md`.

- [ ] **Step 2: Baseline the test suite**

Run: `build\Debug\hdaw_tests.exe --gtest_brief=0 2>&1 | Select-Object -Last 5` (or the standard gtest runner). Expected: all suites pass, capture the pass count. This is the regression baseline — every later task must keep it green.

- [ ] **Step 3: Refresh the knowledge graph so plan gates check current topology**

Run: `python.exe -m graphify extract ./src --no-viz --code-only 2>&1 | Select-Object -Last 3` (interpreter at `graphify-out/.graphify_python`). Expected: extraction updates, no errors. This runs before Task 1 so dependency claims are verified against the just-indexed tree.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt frontend/package.json graphify-out
git commit -m "chore: bump to 0.15.0, refresh knowledge graph"
```

---

### Task 1: Add typed read methods to `AudioEngine` for plugin-instance data

Some FX/plugin reads (program list, live params) live ONLY on the live plugin
instance — `ReadModel` (a persisted snapshot) cannot provide them, so the seam is
thin typed accessors on `AudioEngine` that return *data values* while keeping the
graph-pointing reach internal to the engine. MCP stops calling
`getMainProcessor()->getTrack()->getFXChain()` and calls
`engine.getFxProgramList(trackIdx, slotIdx)` instead.

**Files:**
- Modify: `src/engine/AudioEngine.h` (declare `getFxProgramList` — data return)
- Modify: `src/engine/AudioEngine.cpp` (implement — internal graph reach)
- Test: `tests/unit/engine/audioengine_read_facade_test.cpp`

Background: `McpTools_Audio` reads FX data straight through the live graph: `slot->getNumPrograms()`/`getProgramName()` (L190/193), `getAutomatableParams()` (L240), `getParameters()` (L276). These are **plugin-instance reads** — the program/param lists live only on the live plugin, NOT in the ValueTree, so `ReadModel` (a persisted snapshot) genuinely cannot provide them. The correct seam is therefore NOT a ReadModel snapshot but **thin typed read methods on `AudioEngine` itself**: the engine keeps its internal reach into `MainAudioProcessor`/`TrackFXSlot`, but exposes to MCP *data values* (program list, param list) rather than the graph pointers. MCP calls `engine.getFxProgramList(trackIndex, slotIndex)`; it no longer calls `getMainProcessor()->getTrack(...)->getFXChain()`.

- [ ] **Step 1: Write failing test**

```cpp
#include "AudioEngine.h"
#include <gtest/gtest.h>

// Fails: no typed program-list accessor yet; MCP must not need getMainProcessor().
TEST(AudioEngineReadFacadeTest, GetFxProgramListIsTypedAndReturnsData) {
    // Use the engine test seam to build an engine + a track with a program-bearing
    // plugin loaded on FX slot 0.
    // ... setup engine, add a track, add an FX slot with 2 programs
    auto progs = engine.getFxProgramList(/*trackIndex*/ 0, /*slotIndex*/ 0);
    ASSERT_EQ(progs.size(), 2u);      // returns DATA values, not graph pointers
    EXPECT_EQ(progs[0].name, "Init");
    // A rebuild must not change the returned data (engine re-resolves the slot internally).
    engine.getAudioGraphCommands().rebuildRoutingGraph();
    auto progs2 = engine.getFxProgramList(0, 0);
    EXPECT_EQ(progs2.size(), 2u);
}
```

- [ ] **Step 2: Run to confirm FAIL**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=AudioEngineReadFacadeTest.GetFxProgramListIsTypedAndReturnsData`
Expected: compile error (no `getFxProgramList`) — that is the failing signal.

- [ ] **Step 3: Add typed read method to `AudioEngine`**

In `AudioEngine.h` declare (near the facade `getReadModel()`-style accessors):

```cpp
struct FxProgramListEntry { int index; std::string name; };
std::vector<FxProgramListEntry> getFxProgramList(int trackIndex, int slotIndex) const;
```

Implement in `AudioEngine.cpp` — the engine internally re-resolves the live slot and returns only the data:

```cpp
std::vector<FxProgramListEntry> AudioEngine::getFxProgramList(int trackIndex, int slotIndex) const
{
    std::vector<FxProgramListEntry> out;
    auto* tr = getTracks()[trackIndex];            // engine-side graph reach stays here
    if (!tr) return out;
    auto& chain = tr->getFXChain();
    if (slotIndex < 0 || slotIndex >= (int)chain.size()) return out;
    auto* slot = chain[slotIndex].get();
    if (!slot || !slot->isPlugin()) return out;
    int num = slot->getNumPrograms();
    for (int i = 0; i < num; ++i)
        out.push_back({i, slot->getProgramName(i).toStdString()});
    return out;
}
```

_(The engine already owns `MainAudioProcessor`/`Track` reach — keep it there; this method is additive, not a new god edge from MCP. The GETTERS `getMainProcessor()`/`getTrack()` are NOT re-exposed to MCP consumers.)_

- [ ] **Step 4: Run tests to confirm pass**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=AudioEngineReadFacadeTest.*`
Expected: PASS. Then run full suite `build\Debug\hdaw_tests.exe` to confirm no regression.

- [ ] **Step 6: Commit**

```bash
git add src/common/ReadModel.h src/engine/ReadModelImpl.h src/engine/ReadModelImpl.cpp tests/unit/engine/read_model_facade_test.cpp
git commit -m "feat(engine): add typed getFxProgramList read facade"
```

---

### Task 2: Reroute read-only MCP tools onto `ReadModel`

**Files:**
- Modify: `src/mcp/McpTools_Project.cpp`
- Modify: `src/mcp/McpTools_Audio.cpp`
- Modify: `src/mcp/McpTools.cpp`
- Modify: `src/mcp/McpTools_Transport.cpp`
- Modify: `src/mcp/McpTools_Session.cpp`

**Success gates:** no remaining `getMainProcessor()`/`getRoutingManager()`/`getTrackNode()` call in a read-only tool; every read helper returns the same data via `ReadModel.snapshot()`.

- [ ] **Step 1: Map read-only vs write tools (no code yet)**

Read each `register*Tools` block in the five files. Classify every tool as READ (returns snapshot state: clip list, track list, BPM, scale, FX-slot list, meters) or WRITE (mutates project: add/move/remove FX slots, set bypass, set plugin, set automan param). Writes use the existing `ProjectCommands`/`AudioGraphCommands` (already present — see Task on FX in the File Structure; verified `setFxSlotBypassed`, `addFxSlot`, `setFxSlotPlugin`, `removeFxSlot`, `setFxSlotParam` all exist). Record the READ-only list in a task note.

- [ ] **Step 2: Repoint `getBPM()` to `ReadModel`**

`McpTools_Project` currently: `double bpm = e->getTransportManager().getBPM();`.
Change to `double bpm = e->getReadModel().getTransport().bpm;`.
Replace all 9 sites (L82,119,328,353,389,465,487,819). Use the return of a single local per tool.

- [ ] **Step 3: Repoint scale reads to ReadModel**

`e->getProjectModel().getScaleRoot()/getScaleMode()` and `m.getScaleMode()/getScaleRoot()` (L48,860-972) → `e->getReadModel().getScaleRoot()/getScaleMode()`.

- [ ] **Step 4: Repoint track/clip read helpers in `McpTools.cpp`**

`findClip/findNote/findCc/findLane` currently walk `getProjectModel().getTrackListTree()`. Note: this walks the ValueTree *model* (comm 12), which is the source of truth, NOT the audio graph (comm 5/26/141) — so it is NOT the hard god-leak. Leave these model-tree reads untouched; the hard leaks are only the ones that reach into `getMainProcessor()`/`getRoutingManager()->getTrackNode()` (the realtime graph). Keep this task's scope to those hard leaks.

- [ ] **Step 5: Reroute `McpTools_Audio` audio-graph READS off the live processor**

The `proc->getTrack(ti)->getFXChain()` READ tools (L26-30, L183-193, L233-241, L268-276) → use `e->getReadModel().getFxSlots(ti)` + `e->getInternalFxParams` for persisted data, and the new typed `e->getFxProgramList(ti, si)` (Task 1) for the plugin-instance reads that only the live plugin can answer. Drop the live `getFXChain()`/`getTrackMeter()` read.

Which calls are READ vs WRITE (verified from the call grid):
- READ: L26 (getTrack→chain read), L183 (chain read), L233 (chain params list), L268 (chain params).
- WRITE: L154 `addFXSlotAt`, L156 `setFXSlotPluginID`, L171 `removeFXSlot`, L223 `setFXBypassed`, L282 `setAutomationParam` — replace EACH with its existing `ProjectCommands` equivalent (`addFxSlot`, `setFxSlotPlugin`, `removeFxSlot`, `setFxSlotBypassed`, `setFxSlotParam`). Do NOT call the live processor for writes.

- [ ] **Step 6: `McpTools_Transport` / `Session`**

Remove `e->getAudioEngine()` reads; use `e->getReadModel().getTransport()` and `getProjectSnapshot()`. Keep the MCP tool response shape unchanged.

- [ ] **Step 7: Full build + read gates**

Run: `cmake --build build --config Debug`. Then `build\Debug\hdaw_tests.exe`. Then a manual grep: `grep -rn "getMainProcessor()->getTrack" src/mcp src/frontend` — expect 0. `grep -rn "getRoutingManager()->getTrack"` — expect 0.

- [ ] **Step 8: Commit**

```bash
git add src/mcp tests
git commit -m "refactor(mcp): route reads through ReadModel facade, drop getMainProcessor graph reads"
```

---

### Task 3: `FrontendRouter` — `getWaveformPeaks` + `dispatchAudio`

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp`
- Test: `tests/unit/frontend/rpc_read_facade_test.cpp` (verify seam pattern in `rpc_surface_test.cpp`)

The two wedges from the analysis: (a) `dispatchRead`'s `getWaveformPeaks` (FrontendRouter.cpp:1377-1467) reaches `getProjectModel()`/`getProjectPool()`; (b) `dispatchAudio` (L790) holds the whole `AudioEngine`.

- [ ] **Step 1: Assess seam for `getWaveformPeaks`**

Read `tests/unit/frontend/rpc_surface_test.cpp` to find the seam to construct the router with a fake engine or read-model. If none, add a seam (a slim `EngineView` accessor).

- [ ] **Step 2: Move waveform-peak read into a read-model-backed helper**

The peaks read `getProjectPool().getFormatManager().createReaderFor(file)` and walks `getSnapshot()`. Add an engine-side typed accessor `getWavePeaks(clipId, numBins)` that reads the clip's sourceFile from the pool (behind the engine) and returns peak pairs via a typed engine accessor. Update the router's `dispatchRead` case to call `getReadModel().getWavePeaks(...)`.

- [ ] **Step 3: `dispatchAudio` facade**

Change `dispatchAudio(AudioEngine& engine, ...)` device reads (`getDeviceManager()`, `dm.getAvailableDeviceTypes()`, ...) to a seam `Engine::getDeviceInfo()` that the engine implements, instead of the whole engine passed in. Keep device-write methods (which need the device manager) as they are or behind a `getDeviceManager()` facade, validated.

- [ ] **Step 4: Tests**

gtest for `getReadModel().getWavePeaks` path (returns peaks; doesn't touch audio graph). Must PASS.

- [ ] **Step 5: Full build + suite + grep**

`cmake --build build --config Debug`; `hdaw_tests.exe`; grep `getMainProcessor` returns 0 in mcp + router.

- [ ] **Step 6: Commit**

```bash
git add src/frontend/FrontendRouter.cpp src/engine/ReadModelImpl.cpp tests
git commit -m "refactor(frontend): waveform peaks via ReadModel, thin dispatchAudio device reads"
```

---

### Task 4: Integration gates, latency/quality check, graph refresh, final verification

**Files:**
- (verification only; no code unless gates fail)

- [ ] **Step 1: Full build + all suites**

`cmake --build build --config Debug` succeeds; `build\Debug\hdaw_tests.exe` 100% pass; `cd frontend && npm test` pass (any component touched — list them).

- [ ] **Step 2: Latency & quality evaluation (AGENTS.md, lessons 7–8)**

Measure `getTotalLatency()` before/after via the transport/`Engine` latency read-out; confirm unchanged (this change does not alter `processBlock`, buffer sizes, or plugin graph topology — latency MUST be identical). A/B audition if a plugin edit path changed. Document the measured value in the completion.

- [ ] **Step 3: MCP parity & surface audit**

`grep -rn "getMainProcessor\|getRoutingManager" src/mcp src/frontend/src/frontend` → 0 real hits. Every feature previously reachable via the (now-facaded) reads still reachable: search the MCP tool registry still lists every tool.

- [ ] **Step 4: Knowledge graph refresh**

`python -m graphify extract ./src --no-viz --code-only` (re-run merge) so the graph reflects the facade callers. Verify the seams have moved (`getMainProcessor` edges from mcp/frontend resolved).

- [ ] **Step 5: Final commit + tag**

```bash
git add src tests docs
git commit -m "refactor: complete read facade extraction; all consumers off the live graph"
git tag v0.15.0
```

---

## Completion Contract

1. All gates above pass with evidence (test output, grep output, build results).
2. `hdaw_tests.exe`, `npm test` pass.
3. No `getMainProcessor()`/`getRoutingManager()->getTrack()` live-read remains in `src/mcp` or `src/frontend/FrontendRouter.cpp`.
4. Write path uses command layer (undo/rebuild-safe), not live `Track` set calls.
5. Latency and fidelity unchanged (measured).
6. Knowledge graph reforested; plan gates updated.
7. No new anti-patterns (single batches, no live-graph calls, no raw god-node new edges).

## Rollback

If integration fails, `git revert` merges; the facade is additive (old comment path kept behind a `TODO` until Task 4 confirms) — but note final state fully replaces old reads. . Revert to the current `v0.14.1` state if hand breakage.