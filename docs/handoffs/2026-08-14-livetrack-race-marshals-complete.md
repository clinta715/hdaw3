# Handoff: live-Track race-marshals complete — repo at 0.22.4

Date: 2026-08-14 (fourth session of the day). **The entire live-processor
mutation surface is now message-thread serialized and regression-covered.**
Subsystem D closed earlier in the day; this session landed the race-marshal
family (3 fixes, 3 plans, 1 chore). Read this file first; then `AGENTS.md`
(lessons) and the race-fix plans referenced in §2.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Version **0.22.4**. Version triple to keep in sync on any bump:
  `CMakeLists.txt`, `frontend/package.json`, **and**
  `frontend/src/version.ts`.
- Master-plan progress: A/B/C/D done. Remaining: **E** (sampler loop
  crossfade precompute) + verification-only no-ops.
- Full suite at close of session: **816/816 PASS** (155 suites, ~8.7 min;
  was 807 at D's close — +6 `TrackFxRebuildRace` +3 D gate tests).
- Knowledge graph refreshed after every structural commit (last: 7366 nodes,
  19111 edges, `compositions/` now excluded).

## 2. What this session delivered (commits, in order)

| Commit | Contents |
|--------|----------|
| `ea1b6a4` | docs: Subsystem D plan + gate results (D Task 3 closed) |
| `0316921` | docs: handoff — Subsystem D export gates closed |
| `e68bfc3` | fix(engine): marshal track-FX rebuilds to the message thread — `runOnMessageThread` helper added; `rebuildTrackFX`/`rebuildMidiTrackFX` marshaled; `TrackFxRebuildRace` suite born (2 tests). Version 0.22.2 |
| `717b2d5` | docs: rebuildTrackFX race-fix plan + gate results |
| `43554e0` | fix(engine): marshal modulation/automation/FX-editor mutations — `rebuildModulation`, `rebuildAutomationCache`, `toggleFXEditor`; +`Track::getNumModulations()`. Version 0.22.3 |
| `2a49841` | docs: live-track mutation race-marshals plan + gate results |
| `633c561` | fix(engine): marshal rebuildMidiClipCache — new marshaled `MainAudioProcessor::rebuildMidiClipCache` (internal, NOT RPC-exposed); all 7 listener sites rewritten; +`MidiClipProcessor::getNumCachedNotes()`. Version 0.22.4 |
| `b81959e` | chore: gitignore root `*.hdaw3` test projects + `compositions/` |
| `b2f99bc` | docs: rebuildMidiClipCache marshal plan + gate results |

### The marshal pattern (reusable, established)

- Helper: `runOnMessageThread(Fn&&)` — anonymous namespace, top of
  `src/engine/MainAudioProcessor.cpp` (~line 76), with the full rationale
  comment block. **Marshal, never park:** a `MessageManagerLock` park would
  self-deadlock in-process plugin instantiation (lesson 18 — dispatches to
  the suspended message thread); `callFunctionOnMessageThread` serializes
  against the pump's async rebuild instead (pump delivers one message at a
  time; any other thread's `rebuildRoutingGraph` must park first, which
  suspends the message thread inside the holder — no destruction can overlap
  the mutation from any direction).
- Rule for new uses: **all pointer lookups (`routingManager`, `getTrackNode`,
  `projectModel`) INSIDE the callback** — the manager can be swapped while
  the callback waits for the pump. Callers must not be the audio thread and
  must not hold a `MessageManagerLock` (helper jasserts).
- Marshaled entry points (complete list): `rebuildTrackFX`,
  `rebuildMidiTrackFX`, `rebuildModulation`, `rebuildAutomationCache`,
  `toggleFXEditor`, `rebuildMidiClipCache`.
- Test pattern: `TrackFxRebuildRace` (6 tests,
  `tests/unit/engine/track_fx_rebuild_race_test.cpp`) — queue the async graph
  rebuild (clip add), mutate from the test thread, assert on the LIVE
  processor synchronously, NO sleep. Baseline-relative counts (lesson 9:
  track 0 ships Volume/Pan/Mute automation lanes; tracks 1/2 shapes differ).

## 3. Immediate remaining items (prioritized)

1. **§5 docs-archive commit (housekeeping, closes the dirty tree):** ~104
   deletions (old `docs/superpowers/plans/*`, `docs/phase5.md`, etc.) +
   modified `AGENTS.md`, `README.md`, `docs/realtime-safety.md`,
   `docs/testing-mcp.md`, `docs/plans/2026-08-11-fix-scan-blacklist-bugs.md`,
   `.opencode/skills/hdaw-guard/SKILL.md`,
   `tests/unit/engine/ghost_clips_test.cpp` + untracked `docs/archive/` —
   commit as **"chore(docs): archive legacy plans/specs under
   docs/archive"** AFTER verifying (a) everything deleted exists under
   `docs/archive/`, (b) no tracked file references the old paths
   (`rg "superpowers/plans|docs/phase5|recurring-pitfalls" --glob '!docs/archive'`).
   The `implementation_plan.md` deletion (repo root) is part of this unit.
2. **Drain seam for the async graph bake (de-flake):** engine tests sleep
   ~50 ms ×3 waiting for the pump's `handleAsyncUpdate` →
   `rebuildRoutingGraph`. A test seam would de-flake. **Do NOT add engine
   APIs casually** — see the constraints in
   `tests/unit/engine/audio_pool_dedup_test.cpp:227-241` (handleAsyncUpdate
   is private, graphRebuildPending has no accessor). The marshal work did
   NOT add a seam; the sleeps remain.
3. **Streaming handle sharing:** two clips of the same LONG (> 8 s) file
   still open one reader + double-buffer each (the audio pool covers
   resident decodes only).
4. **Subsystem E — sampler loop crossfade precompute** (last master-plan
   item, independent, quality).
5. Minor: stereo-WAV coverage gap in `AudioPoolDedup` tests.

## 4. ⚠️ Standing warnings

- **Do NOT `git add -A` / `git add .`** — stage exact paths only.
- **Do NOT touch `git stash`** — ancient pre-existing stash
  (`stash@{0}`: "WIP on main: 8b844ac …") once mis-applied a 13-file
  Qt-UI-era stash. If a stash op misbehaves: `git reset`,
  `git checkout HEAD -- <files>`, verify counts.
- **Do NOT kill `HDAW_headless_mcp.exe`** — it is the session's hdaw MCP
  backend. Before debugging proxy-spawn/export-test failures, check for
  orphaned `hdaw_plugin_host.exe` (lesson 20); kill only true orphans.
- New engine code touching live processors from listeners/commands: use the
  marshal pattern (§2), never a park, and add a `TrackFxRebuildRace`-style
  no-sleep regression test.

## 5. Environment

- Build: `cmake --build build --config Debug --target hdaw_tests`; tests:
  `& "build\Debug\hdaw_tests.exe" --gtest_filter=<suite>.*`
- Full suite ~9 min (816 tests). Orphaned `cl.exe` after timed-out builds:
  `Get-Process cl | Stop-Process -Force`.
- Frontend: `cd frontend && npm run build` (NOT `tsc --noEmit` — broken
  baseline); Vitest `npm test`. Frontend changes need `frontend\build.bat`
  or `npm run build` + C++ rebuild to reach the running app.
- Do NOT run `build/Release/HDAW.exe` (stale).
