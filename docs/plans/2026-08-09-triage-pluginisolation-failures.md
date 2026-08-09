# Triage the pre-existing `PluginIsolation.*` test failures

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify the ~4 `PluginIsolation.*` tests that fail "with and without the message pump" (per the silent-export post-mortem, `docs/postmortem-silent-clap-export.md` §7), categorize each as real-bug vs. environment-vs. flaky, and either fix it or formally `DISABLED_` it with a documented reason — so the suite runs **clean** (only intentionally-disabled tests absent from a green run).

**Tech Stack:** C++17, JUCE 8, GTest, plugin process-isolation, `hdaw_plugin_host.exe`, `PassthroughTest.vst3`

---

## Context

The `PluginIsolation` suite is 37 tests in `tests/integration/proxy/isolation_integration_test.cpp`. The post-mortem recorded "4 pre-existing `PluginIsolation.*` failures (fail with AND without the pump; unrelated older WIP, A/B-verified)." A suite with permanently-red tests is exactly what masks real regressions later — the project values a clean suite (AGENTS.md → Testing). This plan is a **triage**, not a feature: the first task is discovery, and the per-test remediation depends on what discovery finds.

### Harness facts (from the test file)
- Tests spawn the real `hdaw_plugin_host.exe` via `ProxyProcessManager::getHostExePath()` (`HostExePathResolves` proves it resolves).
- "DLL" tests need `tests/test-plugin/.../PassthroughTest.vst3` built and `GTEST_SKIP()` if absent (`findBuiltTestPlugin()`, line 17). A missing test plugin is an **environment** failure, not a code bug.
- Many tests use `std::this_thread::sleep_for` polling loops (e.g. `DLLLoadAndAudioRoundTrip:531-539`) → candidates for **flakiness** under load.
- Candidate "older WIP" clusters: the `DLL*` group (`DLLLoadAndAudioRoundTrip`, `DLLParameterEnumeration`, `DLLStateSaveRestore`, `DLLGracefulShutdown`) and `CrashIsolationDuringProcessBlock`.

---

## Dependency Map

- **Blast radius:** tests only (no production code unless a real bug is found).
- **Upstream:** the suite depends on `hdaw_plugin_host.exe` (built), `PassthroughTest.vst3` (built under `tests/test-plugin`), and a running JUCE message pump (`MessagePumpThread`, started in `test_main.cpp`).
- **Downstream:** none (tests are leaves). But a *real* bug surfaced here could indicate a proxy-host defect affecting crash recovery / state round-trip.
- **Projections affected:** none unless a real bug is found.

---

## Tasks

### Task 1: Discovery — capture the exact failing set
- [ ] Confirm `hdaw_plugin_host.exe` and `PassthroughTest.vst3` are built (rebuild if stale: `cmake --build build --config Debug`).
- [ ] Run the suite in isolation, 3×, capturing per-test pass/fail + any abort/crash output:
  ```
  build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.* --gtest_output=xml:isolation_run1.xml
  ```
  (repeat for run2, run3). Run 3× because several tests are timing-based — separate *consistent* failures from *flaky* ones.
- [ ] Record the failing test names and categorize each into: **consistent-fail**, **flaky** (passes on some runs), or **skip** (GTEST_SKIP due to missing artifacts).

### Task 2: Diagnose each consistent failure
For each consistently-failing test, read the test body and the production code it exercises; capture the failure mode (assertion text / crash stack). File one short note per test:
- [ ] Is it a real proxy-host / isolation bug? → promote to a fix task (Task 3a).
- [ ] Is it an environment/missing-artifact issue? → fix the build wiring (Task 3b).
- [ ] Is it a timing/flake (sleep-too-short, racy poll)? → harden the test (Task 3c).
- [ ] Is it testing dead/superseded behavior? → `DISABLED_` with a documented reason (Task 3d).

### Task 3: Remediate per-test (chosen from Task 2's diagnosis)
- **3a — Real bug:** root-cause and fix the production path (open a follow-up if it's large; cross-link to the migrate-UAF plan if it's the same crash-recovery family).
- **3b — Environment:** ensure the artifact is built in the default `cmake --build` (add the test-plugin target as a build dependency if missing); do not leave a `GTEST_SKIP` masking a build gap.
- **3c — Flake:** replace fixed `sleep_for` polls with deterministic waits (poll on the SHM counters with a bounded deadline; reuse the `toPass`-style pattern). Tighten deadlines, do not loosen assertions.
- **3d — Dead path:** rename to `DISABLED_<Name>` and add a comment block (like the `ShowEditor`/`DiagnosticClapExportMatrix` precedent) stating the reason and re-enable condition.

### Task 4: Confirm a clean suite
- [ ] Full `PluginIsolation.*` run is green (excluding only intentionally-`DISABLED_` tests, which gtest omits by default).
- [ ] 3× repeat stays green (no flake reintroduced).

---

## Success Gates (all must pass to declare done)

- [ ] The exact failing set is recorded (test names + failure modes) in this plan or a linked note.
- [ ] Every previously-failing `PluginIsolation.*` test is either **fixed (green 3×)** or **`DISABLED_` with a documented reason + re-enable condition**.
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=PluginIsolation.*` exits 0 (green) on a clean run.
- [ ] No new `sleep_for`-loosening or assertion-weakening introduced (harden, don't hide).
- [ ] `cmake --build build --config Debug` still succeeds.

## Verification commands

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.* --gtest_repeat=3
build\Debug\hdaw_tests.exe
```

## Notes
- If a failing test turns out to be the **same** respawn/migrate UAF covered by the `2026-08-09-fix-respawn-migrate-uaf.md` plan, fix it there and mark it resolved here by cross-reference (do not fix the same bug twice).
- Keep the discovery output (the run logs / XML) — it becomes the "this used to be red" evidence if a future change regresses it.
