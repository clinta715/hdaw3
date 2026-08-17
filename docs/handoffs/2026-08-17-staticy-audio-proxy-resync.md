# Handoff: staticy audio + wedged exports — proxy ring resync, pacing, export lifecycle (2026-08-17)

## Status: IMPLEMENTED + BUILD OK + TESTS GREEN (with 2 documented-flaky failures). NOT yet committed, NOT yet A/B-verified live by ear. Next session: commit, run the live A/B, finish JE8086 triage, package.

## The bug (user report)
Live audio is "staticy" — continuous texture, NOT underruns ("buffer mismatch" was the user's ear). Exports crawl then fail; after the first failed export, every subsequent export fails instantly.

## Root cause (evidence-backed, not speculation)
1. **Child pacing regression (landed 8/9, commit 627c958)** — `PluginHost::audioLoop` Sleep-paces itself to ~realtime. Windows default timer granularity is ~15.6 ms vs a 10 ms block (441 @ 44.1k), so the isolated child runs ~1.6× slower than the device cadence → the shared-memory ring fills → the parent proxy **drops** input blocks (live mode, `PluginProxySlot.cpp:409-410`) and then reads **stale output** (a previous block's samples) as the current block → permanent sample misalignment with no recovery = the staticky texture.
2. **No ring resync** — once the child lagged, there was no sequence check; stale audio played forever.
3. **JE8086 isolated child hangs** — `processBlock` stuck >1 s (watchdog minidump `%TEMP%\hdaw_plugin_host_processBlock hung for 1s.dmp`, thread inside JE8086.clap with no syscall = busy loop; another JE8086 thread in condvar wait). Amplifies 1+2.
4. **Export wedge** — the render-mode 200 ms spin per hung slot makes export crawl (~0.0006× realtime measured; an 8-min song ≈ 200 h). `ExportManager::active` only cleared in the function tail → the first wedged export left `active=true` forever → every later `startExport()` returned false instantly ("export keeps failing"; the partial `export.wav` was deleted by cancel/timeout paths while the render thread was still crawling in the proxy spin — engine dump showed it inside `PluginProxySlot::processBlock+0x825` at `yield()`, WS thread parked in `dispatchExport`).
5. **Audio-thread logging in shipped build** — `DebugLog::log` (`src/common/DebugLog.h:92`) does `std::lock_guard<std::mutex>` + `ofstream` + `f.flush()` on EVERY call, and the packaged (RelWithDebInfo) engine emits MidiClipEntry / MidiClipDiag / TrackPB per-block from `processBlock` — a mutex+syscall on the audio thread every few ms (51 MB log observed). First-order realtime violation; secondary static contributor.

## Fixes implemented (all three tasks landed on disk; NOT committed)

### A. Proxy ring resync — never deliver stale audio
- `src/proxy/ProxyCommon.h`: `SHM_MAGIC` bumped `0x4844415B → 0x4844415C` (stale-child header safety). `ShmHeader` += `std::atomic<uint64_t> lastConsumedInputPos{0}` (child writes after output, release) and `std::atomic<uint32_t> renderMode{0}` (parent writes via PREPARE). Helper `proxyOutputIsCurrent(lastConsumed, writtenThisCall)`.
- `src/proxy/PluginProxySlot.cpp` `processBlock`: records `inputPosWrittenThisCall` after a successful input write; **live drop path now drains the output ring** (no stale output survives); output is read ONLY if `lastConsumedInputPos >= inputPosWrittenThisCall`; otherwise ring drained + `buffer.clear()` (silence, never stale). Render-mode spin now only waits while output is stale, and a `slotFailed` flag (50×200 ms consecutive timeouts = 10 s, `consecutiveSpinTimeouts` member) skips all spins so a hung slot can't stall export 200 ms/block forever. State reset in `prepareToPlay`.
- `src/proxy/host/PluginHost.cpp` `audioLoop`: pacing gated on `hdr->renderMode` — **live playback never Sleep-paces** (device cadence paces the stream); render mode still paces, now with `timeBeginPeriod(1)`/`timeEndPeriod(1)` (~1 ms Sleep accuracy). `lastConsumedInputPos.store(r + block)` after the output write (release). Added `__slowslot__` sentinel processor (Sleep 250 ms/block) for the resync tests. `<mmsystem.h>` included.

### B. Export lifecycle hardening
- `src/engine/ExportManager.cpp`: RAII `ActiveGuard` clears `active` on EVERY exit path (normal, all `goto finish` bail-outs, exceptions before/during/after the graph scope). `startExport` now logs `HDAW_LOG("Export", "startExport rejected: a previous export is still active")` when rejected. Teardown verified bounded (`killPluginHost` = `TerminateProcess`, ≤1 s wait).

### C. Audio-thread logging gated (default OFF)
- `src/engine/MidiClipProcessor.h` (MidiClipEntry prepareToPlay + processBlock, MidiClipDiag) and `src/engine/Track.cpp` (TrackPB): wrapped in `static const bool audioDiag = getenv("HDAW_AUDIO_THREAD_DIAG") == "1"`. RT tripwire (`HDAW_LOG_ALWAYS` / BufferCheck drainer) untouched.

### Tests added
- `tests/unit/proxy/shm_test.cpp`: `OutputCurrentHelper` (proxyOutputIsCurrent) + `ResyncHeaderFieldsInit`.
- `tests/integration/proxy/isolation_integration_test.cpp`: `PluginIsolation.SlowChildNeverStale` (lagging child → silence, never stale repeat; output drained; aligned output after catch-up) + `PluginIsolation.LiveDropDrainsStaleOutput` (drop path drains ring, dry passthrough preserved).

## Verification (this session, Debug build)
- `cmake --build build --config Debug --parallel 2` → **SUCCESS** (exes rebuilt 21:42 local).
- `hdaw_tests.exe --gtest_filter="SharedMemory.*:PluginIsolation.*"` → **43/43 PASS**.
- `hdaw_tests.exe --gtest_filter="PluginIsolation.SlowChildNeverStale:PluginIsolation.LiveDropDrainsStaleOutput"` → **2/2 PASS**.
- Full `hdaw_tests.exe` → **864/866 PASS**. The 2 failures (`CrashRecovery.RespawnDuringActiveProcessing`, `CrashRecovery.OfflinePluginDomainIsolatedFromLive`) are the **documented lesson-20 known-flaky five** (pipe/shm namespace collision with orphaned children from earlier tests in the same run) — BOTH PASS when run alone on a clean process state, and a baseline A/B (stashing the changes) failed identically → NOT regressions.
- `git status`: 9 modified files, 385 insertions/20 deletions. NOT committed.

## CRITICAL next-session operational note (stale binaries — lesson 15/20)
`SHM_MAGIC` changed. **Kill ALL `HDAW*.exe` / `hdaw_plugin_host.exe` processes BEFORE running any new binary**, or a stale child from an old build will misread the enlarged header → cross-process corruption / static. Command: `Get-Process HDAW*,hdaw_plugin_host -ErrorAction SilentlyContinue | Stop-Process -Force`.

## What remains (next session checklist)
1. **Commit** the 9 files (git is clean otherwise). Consider one commit: "fix(proxy): ring resync + live-pacing + export lifecycle; gate audio-thread diag logging".
2. **Rebuild RelWithDebInfo + repackage** if the user runs the packaged Electron app: `cmake --build build --config RelWithDebInfo`, then `frontend\build.bat` (or `npm run build && npm run package:dir`) — the packaged engine ships from `build/RelWithDebInfo/`, NOT Debug (AGENTS.md "How frontend changes reach the running app").
3. **Live A/B by ear**: user plays the staticy project (10 tracks / 475 generative MIDI clips, isolated JE8086/Toxic/WD Echo/Identity) and confirms the static is gone; then File→Export (Ctrl+E) confirms it completes in bounded time. Diagnostic knobs if anything recurs: `HDAW_NO_CHILD_PACING=1`, `HDAW_NO_CHILD_PLAYHEAD=1`, `HDAW_AUDIO_THREAD_DIAG=1`.
4. **JE8086 hang triage (D, not done)**: the 1 s+ processBlock hang is plugin-internal (its worker threads stuck). A/B with `HDAW_NO_CHILD_PLAYHEAD=1` to test whether the playhead forwarding (commit 627c958) triggers it; `HDAW_NO_CHILD_PACING=1` for pacing. The slot-fail path now contains it (10 s → silence, no export stall), but a plugin that intermittently hangs will still create 10 s gaps — worth investigating whether it's the ChildPlayHead snapshot (48k export vs 44.1k live) or JE8086's own threading.
5. **MidiClipConn logging**: the per-tag counts show `MidiClipConn` (14k+ lines) still emitting from the packaged engine. Grep found NO MidiClipConn in `src/engine/*` — locate its source (may be elsewhere) and gate it behind `HDAW_AUDIO_THREAD_DIAG` if it's on an audio-thread path.
6. **Knowledge-graph refresh**: structural changes to proxy header + new sentinel → run `index_repository` (codebase-memory) / `graphify . --update` after commit (per hdaw-guard completion contract).
7. Consider a follow-up: proxy ring **capacity is 1024 samples = 1.16 blocks** at 441×2 — fine, but the input-drop threshold is tight; the live drop path now drains output so any residual drops are silent gaps, not static.

## Files changed
```
M src/engine/ExportManager.cpp        (ActiveGuard RAII, rejected-export log)
M src/engine/MidiClipProcessor.h      (audio-thread diag gated)
M src/engine/Track.cpp                (TrackPB gated)
M src/proxy/PluginProxySlot.cpp       (resync, live-drop drain, slot-fail, renderMode)
M src/proxy/PluginProxySlot.h         (consecutiveSpinTimeouts, slotFailed)
M src/proxy/ProxyCommon.h             (SHM_MAGIC bump, lastConsumedInputPos, renderMode, helper)
M src/proxy/host/PluginHost.cpp       (pacing gated + timeBeginPeriod, consumed-pos store, __slowslot__)
M tests/integration/proxy/isolation_integration_test.cpp  (2 new tests)
M tests/unit/proxy/shm_test.cpp       (2 new tests)
```