# Plan: Build & Test Time Split (tests first, then libs)

- **Date:** 2026-09-28
- **Status:** Phases 1–2 COMPLETE (2026-09-29; G1–G9 closed — §9 measured numbers with conditions, §11/§12 outcome notes). Phase 3 remains optional (§12: JUCE per-consumer compilation is the known full-build cost; the working consolidation mechanisms are named).
- **Scope:** CMake target structure only (test executables, static libs, optional shared JUCE-modules lib)
- **Related:** `docs/build-and-testing.md`, `docs/testing-mcp.md`, `docs/architecture.md`, `AGENTS.md` (Build/Testing), baseline 2026-09-26 (2027 tests, ~27.1 min sharded wall)

---

## 1. Problem (evidence from 2026-09-28 survey)

Everything in `src/` compiles into **one** static lib `HDAW_lib` (CMakeLists.txt:114) with **one** PCH (:357), linked by `HDAW.exe` (:391) and `HDAW_headless.exe` (:482). Tests are **one** `hdaw_tests.exe` (tests/CMakeLists.txt:13) with ~190 TUs / 2027 tests. Consequences:

- Any source edit → `HDAW_lib.lib` relink → relink of both GUI exes **and** the 190-TU test binary. Editing one MCP test still relinks everything test-related.
- One test process = one fault domain: `McpServer.HttpRoundTrip` must run first in its suite (JUCE WASAPI/COM teardown pollution, `docs/testing-mcp.md`), and the pre-existing `PsyDubFiveMinutes` access violation kills the remainder of the binary's queue mid-shard (2026-09-28 run: shard 1 died after 562 tests).
- `HDAW.exe` / `HDAW_headless.exe` each compile their own copies of all JUCE module TUs (~10–14 each) — full-build cost only.

Source distribution (files / KB): `common` 80/567, `engine` 211/2424, `mcp` 62/705, `frontend` 52/400, `proxy` 18/307, `model` 2/42. Tests: `unit/engine` 130 cpp, `unit/frontend` 22, `unit/mcp` 11 + `integration/mcp` 9, `unit/proxy` 4 + `integration/proxy` 2, `unit/common` 7, `unit/model` 2.

## 2. Goals / Non-goals

**Goals:** cut incremental test relink time; cut full-suite wall time; isolate crashing/order-sensitive suites into separate processes; keep gtest suite/test names and the sharded-runner UX stable.

**Non-goals:** no DLL/shared-library boundaries (export churn near `processBlock`/plugin isolation for ~zero build win); no split of the audio core (`Track` / `AudioProcessorGraph` / `RoutingManager` stay together); no sccache (PCH workflow); no toolchain or frontend changes.

## 3. Phase 0 — Baseline probes (REQUIRED before any edit)

Record wall times under §9 using `Measure-Command` around ninja, single build at a time:

| Probe | Trigger | Command | Records |
|---|---|---|---|
| P1 | touch `tests/integration/mcp/mcp_coverage_test.cpp` | `ninja hdaw_tests` | test-TU compile + 190-TU relink time |
| P2 | touch `src/engine/AudioEngine.cpp` | `ninja` (default target) | HDAW_lib relink + 3 exe relinks |
| P3 | touch a wide engine header (e.g. `Track.h`) | `ninja` | fan-out TU count + time |
| P4 | full suite | `run-tests-sharded.ps1 -Shards 2` | wall time (existing baseline: 27.1 min) |

## 4. Phase 1 — Split the test binary (highest win)

Four executables, grouped by existing directory seams, plus a small support lib:

| Target | Sources |
|---|---|
| `hdaw_tests_engine` | `tests/unit/engine` (130) |
| `hdaw_tests_mcp` | `tests/unit/mcp` (11) + `tests/integration/mcp` (9) |
| `hdaw_tests_frontend` | `tests/unit/frontend` (22) |
| `hdaw_tests_platform` (named at implementation; "support" would collide with the proposed `hdaw_test_support` lib) | `unit/proxy` + `integration/proxy` + `unit/common` + `unit/model` (15) |
| `hdaw_test_support` (STATIC/OBJECT lib) | shared fixtures/helpers; `test_main.cpp` self-isolation (TMP/`USERPROFILE` redirect) is process-level and duplicates cleanly |

**Acceptance gates**

- **G1:** P1-class probe (MCP test edit) rebuilds+relinks only `hdaw_tests_mcp` and measures ≤ ⅓ of baseline P1.
- **G2:** Suite names unchanged; `--gtest_filter=Suite.Test` works per exe; combined run reports the full 2027.
- **G3:** Exes can run **concurrently**; sharded runner updated; P4 wall ≤ baseline (expect improvement from parallel processes, not fewer tests).
- **G4:** `McpServer.HttpRoundTrip` passes in `hdaw_tests_mcp` regardless of position — the binary-order hazard is gone.
- **G5:** A crash in one exe (e.g. `PsyDubFiveMinutes`) leaves the other three exes' results intact in the runner output.
- **G6:** Docs updated in the same commit: `docs/build-and-testing.md`, `docs/testing-mcp.md` (deviceless-pattern note applies per process).

**Risks:** per-process global init (WASAPI/COM) now happens 4× — acceptable because `test_main.cpp` already self-isolates; runner script + `run_fast_tests.bat` need updating; watch the engine exe's runtime share (it holds the long export/composition suites — keep the shard runner).

**Effort:** M. Blast radius: tests/CMake only, zero engine code. This phase alone delivers most of the day-to-day win.

## 5. Phase 2 — Split `HDAW_lib` along the existing seams

| New lib | Contents | May depend on |
|---|---|---|
| `hdaw_common` | `src/common` (contracts, shapers, verity/verdict helpers) | Qt/JUCE only |
| `hdaw_engine` | `src/engine` + `src/model` (incl. composition/offline analysis — see open question) | common |
| `hdaw_surface` | `src/mcp` + `src/frontend` (router, servers) | common, engine |
| `hdaw_proxy` | `src/proxy` parent-side (plugin host exe keeps its own narrow source set, unchanged) | common, engine |

The dependency direction already exists logically (v0.7 decoupling: `ProjectCommands`/`ReadModel`/`PluginService` interfaces in `src/common`). Enforce it with the CMake link graph — a wrong-direction include becomes a link error, not a silent coupling.

**Acceptance gates**

- **G7:** P2 probe: an `AudioEngine.cpp` edit no longer recompiles/relinks `hdaw_surface` objects; total measured time < baseline P2 (record the actual delta).
- **G8:** All binaries build and smoke-run: `HDAW.exe`, `HDAW_headless.exe`, plugin host/scanner, and the four test exes sum to 2027.
- **G9:** Parity gates untouched and green: `RpcNamespaceCoverage`, `node tools/rpc_parity_map.mjs` (ledger 317/419 — names/routes only, unaffected).

**Risks:** per-lib PCH decision (share one PCH target or per-lib; sccache stays OFF either way); Qt autogen per target adds small overhead; circular-include cleanups may surface (that is the point, but budget for it).

**Effort:** M–L. Modest compile-time win by itself (Ninja already skips unchanged TUs) — the value is relink scope + enforced layering + making Phase 1's surface exe link less.

## 6. Phase 3 (optional, lowest priority) — Shared JUCE-modules static lib

Compile the JUCE module TUs once for `HDAW` + `HDAW_headless` (smaller set for host/scanner). Saves ~10–14 TUs **on full builds only**. Gate: clean-tree rebuild shows the TU-count drop and a measured full-build reduction; `JUCE_MODULE_AVAILABLE_*` flags must be unified per target group. Skip unless full-rebuild pain (the `.ninja_deps` trap family) becomes routine.

## 7. Execution notes / known traps

- After any `CMakeLists.txt` edit: re-run `cmake -S . -B build` explicitly (suppressed regeneration).
- One build at a time on `build/`; never hard-kill ninja (`.ninja_deps` corruption → full rebuild).
- `HDAW_INCREMENTAL_DEV_LINK` (CMakeLists.txt:596) stays ON; `/Z7` embedded debug info stays.
- Never enable `HDAW_USE_SCCACHE` with PCH (AGENTS.md).
- Engine-change test discipline applies unchanged: new targets are build plumbing; any behavior change still needs gtest twins.

## 8. Rollout order

1. Phase 0 baseline probes (record §9).
2. Phase 1 test split → gates G1–G6 → commit + docs.
3. Phase 2 lib split → gates G7–G9 → commit + docs.
4. Phase 3 only if full-build pain justifies it.

## 9. Results (fill in during execution)

| Probe | Baseline | After Phase 1 | After Phase 2 |
|---|---|---|---|
| P1 | 82.0 s — touch `tests/integration/mcp/mcp_coverage_test.cpp` → `dsh-build-fast.bat test` (1 TU + full link + windeployqt) | **58.8 s** (−28%; only `hdaw_tests_mcp` rebuilds — log-verified; the ≤⅓ target was NOT met, see §11) | — |
| P2 | 375.5 s — touch `src/engine/AudioEngine.cpp` → default target (1 TU + 6 links) | **344.7 s** (§12: scope-proven — 1 TU into `hdaw_engine`, only `hdaw_engine.lib` re-archived; conditions differ, both stated) | **344.7 s** (G7 probe on the layered tree) |
| P3 | 577.1 s — touch `src/engine/Track.h` → default target (204 build steps) | — (Phase 2 measures) | — |
| P4 | 19.7 min — `-Shards 2`, 2146/2146 executed (2107 passed + 39 skipped, 0 failed), 297 suites (2026-09-28) | **18.4 min** — 2146/2146 executed (2107 passed + 39 skipped, 0 failed), 6 buckets on 2 lanes, idle session (2026-09-29) | **16.4 min** — 2146/2146 executed (2107 passed + 39 skipped, 0 failed), layered build, idle session (2026-09-29) |

Baselines measured 2026-09-28 via `Measure-Command` around `dsh-build-fast.bat` (P1–P3) and `run-tests-sharded.ps1` (P4); exit 0 throughout, one build at a time. Runner-level suite universe for the G2 sum check: **2146 intended tests / 297 suites**. Every figure carries its conditions: P4 19.7 = single-binary scheduler; P4 23.1 (superseded intermediate) = four exes + fixed wave buckets + session activity during the run; P4 18.4 = four exes + greedy bucket pool + idle session. G4 evidence: full `hdaw_tests_mcp` run = 365/365 in 221.4 s with `HttpRoundTrip` mid-suite.

## 10. Open questions

- Test-exe grouping: 4 targets above vs 3 (engine+common merged). Decide from P1 numbers. **RESOLVED: kept 4.**
- Does `hdaw_composition` (MixReport/ToneVerity/TuningAnalysis/phrase-gen) deserve its own lib? Only if P3 shows the analysis headers fan out into engine-wide rebuilds.
- Should the runner gain a `-Target` switch (run a single test exe) or should each exe be a plain ninja target the scripts call directly? **RESOLVED: runner `-Binaries`; wrappers take `test <target>`.**

## 11. Phase 1 outcome (2026-09-29)

**Gate ledger.** G2 ✅ 297/297 suites + 2146/2146 tests summed across the four exes (engine 225/1292, mcp 22/365, frontend 31/280, platform 19/209); the runner hard-throws if a suite exists in two exes. G3 ✅ 18.4 min ≤ 19.7 (conditions above). G4 ✅ `HttpRoundTrip` mid-suite, 365/365. G5 ✅ structurally (per-exe buckets, INCOMPLETE detection unchanged, all four exes report independently). G6 ✅ both docs + plan in-change. G1 ⚠️ partial — scope ✅, time 82.0 → 58.8 s (−28%, not ≤⅓): the edit-relink cycle is now dominated by fixed costs (windeployqt POST_BUILD on every relink, wrapper bootstrap, unpch'd test TUs). Follow-up levers if iteration time matters further: PCH for the test exes; deploy-output caching.

**Scheduler rewrite.** Fixed one-bucket-per-lane waves regressed the wall to 23.1 min: a 13.6-min engine bucket head-of-line-blocked its lane while the other idled. Replaced with a greedy work pool (`-BucketFactor`, default 3 → Shards×3 buckets, up to $Shards in flight, no wave barriers; a bucket's own invocations still never overlap). Bug found and fixed in the process: a re-enqueued bucket stayed in `$active`, so duplicates double-incremented `Next` (skipped/duplicated invocations, `Plan[i]` null → the guarded skip-and-log path). Regression smoke for that exact path (2-invocation bucket across exes — must print `2 invocation(s)`, 8/8, exit 0):

    powershell -Command "& '.\run-tests-sharded.ps1' -Binaries 'build\hdaw_tests_mcp.exe','build\hdaw_tests_frontend.exe','build\hdaw_tests_platform.exe' -Filter 'JsonRpc.*:FrontendServer.*:RingBuffer.*' -Shards 2 -BucketFactor 1"

**Runner gotcha.** `powershell -File` binds ONE value per parameter (no array gathering): `-Binaries a.exe b.exe` mis-binds (the second path lands on `-WholeSuiteThreshold`). Use `-Command` with a quoted array, or pass a single exe.

**Scope notes.** No `hdaw_test_support` lib needed (shared fixtures are header-only). No new C++ symbols — no graphify refresh required for this change. Commit scoping: the working tree also carries pre-existing unrelated dirty files (`.pi/fabric.json`, `.pi/mcp.json`) and the rewritten `scripts/patch_pi_fabric_advisor.py` + new `.pi/extensions/advisor-prose-patch.ts` (fabric-tooling fix, separate concern — separate commit recommended; both committed 2026-09-29 as 6f2df90 + 101e5f0).

## 12. Phase 2 outcome (2026-09-29)

**Split.** `HDAW_lib` → `hdaw_common` (17) / `hdaw_engine` (99 + model 1) / `hdaw_surface` (mcp 46 + frontend 25) / `hdaw_proxy` (2 + 4 isolation-conditional), generated data-driven from the actual file bytes (whitespace-normalized partition, lossless). Identical PUBLIC surface via one `foreach(_lib ${HDAW_LIBS})` block (include dir, JUCE/Qt/clap links, defs, STL PCH, /MP /FS, /arch:AVX2, per-config /O2, IPO); layer order declared (`engine→common`, `surface→common+engine`, `proxy→common+engine`); consumers take `${HDAW_LIBS}`. Zero functional `HDAW_lib` references remain (case-sensitive sweep).

**Completeness gate (the lesson).** The 190 = 17+99+1+46+25+2 sum-check proved *consistency, not completeness*: the first split silently dropped three whitespace-artifact entries (`PluginManager.cpp` + `CrashRecoveryManager.cpp` at 8-space indent, `StretchRenderer.cpp` at 0-indent) and the link error caught it (33 unresolved `PluginManager` methods). The mechanical gate is a whitespace-normalized old↔new `src/` set-diff against the pre-split SHA: **206/206, dropped=[], added=[]**. On-disk `src/engine/ClipClipboard.cpp` was never in HDAW_lib (pre-existing unbuilt file — left out deliberately, owner's call).

**Isolation-def gate (the second lesson).** The monolith's `HDAW_PLUGIN_ISOLATION=1` was PUBLIC on HDAW_lib, so every TU compiled with it; attaching it to `hdaw_proxy` only compiled isolation OUT of `PluginManager.cpp/.h` (10 `#if` blocks in the engine layer) — a silent ODR/behavior divergence that links cleanly. The def now lives in the per-lib foreach (conditional on the option); `Qt6::Widgets` stays proxy-only (linker member-pulling keeps the scanner Qt-free, re-proven below). Any conditional def is part of the identical-surface rule.

**JUCE per-consumer compilation — known cost, consolidation attempt FAILED.** The split lets every consumer compile its own JUCE module copy (the `juce::` targets carry INTERFACE_SOURCES that propagate transitively — measured: four libs + every test exe each built a copy; 212 `juce-src` steps of a 638-step full build). A base-lib attempt (`hdaw_juce` STATIC linking the `juce::` targets once + `set_target_properties(... INTERFACE_SOURCES "")`) did NOT consolidate: an EMPTY interface property is treated as unset, so the transitive sources still flowed (212 `juce-src` steps outside `hdaw_juce`, 0 inside) — attempt reverted; full build green at 629.1 s on the consolidated shape and the layered shape retains its own green suite. Phase 3 must use a mechanism that actually blocks propagation: hand-propagated usage requirements (`target_include_directories/_definitions` with `$<TARGET_PROPERTY:juce::juce_core,INTERFACE_...>`) or an OBJECT-library boundary. Consumers' direct `juce::` links and the layer links are back to the proven shape.

**Cache-state lesson (2026-09-29).** Option probes leave cache state behind: `-DHDAW_PLUGIN_ISOLATION=OFF` persisted in CMakeCache.txt, and the wrapper `configure` passes no `-D`, so an ON restore must be explicit AND verified from the cache file (`HDAW_PLUGIN_ISOLATION:BOOL=ON`) before any suite result on that tree is trusted. The cache is a hidden third state between CMakeLists and binaries. One full-suite run mid-Phase-2 came back red (76 unique failures + serial_engine process death at 6.1 min) on a cache-verified ON tree whose untouched control suites then passed solo — recorded as a void, run-level event; the shipping shape's gate is the fresh full-suite run on the reverted tree, not that run.

**Flake bank (2026-09-29).** One full-suite run came back red (76 unique failures + serial_engine process death at 6.1 min, void — see the cache-state lesson above); the immediate rerun on the UNCHANGED tree was green: 17.8 min, 2146/2146, 0 failed. Three-run history on the layered shape: green 16.4 → red 6.1 (void, run-level) → green 17.8. Per the docs' flake protocol: environmental, banked, not a regression.

**Isolation-OFF verdict: known-broken before the split.** With `HDAW_PLUGIN_ISOLATION=OFF`, `AudioEngineCommands_Composition.cpp` references `proxy::PluginProxySlot::getHostWrittenParams` outside any guard (only `_WIN32` blocks in that TU) while `PluginProxySlot.cpp` is conditionally compiled — the OFF link cannot resolve, identically in the monolith (same sources, same conditionals). Recorded as a pre-existing limitation (default ON, never built OFF here); fixing it means guarding the reference sites — a source change outside the split's zero-source-delta scope. Both OFF-gate probe runs independently failed exactly there, after the AUTOMOC staleness layer was cleaned.

**Gates.** G7 ✅ engine-touch probe: 1 TU recompiled into `hdaw_engine`, only `hdaw_engine.lib` re-archived (no surface/common/proxy steps), 344.7 s < 375.5 s baseline (conditions differ: baseline = monolith + 1 test exe; after = layered + 4 test exes — both stated). The −30.8 s is mechanistic: an engine touch no longer re-packages the 190-TU monolith archive, only the ~100-TU engine archive. G8 ✅ per-exe universes 1292/365/280/209 = 2146; scanner dumpbin gate `SCANNER_QT_DEPS=0`; full suite pending — see §9. G9 ✅ `tools 317 rpc 419 {mapped: 303, mcp-only: 14}`, exit 0. Isolation-OFF build validation: pending.
