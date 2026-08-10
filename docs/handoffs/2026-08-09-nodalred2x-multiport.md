# HANDOFF: NodalRed2x multi-port CLAP support — verified working, finish + commit

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, MSVC/VS generator, JUCE 8 via CMake FetchContent (`build\_deps\juce-src`).

## Mission (remaining work)

1. **Run the full suite** (command below) — expect 637 pass / 4 known-baseline failures in `PluginIsolation.*` (SpawnWithBadPluginExits, CheckAllChildrenFiresCallback, CrashDetectionViaSelfExit, PerSlotCrashCallback — they assert a pre-`e917c1f` contract; pre-existing, documented in AGENTS.md).
2. **Commit the 9-file uncommitted WIP** (see below) with a message like:
   `fix: multi-port CLAP support — NodalRed2x (4-out Nord-2x port) renders audio`
3. **Update docs**: add the multi-port lesson to `AGENTS.md` (lesson 17 candidate: "CLAP multi-port plugins must get their summed channel count — the host, not the plugin, decides the buffer width") and mark the plan in `docs/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md` as RESOLVED for NodalRed2x.
4. **Optional**: a gtest for multi-port width (e.g., extend the `__transportprobe__`-style sentinel with a 2-port variant and assert the child prepares 4 channels).

## CURRENT STATE — verified working (2026-08-09, ~21:30)

- **Build:** `cmake --build build --config Debug` → exit 0, no errors.
- **Matrix green:** `McpServer.DiagnosticClapExportMatrix` PASSES — **NodalRed2x now renders `peak=0.247864`** (was 0.0 forever) and **Odin2 now renders `peak≈0.5`** (recovered by the CLAP lifecycle thread-contract fix — see `2026-08-10-clap-lifecycle-thread-contract.md`). ShinRonin/Gneiss/Retrospect remain `kKnownSilent` with a healthy child pipeline (their factory default patches are genuinely silent).

## Uncommitted WIP — 9 files, +148/-12 (the multi-port fix)

- `src/engine/CLAPPluginInstance.cpp` — `buildBuses()` now **sums ALL output ports' channel counts** (was: main port only). Inputs stay main-port-only (sidechains). `numOutputs` for NodalRed2x = 2+2 = 4.
- `src/engine/CLAPPluginInstance.h` — added `getNumInputChannels()` / `getNumOutputChannels()`.
- `src/proxy/ProxyCommon.h` — `ShmHeader` + `pluginNumInputChannels`/`pluginNumOutputChannels` (child writes once at load, parent reads at prepare); + `kMaxShmChannels=8`, `kMaxShmBlockSize=4096`, `kMaxShmCapacitySamples=32768` (mapping is sized for the worst case at spawn).
- `src/proxy/ProxyProcessManager.cpp` — spawn creates the shm mapping with `computeShmSize(kMaxShmChannels, kMaxShmBlockSize)` instead of `(2,512)` (the child grows `hdr->capacity` at PREPARE; indexing must stay inside the mapping).
- `src/proxy/host/PluginHost.cpp` — (a) PREPARE handler grows `hdr->capacity` to fit `blockSize×numChannels` (clamped to `kMaxShmCapacitySamples`); (b) audioLoop scratch-buffer resize condition now also checks **channels** (`getNumSamples() != preparedBlockSize || getNumChannels() != preparedNumChannels`); (c) `loadPlugin()` writes the hosted plugin's summed channel counts into the shm header; (d) added `#include "engine/CLAPPluginInstance.h"`.
- `src/proxy/PluginProxySlot.h/.cpp` — `setNumChannels()`; `getReportedNumInputChannels()/getReportedNumOutputChannels()` (NOT overrides — juce 8 `getTotalNum*Channels()` is **non-virtual**, confirmed in `juce_AudioProcessor.h:752,766`); `prepareToPlay` refreshes the reported counts from the shm header **before** sending PREPARE and bumps `numChannels` to the plugin's output width; `fillInPluginDescription` reports the real counts.
- `src/engine/TrackFXSlot.h` — the **workspace**: in `prepare()`, call `prepareToPlay` FIRST (the proxy refreshes its reported width during the PREPARE round-trip), then compute the plugin's output width (`CLAPPluginInstance::getNumOutputChannels()` or `PluginProxySlot::getReportedNumOutputChannels()`); if wider than the track bus, allocate a `pluginWorkspace` and in `process()` copy the track buffer in, process in the workspace, downmix channels 0-1 back.
- `tests/integration/mcp/mcp_server_test.cpp` — NodalRed2x removed from `kKnownSilent` (now fully asserted).

## How the fix works (the story)

NodalRed2x = gearmulator's Nord 2x port (`D:\pdf\gearmulator-2.2.9\source\nord\n2x`). It declares **two stereo output buses** ("Out AB" + "Out CD", `n2xPluginProcessor.cpp:33-35`) and its DSP **unconditionally copies 4 channels** into the host buffer (`n2xhardware.cpp:157-163`). It works in other DAWs because they host multi-bus plugins. HDAW's CLAP host read only the `IS_MAIN` port (2ch) → gave it a stereo buffer → the 4th-channel write hit nullptr → AV every block (contained by /EHa → silence). The fix threads the real width end-to-end: port sum → child reports → proxy PREPARE → workspace + downmix in the track.

## BUILD OPERATIONAL NOTES (learned the hard way — READ THIS)

- **The bash-tool default timeout is 120 s; a Debug rebuild takes minutes. ALWAYS pass a big timeout** (e.g. `-Timeout` 3600000 ms). A killed build **orphans cl.exe/MSBuild processes that lock source files** (PluginProxySlot.h etc.) — edits then fail with "Busy: FileSystem.writeFile".
- Before building: `taskkill /IM cl.exe /F; taskkill /IM MSBuild.exe /F; taskkill /IM link.exe /F; taskkill /IM mspdbsrv.exe /F` (ignore errors). Then verify `Get-Process` shows none left.
- **Stale-binary trap (AGENTS.md lesson 15):** after editing `mcp_server_test.cpp`, MSBuild may NOT recompile it (source older than `.obj`). The matrix then runs the OLD kKnownSilent silently. If a test "doesn't take," force: `(Get-Item tests\integration\mcp\mcp_server_test.cpp).LastWriteTime = Get-Date; cmake --build build --config Debug --target hdaw_tests`.
- Do NOT run `build\Release\HDAW.exe` (stale). Debug exes are current.
- The matrix (`McpServer.DiagnosticClapExportMatrix`) drives real CLAPs in `C:\Program Files\Common Files\CLAP` and takes ~80 s.

## Verification commands

```powershell
taskkill /IM cl.exe /F 2>$null; taskkill /IM MSBuild.exe /F 2>$null   # clear orphans first
cmake --build build --config Debug                                    # ~3-6 min
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix
& .\build\Debug\hdaw_tests.exe                                        # full suite ~6.5 min
```

## Context: committed history (all stable)

- `627c958` transport playhead forward to isolated children (shm snapshot + ChildPlayHead)
- `c59e08a` /EHa on hdaw_plugin_host (SEH containment)
- `f5f26b9` transport-handoff test + kKnownSilent evidence
- `b478d7c` docs plan + NodalRed2x root cause
- `4b55d2c` control-thread plugin exception containment (`__throwprepare__` sentinel)

Related docs: `docs/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md`, `docs/plans/2026-08-09-investigate-isolated-export-silence.md`, AGENTS.md lessons 11-16.

## Key residual knowledge

- Odin2's isolation silence was **NOT** a plugin defect: the child called CLAP lifecycle functions on its pipe thread while `threadCheckIsMainThread()` reported false → Odin2's thread-checking wrapper `std::terminate`d the child at PREPARE. Fixed by `runLifecycleOnMessageThread` + export-teardown graph scoping; Odin2 now renders (removed from `kKnownSilent`). See `docs/plans/2026-08-10-clap-lifecycle-thread-contract.md`.
- ShinRonin/Gneiss/Retrospect stay silent at their default patch with a healthy child pipeline (act=1, proc=1, MIDI + transport delivered, process() returns CONTINUE, output peak 0.0) — a preset-load sweep would be the follow-up to test them properly. (SUPERSEDED 2026-08-10: these are EFFECT plugins; the matrix fed them MIDI with no audio input — see docs/plans/2026-08-10-fx-audio-input-sweep-kknownsilent.md; with a 440 Hz sine clip fed through them on isolated export they render ShinRonin peak=0.298889 / Gneiss peak>0.01 (varies per run, observed 0.07-0.25) / Retrospect peak=0.300385.)
- ShinRonin/Gneiss/Retrospect stay silent at their default patch (no state/preset fed by the matrix) — a preset-load sweep would be the follow-up to test them properly. (SUPERSEDED 2026-08-10: these are EFFECT plugins; the matrix fed them MIDI with no audio input — see docs/plans/2026-08-10-fx-audio-input-sweep-kknownsilent.md; with a 440 Hz sine clip fed through them on isolated export they render ShinRonin peak=0.298889 / Gneiss peak>0.01 (varies per run, observed 0.07-0.25) / Retrospect peak=0.300385.)
- The transport-forward work is proven by `PluginIsolation.TransportClockHandoff` + the matrix peaks.
- The child's SEH containment (`/EHa` + `SIL CRASH` catch) is what turns plugin AVs into silent-survive — don't regress it.
