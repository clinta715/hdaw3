# Plan: CLAP lifecycle thread-contract fix (Odin2 isolated silence)

**Status: RESOLVED (2026-08-10).** Goal reached: Odin2 renders audio on isolated
export (`peak≈0.5`) and the in-process teardown `abort()` is gone. The fix is
committed in this session; `kKnownSilent` now holds only genuinely-silent
defaults (ShinRonin, Gneiss, Retrospect).

## Symptom

`McpServer.DiagnosticClapExportMatrix` reported Odin2 as `peak=0` on isolated
export forever. The prior story (AGENTS.md lesson 16, 2026-08-09) claimed Odin2
"fail-fasts via `std::terminate` inside its own `noexcept` activate" — with
source access (`D:\pdf\odin2-NightlyDevel`) that theory was checked and found
unsupported: **no `noexcept`, no `throw` anywhere in Odin2's `Source/`**, and
its default patch produces sound (verified in Bitwig by the user). The real
mechanism was found empirically:

## Root cause (proven by child-side diagnostics + cdb)

CLAP plugins built with thread-checking (Odin2.clap is a Debug build using
`clap::helpers::Plugin<MisbehaviourHandler::Terminate, CheckingLevel::Maximal>`)
query `clap_host_thread_check::is_main_thread` during `activate()` /
`deactivate()` / state calls. HDAW's `CLAPHost::threadCheckIsMainThread()`
(`src/engine/CLAPPluginInstance.cpp`) answers true only for the JUCE message
thread or the export render thread (`proxy::isRenderThread()`). Two violations:

1. **Isolated child died at PREPARE.** `PluginHost::controlLoop` runs on a pipe
   thread and called `plugin->prepareToPlay()` (→ CLAP `activate()`),
   `setStateInformation()`, `getStateInformation()` directly. The control
   thread is neither the child's message thread nor a render thread → the
   plugin's thread check fails → `std::terminate` → child exits silently (abort
   dialog suppressed). Evidence: 0 blocks consumed (endless `SPIN w-r=0`), no
   `plugin_host` log, no dump, export "completes" at CPU speed with silence.
2. **In-process export teardown aborted.** `ExportManager::renderThreadFunc`
   ran `finish: ...; proxy::setRenderMode(false);` while the local
   `renderGraph` was destroyed only when the function returned — i.e. AFTER
   render mode cleared. `~AudioProcessorGraph` → `~TrackFXSlot` →
   `~CLAPPluginInstance::releaseResources` → `plugin->deactivate()` then ran on
   the export worker with render mode off → thread check false →
   `std::terminate`/`abort` (cdb: `ucrtbase!abort`, c0000409, on the render
   thread). This also made in-process `HDAW_NO_PLUGIN_ISOLATION=1` runs die at
   Odin2's teardown.

(An additional, earlier fix in this session: `start_processing()` was called
from `prepareToPlay` on the message thread — the CLAP spec requires the audio
thread. It is now deferred to the first `processBlock`. This alone recovered
in-process Odin2 rendering but did not fix the child, whose death was the
`activate()` thread check.)

## Fix

- **`src/proxy/host/PluginHost.{h,cpp}`** — new
  `PluginHost::runLifecycleOnMessageThread(fn, timeoutMs)`: posts `fn` via
  `juce::MessageManager::callAsync` and waits bounded; exceptions are captured
  as `std::exception_ptr` and rethrown on the control thread so the existing
  try/catch + `pluginFailed` handling is unchanged. All four control-thread
  lifecycle call sites (PREPARE, SET_STATE, STATE_CHUNK completion, GET_STATE)
  now marshal their `plugin->...` call through it. The try/catch stays as a
  second line of defense (`PluginIsolation.ControlThreadPluginExceptionContained`
  sentinel test still passes).
- **`src/engine/ExportManager.cpp`** — the render pipeline is scoped so the
  `renderGraph` destructor runs while render mode is still set: `finish:`
  does `renderGraph.releaseResources(); renderGraph.clear();` inside the scope,
  and `proxy::setRenderMode(false)` runs after the scope closes. `clear()`
  alone is insufficient (the graph's baked `GraphRenderSequence` keeps
  `Node::Ptr` refs, so nodes only die in `~AudioProcessorGraph`).

## Evidence

- Isolated matrix (post-fix): Odin2 `peak=0.592865` → stable `peak≈0.50`;
  child logs showed `act=1 proc=1`, `startProcessing ret=1`,
  `process status=1`, non-zero output peaks. ShinRonin/Gneiss/Retrospect remain
  `peak=0` with a fully healthy child pipeline — their factory defaults are
  genuinely silent (verified identical across runs).
- In-process matrix (`HDAW_NO_PLUGIN_ISOLATION=1`): completes all 10 plugins,
  no c0000409 (previously died at Odin2 teardown); Odin2 `peak=0.408`.
- `McpServer.DiagnosticClapExportMatrix` PASSES with Odin2 asserted.

## Files changed

- `src/proxy/host/PluginHost.h` — helper declaration + `#include <functional>`
- `src/proxy/host/PluginHost.cpp` — helper + 4 marshaled call sites
- `src/engine/ExportManager.cpp` — render-graph scoping at `finish:`
- `src/engine/CLAPPluginInstance.cpp` — (prior fix) `start_processing` deferred
  to first `processBlock`
- `tests/integration/mcp/mcp_server_test.cpp` — Odin2 removed from
  `kKnownSilent` (NodalRed2x had already been removed by the multi-port fix);
  trio kept with updated evidence comment
- `AGENTS.md` — lesson 16 corrected (thread-check terminate, not `noexcept`)
- `docs/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md` —
  disposition updated
- `docs/handoffs/2026-08-09-nodalred2x-multiport.md` — Odin2 claims corrected

## Residual

- ShinRonin/Gneiss/Retrospect stay in `kKnownSilent` (genuinely silent default
  patches; healthy child). Preset-load sweep would be the follow-up.
- Odin2.clap is a Debug build (~38 MB) with full thread-checking — the
  authoritative test bed for any future CLAP threading contract change.
