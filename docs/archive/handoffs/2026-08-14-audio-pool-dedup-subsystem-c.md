# Handoff: Subsystem C (audio pool dedup) complete — repo at 0.22.1

Date: 2026-08-14 (second session of the day). **Subsystem C — audio pool
dedup / shared decodes — is COMPLETE, reviewed, and committed** (6 commits).
The repo is at **0.22.1**. Read this file first; then read
`AGENTS.md` (lessons) and the master plan
`docs/plans/2026-08-13-hise-derived-features-master-plan.md` before picking
work.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Version **0.22.1** (audio pool dedup; version TRIPLE in sync:
  `CMakeLists.txt`, `frontend/package.json`, **and
  `frontend/src/version.ts`** — the third file was missed twice before;
  ALWAYS check all three).
- Master-plan progress: **A (clip disk streaming) done, B (realtime-safety
  instrumentation) done, C (audio pool dedup) done.** Remaining: **D**
  (export non-realtime streaming — blocked-on-A is now satisfied),
  **E** (sampler loop crossfade precompute), verification-only items.
- Full suite at last full run: **807/807 PASS** (no stale engines running).
  The 5 CrashRecovery/PluginIsolation proxy tests fail ONLY when a stale
  `HDAW_headless_mcp.exe`/`HDAW.exe`/`hdaw_plugin_host.exe` tree is alive
  (lesson 20) — check before blaming code.
- Knowledge graph refreshed after C (`codebase-memory`, project
  `D-pdf-roo-projects-hdaw3`, 7354 nodes).

## 2. What Subsystem C delivered (commits, in order)

| Commit | Contents |
|--------|----------|
| `878f7d3` | `src/engine/DecodedSoundPool.h` — `DecodedSound` (immutable float PCM, static `decode`) + `DecodedSoundPool` (STRONG map keyed by full path; `acquire` decodes once per file; `pruneUnreferenced` evicts `use_count()<=1`; INT_MAX guard) + 6 unit tests |
| `e7eaf12` | `ClipSourceProcessor` borrows pooled buffers (HeapBlock<float> → `shared_ptr<const DecodedSound> decoded_`; mono aliases ch1→ch0; 3rd ctor param `DecodedSoundPool* = nullptr` keeps 2-arg callers compiling) |
| `582c8b8` | Sampler via pool: `TrackFXSlot::loadSamplerState(slotTree, fm, pool)` (pooled copy → SamplerSound::Builder; no-pool fallback byte-identical), `Track::setDecodedSoundPool`, raw-pointer test hooks |
| `3298a28` | Wiring: `ProjectPool` owns the pool → `AudioEngine::initialize` → `MainAudioProcessor` → BOTH `RoutingManager` ctor sites → `rebuildClipsForTrack` (clips) + `buildTrackProcessor`→`Track` (sampler). Export deliberately pool-free (render thread). |
| `d1fc345` | 0.22.1 bump (2 files) + review doc fixes |
| `4d1db85` | `version.ts` 0.22.1 (final-review catch) + production `pruneUnreferenced()` at top of `MainAudioProcessor::rebuildRoutingGraph` |

**Contract:** two clips (or clip + sampler slot) referencing the same file →
ONE decode (`decodeCount==1`), asserted across `rebuildRoutingGraph()` on
LIVE processors. Audio thread never touches the pool (reads raw pointers
only). Suite `AudioPoolDedup` = 11 tests.

**Threading notes (documented in `DecodedSoundPool.h`):** pool is
message-thread-only with two serialized-in-practice exceptions —
`prebuildTracks` (pre-park) and `prepareToPlay` at device start (always a
cache hit). Prune runs on `rebuildRoutingGraph` (message/command threads,
verified all 18 callers).

## 3. Follow-ups identified during C reviews (all non-blocking, prioritized)

1. **Pre-existing `rebuildTrackFX` race (NOT introduced by C):** it mutates
   track state without parking the pump while the pump's async
   `rebuildRoutingGraph` (queued by clip/track adds) can destroy the same
   Track mid-rebuild → use-after-free window. Fix candidate: wrap
   `rebuildTrackFX` in the Gate-12 park idiom (see
   `MainAudioProcessor::rebuildRoutingGraph` for the reference pattern).
   Tests currently dodge it with documented `juce::Thread::sleep(50)`s.
2. **No public drain seam for the async graph bake:** engine-level tests
   sleep ~50 ms ×3 because `rebuildRoutingGraph()` returns before the
   `AudioProcessorGraph` async bake (pump thread) prepares clips, and
   `handleAsyncUpdate`/`graphRebuildPending` are private. A test seam (or
   poll hook) would de-flake. Do NOT add engine APIs casually.
3. **Streaming handle sharing (Subsystem A pairing, deferred):** two clips
   of the same LONG (>8 s, streaming) file still open one reader + one
   double-buffer each. Pool currently covers only whole-file resident
   decodes (≤8 s preload path + sampler).
4. Minor polish: missing-file DIAG log in `preloadWholeFile` lost the
   `exists=`/`reader=` detail (restore when touching it next); pool path
   key is case-sensitive on a case-insensitive FS; no stereo-file coverage
   in AudioPoolDedup tests (all WAVs mono); take-switch decode-count not
   pinned by a test; `ClipAndSamplerShareOneDecodeAcrossRebuild` final
   live-slot read has a theoretical pump-race window (documented in-file).

## 4. ⚠️ Stash incident (unchanged — read before touching git stash)

An **ancient pre-existing stash** (`stash@{0}`: "WIP on main: 8b844ac Initial
source drop…") is still in the stash list. **Do NOT `git stash push/pop`
casually** — a failed push + pop once applied a 13-file Qt-UI-era stash onto
the modern tree with conflicts (cleaned up; the entry was kept — not ours to
delete). If a stash op misbehaves: `git reset`, `git checkout HEAD --
<files>`, verify status counts.

## 5. Remaining dirty tree (intentional — do not sweep blindly)

- **Docs restructure (pre-existing):** 104 D (old `docs/superpowers/plans/*`,
  `docs/phase5.md`, etc.) + modified `AGENTS.md`, `README.md`,
  `docs/realtime-safety.md`, `docs/testing-mcp.md`,
  `docs/plans/2026-08-11-fix-scan-blacklist-bugs.md`,
  `.opencode/skills/hdaw-guard/SKILL.md` + untracked `docs/archive/` (the
  moved tree) + `tests/unit/engine/ghost_clips_test.cpp` ref → one coherent
  unit: **"chore(docs): archive legacy plans/specs under docs/archive"** —
  verify archive completeness + reference consistency before staging.
- **Untracked docs worth committing now:** ~8 handoff docs in
  `docs/handoffs/` (incl. this one) and plan docs whose features HAVE now
  landed — notably `docs/plans/2026-08-13-audio-pool-dedup.md` (C is done —
  commit it), plus clip-disk-streaming, realtime-safety-instrumentation,
  hise master plan (all landed).
- **Untracked artifacts:** 10 `polyrhythm_drums_*.hdaw3` test projects +
  `compositions/` (audio test media — gitignore candidates).
- Nothing in `src/`, `tests/`, or `frontend/src` is dirty (all code
  committed through `4d1db85`).

## 6. Next steps (suggested)

1. Commit the landed plan docs + handoffs (convention: commit when final),
   and/or the whole docs-restructure unit (§5).
2. **Subsystem D — export non-realtime streaming** (master plan §D): loader
   `setNonRealtime(true)` synchronous read during export. A's
   `StreamingClipSource::setNonRealtime` already exists and
   `ClipSourceProcessor::setNonRealtimeFlag` is wired by ExportManager —
   verify D's gates (sync read, bit-identical export) may be partially met;
   write the dedicated plan first (`docs/plans/2026-08-13-export-non-realtime-streaming.md`
   per master plan; use `writing-plans` + hdaw-guard pre-flight).
3. **Subsystem E — sampler loop crossfade** (independent) or follow-up #1
   (the `rebuildTrackFX` park fix — small, high-value, test-de-flaking).
4. Optional cleanup: gitignore `*.hdaw3` test projects + `compositions/`;
   re-index codebase-memory if structural code lands.

## 7. Environment

- Build: `cmake --build build --config Debug --target hdaw_tests`; tests:
  `& "build\Debug\hdaw_tests.exe" --gtest_filter=<suite>.*`
- Frontend: `cd frontend && npm run build` (NOT `tsc --noEmit` — broken
  baseline); Vitest `npm test`. Frontend changes need `frontend\build.bat`
  or `npm run build` + C++ rebuild to reach the running app.
- Do NOT run `build/Release/HDAW.exe` (stale).
- If orphaned `cl.exe` holds locks after a timed-out build:
  `Get-Process cl | Stop-Process -Force`.
- Before debugging proxy-spawn test failures: check for live engines
  (lesson 20); killing `HDAW_headless_mcp.exe` kills the session's hdaw MCP
  backend.
