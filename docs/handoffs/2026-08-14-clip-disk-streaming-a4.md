# Handoff: Clip Disk Streaming — Subsystem A (DONE: A4 `76bc275`, A5 `5fa81f8`, version 0.22.0)

Date: 2026-08-14. Continue the **clip disk streaming** subsystem
(`docs/plans/2026-08-13-clip-disk-streaming.md`) per the master plan
(`docs/plans/2026-08-13-hise-derived-features-plans.md`). Subsystem B
(realtime-safety) is **complete** (0.21.1, commits `5612809`…`f4f0641`).
Tasks A1–A3 of Subsystem A are **committed**. **Task A4 is DONE: wired,
fixed, test GREEN, committed as `76bc275` (see §9). A5 complete: full
suite green, release clean, version bumped to 0.22.0 as `5fa81f8` (see §10).**

---

## 1. Current status (verified this session)

| Task | State |
|------|-------|
| A1 `StreamingClipSource` + unit tests | ✅ committed `668ed00` (+530) |
| A2 adopt into `ClipSourceProcessor` | ✅ committed `37e677d` (+152/−24) |
| A3 long-file-not-whole-file e2e | ✅ committed `bf6e056` (test-only) |
| A4 export non-realtime propagation | ✅ committed `76bc275` (+88/−5) — see §9 |
| A5 full suite / version 0.22.0 / release | ✅ complete — see §10 |

Working tree is **dirty and must not be swept** (≈104 pre-existing doc
deletions, sampler-followup WIP, ~30 untracked `.hdaw3`). Commit per task with
`git add <explicit paths>` only. **Danger:** `src/engine/RoutingManager.{h,cpp}`
and `src/engine/ExportManager.cpp` already carried uncommitted WIP when A4
started — see §5 for commit hygiene.

## 2. What A4 wiring is done (compiles, Debug `hdaw_tests` builds clean)

1. `src/engine/RoutingManager.h` — declared `void setClipSourcesNonRealtime(bool nr);`
   (after `getMidiClipSources`).
2. `src/engine/RoutingManager.cpp` — implemented it directly after
   `switchClipTake`: iterates `audioClipSources` and calls
   `clip->setNonRealtimeFlag(nr)` on each live `ClipSourceProcessor`.
3. `src/engine/ExportManager.cpp` — in `renderThreadFunc`, immediately after
   `renderGraph.setNonRealtime(true)` (≈line 219), calls
   `routingManager.setClipSourcesNonRealtime(true);` so the already-built live
   clip sources switch to synchronous streaming before the first `processBlock`.
4. `tests/unit/engine/clip_streaming_e2e_test.cpp` — added
   `ClipStreamingE2E.NonRealtimeStreamingMatchesPreloadAcrossLongFile` per the
   plan's Task 4.

API note (carried from A2): `ClipSourceProcessor` exposes
**`setNonRealtimeFlag(bool)`** — NOT `setNonRealtime`, which collides with
JUCE's virtual `AudioProcessor::setNonRealtime(bool) noexcept` (error C2694).
`StreamingClipSource::setNonRealtime(bool)` is its own (non-virtual) method and
is used directly in the test — unaffected.

## 3. THE DEFECT: plan's Task 4 test is RED — realtime starvation at refill boundary

`ClipStreamingE2E.NonRealtimeStreamingMatchesPreloadAcrossLongFile` **fails**:
**37,372 samples differ**, min sample 176128, max 213503 (≈blocks 344–416).
Full output: `C:\Users\hapbt\AppData\Local\Temp\opencode\e2e_fail.txt`.

- First failing sample = **176128 = block 344**, which is exactly the first
  block whose window `[176128, 176640)` extends past the 4 s preload head
  `[0, 176400)` (`kBufferSeconds=4` @ 44100 → `bufLen_=176400`).
- `outB` (realtime) is **silence (0)** there; `outA` (non-realtime) is the sine
  waveform. The two diverge at every block spanning a refill boundary.
- The non-realtime path is **correct** (synchronous refill in `readNextBlock`,
  lines 181–196). The **realtime path starves** because:
  1. `readNextBlock` detects `!covered` only *after* the audio thread has
     already arrived at the block it needs → sets `targetPos_=sourcePos`,
     `swapRequest_=true`, returns **silence for that block** (lines 197–203).
  2. The reader fills the **inactive** side at `targetPos_` and flips
     `activeBuffer_` (lines 328–344). The window starts at the starved block's
     own `sourcePos`, so the NEXT block is covered — but the starved block is
     already gone. Under an unthrottled test loop the reader lags ~73 blocks;
     under real 11.6 ms/block playback it lags ~1 block.
- **Consequence in real playback: an audible 11.6 ms dropout (silent block)
  at every 4 s refill boundary of every long clip** — a realtime quality bug
  (Gate 8), not just a test artifact. The old test
  `LongFileNotWholeFileResident` only checks the *final* block has signal and
  masked this.

Root cause summary: the request/swap protocol has **no lookahead**. It only
requests a refill after the audio thread has already been asked for a block it
can't serve, so the boundary block is sacrificed by design. The plan's own
design note ("realtime may starve the first block") is incompatible with its
Task 4 assertion of bit-exact realtime==non-realtime equality.

## 4. The fix (recommended): lookahead request so realtime NEVER starves

Make the audio thread request the next window **before** it needs it, and let
the reader fill the inactive side starting at the *current* audio position.
This removes boundary starvation entirely and makes the plan's Task 4 test pass
as written (realtime ≡ non-realtime).

Concretely in `src/engine/StreamingClipSource.h`:

1. **Window length grows by one block:** `WindowLen = bufLen_ + blockSize_`
   (sides sized for it). `fillBuffer` computes
   `available = min(sourceLength_ - base, WindowLen)` and publishes that as
   `fill`. Head window becomes `[0, 176912)` at 44.1 kHz/512.
2. **Lookahead trigger** (realtime only, after a successful copy in
   `readNextBlock`):
   ```cpp
   if (!nonRealtime_ && sourcePos + 2 * numSamples > base + fill)
   {
       targetPos_.store(sourcePos);
       swapRequest_.store(true, std::memory_order_release);
   }
   ```
   This fires when the audio thread is ≥1 block *inside* the current window but
   within 2 blocks of its end → the reader starts filling the inactive side at
   the audio's current position. By the time the audio crosses the old edge,
   the inactive side already covers it → `covered` is true on the boundary
   block. (Use `numSamples` read from `out.getNumSamples()`.)
3. Keep the existing `!covered` fallback (sync refill in non-realtime; silent
   starve + request in realtime) as a robustness net for reader-lag edges, but
   it should become unreachable in sequential playback.
4. Verify `getDiskUsagePercent()` denominator still reads sensibly with the
   wider window (cosmetic — fill/WindowLen or fill/bufLen both fine; keep it
   consistent).
5. Memory ordering already correct: `fillBuffer` publishes `fill`/`base` with
   release *before* the reader flips `activeBuffer_` with release; the audio
   thread acquires `activeBuffer_` then `base`/`fill`. Do not reorder.

**Trace that must hold after the fix** (kBufferSeconds=4, sr=44100, block=512):
- Head A=`[0,176912)`. Blocks 0…345 served by A; block 344
  (`[176128,176640)`) fires lookahead → reader fills B=`[176128,353040)`,
  flips active=B.
- Block 345 (`[176640,177152)`) reads B: `covered` (177152 ≤ 353040). No
  starvation anywhere. Same pattern at every subsequent 4 s boundary.
- `NonRealtimeStreamingMatchesPreloadAcrossLongFile` must then pass unchanged.

If you are skeptical of the protocol change, at minimum the plan's test as
written cannot pass while starvation is accepted — do NOT delete the test; it
correctly encodes the invariant the plan promises. Fix the protocol (§4), not
the assertion.

## 5. Commit hygiene for A4 (working tree entanglements)

`git diff` on `RoutingManager.{h,cpp}` currently mixes **pre-existing
uncommitted WIP** (the lesson-18 two-phase rebuild: `prebuildTracks()`,
`buildTrackProcessor()`, `prebuiltTracks` map, destructor/`rebuildFromValueTree`
changes — present since a prior session) with **my A4 hunks**
(`setClipSourcesNonRealtime` decl + impl). `ExportManager.cpp` also had
pre-existing uncommitted edits before my 9-line wiring. Options, in order of
preference:
1. `git add -p` and stage **only** the A4 hunks (decl, impl, wiring, test),
   then commit the pre-existing WIP separately (it builds; part of the
   sampler-followup).
2. If separation is impractical, commit the four A4 files together and say in
   the message that pre-existing WIP rides along.

Either way, verify the commit contains no stray files (`git status` after
staging). Do not `git add -A`.

## 6. Baseline / gates / environment

- Full suite: **794 tests, 789 pass, 5 known pre-existing failures**
  (CrashRecovery.AutoRespawnAfterCrash, CrashRecovery.RespawnDuringActiveProcessing,
  CrashRecovery.DestroyedProxyIsDeregistered, CrashRecovery.OfflinePluginDomainIsolatedFromLive,
  PluginIsolation.UniqueSlotIdPerInstance — all proxy-spawn environment issues,
  non-blockers). Gate = **no new failures, no false positives**.
- `McpServer.DiagnosticClapExportMatrix` heap assertion is a **verified one-off
  flake** (absent in baseline + isolation runs; do not chase it unless it
  repeats).
- Build: `cmake --build build --config Debug --target hdaw_tests`.
  Run: `& "build\Debug\hdaw_tests.exe" --gtest_filter=...` (PowerShell,
  workdir `D:\pdf\roo projects\hdaw3`).
- If orphaned `cl.exe` holds file locks after a timed-out build:
  `Get-Process cl | Stop-Process -Force`.
- **A5 (after A4 green):** full suite → release compile-out
  `cmake --build build --config Release --target HDAW` → version bump to
  **0.22.0** in `CMakeLists.txt:2` and `frontend/package.json:3` →
  `codebase-memory` refresh (`index_repository`, project
  `D-pdf-roo-projects-hdaw3`) since new files/RPC/methods were added.

## 7. Key reference points

- `src/engine/StreamingClipSource.h` (A1, committed): `readNextBlock` lines
  152–223; realtime starve at 197–203; reader swap at 328–344; `fillBuffer`
  281–326; constants 25–26.
- `src/engine/ClipSourceProcessor.h` (A2, committed): `prepareToPlay` streaming
  decision, `setNonRealtimeFlag`, `shouldStream`/`preloadWholeFile` helpers.
- `src/engine/RoutingManager.{h,cpp}`: my A4 decl/impl; `audioClipSources` map
  near `RoutingManager.cpp:596`.
- `src/engine/ExportManager.cpp`: seam at `renderGraph.setNonRealtime(true)`
  ≈line 219 (my wiring just after).
- `docs/plans/2026-08-13-clip-disk-streaming.md` (Task 4), `docs/architecture.md`
  (time-unit convention: frontend beats ↔ engine seconds), `docs/realtime-safety.md`
  (Gate 7/8: every engine change re-evaluates latency + quality),
  `AGENTS.md` lessons 12/18 (parked-pump / two-phase rebuild).
- `D:\pdf\HISE-4.1.0` HISE reference (StreamingSamplerVoice.cpp SampleLoader:
  `setIsNonRealtime` :139, starvation :1147, 16-bit :897) for the non-realtime
  streaming precedent.

## 8. Load this first

Per `AGENTS.md`, invoke the `hdaw-guard` skill before any code change, then
re-read `docs/plans/2026-08-13-clip-disk-streaming.md` Task 4 and the A1
`StreamingClipSource.h` realtime path before editing. The failure artifacts are
in `$env:TEMP\opencode\e2e_fail.txt` (full gtest output) if you want the raw
evidence.

## 9. What was actually done to fix A4 (commit `76bc275`)

The fix diverged from §4's prescription (the `sourcePos + 2*numSamples > base + fill`
lookahead) because that trigger — measured on this machine — still left the
realtime reader starved for ~57 blocks per boundary (~1 block of lead is ~11.6 ms
realtime / ~0.25 ms in the test loop, less than the reader's fill+swap latency:
Windows ~15.6 ms timer granularity + WAV read). The shipped trigger is a
**half-window-consumed** lookahead instead:

1. `windowLen_ = bufLen_ + blockSize_` (= 176912 @ 44.1 kHz/512). `prepare()`
   sizes both sides to `windowLen_ * numChannels_`. `fillBuffer()` uses
   `windowLen_` for `available = min(sourceLength_ - base, windowLen_)` and
   zero-tails to `windowLen_`. `getDiskUsagePercent()` divides by `windowLen_`
   (fill ≤ windowLen_ keeps the ≤1.0f invariant; both now guarded on ≤ 0).
2. `readNextBlock`, after a successful copy, realtime-only:
   `if (!nonRealtime_.load() && sourcePos - base >= windowLen_ / 2) { targetPos_.store(sourcePos); swapRequest_.store(true, release); }`
   Fires when the audio thread has consumed half the current window → the reader
   gets ~half a window (~172 blocks ≈ 43 ms test / ~2 s realtime) of lead. The
   new window starts at the CURRENT audio position so the edge block stays covered
   even if the swap lands late. The existing `!covered` realtime starve+request
   branch (§3) stays as a robustness net.
3. Memory ordering unchanged (fillBuffer release-publishes fill/base before the
   reader flips activeBuffer_ with release; audio thread acquires activeBuffer_
   then base/fill; targetPos_ store sequenced-before the swapRequest_ release).

**Verification (all on Debug `hdaw_tests`):**
- `ClipStreamingE2E.NonRealtimeStreamingMatchesPreloadAcrossLongFile`: PASS
  (538 ms), determinism check 5/5.
- `ClipStreamingE2E.*` 2/2 PASS; `StreamingClipSource.*` 5/5 PASS.
- Export suites `*Export*:ClipSourceProcessor.*` (minus the known flake) 7/7 PASS.
- **Full suite: 796 tests, 791 PASS, 5 FAILED — exactly the 5 known
  pre-existing CrashRecovery/PluginIsolation proxy-spawn failures (§6). No new
  failures, no false positives.**
- `McpServer.DiagnosticClapExportMatrix` heap assertion did NOT recur in the full
  run — consistent with the verified one-off flake diagnosis (§6).

**Commit hygiene:** staged ONLY the A4 hunks (`git add -p` + hand-crafted
patches for the two entangled files): `StreamingClipSource.h` (windowLen_ +
lookahead), `RoutingManager.{h,cpp}` (`setClipSourcesNonRealtime` decl + impl
only — the lesson-18 `prebuildTracks`/`buildTrackProcessor`/`prebuiltTracks` WIP
remains **unstaged**), `ExportManager.cpp` (wiring), and the e2e test. The
pre-existing WIP and the ~104 doc deletions are untouched. To resume WIP work:
`git diff src/engine/RoutingManager.{h,cpp}` shows the remaining uncommitted
two-phase-rebuild hunks.

**A5 (plan Task 5):** complete — full suite green, release compile-out clean,
version **0.22.0** committed `5fa81f8` — see §10.

## 10. A5 (Task 5 gates) — complete

- Full engine suite: 796 tests, 791 PASS, 5 FAILED = exactly the known
  pre-existing CrashRecovery/PluginIsolation proxy-spawn failures. No new
  failures, no false positives.
- `audio-dsp-review` + `audio-numerics-review` on `StreamingClipSource.h`: both
  **Safe**. Audio-thread `readNextBlock` allocates nothing, locks nothing, does
  no I/O (atomics + fixed buffers only); I/O/allocation live on the background
  reader thread (`readerLoop`/`fillBuffer`) or message thread (`fillWholeFile`).
  int16→float via `/32768.0f` is bounded (no denormals); all reads guarded by
  `off + s < fill` / `covered`.
- Memory ceiling: `LongFileNotWholeFileResident` asserts long clips stream
  (~700 KB double buffer) instead of whole-file preload (≈21 MB for 60 s) and
  that `preloadedData` is not filled for streaming clips.
- Anti-pattern scan: no audio-thread I/O/alloc/lock, no `DBG`, no full-tree
  walks, starvation renders silence (HISE contract), no new RPC/MCP surface.
- Version bump: **0.22.0** — `CMakeLists.txt:2` + `frontend/package.json:3`,
  committed `5fa81f8` (`chore: bump to 0.22.0 (clip disk streaming)`).
- Release compile-out: `cmake --build build --config Release --target HDAW`
  clean; `build/Release/HDAW.exe` fresh. Frontend `npm run build` clean.
- Knowledge graph refreshed: `index_repository` (full) → 7291 nodes / 19311
  edges.
- **Subsystem A is complete.** Remaining known debt: `RoutingManager.{h,cpp}`
  still carries the uncommitted lesson-18 `prebuildTracks`/`buildTrackProcessor`/
  `prebuiltTracks` two-phase-rebuild WIP (sampler-followup; builds, but commit
  it separately when the feature lands).