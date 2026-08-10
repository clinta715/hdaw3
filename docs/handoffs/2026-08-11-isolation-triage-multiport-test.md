# HANDOFF: PluginIsolation triage (#3) + multi-port width gtest (#4)

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, MSVC/VS generator, JUCE 8 via CMake FetchContent (`build\_deps\juce-src`).
> **MANDATORY:** before ANY code change, load the `hdaw-guard` skill (AGENTS.md) — plan-first, success gates, subagent execution, lesson-15 stale-binary discipline.

## Mission (remaining work)

1. **#3 — Triage the 4 pre-existing `PluginIsolation.*` failures** to a clean suite. Follow `docs/plans/2026-08-09-triage-pluginisolation-failures.md` (Tasks 1-4 + success gates): run the suite 3×, categorize each failure (real bug / env / flake / dead path), then fix or `DISABLED_` with a documented reason + re-enable condition. Suite must run green (only intentionally-`DISABLED_` tests absent).
2. **#4 — Multi-port width gtest**: add a sentinel probe plugin with **two stereo output ports** (4 channels — mirroring NodalRed2x's bus layout) and a `PluginIsolation` test proving the child PREPAREs 4 channels and the parent reports them. This closes the gap that the real-plugin matrix is the only multi-port coverage today.

## CURRENT STATE — verified (2026-08-10, after `a5871dd`)

- **Full suite: 637/641.** Exactly 4 failures, all `PluginIsolation.*`:
  `SpawnWithBadPluginExits` (`tests/integration/proxy/isolation_integration_test.cpp:43`),
  `CheckAllChildrenFiresCallback` (:89), `CrashDetectionViaSelfExit` (:413),
  `PerSlotCrashCallback` (:1041). **Verified pre-existing**: reproduced on a clean
  tree (stash all changes → rebuild → identical 4 failures) — NOT caused by the
  export/matrix/async work.
- All 4 share one shape: spawn with a nonexistent plugin
  (`C:\nonexistent\fake.vst3`), sleep 1-1.5 s, expect the child to exit, then
  `mgr.checkAllChildren()` must fire the `setSlotCrashCallback` callback. They
  assert a pre-`e917c1f` contract (e917c1f = process-wide message pump fix,
  2026-08-08, which changed child-process startup).
- **Diagnostic asymmetry (investigate first):** `RemoveSlotCrashCallback` (:1068)
  uses the SAME spawn-bad-plugin pattern and asserts the callback does NOT fire
  — and it **passes**. So the question is whether callbacks fail to fire because
  the dead slot was deregistered (the `liveProxySlots` lifetime fix) vs.
  `checkAllChildren` no longer seeing dead children at all.
- **Multi-port fix landed** (`738a65c`): `ShmHeader` carries
  `pluginNumInputChannels`/`pluginNumOutputChannels` (child writes at load);
  `PluginProxySlot::prepareToPlay` refreshes the reported counts from the header;
  `PluginProxySlot::getReportedNumInputChannels()`/`getReportedNumOutputChannels()`
  exist (`src/proxy/PluginProxySlot.h:96-97`, `.cpp:668-672`).
  NodalRed2x (2 stereo ports = 4 out) renders `peak=0.247864` via the matrix.
  **No dedicated gtest covers the width** — only the real-plugin matrix
  (`McpServer.DiagnosticClapExportMatrix`, ~85 s, requires the CLAPs installed).

## Item 4 — multi-port sentinel test (design sketch, verify details while implementing)

- **New `MultiPortProbeProcessor`** in `src/proxy/host/PluginHost.cpp` (anonymous
  namespace, next to `TransportProbeProcessor` :381 and `BlockSizeProbeProcessor` :192):
  - BusesProperties with **two stereo outputs** ("Out AB", "Out CD") — the
    NodalRed2x layout; `n2x` declares 2 stereo outs and unconditionally writes
    4 channels, so the host must SUM port widths, not read main-port-only.
    Zero inputs (instruments are 0-in/N-out in CLAP).
  - `processBlock`: clear; write a `ProbePayload`-style struct into channel 0 AND
    a distinct marker value into channel 2 — a 2-channel-prepared child would
    never touch channel 2 (regression probe: the pitch-up/resize bug class and
    the main-port-only width bug both fail this).
  - `fillInPluginDescription`: `d.fileOrIdentifier = "__multiport__"`; register
    in `loadPlugin()` where the other sentinels are selected (:1498 `__blocksize__`,
    :1515 `__transportprobe__`, :1521 `__throwprepare__`).
- **Test** in `tests/integration/proxy/isolation_integration_test.cpp`, modeled on
  `PluginIsolation.TransportClockHandoff` (:537) and its helpers
  (`pushBlockAndReadProbe`, :525-534; `TestPlayHead` :484):
  - `mgr.spawnPluginHost("__multiport__", <port>)`, wait alive, `PluginProxySlot slot(mgr, <port>, "TestPlugin")`, `slot.prepareToPlay(44100.0, 512)`.
  - `EXPECT_EQ(slot.getReportedNumOutputChannels(), 4)` — the parent-side regression
    target (pre-`738a65c` code reports 2).
  - Push a block, read the probe back through the shm ring; assert the channel-0
    payload fields AND the channel-2 marker are present.
  - Also assert the child's `hdr->pluginNumOutputChannels == 4` directly (the
    header is the contract the parent refreshes from).
- **Gate:** the test must FAIL on the pre-`738a65c` host behavior and PASS now.

## Item 3 — triage procedure (key facts, full detail in the plan)

- Plan: `docs/plans/2026-08-09-triage-pluginisolation-failures.md` — Tasks 1-4,
  success gates at :62-68. Use `subagent-driven-development` or
  `executing-plans` (plan-first; hdaw-guard).
- Task 1: run the suite 3× (`--gtest_repeat=3` or 3 runs with
  `--gtest_output=xml:runN.xml`) — separate consistent-fail from flake.
- Task 2: per test, read the body + the production path; categorize:
  - **3a real bug** → fix production (if it's the respawn/migrate UAF family,
    fix under `docs/plans/2026-08-09-fix-respawn-migrate-uaf.md` / crash-recovery,
    don't fix twice — plan note :79);
  - **3b env/artifact** → fix build wiring, no `GTEST_SKIP` masking;
  - **3c flake** → deterministic bounded polls, **no `sleep_for` loosening**;
  - **3d dead path** → `DISABLED_<Name>` + comment with reason + re-enable
    condition (precedent: `ShowEditor`, the old `DiagnosticClapExportMatrix`).
- Task 4: `PluginIsolation.*` green 3×; keep the run logs (the red baseline).
- Suspect first (hypothesis, verify): the `liveProxySlots` deregistration from
  the lifetime fix vs. `checkAllChildren` iteration — plus anything e917c1f
  changed in `PluginHost::main`/`ProxyProcessManager::spawnPluginHost` startup
  (README the diff since e917c1f for those two files).
- HARDEN, don't hide: the suite must be green BECAUSE each test is right, not
  because assertions were weakened.

## BUILD OPERATIONAL NOTES (learned the hard way — READ THIS)

- **The bash-tool default timeout is 120 s; a Debug rebuild takes minutes. ALWAYS
  pass a big timeout (e.g. 3600000 ms).** A killed build orphans cl.exe/MSBuild
  processes that lock source files — edits then fail with "Busy: FileSystem.writeFile".
- Before building: `taskkill /IM cl.exe /F; taskkill /IM MSBuild.exe /F;
  taskkill /IM link.exe /F; taskkill /IM mspdbsrv.exe /F` (ignore errors).
- **Stale-binary trap (AGENTS.md lesson 15):** after editing a test/host file,
  force `(Get-Item <file>).LastWriteTime = Get-Date` before building; if a
  change "doesn't take," verify the BINARY (timestamps / behavior probe), not
  the source.
- Do NOT run `build\Release\HDAW.exe` (stale). Debug exes are current.
- The matrix (`McpServer.DiagnosticClapExportMatrix`) drives real CLAPs in
  `C:\Program Files\Common Files\CLAP` and takes ~85 s — not CI-friendly; the
  sentinel tests ARE the deterministic coverage.

## Verification commands

```powershell
taskkill /IM cl.exe /F 2>$null; taskkill /IM MSBuild.exe /F 2>$null   # clear orphans first
cmake --build build --config Debug                                     # ~3-6 min
& .\build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.* --gtest_repeat=3
& .\build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.TransportClockHandoff  # pattern for #4
& .\build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*        # green after triage
& .\build\Debug\hdaw_tests.exe                                        # full suite ~6.5 min (expect 641/641 or N-<disabled>)
```

## Context: recent commits (all stable)

- `a5871dd` mcp: async export_audio (notifications/exportComplete)
- `3f689c2` test: FX audio-input sweep — kKnownSilent emptied (ShinRonin/Gneiss/Retrospect are effects)
- `738a65c` fix: multi-port CLAP support + lifecycle thread-contract (NodalRed2x, Odin2 render)
- `e917c1f` fix: silent WAV exports — process-wide JUCE message pump
- `4b55d2c` control-thread plugin exception containment (`__throwprepare__` sentinel)
- `627c958` transport playhead forward to isolated children (shm snapshot + ChildPlayHead)

## Key residual knowledge

- The 4 failing tests were verified **pre-existing on a clean tree** (2026-08-10)
  — do not attribute them to the export/matrix/async work.
- `RemoveSlotCrashCallback` passing while the 4 fail is the sharpest lead: the
  crash-callback path fires for one spawn pattern (or fails to) — check
  `checkAllChildren` vs. slot deregistration/`liveProxySlots` first.
- Sentinel probes (`__transportprobe__`, `__throwprepare__`, `__blocksize__`)
  are deterministic, real-plugin-free coverage at the exact proxy/host seam —
  the right level for the multi-port width regression test.
- The multi-port channel-count contract: host sums ALL output ports
  (`CLAPPluginInstance::buildBuses`); child writes the sum into the shm header
  at load; parent refreshes at PREPARE and (in `TrackFXSlot::prepare`) allocates
  a wider workspace + downmixes. Any new width change must preserve that chain.
- hdaw-guard is mandatory before code changes; run plans through subagents;
  success gates before "done".

Related docs: `docs/plans/2026-08-09-triage-pluginisolation-failures.md`,
`docs/plans/2026-08-09-fix-respawn-migrate-uaf.md`,
`docs/plans/2026-08-10-fx-audio-input-sweep-kknownsilent.md`,
`docs/plans/2026-08-10-clap-lifecycle-thread-contract.md`, AGENTS.md lessons 11-16.
