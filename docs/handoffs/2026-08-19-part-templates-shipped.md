# Handoff — #2 part-templates/role-defaults SHIPPED; START #3 namespace gaps (2026-08-19, session 5)

## Purpose

This session completed agenda **#2 "Part templates + typed track presets"** —
the `role → defaults` map inside `addInstrumentPart`, exposed as a `role` param
via RPC + MCP, with **explicit params always win** semantics. Shipped exactly as
scoped in handoff #4's briefing: no new processor state, full `TRACK_TEMPLATE`
store deferred. **Next session: start with §Remaining agenda — #3 (Lesson-20
namespace guard) remains; its core prefix already shipped in `b9545d1`, so read
that section before planning.**

## What shipped (#2 — complete)

Plan: `docs/plans/2026-08-19-part-templates-role-defaults.md` (gates + evidence).

| File | Change |
|---|---|
| `src/common/ProjectCommands.h:330,364-365` | `enum InstrumentPartRoleBit : uint32_t` (9 bit flags: style, lowNote, highNote, density, noteDuration, minVelocity, maxVelocity, targetRms, allowGlobalScale) + additive `role` / `explicitMask` fields on `InstrumentPartParams` |
| `src/engine/AudioEngineCommands_Composition.cpp:459` | `addInstrumentPart` copies params, normalizes+validates `role` (case-insensitive; unknown → `"unknown role: "` error) and applies role defaults for fields whose explicit bit is UNSET, **before any mutation** (Gate 9); `"style or role required"` guard; file-local `kRoleDefaults[]` table (no new `.cpp`); single transaction + single `rebuildRoutingGraph()` preserved |
| `src/frontend/router/Router_Composition.cpp:276-307` | `style` → optional `optString`; new `role` optString; `-32602 "style required (or provide role)"` when both missing; 9 explicit bits from `o.contains(...)` |
| `src/mcp/McpTools_Project.cpp:1050,1071-1104` | `role` schema (string, engine validates — tolerates case); `required` shrinks `{trackName,style}` → `{trackName}`; handler reads role + sets 9 bits from `a.contains(...)` |
| Tests | `InstrumentPartRole.*` (5) in `tests/unit/engine/instrument_part_test.cpp`; `McpServer.AddInstrumentPartRole` / `AddInstrumentPartUnknownRole`; `FrontendServer.AddInstrumentPartRoleRpc` / `AddInstrumentPartMissingStyleAndRole` |

### Role defaults table (the contract — tune in future sessions, tests assert it)

| Field | Bass | Lead | Chords | Drums |
|-------|------|------|--------|-------|
| style | `BassLine` | `Lead` | `ChordStab` | `Euclidean` |
| lowNote | 36 | 60 | 48 | 36 |
| highNote | 48 | 76 | 72 | 60 |
| density | 10 | 6 | 5 | 12 |
| noteDuration | 0.5 | 0.25 | 2.0 | 0.25 |
| minVelocity | 70 | 70 | 60 | 90 |
| maxVelocity | 110 | 110 | 100 | 120 |
| targetRms | 0.126f (≈ -18 dB) | 0.0f | 0.0f | 0.0f |
| allowGlobalScale | true | false | false | false |

### Verified results

- `InstrumentPartRole.*` — **5/5 PASSED** (Bass==hand-configured element-by-element incl. gain fader/measuredRms; explicit-wins; unknown-role leaves project untouched; case-insensitive; empty-role legacy).
- `*AddInstrumentPart*` MCP + Frontend — **7/7 PASSED** (4 McpServer + 3 FrontendServer); neighbors `McpServer.*:FrontendServer.*` **49/49**.
- FULL suite `build/Debug/hdaw_tests.exe` — **966 tests / 178 suites: 958 passed, 0 failed, 8 skipped, exit 0** (8 skipped are env-guarded real-plugin tests, pre-existing).
- Diff scan (Gate 5): no new files, no CMake change, no raw `DBG`, no new processor/DSP state (Gate 1 N/A by construction), RPC+MCP share one command path.

## Remaining agenda — briefing for the next session

## #3 (RE-SCOPED) — Lesson-20 namespace guard: core ALREADY SHIPPED; remaining gaps are in-suite + export-domain

**⚠️ Do NOT re-implement the per-instance prefix.** It shipped in
`b9545d1` (2026-08-15): `AudioEngine::initialize` sets
`setProxyNamespacePrefix("<pid-hex>_")` (`AudioEngine.cpp:80`); the export
domain uses `"export_"` (`ExportManager.cpp:145`); plumbing is
`PluginManager::setProxyNamespacePrefix` → `ProxyProcessManager::setNamePrefix`
→ names `\\.\pipe\hdaw_plugin_<prefix><n>` / `hdaw_plugin_shm_<prefix><n>`
(`ProxyProcessManager.cpp:330-334`). At commit time 43/43 proxy tests passed.
Handoff #3's agenda item pre-dated awareness of this.

### What remains (evidence-based)

1. **In-suite collisions inside ONE test process.** The prefix is per-PROCESS
   (`hdaw_tests.exe` pid), but the slot counter restarts at 1 per
   PluginManager/ProxyProcessManager — so an orphaned child from an EARLIER
   test in the same run holds `<testpid>_1` and a later test's slot 1
   collides. This is the most likely vector for the
   `PluginIsolation.LiveDropDrainsStaleOutput` full-suite failure (passes
   alone) and the historical "known to fail five" CrashRecovery flakes when
   they occur in-suite. (Cross-engine collisions are already fixed.)
2. **Static `export_` prefix** — shared by every offline render of every
   engine; orphaned export children (or overlapping exports across processes)
   can still collide. Candidate: `export_<pid>_` or a per-render suffix.
3. **(Optional second line) held-name skip** — before spawn, probe the pipe
   name and bump the slot id if occupied. Cheap insurance; not required if
   1+2 land.

### Where to look

- `src/proxy/ProxyProcessManager.{h,cpp}` (spawn args at ~line 73, name
  builders at ~330, `setNamePrefix`), `src/proxy/host/main.cpp` (child parses
  `--slot=`/`--pipe=` — the child uses whatever name it is given, so prefix
  changes are parent-only).
- `src/engine/PluginManager.{h,cpp}` (`setProxyNamespacePrefix`, slot
  allocation), `src/engine/AudioEngine.cpp:80`, `src/engine/ExportManager.cpp:145`.
- Affected tests: `tests/integration/proxy/isolation_integration_test.cpp`,
  `tests/unit/proxy/crash_recovery_test.cpp` (already sets `"export_"` on an
  offline copy at line 463 — pattern to extend).

### Suggested approach + gates (finalize in the plan)

- Make the prefix per-PluginManager-instance (e.g. `<pid>_<instanceCounter>_`
  or a GUID suffix) for BOTH live and offline domains; keep it short (pipe
  name length limits).
- Gate A: two PluginManagers in one process get distinct namespaces (unit
  assert on the built pipe/shm names).
- Gate B: simulate an orphan — hold `\\.\pipe\hdaw_plugin_<prefix>1` from a
  test thread/dummy listener, then spawn through the manager and assert the
  slot either bumps or the spawn cleanly reports the collision (depending on
  chosen design).
- Gate C: the five CrashRecovery tests + `LiveDropDrainsStaleOutput` pass in
  the FULL suite (that is the real acceptance — run the whole
  `hdaw_tests.exe`, not filters).
- Gate 14 applies (cross-process protocol): name changes must round-trip
  through the spawn args; verify the child logs/uses the given name.
- Measure first if a flake reproduces: attribute `%TEMP%\hdaw_debug.log`
  lines to pid before concluding anything (handoff #3 operational context).

## Low priority (carried)

- **Beats-vs-seconds ergonomics** (old #4): `paintToProjectEnd` helper /
  uniform bars-beats acceptance.
- Composer follow-ups unlocked by #2 (optional, do NOT plan now):
  - **Drums role via `RhythmPatternBuilder`** — the plan deliberately deferred
    this; `role:"Drums"` currently uses PhraseGenerator `Euclidean` style, not
    the rhythm-DSL path. Decide in a future plan whether Drums should route to
    `generate_rhythm_pattern` (generative toolkit rule in AGENTS.md applies).
  - **fm_synth patch `param_N` role defaults** — deferred deliberately (don't
    ship guessed DX7 algorithms); would set slot-tree `param_N` props (they
    survive rebuild via `TrackFXSlot::loadParamsFromTree`; live-forwarding
    already exists at `AudioEngine.cpp:1076-1131`). If ever pursued: use
    `setFxSlotInternalParam` (stateLock-guarded, `Track.cpp:777`), not the
    listener path.
  - fm_synth caveat (handoff #3 finding #4): the default patch is DX7 "init"
    (all-99 EG), velocity-insensitive — role presets relying on velocity for
    dynamics won't get them; tests assert config, not timbre.

## Operational context (unchanged, still true)

All "Operational context a fresh session MUST know" items from handoff #4
remain valid: audio-device environmental failures, message pump, probe
hygiene + pid attribution, real-plugin env guards (NEVER
`HDAW_NO_PLUGIN_ISOLATION` for real plugins), beats-vs-seconds, 3 empty
default tracks, stale-binary traps, master-gain pinning in render tests.
New from this session: `role` defaults on `addInstrumentPart` are stable
(config-tested, not timbre-tested) — keep that distinction if tuning values.

## Where to look (this session's work)

- Plan + gates: `docs/plans/2026-08-19-part-templates-role-defaults.md`
- Engine: `src/engine/AudioEngineCommands_Composition.cpp:459` (role block),
  `src/common/ProjectCommands.h:330,364-365`
- RPC: `src/frontend/router/Router_Composition.cpp:276-307`
- MCP: `src/mcp/McpTools_Project.cpp:1050-1104`
- Tests: `InstrumentPartRole.*` (`tests/unit/engine/instrument_part_test.cpp`),
  `McpServer.AddInstrumentPartRole/UnknownRole`, `FrontendServer.AddInstrumentPartRoleRpc/MissingStyleAndRole`
- Learnings: AGENTS.md lessons 1–22, `docs/realtime-safety.md`

Per hdaw-guard: every code change gets a plan with success gates first,
dependency analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`, 9983 nodes), pitfall-gate scan, and subagent
dispatch with verification.