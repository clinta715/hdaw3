# Sampler RPC/MCP Method Family — Implementation Plan

> Addresses `docs/plans/2026-08-13-internal-sampler-plan.md` Tasks 10-11 (never
> landed as named) found by the plan audit (2026-08-17). Also fixes the
> SamplerEditor's broken RPC calls.

## Goal
Land the `sampler.*` RPC namespace + `sampler_*` MCP tools the plan promised
(`setSample`, `setParam`, `setMode`, `setSliceMode`, `detectSlices`,
`triggerSlice`, `getState`), with slice detection + audition, and fix the
frontend editor so those calls actually work.

## Discovery (verified this session)
- **The frontend dispatcher has NO `sampler` namespace.** `FrontendRpc.h`
  `method::` constants stop at `library`. So the SamplerEditor's
  `rpc.call("sampler.setSample", …)`, `rpc.call("setFxSlotParam", …)` (no dot →
  -32601), and `setMode` → `setFxSlotParam paramIndex:6` (= Hold, not mode) are
  ALL BROKEN today. Only `read.sampler.getState` works (read ns accepts
  `sampler.getState` sub-method).
- Engine already supports slicing: `SamplerSound::slicePoints` (frames),
  `SliceDetector::grid/transient`, `SamplerEngine::handleNoteOn` chromatic slice
  mapping, `SamplerVoice::startSlice`. `TrackFXSlot::loadSamplerState` does NOT
  read slicePoints from the tree (needs extension).
- `mode` is a slot-tree property ("classic"/"one-shot"/"slice") consumed in
  `loadSamplerState`; NOT a param. `setSamplerMode` must write the tree prop +
  rebuild so the live engine picks it up.
- `triggerSlice` (audition) needs a message-thread→audio-thread gate: the
  engine's `reloadGate_` idiom is the model (message thread writes atomics,
  `render()` consumes at block start).

## Success Gates (all must pass with evidence)
- [x] Gate A: `cmake --build build --config Debug` succeeds. — 0 errors; `hdaw_tests.exe` fresh (8/17 5:30 PM); `Router_Sampler.obj` compiled.
- [x] Gate B: `FrontendServer.SamplerRpcFamily` passes — add sampler slot → setSample → setMode slice → setSliceMode → detectSlices → getState (mode/slicePoints) → triggerSlice → LIVE engine `currentSound()->slicePoints.size()>=2` → mono property round-trip. Re-confirmed in full-suite run.
- [x] Gate C: sampler/slice suites green — `SamplerEngine.* 7` (incl. 2 new audition tests), `SamplerVoice.* 2`, `SamplerFxSlot.* 6` (incl. `LoadStateRestoresSlicePoints`), `SliceDetector.* 3`, `LoopCrossfade.* 4` = 22.
- [x] Gate D: `FrontendServer.*` 12/12 (in full run).
- [x] Gate E: MCP — `GuiFuncTest.SamplerToolsRegistered` (new tools in `server->tools()`) + `GuiFuncTest.SamplerSetModeRoundTrip`; `GuiFuncTest.*` 72/72.
- [x] Gate F: `npm run build` ✓ + `npm test` 344/344 (SamplerEditor 9 tests).
- [x] Gate G: E2E `sampler.spec.ts` 2/2 — load sample → set mode slice → detect grid slices → slice list renders → click slice fires `sampler.triggerSlice` (re-ran against final binary).
- [x] Gate H: full engine suite (exclusion filter) — `839/839 passed` (re-ran after the transient audio-device outage recovered; the first post-change run hit 25 `getTrack()==null` failures from a machine audio-endpoint outage — `DirectSound "Primary Sound Driver: No driver"` in `%TEMP%\hdaw_debug.log` — environmental, same binary passed at 4:41/5:12, not a code regression).
- [x] Gate I: pitfall audits — Gate 2 (full path RPC→command→live engine asserted in gtest + E2E), Gate 3/13 (audition = 3 atomics on message thread, `applyPendingAudition` audio-thread consumer at block start, no alloc/lock/string on RT), Gate 1/10 (mode + slicePoints in tree, `loadSamplerState` restores; `LoadStateRestoresSlicePoints` asserts live engine), Gate 5 (editor deps audited), Gate 8 (tokens only), Gate 9 (bounds/null on slot/slice indices + pointers).

## Dependency Map
- **Blast radius:** new `method::Sampler` constant (additive), new
  `dispatchSampler` in FrontendRouter.cpp (additive), new `Router_Sampler.{h,cpp}`,
  `AudioEngineCommands` sampler methods, `SamplerEngine::triggerSlice` (additive),
  `TrackFXSlot::loadSamplerState` (additive, sampler-only), `ReadModel.h`
  `SamplerStateSnapshot` (additive defaults), `Router_Read`/`Router_Project`
  (leave existing dead `sampler.setSample`/`read.sampler.getState` paths working),
  `McpTools_Audio.cpp` (4 new tools), `SamplerEditor.tsx/.css/.test.tsx`,
  `frontend/e2e/sampler.spec.ts`.
- **Upstream:** frontend editor (fixed), MCP clients (new tools).
- **Downstream:** `TrackFXSlot::loadSamplerState` (slicePoints parse), engine
  audio-thread render (audition gate — audio-thread safe, atomics only).
- **God nodes:** none touched (AudioEngineCommands is a hub but additions are
  leaf methods).
- **Projections:** ReadModel getSamplerState (extended, additive); snapshot
  fullSync on slot-tree changes (unchanged behavior).
- **SPSC:** the audition gate crosses message→audio — atomics only, release on
  write / acq_rel exchange on consume. No lock, no allocation.
- **MCP parity:** all 4 new RPCs get MCP twins; `sampler_get_state` extended.

## Contract — RPC methods (all under `sampler.` namespace)
- `sampler.setSample` `{trackIndex, slotIndex, filePath, rootNote?}` → void.
- `sampler.setParam` `{trackIndex, slotIndex, paramIndex, value}` → writes
  `param_N` tree prop (reuses setFxSlotParam semantics).
- `sampler.setMode` `{trackIndex, slotIndex, mode}` mode ∈ {classic,one-shot,slice}
  → writes tree "mode" + rebuildTrackFX.
- `sampler.setSliceMode` `{trackIndex, slotIndex, sliceMode, sliceGrid?,
  sliceSensitivity?}` sliceMode ∈ {transient,grid} → stores
  "sliceMode"/"sliceGrid"/"sliceSensitivity" props.
- `sampler.detectSlices` `{trackIndex, slotIndex, sliceMode?, sliceGrid?,
  sliceSensitivity?}` → decodes the sample file (message thread), runs
  SliceDetector (transient or grid w/ transport BPM), stores "slicePoints" as a
  comma-separated normalized string, rebuilds, returns
  `{ok, totalSlices, slicePoints:[normalized…]}`.
- `sampler.triggerSlice` `{trackIndex, slotIndex, sliceIndex, velocity?}` →
  requires mode==slice + slices present; calls `SamplerEngine::triggerSlice`
  (atomic gate; consumed at next render block start). Returns
  `{ok, totalSlices}`.
- `sampler.getState` → same JSON as `read.sampler.getState` + slice fields
  (`sliceMode`, `sliceGrid`, `sliceSensitivity`, `slicePoints`).

## Pitfall Gates Triggered
- **Gate 2:** full path asserted in gtest (live engine) + E2E (UI). No dead
  handler: every method has a command/engine consumer.
- **Gate 3/13:** `triggerSlice` writes 3 atomics on message thread; `render()`
  consumes via `exchange` and runs `applyPendingAudition` (audio thread,
  mirroring `handleNoteOn` — voice pool preallocated, no allocation).
- **Gate 1/10:** mode + slicePoints live in the tree; `loadSamplerState` reads
  them → rebuild restores. Test: mutate mode + detect → rebuild → assert live
  `samplerEngineForTest()->currentSound()->slicePoints` + mode.
- **Gate 5:** editor callbacks — `slotIndex`/`selectedTrackIndex` stable;
  `detectSlices`/`triggerSlice` read fresh via refs where needed; deps audited.
- **Gate 8:** reuse existing `.sampler-editor__*` classes; tokens only.
- **Gate 9:** bounds-check slotIndex/sliceIndex/mode strings; null-check
  processor/track/slot/engine/sound.

## Anti-Pattern Scan
- Single RPC per user action (no loops). No `syncSnapshot`. No full-tree walks
  (indexed `findFxSlot`). No raw hex in CSS. No `rebuildRoutingGraph` (only
  `rebuildTrackFX(trackIndex)` for mode/sample/slice changes, the existing
  per-slot pattern).

## Steps
1. **C++ engine (subagent A):** `SamplerEngine` `triggerSlice` + `applyPendingAudition`
   (atomics + audio-thread consumer in render after applyPendingParams);
   `TrackFXSlot::loadSamplerState` reads "slicePoints" (normalized, comma-sep) →
   builder.slicePoints (frames); `ReadModel.h` extend SamplerStateSnapshot
   (+`sliceMode`/`sliceGrid`/`sliceSensitivity`/`slicePoints`) and
   `ReadModelImpl::getSamplerState` populates them; add
   `AudioEngineCommands` sampler methods (`setSamplerMode`, `setSamplerSliceMode`,
   `detectSamplerSlices` (decode file, SliceDetector, store string, rebuild,
   return normalized points), `triggerSamplerSlice` (validate + gate));
   `sampler_engine_test.cpp` new `TriggerSlice` unit test.
2. **C++ router+MCP (subagent B):** `FrontendRpc.h` add `method::Sampler`;
   `FrontendRouter.cpp` dispatch; new `Router_Sampler.{h,cpp}` with the 7
   handlers (getState mirrors Router_Read; keep `read.sampler.getState`
   working); `McpTools_Audio.cpp` add `set_sampler_param`, `set_sampler_mode`,
   `detect_sampler_slices`, `trigger_sampler_slice` + extend `sampler_get_state`;
   `sampler_rpc_test.cpp` (or add to frontend_server_test.cpp) covering Gate B/E.
3. **Frontend (subagent C):** `SamplerEditor.tsx` — switch to `sampler.setSample`/
   `sampler.setParam`/`sampler.setMode`/`sampler.getState`; add slice strip
   (slice-mode select, sensitivity/grid inputs, Detect button, slice list with
   click-to-audition via `sampler.detectSlices`/`sampler.triggerSlice`) shown in
   slice mode; CSS additions using existing classes/tokens; update
   `SamplerEditor.test.tsx` to the new RPC names + slice actions.
4. **E2E (subagent D, after A+B+C):** extend `frontend/e2e/sampler.spec.ts`:
   add sampler slot → `sampler.setSample` (writeSineWav) → `sampler.setMode`
   slice → `sampler.detectSlices` → getState slicePoints non-empty → click a
   slice → assert `sampler.triggerSlice` called.

## Completion Contract
- All gates A–I pass with evidence.
- Diff scanned (no anti-patterns, deps confirmed, no silent breakage).
- MCP parity: new tools registered + tested.
- Knowledge graph refreshed (`index_repository` fast) after the new
  RPC namespace/files.