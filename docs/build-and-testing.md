# Build & testing — traps, measurements, recipes

Moved out of AGENTS.md (2026-09-22). AGENTS.md keeps the quick commands
and one-line rules; the full narratives live here.

# Build

- **Canonical (native Windows, from a plain shell):** `build-fast.bat` builds
  `HDAW.exe` (RelWithDebInfo), `build-fast.bat test` builds `hdaw_tests.exe`,
  `build-fast.bat all` builds everything, `build-fast.bat debug` builds Debug.
  The script bootstraps MSVC (`vcvars64.bat`) and resolves the VS-bundled CMake
  when neither is on PATH — no developer prompt required.
- Raw configure/build: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests.exe`
- Do NOT run `build/Release/HDAW.exe` — stale binary, contains none of the fixes.
- **Two launch modes:** Default (browser), Headless (Electron).
- **Frontend build — DEPRECATED (2026-09-23):** the Electron frontend is a separate
  project; do NOT build it as part of engine work (`npm run build`,
  `frontend\build.bat`). Engine-only verification: `build/hdaw_tests.exe` (gtest) +
  the MCP surface. See the dated banner on "How frontend changes reach the running
  app" below.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.

### Build speed: the `.ninja_deps` trap (2026-09-21) — 285 s → 2 s

A **truncated `build/.ninja_deps`** (from a hard-killed Ninja build) makes Ninja
read every recorded target as `STALE`, which re-runs AUTOMOC → rewrites
`HDAW_lib_autogen/mocs_compilation.cpp` → dirties the **PCH** → **all ~300
`HDAW_lib` TUs recompile on every build**. Measured on this repo: a no-op build
was **285 s** (300 steps) and a one-test-TU edit was **331 s**; after the repair
the same no-op is **2 s (0 steps)** and the TU edit is **52 s (3 steps)**.

- **Symptom:** `ninja: warning: premature end of file; recovering`, and
  `ninja -t deps <target>` printing `(STALE)`.
- **Repair:** delete the deps log — `rm build/.ninja_deps` (harmless; costs at
  most one rebuild, then it settles).
- **Prevention:** never hard-kill a `cmake --build` / `ninja` process
  (`taskkill` on a *test* process is fine; killing the build mid-flight is what
  truncates the log).
- Legitimate costs that remain: a widely-included header edit rebuilds its real
  fan-out (e.g. `src/common/ProjectCommands.h` → 155 TUs, ~260 s);
  `HDAW_lib` is built with LTO (`INTERPROCEDURAL_OPTIMIZATION`);
  `windeployqt` runs as a POST_BUILD step on `hdaw_tests`.

### `cmake --build` never re-runs CMake here (the suppressed-regeneration trap)

This build tree is configured with **`CMAKE_SUPPRESS_REGENERATION=ON`**
(`build/CMakeCache.txt`, `UNINITIALIZED`), so `build.ninja` contains **no
`build build.ninja: RERUN_CMAKE` statement at all** (verify:
`Select-String build\build.ninja -Pattern ': RERUN_CMAKE'` → 0 hits). Ninja's
special "regenerate the manifest first" behaviour is therefore compiled out:
**editing `CMakeLists.txt` / `tests/CMakeLists.txt` does NOT trigger a
reconfigure when you run `cmake --build` / `ninja`.**

The failure mode is genuinely confusing, because the source is correct and only
the *build graph* is stale:

- You add a new `.cpp` **and** its `CMakeLists.txt` entry → the file is never
  compiled (no `.obj` is ever produced) → the build fails at **link** with
  `LNK2001: unresolved external symbol` for a function that plainly exists in
  the source, or (worse) an edited `main()`/entry point never takes effect —
  the same "the source says X" unreliability as lesson 15 / the `.ninja_deps`
  trap, in a different disguise.
- Diagnose: `Select-String build\build.ninja -Pattern '<NewFile>'` → **ABSENT**
  means the graph was never regenerated (`build.ninja` mtime older than
  `CMakeLists.txt` confirms it).

- **Fix (always after adding/removing/renaming a source, target, or option):**
  re-run CMake explicitly, then build:
  `cmake -S . -B build` then `cmake --build build --target hdaw_tests`
  (equivalently `cmake --build build --target rebuild_cache`, or
  `cmake --regenerate-during-build -S . -B build` — what CMake itself would run).
- **Do NOT** conclude "the tool/file is broken" from an unresolved external
  before checking the manifest — confirm the new source is in `build.ninja`.
- The suppression is deliberate (it keeps no-op builds from paying a CMake
  manifest check). Keep it; just pair every structural edit with an explicit
  configure.

### Test speed: shard the suite across processes (2026-09-21)

`run-tests-sharded.ps1 [-Shards N] [-Filter <regex>] [-SerialSuites <regex>]`
(repo root) shards the gtest list across N
concurrent `hdaw_tests.exe` processes (the engine is a singleton *per process* and
proxy children get a unique namespace per manager instance, so concurrent runs are
safe). It shards small suites whole and large ones per test. Run it with
`powershell -File run-tests-sharded.ps1 -Shards 4` (or `pwsh`).

- **Native, no env forwarding:** the child processes are launched by PowerShell and
  inherit this shell's environment directly — `$env:HDAW_REAL_PLUGIN_TESTS=1`
  before invoking it, and the real-plugin gates run. (The retired WSL wrapper
  `scripts/run-tests-parallel.sh` needed `WSLENV=HDAW_REAL_PLUGIN_TESTS` to push
  the variable through interop; that constraint is gone with the file.)
- **Measured:** the 20-gate `FxMidiInjection` suite (the heaviest) runs **600 s
  serial → 307 s with 4 shards** (~2x; the shards contend on the CPU-heavy
  emulations, so the longest shard dominates).
- `run_fast_tests.bat` remains the fast iteration tier (excludes the
  render/recipe/spawn-heavy suites).
- **Render temp targets are process-unique (fixed 2026-09-21).** `renderTrackWindow`
  now writes `%TEMP%\hdaw_render_p<pid>_<trackIndex>_<counter>.wav`. Before the pid
  tag the counter restarted per process, so two concurrent shards rendering the same
  track index picked the same path and one export failed with `export failed: Could
  not create output file` (exactly 1 spurious failure in a 4-shard `FxMidiInjection`
  real-plugin run, 22/23; the same test passed solo in 25 s). After the fix the
  identical 4-shard sweep is **24/24**. Keep any new temp target process-unique as
  well — `%TEMP%\hdaw_paramtrace_<pid>.log` and the proxy state files already are;
  a per-process counter alone is not enough when one suite runs in several processes.
- **Pre-build time sync (WSL/Windows clock drift) — OPT-IN, no-op on a native
  box.** `scripts\time-sync.cmd` exits 0 immediately and prints nothing unless
  `HDAW_TIME_SYNC=1` is set, so the native Windows build path pays no WSL spawn.
  `build-fast.bat` and CMake (`hdaw_time_sync` ALL target, via
  `cmake/RunTimeSync.cmake`) still call it — that is intentional and free. Set
  `HDAW_TIME_SYNC=1` (cmd: `set HDAW_TIME_SYNC=1`, PowerShell:
  `$env:HDAW_TIME_SYNC=1`) only when the source tree is reached through WSL's
  drvfs/9p view; the hook then snaps the WSL clock to the Windows host
  (`sudo ntpdate -b time.windows.com`) so ninja/MSBuild never misjudge mtimes
  under WSL2 clock drift (lesson 15 / the WSL-side-edit sync recipe). WSL users
  can also run `scripts/time-sync.sh` directly. The hook NEVER fails a build on
  any path. See `docs/archive/plans/2026-09-05-time-sync-build-hook.md` and
  `docs/skills/pre-build-time-sync/SKILL.md`.
  **DEPRECATED (2026-09-23):** the `frontend\build.bat` / `npm run build` entries
  above are deprecated with the Electron frontend (see the frontend banner below);
  the rule now applies only to the live engine builds (`cmake --build`,
  `build-fast.bat`, bare `ninja`).

### Disk housekeeping: `scripts/cleanup-stale.ps1` (2026-09-22)

Reclaims disk from stale scratch on this dev box: crash dumps + debugger symbol
caches, `%TEMP%`, HDAW param traces / debug log / render WAVs / engine copies,
agent chat logs (pi / omp / opencode / codex), and re-downloadable caches
(`-Aggressive`). **Dry-run by default** — `-Apply` deletes. Files held open by a
running process cannot be deleted and are reported as `LOCKED`, which is what
protects a live engine's `hdaw_paramtrace_<pid>.log` / `hdaw_debug.log`
deliberately.

`scripts/cleanup-stale-db.mjs` (`-AgentDb`) is the sqlite companion for
`~/.local/share/opencode/opencode.db`: `VACUUM` reclaims the freelist (that DB
had grown to 9.1 GB of which **6.3 GB was free pages** — `auto_vacuum=0`), and
`--days N` prunes whole sessions with the FK cascades it needs. Both refuse to
write while another process holds the DB.

Two invariants worth keeping when editing either script: `%TEMP%\hdaw_crash_captures`
and its `wer` child are **protected dirs** (crash-diag.ps1 registers `wer` as the
WER LocalDumps folder and WER does not always recreate it), and the capture-tree
sweep globs `engine_*` only — a bare `hdaw_crash_captures\*` matched `wer` and
deleted it.

### Shell: PowerShell only (no `&&` or `&`)

This development system runs **Windows PowerShell 5.1**, where `&&` and `&` (as a command separator) are **not valid**. Every command in AGENTS.md, scripts, and docs must use PowerShell-native syntax:

| Goal | Use this | Not this |
| ------ | ---------- | ---------- |
| Run commands sequentially (fail on error) | `cmd1; if ($?) { cmd2 }` | `cmd1 && cmd2` |
| Run commands sequentially (ignore errors) | `cmd1; cmd2` | `cmd1 & cmd2` |
| Run in subshell / change dir | Use the `workdir` parameter on tool calls, or `Set-Location` | `cd dir && cmd` |
| Background jobs | `Start-Job { ... }` | `cmd &` |
| Boolean AND / OR | `if ($?) { ... }` / `if ($LASTEXITCODE -eq 0) { ... }` | `&&` / ` | | ` |

**When writing new commands in this project**, always prefer PowerShell-compatible forms. Existing references to `&&` in documentation (including this file, `README.md`, and `docs/`) are legacy from bash-originated docs and should be updated on sight.

### How frontend changes reach the running app (the stale-frontend trap)

**DEPRECATED (2026-09-23):** the Electron frontend is a separate project as of this
date — the repo no longer builds or tests it. The table and instructions below are
retained for reference only (do NOT run `frontend\build.bat`, `npm run build`,
`npm run package:dir`, or `npm run dev`). The stale-`app.asar` warning printed by
`frontend\build.bat` is therefore expected noise that can be ignored. Engine-only
verification — `build/hdaw_tests.exe` (gtest) + the MCP surface — is the live path.

The React frontend is delivered three ways, and **a plain `cmake --build`
updates NONE of them**. If a frontend fix "doesn't take effect after
rebuilding," this is almost certainly why:

| Run mode | Binary | Frontend source | To pick up frontend changes |
| ---------- | -------- | ----------------- | ------------------------------ |
| **Packaged Electron** | `frontend/release/win-unpacked/HDAW.exe` | Frozen in `resources/app.asar` | **Repackage:** `frontend\build.bat` (or `npm run build; if ($?) { npm run package:dir }`). Ctrl+Shift+R does nothing here. |
| **Browser (standalone exe)** | `build/Debug/HDAW.exe` | Embedded via `frontend.qrc` | `frontend\build.bat` forces a clean C++ rebuild when `dist/` is newer (AUTORCC under the VS generator does NOT treat changed `dist/` as a rebuild trigger). |
| **Vite dev server** | `npm run dev` (+ engine for WS on 8766) | Live from `frontend/src` | Hard-refresh the browser (Ctrl+Shift+R). No build needed. |

**The packaged Electron app is the one users run.** Its frontend is baked into
`app.asar` at packaging time - editing source, rebuilding `dist/`, or
refreshing the window has zero effect until you repackage. `frontend\build.bat`
rebuilds the SPA, the C++ engine, runs the tests, and repackages Electron in one
command. Both build scripts detect an obsolete `app.asar` and fail/warn loudly,
so you can't silently iterate against a stale `.asar`.

**The packaged app's ENGINE comes from `build/RelWithDebInfo/`** (see
`electron-builder.yml` `extraResources`), NOT `build/Debug/`. A bare
`npm run package:dir` re-ships whatever `RelWithDebInfo` happens to contain —
on 2026-08-16 that was an 11-day-old engine (8/5) with none of the
respawn-storm fixes, so the app kept crashing plugins while every Debug-mode
verification looked clean. `frontend\build.bat` builds the engine too; if you
repackage by hand, run `cmake --build build --config RelWithDebInfo` FIRST and
verify the binary (string-search the shipped `resources\engine\HDAW_headless.exe`
for a fix marker) before trusting the package.

### Engine launch: locks and `LNK1104` (2026-09-23)

Never run live engine/tests straight out of `build/` — a running exe locks its
own file and the next link dies with **`LNK1104`** (hit twice on 2026-09-23).
Launch via the repo-root **`mcp-launch.bat`**; it copies `HDAW_headless.exe`,
`hdaw_plugin_host.exe` and `hdaw_plugin_scanner.exe` to `%TEMP%`, verifies each
copy (size + MD5 + the `audit_song_structure` sentinel), prepends the build
dirs to `PATH` so the temp copy resolves its DLLs, and `taskkill`s stale
engines first — so the build-dir exes stay free for the linker.

- **Do not re-derive a manual copy-and-launch.** A hand-rolled copy of the
  engine to `%TEMP%` died with exit `0x7FFFFFFF` even with `PATH` set: it skips
  the launcher's build-dir `PATH` prepend, so the DLLs never resolve.
- **Symptom:** `LNK1104: cannot open ...` on `HDAW_headless.exe` /
  `hdaw_plugin_host.exe` / `hdaw_plugin_scanner.exe` after an engine or test
  was left running from `build/` — kill it (the launcher does this itself) and
  rebuild.

## Testing

- **C++ engine tests (gtest):** `build/hdaw_tests.exe` (flat Ninja RelWithDebInfo layout — there is no `build/Debug/`; `build-fast.bat test` builds it, `build-fast.bat all` also builds `hdaw_plugin_host.exe` which the PluginIsolation/CrashRecovery suites require)
  - Filter: `--gtest_filter=SuiteName.*`
  - Full suite: **1768 tests / 264 suites, ~44 min serial** (measured 2026-09-21; the suite keeps growing — it was 1328/216 on 2026-09-02). Fast iteration tier: `run_fast_tests.bat` (~3.3 min; excludes the render/recipe/spawn-heavy suites — run the full suite before delivery). A native **shard runner** exists — `run-tests-sharded.ps1 [-Shards N] [-Filter ...]` (the canonical parallel runner; it replaced the WSL-only `scripts/run-tests-parallel.sh`): it splits the gtest list, keeps the device/plugin-dependent suites in ONE extra serial process, aggregates per-shard logs, and exits non-zero on failure. Its shard count is **not validated** — see the caveat below.
  - **Device-dependent suites need a working audio route; when it is missing they fail with `getTrack() == nullptr` / `tr == nullptr` even SOLO.** That is the documented deviceless pattern (lessons 9/17: no device → `rebuildRoutingGraph` no-ops → `getTrack()` nulls), and it hits `InternalFx`, `MasterGain`, `MasterBusFx`, `AudioPoolDedup`, `AudioEngineReadFacadeTest`, `AutomationPidRouting`, `RenderSequenceRelease`, `McpCoverageTest` and the export/plugin-spawn suites. **Diagnostic rule:** if every failing assertion is a null track/processor, the run is environmental (check the audio device) — do not blame parallel runs, the runner, or your change. Observed 2026-09-21: a device-healthy serial run passed all of them, and the same binary failed them an hour later.
  - Sharding caveat: the calibration runs for `run-tests-sharded.ps1` were contaminated by exactly that environmental failure (its failures were all null-track ones), so **no safe shard count has been established** — the default is a conservative 2. Re-measure on a device-healthy machine before trusting a higher `-Shards`. What *is* independently true: concurrent runs cannot collide on proxy pipe/shm names (unique namespace per manager instance, lesson 20) or on render temp targets (pid-tagged since 2026-09-21).
  - **The audio route also disappears in a disconnected RDP session (2026-09-23).** The deviceless pattern above can appear even when `Get-CimInstance Win32_SoundDevice` reports the hardware fine — a **disconnected** RDP session exposes no routable endpoint, so `rebuildRoutingGraph` still no-ops and every live-graph/render suite fails with the identical null `(track)`/`(rm)`/`(tr)` signature. Observed 2026-09-23: `query session` showed the active session as `> hapbt 2 Disc` (`rdp-tcp … Listen`) while Realtek/Focusrite/NVIDIA sound devices all reported Status OK, yet `TrackFxRebuildRace.*` (11), `TrackMixerState.*` (2), `MasterBusFx` (6, some as SEH `0xc0000005` inside the test body), `AudioPoolDedup.*` (3), `InternalFx.*` (7), `MasterGain.SurvivesRebuild`, `AudioEngineReadFacadeTest.GetFxProgramList*`, `RenderSequenceRelease.RebuildReleasesPreviousGraphChildren` ("no new `hdaw_plugin_host.exe` after `addFxSlot`"), `StreamingPoolDedup.EngineWires…` (`openCount 0`) and `SongCells.LockSkipsAndRerollBumpsSeed`/`HarvestNotesAndRemove` (`filled == 0`) all failed. **Diagnostic recipe:** (1) `query session` — is the active session `Disc`?; (2) run an **untouched** device-dependent control suite (`MasterGain.SurvivesRebuild`, `InternalFx.FilterLowpassAttenuatesAboveCutoff`, `AudioPoolDedup.EngineWires…`) — if those fail null-track too, the run is environmental, not change-induced; (3) if the failures land in **40–70 ms** instead of the ~**1900 ms** they take with a healthy route, the graph never settled — no route. Timing discriminator: the same binary passed live-graph tests minutes earlier (`AutomationSendBusPids` asserting on LIVE `rm->getSend`/`rm->getFxBus` + `processBlock`; `VerifyPart` renders), so a device-regression is the cause, not the batch. **Rule:** verify a batch's engine changes in a session with a live route (or compare against a pre-change log) and never attribute null-track failures to the change without that untouched control.
  - Current baseline (2026-09-21, full serial run): **1768 tests / 264 suites -> 1728 passed, 39 skipped, 1 failed**. The single failure `PluginIsolation.LargeStateRoundTripThroughProxy` (a 0-byte read of a 100 KB chunked state) passes solo and the whole `PluginIsolation.*:CrashRecovery.*` set (57 tests) is green solo — a load flake in the state-chunk path (lessons 14/26), not a regression. Real-plugin `FxMidiInjection.*` was verified separately: 22/23, the single failure being the cross-shard temp-file collision documented above, since fixed with a pid-tagged temp name (the identical sharded sweep is 24/24 post-fix).
  - Previous baseline (2026-09-02, post DISABLED-test rewrite pass): 0 failed; 4 RealtimeSafety detector tests SKIP in release configs (`BufferCheck` is `#if JUCE_DEBUG`-only by design); 0 DISABLED — every formerly `DISABLED_` test is either re-enabled against current contracts (PluginIsolation ×4, ExportVolumeBypass.RealProjectVolumeSensitivity, TrackFXSlotShowEditor — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md`) or re-enabled after its fix (`ExportAudioWithMultipleIsolatedInstances`, commit abf8a3d).
  - Build sequentially: two concurrent `build-fast` invocations on the same `build/` dir overwrite each other's `.ninja_log`, and the next build re-runs as near-full. One build at a time.
  - **(WSL-only — does not apply on the native Windows dev box; there, edit the file directly and the mtimes are correct.)** WSL-side edits must be synced for the Windows compiler (drvfs/9p attribute cache shows stale content/mtimes for minutes): after editing from WSL, `cp <file> /mnt/c/temp/sync_tmp.cpp`, then from Windows `Copy-Item C:\temp\sync_tmp.cpp -> <D: path> -Force`, then touch `(Get-Item <path>).LastWriteTime = Get-Date`, and verify with PowerShell `Select-String`/`Get-Content` (never findstr through bash→cmd quoting). Symptom if skipped: ninja rebuilds "succeed" against stale sources. Verified recipe — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md` outcome.
  - 1015→1768 tests across 182→264 suites: MCP tools/server, transport, tracks, clips,
    notes, FX, automation, undo, save/load, phrase generation, slicing, merge,
    ripple delete, ghost clips, stretch, markers, error conditions, batch ops,
    plugin isolation, audio pool, streaming, arranger, session, library,
    note operators, tempo points, sends, MIDI FX.
  - **Engine change test discipline:** when modifying the C++ core, RPC surface,
    or JUCE interfaces, assess test impact before finishing: (1) identify gtest
    suites that exercise the changed code (RPC handlers → MCP tool tests,
    ValueTree mutations → track/clip/note tests, audio-thread logic → transport
    tests) and update them if signatures/return shapes/behavior changed; (2) if
    the change adds a new RPC method, command, or JUCE-facing interface with no
    coverage, add a gtest — the suite is the contract the frontend and MCP
    server rely on; (3) run `build/Debug/hdaw_tests.exe` to confirm no
    regression. An engine change with no test consideration is incomplete.
- **DEPRECATED (2026-09-23):** the Electron frontend is a separate project — do NOT
  run its suites (`cd frontend; npm test`, `npm run test:watch`,
  `npm run test:coverage`, `cd frontend; npm run test:e2e`). Retained below for the
  day it returns; engine-only verification is `build/hdaw_tests.exe` (gtest) + the
  MCP surface.
- **Frontend unit tests (Vitest) — DEPRECATED (2026-09-23):** do NOT run (`cd frontend; npm test`)
  - ~177 tests: Zustand stores (transport, ui, project, notify, meter, browser),
    hooks (useTimelineDrag), utils (rowLayout, theme, grooveUtils), and
    components (StatusBar, Toaster, BottomTabs, MidiFxChain, WaveformCanvas,
    MidiThumbnailCanvas, StepSequencer, TimelineContextMenu, MixerStrip,
    TrackHeaders, Icons).
  - Watch: `npm run test:watch` · Coverage: `npm run test:coverage`
- **Frontend E2E tests (Playwright) — DEPRECATED (2026-09-23):** do NOT run (`cd frontend; npm run test:e2e`)
  - ~197 tests in `e2e/*.spec.ts`. `app.spec.ts` = render smoke; the rest are
    user-journey regressions that drive the real app (click/drag/keyboard) and
    assert on DOM/canvas/snapshot state — the layer that catches the recurring
    interaction bugs unit tests miss (drag stale-closures, rubber-band
    hit-testing, waveform display, selection→editor opening, context menus).
  - The `webServer` in `playwright.config.ts` auto-starts the engine
    (`build\Debug\HDAW.exe`, with `HDAW_NO_BROWSER=1`) plus the Vite dev server
    (port 5173); tests run against the **live** frontend, so frontend changes
    are picked up with no rebuild/repackage. Requires a current
    `build/Debug/HDAW.exe` and Playwright browsers (`npx playwright install chromium`).
  - `workers: 1` — the engine is a singleton serving one project, so tests run
    serially; each calls `startApp()` (clicks "New Project") for a clean state.
  - Test seams: `window.rpc` for RPC setup, `data-clip-id` on `.tl-clip` for
    targeting clips, `HDAW_NO_BROWSER`. Shared helpers in `e2e/helpers.ts`
    (`startApp`, `rpcCall`, `addMidiClip`/`addAudioClip`, `dragClip`, `writeSineWav`).
  - **Clip-position assertions must poll.** After an RPC that shifts clips
    (insert silence, duplicate region, move), the tree-change notification is
    debounced (~16 ms) so the DOM doesn't update immediately. Assert positions
    with Playwright's `expect.toPass()` polling, not a one-shot read:
    `await expect(async () => { expect(await clipLeft(...)).toBeGreaterThan(...); }).toPass({ timeout: 10000 });`

