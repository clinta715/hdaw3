# Plan: Isolate Offline Export from Live Plugin Machinery (isolation-export-wedge)

Date: 2026-08-11 · Area: src/engine, src/proxy · Severity: high (export hang)

## Goal
Make isolated-plugin offline export immune to live-graph interference: the export
render graph must not share slot ids, health monitoring, crash recovery, or
proxy registries with the live graph, and the JUCE non-realtime bake wait must be
bounded so a stalled bake fails the export instead of hanging forever.

## Root cause (evidence: %TEMP%\hdaw_debug.log, backup in .opencode/…/hdaw_debug_wedged_backup.log)
1. `McpExportTool` passes `e->getPluginManager()` into `ExportManager::renderThreadFunc`
   (McpExportTool.cpp:79) — the export graph's isolated instances are created in the
   SAME `PluginManager`/`ProxyProcessManager` as the live graph: shared
   `nextProxySlotId`, shared health monitor (`ProxyProcessManager::checkAllChildren`,
   started at PluginManager.cpp:647), shared `CrashRecoveryManager`, shared
   `liveProxySlots` registry and per-slot crash callbacks (PluginManager.cpp:604-625).
2. During the wedged export the LIVE graph repeatedly rebuilt FX chains and spawned
   hosts (log: `FXRebuild … sr=44100 getSr=44100` at 22:41:31 → slot 352, 23:28:16 →
   slot 353). Slot 353 collided with the export's own instance; `spawnPluginHost`'s
   defensive `killPluginHost(slotId, KillHard)` (ProxyProcessManager.cpp:34) killed
   the export's child and replaced the shm region — the export's `PluginProxySlot`
   keeps a cached `shmHandle` (shared_ptr with no-op deleter) pointing at the freed
   region.
3. The churn self-sustains: killed export children → crash callbacks →
   `CrashRecoveryManager` entries → live-graph FX rebuilds (log shows the cycle
   continuing every 11→38 s with growing intervals — one `MidiClipConn`+`MidiClipEntry
   prepareToPlay sr=44100 spb=2560` pair per cycle = one live clip node re-added per
   rebuild pass) — the live graph never settles, the export render thread never
   completes a block, WAV stays 0 bytes, idle children yield-spin (~50 % CPU each).
4. Even without churn, `AudioProcessorGraph::processBlock` in non-realtime mode
   (export sets `setNonRealtime(true)`, ExportManager.cpp:188) spins in
   `while (renderSequenceExchange.getAudioThreadState() == nullptr) Thread::sleep(1)`
   (juce_AudioProcessorGraph.cpp:1888-1895) with NO timeout — if the bake never
   lands, the export hangs forever with no output and no error.

## Fix layers (each independently valuable)

### Layer 1 — Export gets its own plugin domain (primary)
Create the export's RoutingManager against a DEDICATED `PluginManager` instead of the
live one, mirroring the existing `ProjectModel localModel` copy pattern in
`ExportManager::renderThreadFunc`:
- The dedicated PM must be seeded with the plugin list (copy the live PM's
  knownPluginList / resolveIdentifierToPath needs) and formatManager; its
  ProxyProcessManager is a fresh instance whose health monitor is NEVER started and
  whose crash callbacks are registered only into its own registry.
- Consequence: export slot ids live in their own counter space; live rebuilds can
  never kill export children (`killPluginHost` only ever touches its own PM's map);
  export crash callbacks never cascade into live-graph rebuilds.
- Teardown: dedicated PM destroyed at the end of `renderThreadFunc` (its
  `~ProxyProcessManager` terminates its children) — reuse the existing RAII
  respawn-suppression guard ordering.

### Layer 2 — Bounded render-sequence bake wait (defense in depth)
Replace the implicit infinite JUCE spin with an explicit bounded wait in
`ExportManager::renderThreadFunc`: after `setNonRealtime(true)`, wait up to N seconds
(e.g. 15 s) for the first render sequence to be baked (poll
`renderGraph`'s render sequence readiness — expose a helper on the graph or probe via
`processBlock` on a zeroed buffer once; simplest: keep calling processBlock with a
bounded overall deadline in the loop itself — if the first K calls never complete,
fail the export with "render graph bake timeout" + delete the partial file). The
existing per-block proxy spins (200 ms) remain bounded; the change is only about the
bake/stall case never blocking forever.

### Layer 3 — Verify no other live/export shared state
Audit `PluginManager`/`ProxyProcessManager`/`CrashRecoveryManager` for anything else
shared across graphs during export (e.g. `liveProxySlots` erasure from export-slot
crash callbacks, `saveStateToTemp`/`hdaw_proxy_state_*` files keyed by slot id which
would collide across domains) and scope each to the owning PM. Crash-recovery state
files should be keyed per-domain (or the export domain should skip
`saveStateToTemp`).

## Success gates (all must pass, with evidence)
- [ ] Gate 1: Repro harness — load `polyrhythm_aminor_120bpm.hdaw3` (475 clips, 10
      tracks, 5× Identity + 4× JE8086 isolated), export 0–240 s with isolation ON
      (no HDAW_NO_PLUGIN_ISOLATION): export completes, WAV ≈ 69,120,044 bytes,
      and completes in < 20 min. Harness: the C# stdio MCP client in
      `%TEMP%\opencode\mcp_repro*.ps1`.
- [ ] Gate 2: The wedge signature is gone — during the export the debug log shows NO
      `MidiClipConn`/`MidiClipEntry prepareToPlay sr=44100` churn cycles and NO
      `rebuildFXChain sr=44100` / `spawnPluginHost` for slots owned by the export.
- [ ] Gate 3: `build\Debug\hdaw_tests.exe` — `PluginIsolation.*`, export suites, and
      `TransportLoopback.*` pass; no regressions in the full suite (run full binary,
      report failures if any).
- [ ] Gate 4: In-process export (HDAW_NO_PLUGIN_ISOLATION=1) of the same project
      still completes (regression).
- [ ] Gate 5: Bounded-bake path exists: if the bake cannot land within the timeout,
      the export reports failure (message) instead of hanging (code-reviewed; test
      via a deliberately mis-configured graph if feasible).
- [ ] Gate 6: Binary freshness — build `build\Debug`, confirm the export ran against
      the newly built engine (timestamps; lesson 15).

## Dependency map
- Upstream callers of the changed paths: `McpExportTool` (export_audio MCP tool),
  `FrontendRouter` (dispatchExport), `ExportManager::startExport`.
- Downstream: render WAV/AIFF/FLAC output; the read.snapshot project state is
  untouched.
- Files in scope (verify current layout while implementing):
  `src/engine/ExportManager.cpp`, `src/engine/PluginManager.{h,cpp}`,
  `src/proxy/ProxyProcessManager.{h,cpp}`, `src/proxy/PluginProxySlot.cpp`,
  `src/engine/CrashRecoveryManager.{h,cpp}`, tests in `tests/` (export + isolation).
- God nodes: `PluginManager::createPluginInstance`, `ProxyProcessManager::spawnPluginHost`.
- Projections: none (export is offline; ReadModel/frontend untouched).
- SPSC paths: the proxy ring buffers are per-slot; per-domain PM keeps them disjoint.

## Pitfall gates triggered
- Gate 3 (audio-thread safety): all waits stay bounded (existing 200 ms spins +
  new bounded bake deadline); no locks/allocations added to processBlock paths.
- Gate 4 (stale binaries): after C++ changes verify the freshly built engine runs the
  export (Gate 6).
- Gate 9 (id collisions): per-domain slot counters make cross-graph collision
  impossible; crash-state temp files must be per-domain.
- Gate 2 (full path): the repro harness exercises export → wav bytes end-to-end.

## Steps
1. Read current `ExportManager::renderThreadFunc`, `PluginManager` ctor/list seeding,
   `ProxyProcessManager` ownership, `CrashRecoveryManager` integration.
2. Implement Layer 1 (dedicated export PluginManager + wiring through RoutingManager
   construction in renderThreadFunc), Layer 2 (bounded bake wait + failure path),
   Layer 3 audit.
3. Build `cmake --build build --config Debug`; fix compile errors.
4. Run targeted gtests, then full `hdaw_tests.exe`.
5. Run repro harness (isolated) + in-process regression (Gate 1/2/4); check log
   signature (Gate 2); confirm wav bytes (Gate 1).
6. Report evidence per gate.

## Anti-pattern alerts to avoid
- Do NOT add per-clip loops in export; do NOT touch the live graph's slots.
- Do NOT disable isolation as the fix — the fix is isolation OF the export.
- No new raw logging on the export render hot path beyond existing HDAW_LOG tags.
