# Handoff — B2b stable-ID acceptance, test-infra hardening, v0.39.0

**Date:** 2026-09-26 · **Base:** `2d6be72` (the v0.38.0 closeout commit) ·
**HEAD at writing:** uncommitted, commit-ready · **Version:** 0.38.0 → **0.39.0**.
**Continues:** [`2026-09-25-psy-dub-and-parity-closeout.md`](2026-09-25-psy-dub-and-parity-closeout.md)
— its §4a–§4e are all now RESOLVED (see the dated notes in place; this file does
not repeat them).

## 1. State

- **Authoritative validation (2026-09-26):** `run-tests-sharded.ps1 -Shards 2` —
  **287 suites / 2027 tests (0 DISABLED), 2027/2027 executed, 1988 passed,
  39 skipped, 0 failures**, every shard `ran == intended` (850/850, 855/855,
  322/322), 27.1 min. This is the first complete green full-suite run of the
  campaign; AGENTS.md's Testing section now carries it as the baseline.
- Version declarations bumped in `CMakeLists.txt` + `frontend/package.json`
  (the two compared by `check_version`), plus `frontend/package-lock.json`
  (was stale at 0.34.0), AGENTS.md and README (highlights + changelog).

## 2. What landed

| area | scope | verified by |
| --- | --- | --- |
| **B2b (§4d)** | stable `trackID` on ~49 MCP tools + ~44 RPC routes + the shared `automation_preset` / `apply_movement_plan` entry points; split parsing (strict when `trackID` present, byte-for-byte legacy otherwise); sentinels preserved | 10 family twins in `AddFxParityTest` + `McpCoverageTest.TrackIdAloneDrivesTheFxAndAutomationTools`; parity suites 80/80; `rpc_parity_map.inc` byte-unchanged (307/411/295/12) |
| **registry merge-on-save (§4b)** | `saveRegistry` merges the on-disk registry (in-memory wins for owned ids, unknown ids kept + adopted, `removedIds` sticky per process) | `FileLibraryTest.RegistryMergeOnSaveKeepsExternalEntries/KeepsExplicitRemovals/PrefersMemoryValues` — red/green + two mutation-proven; `FileLibraryTest.*` 43/43 |
| **engine: warmup watchdog** | the intentional Virus warmup no longer trips the 1s-hang minidump (was 330-670 MB per spawn, 4.5 GB in one session; the same code wrote them into `%TEMP%` on every Virus export); real hangs still dump (`warmupExpectedMs + 1 s`) | `PluginIsolation.VirusWarmupWritesNoHangDump` / `RealHangWritesHangDump`; `PluginIsolation.*` 51/51; dump byte-counts before/after |
| **engine: device init** | the doomed 2-in/2-out open is skipped when the device type has no capture endpoint (memoized per process); unchanged when inputs exist | `AudioEngineReadFacadeTest.ShouldRequestInputsCoversBothBranches` / `CapturelessDeviceTypeOpensOutputOnlyAndEngineIsUsable` / `ForcedCapturelessOpensOutputOnly` |
| **test infra** | harness self-isolation (per-pid TMP/TEMP, user-data root + read-mirror of the real caches, isolated QSettings INI) → 12 documented environmental failures gone (`VerifyPart.*` 9-red → 13/13); shard runner: unique-failure counting, incomplete-shard detection, **one `--gtest_filter` per process** (the multi-arg version silently ran only its last chunk — `-Shards 2` reported 5.5 min "green" while executing ~330/2008), intended-vs-executed coverage; shared-engine fixture (`Commands` 39.5 s → ~1 s; +4 suites 369 → 49 ms/test) | `run-tests-sharded.ps1` runs; `ExportVolumeBypass.*`; `FileLibraryTest` 40/40 under 4 concurrent shards; fixture green under `--gtest_repeat=2 --gtest_shuffle` |
| **test defects fixed** | `HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900` test-side UAF (declaration order: `QTcpSocket` outlived the captured `QEventLoop` → `~QTcpSocket` → `loop.quit()` on a destroyed loop) — **the cause of the intermittent shard-death class**; `apply_preset` fake id `OsirusFake.clap` paid the 12 s name-based warmup + a 97 MB minidump per run (→ `VavraFake.clap`, 26.9 s → 2.6 s) | 25/25 repeat; both later full runs completed every shard |
| **§4c PsyDub** | breakdown tail: the melody died at ~beat 623 while the section runs to 640; one held tonic F4 (vel 80) at beat 632 rings into the drop edge | beats 636-640 −46.0 → **−16.5 dBFS**; drop windows within 0.003 dB; full `PsyDubFiveMinutes` gates green (finalPeak 0.899, 307.3 s) |
| **docs** | §4a–§4e resolution notes, B2b shipped notes (both handoffs), trap #4 → FIXED, testing-mcp flake catalog (incl. the UAF + the intermittent CLAP bake-bed dropout), build-and-testing (harness isolation, runner correctness, sccache/label traps, stale fast-tier figure), README v0.39.0 highlights + changelog, lesson 32 | diffs in-tree |

## 3. Contracts worth remembering

- **B2b split parsing**: strict resolution ONLY when the stable key is present;
  otherwise byte-for-byte legacy (historical positional spellings incl. the
  `trackIndex`/`trackId` dual-reads, historical defaults like `automation_preset`'s
  −1 track, historical validation precedence and error texts). That is how an
  argument contract can be extended without moving a single pre-existing pin —
  and the 3 parity regressions the first cut caused are exactly what happens
  when legacy acceptance is dropped.
- **Disagreement text is spelling-preserving** per surface (`trackId X and
  trackID Y disagree` on tools, `trackIndex …` on the routes that spell
  `trackIndex`) — the keys name the surface's own positional spelling.
- **Automation owns the parameter offline** (lesson 32): an enabled paramID-1
  `Volume` lane overrides `setTrackVolume`/`IDs::volume` every block in the
  offline render — audit isolation must use mute.
- **One `--gtest_filter` per process**; a runner that passes several gets only
  the last one honoured (gtest flag overwrite) — hence the intended-vs-executed
  coverage check in `run-tests-sharded.ps1`.

## 4. Traps learned this session

1. **A test-side UAF can masquerade as infra flakiness.** The "one process dies
   per full-suite run" class was a declaration-order UAF in one HTTP test; the
   debugger's first-chance break on the *failing test's* body found it in one
   try. Check the faulting stack before blaming the runner/OS.
2. **A green summary can hide 72% of the suite** (the multi-`--gtest_filter`
   bug). Always verify intended-vs-executed counts, not just pass/fail.
3. **Fake plugin ids are not free**: a name containing `osirus`/`ostirus`/`virus`
   trips the child's name-based warmup (12 s) *and* its hang watchdog's
   minidump (330-670 MB) even when the plugin does not exist.
4. **Low-IL toolchain + Medium-IL stale `.obj` = C1083**; and a sccache daemon
   started with a denied `%TEMP%` fails every compile ("failed to write
   temporary file"). Remedy: `sccache --stop-server` then build with
   `TMP`/`TEMP` pointed at a workspace scratch dir. Builds from a normal
   (medium-integrity) prompt avoid the whole class.
5. **Iterate music through windowed renders, freeze last** (handoff §4f's
   lesson, confirmed): the §4c tail fix converged in one iteration because the
   debug path rendered 112 beats instead of the 5-minute deliverable.

## 5. Housekeeping (non-blocking)

- `build/SoundTouch.dll.locked-20260925` — delete once the live
  `HDAW_headless_mcp.exe` exits (it holds the mapping).
- Scratch (all gitignored): `.tmp_tests/`, `.tmp_build/`, `.tmp_build_scratch/`,
  `.tmp_feynman/` (an unrelated repo clone kept for a follow-up question).
- Pre-existing ambient files left uncommitted on purpose (unknown ownership):
  `.gitignore`, `docs/skills/*`, `timbre-lib/psy_sample_selection.tsv`,
  `.render_log`, `dsh-build-fast.bat`.
- OPEN from the campaign: none. `McpJobs.AnalyzeTuningWaitFalsePollMatchesSynchronousResult`
  is timing-sensitive under heavy load (passes solo and in the full run) — keep
  it visible if it ever recurs; and one intermittent CLAP bake-bed dropout is
  catalogued in `docs/testing-mcp.md` (root cause unknown, re-render the window
  before blaming a mix change).
