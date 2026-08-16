# Handoff: WIP cleanup + sampler follow-up committed — docs restructure remains

Date: 2026-08-14. **All uncommitted CODE is now committed.** The repo is at
**0.22.0**. What remains dirty is the pre-existing **docs restructure** and
untracked artifacts — nothing code-wise is outstanding.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(AGENTS.md). Read `docs/handoffs/2026-08-14-clip-disk-streaming-a4.md` for
Subsystem A history (A1–A5, clip disk streaming — **complete**).

---

## 1. Current status

- Version **0.22.0** (clip disk streaming — Subsystem A complete: A1–A3, A4
  `76bc275` half-window lookahead, A5 gates + version bump `5fa81f8`).
- Full suite: **796 tests, 791 PASS, 5 FAILED** = exactly the known
  pre-existing CrashRecovery/PluginIsolation proxy-spawn failures
  (CrashRecovery.AutoRespawnAfterCrash, CrashRecovery.RespawnDuringActiveProcessing,
  CrashRecovery.DestroyedProxyIsDeregistered,
  CrashRecovery.OfflinePluginDomainIsolatedFromLive,
  PluginIsolation.UniqueSlotIdPerInstance). No new failures, no false
  positives.
- Frontend: `npm run build` clean; Vitest SamplerEditor/StatusBar/
  useTimelineZoom suites pass (13 + 12). **`tsc --noEmit` is NOT a usable
  gate: it fails identically at clean HEAD (14 pre-existing error files:
  Inspector.test, MixerStrip, NoteGrid, meterStore.test, projectStore.test,
  etc.) — use `npm run build` instead.**
- Known flake: `McpServer.DiagnosticClapExportMatrix` heap assertion is a
  verified one-off — ignore unless it repeats.

## 2. Committed this session (12 commits, in order)

| Commit | Contents |
|--------|----------|
| `9f4a4e7` | docs: handoff clip disk streaming A4/A5 complete |
| `1d5af3f` | refactor(engine): **two-phase rebuild** — `prebuildTracks()`/`buildTrackProcessor()`/`prebuiltTracks` in `RoutingManager.{h,cpp}` + `MainAudioProcessor.cpp` caller (lesson 18: instantiate plugins BEFORE parking the pump; `needsPark && !isolationEnabled`) |
| `6f40cec` | fix(clap): **audioThreadId** recorded in `processBlock`; `threadCheckIsAudioThread` compares real ids (lesson 19) |
| `30b2d9c` | fix(plugin): `resolveIdentifierToPath` returns full known entry (VST3 module matching needs scanned uniqueId) |
| `375f516` | feat(readmodel): `SamplerStateSnapshot` + `getSamplerState` + `read.sampler.getState` RPC |
| `02e7694` | feat(sampler): hold/glide/reverse/sample-end atomics in `SamplerEngine.h` + `ProjectSerializer::save` captures live plugin state (callers pass `getMainProcessor()`) |
| `ae051bf` | feat(frontend): SamplerEditor reverse/hold/glide/sample-range controls |
| `7d2dd20` | test(engine): sampler re-prepare keeps loaded sound |
| `cdfd50c` | test(engine): identifier resolution preserves uniqueId |
| `402d867` | test(mcp): transport declared before McpServer (teardown hang fix) |
| `5eaeb0d` | fix(frontend): version.ts → 0.22.0 (missed by `5fa81f8`) |
| `48ad694` | feat(timeline): cursor-pinned wheel zoom + ruler marquee zoom (Ctrl+Alt+drag), plan + unit + e2e |
| `26fadcd` | feat(frontend): .hdaw3 file association + open/save filters |

## 3. ⚠️ Stash incident (read before touching git stash)

An **ancient pre-existing stash** from the repo's initial commit
(`stash@{0}`: "WIP on main: 8b844ac Initial source drop, docs, and v0.2.0
documentation") is still in the stash list. **Do NOT `git stash push` or
`git stash pop` casually** — a failed `stash push` followed by `stash pop`
applied that 13-file Qt-UI-era stash (CMakeLists.txt, AudioEngine.cpp,
PluginManager.cpp, ProjectModel.h, deleted `src/ui/*`) onto the modern tree
with conflicts; it was fully cleaned up (files restored to HEAD, orphaned
`src/ui/` removed, tree verified back to pre-incident state), but the stash
entry itself was **kept** — it is not ours to delete. If a stash operation
ever misbehaves again: `git reset`, `git checkout HEAD -- <files>`, verify
`git status` counts (23 M / 104 D / 27 ?? / 0 conflicts as of this writing).

## 4. Remaining dirty tree (intentional — do not sweep blindly)

- **Docs restructure (pre-existing):** 104 deletions (old
  `docs/superpowers/plans/*`, `docs/phase5.md`, `docs/recurring-pitfalls.md`,
  `docs/handoff-scan-blacklist-bugs.md`, `docs/plans/HANDOFF-…`) + modified
  `AGENTS.md`, `README.md`, `docs/realtime-safety.md`, `docs/testing-mcp.md`,
  `docs/plans/2026-08-11-fix-scan-blacklist-bugs.md`,
  `.opencode/skills/hdaw-guard/SKILL.md` + untracked `docs/archive/`
  (the moved `docs/superpowers/` tree) + `tests/unit/engine/ghost_clips_test.cpp`
  spec-path ref → `docs/archive/…`. This is one coherent unit: **"chore(docs):
  archive legacy plans/specs under docs/archive"** — decide whether to
  commit it as-is (verify the archive tree is complete and references are
  consistent before staging).
- **Untracked artifacts:** ~9 handoff docs in `docs/handoffs/` (previous
  sessions — commit per convention when they're final), ~8 plan docs in
  `docs/plans/` (clip-disk-streaming, internal-sampler-*, realtime-safety,
  hise master plan, preset-file tool — commit when their features land),
  10 `polyrhythm_drums_*.hdaw3` test projects + `compositions/` (audio test
  media — likely gitignore candidates).
- Nothing in `src/`, `tests/` (except ghost_clips ref), or `frontend/src`
  (except docs-related) is dirty.

## 5. Next steps (suggested)

1. (a) Commit the docs restructure as one unit (§4), or (b) leave it and pick
   a feature.
2. Master plan: `docs/plans/2026-08-13-hise-derived-features-master-plan.md`
   — Subsystems A (clip disk streaming) and B (realtime-safety) are done;
   next subsystem/item per the plan. The internal sampler design/plan docs
   (`docs/plans/2026-08-13-internal-sampler-*`) describe the follow-up that
   the sampler work this session built toward.
3. Optional cleanup: gitignore `*.hdaw3` test projects + `compositions/`;
   re-index codebase-memory (`index_repository`, project
   `D-pdf-roo-projects-hdaw3`) if you add structural code.

## 6. Environment

- Build: `cmake --build build --config Debug --target hdaw_tests`; Release
  compile-out: `cmake --build build --config Release --target HDAW`.
- Tests: `& "build\Debug\hdaw_tests.exe" --gtest_filter=<suite>.*`; frontend
  `cd frontend && npm test` / `npm run build` (NOT `tsc --noEmit` — broken
  baseline).
- Do NOT run `build/Release/HDAW.exe` (stale binary). Frontend changes need
  `frontend\build.bat` or `npm run build` + C++ rebuild (AGENTS.md).
- If orphaned `cl.exe` holds locks after a timed-out build:
  `Get-Process cl | Stop-Process -Force`.
