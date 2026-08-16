# Handoff: Subsystem D (export non-realtime streaming) gates closed — repo at 0.22.1

Date: 2026-08-14 (third session of the day). **Subsystem D is functionally
complete and gate-verified.** The functional code landed in 0.22.0
(`76bc275`); this session wrote the dedicated plan, added the three gate
tests (all PASS), and recorded the results. **Two small Task-3 items remain
(see §3).** Read this file first; then read `AGENTS.md` (lessons) and the
master plan `docs/plans/2026-08-13-hise-derived-features-master-plan.md`.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Version **0.22.1** (unchanged this session — D adds tests/docs only; the
  no-bump decision is recorded as Deviation 2 in D's plan). Version triple
  to keep in sync if you ever bump: `CMakeLists.txt`,
  `frontend/package.json`, **and** `frontend/src/version.ts`.
- Master-plan progress: **A (clip disk streaming) done, B (realtime-safety
  instrumentation) done, C (audio pool dedup) done, D (export non-realtime
  streaming) done.** Remaining: **E** (sampler loop crossfade precompute),
  verification-only items (documented as no-ops in the master plan).
- Test evidence at close of session: `ClipStreamingE2E.*:StreamingClipSource.*`
  **9/9 PASS**; `McpServer.Export*` **7/7 PASS** (incl. CLAP export tests;
  no orphaned plugin hosts — lesson 20 checked).
- Full suite at last full run (previous session): 807/807 PASS.

## 2. What this session delivered (commits, in order)

| Commit | Contents |
|--------|----------|
| `e488482` | docs: 8 landed implementation plans (sampler design+plan, clip-disk-streaming, RT instrumentation, audio-pool-dedup, preset tool, sampler-silent-export-fix, hise master plan) |
| `18e0258` | docs: 9 session handoff notes (2026-08-10..14) |
| `9bba444` | test(stream): D gate G1 — `NonRealtimeJumpRefillsSynchronouslyWithoutStarvation` (20 s jump, sync refill, `starvedCount()==0`) + `NonRealtimeStreamingMatchesWholeFileDecode` (NR stream == `DecodedSound` float decode within 2 LSB int16 over 9 s / two window refills) |
| `2a2cefe` | test(mcp): D gate G2 — `ExportAudioStreamsLongClipWithoutDropouts` (12 s streamed clip via `export_audio`; every 100 ms slice RMS > 0.1; catches the pre-`76bc275` ~74-block starvation dropout) |

Also written (UNCOMMITTED — see §3): 
`docs/plans/2026-08-13-export-non-realtime-streaming.md` — the dedicated
Subsystem D plan with gate checkboxes marked PASS and the two recorded
deviations:
- **Deviation 1:** master-plan G1 said "bit-identical to whole-file
  preload"; streaming stores int16 (A's decision, lesson 8), so parity is
  within int16 requantization (idempotent for 16-bit sources; 2 LSB guard
  band in tests).
- **Deviation 2:** no version bump — D shipped functionally in 0.22.0.

**Implementation deviation worth knowing:** the integration test needed
`engine.initialize()` — `getProjectCommands()` is only created inside it
(`AudioEngine.cpp:220`); without it the first RPC (`set_tempo`) SEH-crashes.
The plan's original code block omitted this; the committed test has it.
When committing the plan doc (§3), optionally note this in the plan file.

## 3. Immediate remaining items (small, start here)

1. **Commit the D plan doc:** `git add docs/plans/2026-08-13-export-non-realtime-streaming.md`
   then `git commit -m "docs: Subsystem D plan + gate results (export non-realtime streaming)"`.
   (Gate checkboxes were marked PASS after the test commits; the file is
   otherwise final.)
2. **Refresh the knowledge graph:** `codebase-memory` MCP `index_repository`
   (repo_path `D:\pdf\roo projects\hdaw3`, mode `fast`) — D plan G5; the
   graph predates this session's 4 commits.
3. Optionally commit this handoff doc too.

## 4. Follow-ups (from C/D sessions, all non-blocking, prioritized)

1. **`rebuildTrackFX` race (pre-existing, highest-value small fix):** it
   mutates track FX state without parking the pump while an async
   `rebuildRoutingGraph` can destroy the same Track mid-rebuild
   (use-after-free window; tests dodge it with `juce::Thread::sleep(50)`).
   Fix candidate: the Gate-12 park idiom, reference at
   `MainAudioProcessor::rebuildRoutingGraph` (`MainAudioProcessor.cpp:478`).
   **Careful:** lesson 18 — `rebuildFXChain` instantiates plugins in-process,
   which must happen PRE-park (see `prebuildTracks`), so the fix needs a
   two-phase shape, not a naive wrap.
2. No public drain seam for the async graph bake — engine tests sleep
   ~50 ms ×3. A test seam would de-flake (do NOT add engine APIs casually).
3. Streaming handle sharing: two clips of the same LONG (> 8 s) file still
   open one reader + double-buffer each (pool covers resident decodes only).
4. Subsystem E — sampler loop crossfade precompute (independent, quality).
5. Minor: gitignore the 10 `polyrhythm_drums_*.hdaw3` test projects +
   `compositions/`; stereo-WAV coverage gap in AudioPoolDedup tests.

## 5. Remaining dirty tree (intentional — do not sweep blindly)

- **Docs restructure unit (pre-existing, one coherent commit):** 104 D
  (old `docs/superpowers/plans/*`, `docs/phase5.md`, etc.) + modified
  `AGENTS.md`, `README.md`, `docs/realtime-safety.md`, `docs/testing-mcp.md`,
  `docs/plans/2026-08-11-fix-scan-blacklist-bugs.md`,
  `.opencode/skills/hdaw-guard/SKILL.md`, `tests/unit/engine/ghost_clips_test.cpp`
  + untracked `docs/archive/` — commit as
  **"chore(docs): archive legacy plans/specs under docs/archive"** after
  verifying archive completeness + reference consistency.
- **Untracked artifacts:** 10 `polyrhythm_drums_*.hdaw3` + `compositions/`
  (audio test media — gitignore candidates).
- Nothing in `src/`, `tests/`, or `frontend/src` is dirty (all code
  committed through `2a2cefe`).

## 6. ⚠️ Standing warnings

- **Do NOT `git add -A` / `git add .`** — stage exact paths only (§5).
- **Do NOT touch `git stash`** — an ancient pre-existing stash
  (`stash@{0}`: "WIP on main: 8b844ac Initial source drop…") once
  mis-applied a 13-file Qt-UI-era stash onto the modern tree. If a stash op
  misbehaves: `git reset`, `git checkout HEAD -- <files>`, verify counts.
- **Do NOT kill `HDAW_headless_mcp.exe`** — it is the session's hdaw MCP
  backend. Before debugging proxy-spawn/export-test failures, check for
  orphaned `hdaw_plugin_host.exe` (lesson 20); kill only true orphans.

## 7. Environment

- Build: `cmake --build build --config Debug --target hdaw_tests`; tests:
  `& "build\Debug\hdaw_tests.exe" --gtest_filter=<suite>.*`
- If orphaned `cl.exe` holds locks after a timed-out build:
  `Get-Process cl | Stop-Process -Force`.
- Frontend: `cd frontend && npm run build` (NOT `tsc --noEmit` — broken
  baseline); Vitest `npm test`. Frontend changes need `frontend\build.bat`
  or `npm run build` + C++ rebuild to reach the running app.
- Do NOT run `build/Release/HDAW.exe` (stale).
