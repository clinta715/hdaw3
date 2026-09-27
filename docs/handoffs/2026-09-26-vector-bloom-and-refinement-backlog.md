# Handoff — vector-bloom composition, multi-sampler chain fix, refinement backlog

**Date:** 2026-09-26 · **Continues:**
[`2026-09-26-b2b-and-test-infra.md`](2026-09-26-b2b-and-test-infra.md)
**Scope:** psy-song-session vector-bloom composition session (PASS deliverable),
multi-sampler chain render fix (shipped + full regression), and the OPEN
refinement/improvement/bugfix backlog for the next session.

## 1. State

- **Engine:** v0.39.0 + the multi-sampler chain render fix. Working tree is
  commit-ready, uncommitted at writing: `src/engine/TrackFXSlot.h`,
  `src/engine/Track.cpp`, `tests/unit/engine/sampler_key_range_test.cpp`, README
  changelog, `docs/lessons-learned.md` #33, AGENTS.md index #33,
  `docs/skills/psy-song-session/*` sync,
  `docs/psytrance-composition-guide.md` registry correction.
- **Authoritative validation (2026-09-26):** `run-tests-sharded.ps1 -Shards 2` →
  **2031/2031 executed, 1992 passed, 39 skipped, 0 failures** (288 suites; shards
  825/136, 884/137, 322/22).
- **Deliverable:** `compositions/vector-bloom/vector-bloom.wav` — 304.714 s =
  301.714 s body + 3.0 s tail, 48 kHz/24-bit; `mix_verdict` ok:true, issues [],
  1 warning, 0 re-renders — plus `vector-bloom.hdaw`, `brief.json`
  (`artifacts.renders` recorded), `session-report.md`.
- Engine issues were filed via the QA channel (`import_pattern`,
  `automation_preset` cycles:6, `sampler_get_state.hasSound`) — the backlog rows
  below carry the durable detail.

## 2. What landed

- **Workflow-docs sync to the v0.39 contract:** stable trackID split parsing,
  `set_cells`/`mix_verdict`, windowed-render rule, breakdown-tail recipe,
  multi-CLAP budgets, warmup/fake-id traps, registry merge-on-save, host call
  variants; two wording defects fixed (spelling-preserving disagreement text;
  strict-on-present semantics).
- **Full psy-song-session run:** offline Curator + Pattern Researcher → Sound
  Selector → Arranger plan/cells → FX & Automation → Mix Verifier, with every
  gate evidenced (detail in `compositions/vector-bloom/session-report.md`).
- **Multi-sampler chain render fix:** every sampler slot cleared the shared
  chain buffer before `SamplerEngine::render()`, so ≥2-sampler chains kept only
  the LAST slot's audio; chain-level accumulate now preserves the running sum
  across render()'s clear (first engaged sampler = legacy REPLACE, byte-identical
  single-sampler). Shipped with the full regression in §1.

## 3. OPEN — refinement / improvement / bugfix backlog

Each row: repro + what a fix needs.

### B1 — `import_pattern` stores a hollow envelope (FIXED 2026-09-26, follow-up session)

- **Repro:** `import_pattern` a JSON pattern — reports success, but
  `export_pattern` of the new id returns only the preset envelope (no
  notes/role/descriptor); 10/10 hollow on 2026-09-26. `save_pattern` retains
  everything (used as the re-stock path; dated caution in
  `docs/skills/psy-song-session/roles/pattern-researcher.md`).
- **Fix needs:** diff the import vs save preset-serialization paths (import
  likely writes the envelope but not `params.notes/role/descriptor`), then an
  import→export round-trip regression test asserting notes/role/descriptor
  equality.

### B2 — `automation_preset` sine `cycles:6` degenerates (FIXED 2026-09-26, follow-up session)

- **Repro:** sine preset, section [256,352) on the pad filter lane, cycles:6 →
  section nearly silent (−88.6% rms); cycles:4, same call → −17.4% (musical).
- **Fix needs:** unit-test the sine generator across cycles 1..8 on windows not
  divisible by the cycle count; assert point amplitudes span the intended range
  (no collapse to the floor); fix the step math.

### B3 — `sampler_get_state.hasSound` is property-only (FIXED 2026-09-26, follow-up session)

- **Repro:** a slot with non-empty `sampleFile` but no decoded sound reports
  `hasSound=true` (masked the silent-hats incident — staging read "green"). The
  live decoded-sound check exists at `src/engine/ReadModelImpl.cpp:669` but is
  not exposed; the tool (`src/mcp/McpTools_Sampler.cpp:118`) reads only the
  property.
- **Fix needs:** expose the live check (e.g. `hasSound` = live +
  `hasSampleFile` = property), MCP + RPC twin (parity rule), regression test.

### B4 — Multi-sampler fix leftovers (CLEANUP)

- **Repro/known state:** (a) new C4267 size_t→int warnings at
  `TrackFXSlot.h:1276-1277` from the fix. (b) `Track.cpp`'s `anyPartialSampler`
  pre-clear is now redundant for engaged chains (the first sampler's `render()`
  clears anyway); it was deliberately preserved for bypassed-chain behavior. (c)
  the documented one-block bypass-toggle race at `isEngagedSampler()`
  self-corrects.
- **Fix needs:** tidy (a); decide and simplify (b) with a test either way;
  revisit (c) only if bypass toggling becomes hot.

### B5 — `analyze_tuning` role rows are meaningless on a full mix (IMPROVEMENT)

- **Repro:** analyze the vector-bloom master — every row shares ONE spectrum
  (centroid 336.9 Hz etc.) and 5/6 role rows "fail" while the mix is
  release-pass.
- **Fix needs:** accept per-role stems/`trackIndex` (role rows only when stems
  supplied; else a single master verdict) or return N/A for role rows on a
  master file.

### B6 — RMS convention + target-aware verdict (IMPROVEMENT)

- **Repro:** `mix_report`/`mix_verdict` measure mono-downmix (L+R)/2
  (vector-bloom: 0.1524 mono vs 0.1604 full-bandwidth vs the brief's 0.16 target
  — reconciled by hand).
- **Fix needs:** pin the convention (`brief.schema.json` `targets.masterRms`
  description + tool docs) and feed `brief.targets` into
  `mix_report`/`mix_verdict` as per-target PASS/FAIL rows (`masterRms`,
  `ceilingHitPctMax`, `kickProminenceMin`, `targetDurationSeconds`). Also add
  `ceilingHitPct` as tool output (the Mix Verifier computed 0.000065% by parsing
  the WAV by hand; the engine's mono-peak clipping gate is blind to per-channel
  FS clamps). Optional `loudness_headroom {targetRms}` helper returning
  maxReachableRms + sanctioned levers (the "+0.42 dB would clip the sum"
  arithmetic was manual).

### B7 — vector-bloom polish (MUSICAL, optional)

- **Repro:** 19 R-channel frames pin at −1.0 FS (0.000065% of samples, within
  the 5% ceiling budget) at beats ≈ 544.5/551/583/583.5/586.5/640.8/651/689
  (drop-c/outro edges).
- **Fix surface:** transient-edge trim at those boundaries (FX/arranger), NOT
  master gain; then re-render + re-verdict.

### B8 — Build ergonomics (REFINEMENT)

- **Repro:** `dsh-build-fast.bat`/`build-fast.bat` should set TMP/TEMP to a
  workspace scratch (e.g. `.tmp_build_scratch/lnk`) automatically — the low-IL
  sandbox denies the real `%TEMP%` and `link.exe` dies on `lnk{GUID}.tmp`
  (LNK1104, hit twice 2026-09-26; documented class = 09-26 trap 4). Also
  housekeeping: `build/SoundTouch.dll.locked-20260926` (locked-DLL rename
  workaround leftover) — delete once no live engine maps it;
  `%TEMP%\hdaw_debug.log` reached 393 MB (`scripts/cleanup-stale.ps1` covers
  it).
- **Fix needs:** the TMP/TEMP scratch default in the build scripts; the two
  leftover cleanups above.

## 3a. Follow-up session (2026-09-26, later): B1–B3 fixed

- **B1** — `PatternPreset.extraJson` verbatim passthrough + ONE `buildOutputObject` for save/import/export; shared `src/common/PatternPresetJson.h` unifies `load_pattern` ↔ `composition.loadPattern` (the RPC copy had also lost category/author/createdAt). Tests: `PatternLibraryTest.*` (4 new).
- **B2** — root cause was the SECTIONS parse, not the generator (pure math proven healthy for cycles 1..8 by simulation): the sections form never read `cycles`/`midPoint` (no per-section key, no top-level fallback), so `{cycles:6}` landed the 24-cycle len/4 default. Fixed in `src/common/AutomationPresetRequest.h` + tool schema/description. Tests: `Automation.SinePresetCyclesSpanAllCycleCounts`, `Automation.SinePresetDefaultCyclesIsWindowLengthOver4`, `AddFxParityTest.SectionsFormCyclesReachThePlanOnBothSurfaces` (both surfaces).
- **B3** — `SamplerStateSnapshot.hasSampleFile` (property) + live `hasSound` emitted by ONE shaper `src/common/SamplerStateJson.h` on MCP `sampler_get_state`, `sampler.getState`, `read.getSamplerState` (the read route had also lost the slice/voice fields). Tests: `GuiFuncTest.SamplerGetStateHasSoundIsLiveNotPropertyOnly`, `FrontendServer.SamplerGetStateLiveHasSoundPlusHasSampleFile`.
- Gates: 298-test regression net (Automation, PatternLibrary, AddFxParity, MissingRouteParity, GuiFunc, FrontendServer, McpCoverage, sampler suites) 0 failures; parity ratchet ledger unchanged (307/411/mapped 295). Lessons: #33 resolution + new #34 (AGENTS.md index 34). Full sharded run NOT re-run — baseline numbers in AGENTS.md still cite the 2026-09-26 authoritative run.

## 4. Contracts worth remembering

1. Engine calls from omp-harness subagents work via `write` to
   `xd://mcp__hdaw_<tool>` (probed) — the psy-song-session dispatch preamble
   documents both host variants.
2. `run-tests-sharded` results survive a dead orchestrator: shard logs in
   `.tmp_tests/shard_logs/` + gtest banners + `--gtest_list_tests` reconstruct
   the runner's intended-vs-executed accounting — consume them instead of
   re-running (a second concurrent run would collide).
3. Engine and tests are restart-safe through checkpoints — the FX pass lost
   zero work to a mid-pass engine restart because it saved per mutation group.

## 5. Traps learned this session

1. An eval-kernel death (exit 130) leaves ORPHANED child processes (runner +
   shards kept running) — check tasklist and consume surviving logs before any
   retry.
2. `tasklist` in the sandboxed eval may not show the MCP engine at all (it lives
   outside the visible session) — engine state = `engine_info` / `proc://`, not
   tasklist.
3. `SoundTouch.dll` write-denied with no visible holder = low-integrity access
   class, not a live mapping — `tasklist /m` empty + rename-aside works; build
   with TMP/TEMP scratch; `*.locked-<date>` leftovers deletable when their
   holder exits.
4. GO signals to held subagents can race their ACK turns — re-send an
   unambiguous GO rather than assuming delivery order.
5. A build blocked on `build\SoundTouch.dll` is the running engine mapping it
   via the launcher's PATH — kill engine services first (`proc://<id>/kill`),
   don't retry blindly.

## 6. Housekeeping

- Uncommitted files at writing (commit-ready): `src/engine/TrackFXSlot.h`,
  `src/engine/Track.cpp`, `tests/unit/engine/sampler_key_range_test.cpp`, README
  changelog, `docs/lessons-learned.md` #33, AGENTS.md index #33,
  `docs/skills/psy-song-session/*` sync,
  `docs/psytrance-composition-guide.md` registry correction.
- `build/SoundTouch.dll.locked-20260926` — delete once no live engine maps it.
- Scratch dirs (gitignored): `.tmp_tests/`, `.tmp_build_scratch/`.
