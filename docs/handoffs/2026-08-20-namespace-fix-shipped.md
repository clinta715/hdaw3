# Handoff — #3 proxy namespace fix SHIPPED; v0.23.2 pushed (2026-08-20, session 6)

## Purpose

This session completed agenda **#3 "Lesson-20 namespace gaps"** — the permanent
guard against pipe/shm/state-file namespace collisions in the proxy plugin
isolation system. Shipped as v0.23.2 alongside the prior session's #2 part
templates. **Next session: low-priority carried items (beats-vs-seconds,
drums role, fm_synth defaults) — see §Remaining agenda.**

## What shipped (#3 — complete)

Plan: `docs/plans/2026-08-20-proxy-namespace-per-instance.md` (gates + evidence).

| File | Change |
|------|--------|
| `src/proxy/ProxyProcessManager.h:68-111` | New `static makeUniqueNamespacePrefix(domainLabel)`, public `makePipeName`/`makeShmName` const, `spawnPluginHost` with `uint32_t* actualSlotId = nullptr` out-param |
| `src/proxy/ProxyProcessManager.cpp:9-20,33-100,193-202` | Ctor auto-prefix `<pidhex>_<counter>_`, 8-attempt bump retry on pipe/shm collision, `KillGraceful` now waits (`WaitForSingleObject`) for child termination |
| `src/proxy/ProxyPipe.h:16` / `ProxyPipe.cpp:12-28` | `start(DWORD* errorOut = nullptr)` surfaces `GetLastError()` |
| `src/proxy/ProxySharedMemory.cpp:8-32` | `ShmRegion::create` rejects `ERROR_ALREADY_EXISTS` (never silently shares a stale region) |
| `src/engine/PluginManager.h:51-55` | New `getProxyNamespacePrefix()` getter |
| `src/engine/PluginManager.cpp:120-136,642,964` | `setProxyNamespacePrefix` appends uniqueness via `makeUniqueNamespacePrefix`; `createPluginInstance`/`respawnIsolatedSlot` pass out-param |
| `src/engine/AudioEngine.cpp:80-82` | Removed explicit `%x_` prefix call (auto-unique by ctor) |
| `src/engine/ExportManager.cpp:130-140` | Kept `"export_"` domain label; comment updated (now `export_<pidhex>_<n>_` per copy) |
| `tests/unit/proxy/crash_recovery_test.cpp:488-496` | State-file assertions read live prefixes via getter |
| `tests/integration/proxy/namespace_collision_test.cpp` | **New** — 5 tests (Gate A: unique prefixes; Gate B: held-pipe bump, held-shm bump) |
| `tests/CMakeLists.txt:113` | Registered new test file |

### Empirical evidence (lesson-20 mechanism confirmed)
- Held-pipe error code: **ERROR_PIPE_BUSY (231)** — `CreateNamedPipeA` with `nMaxInstances=1` fails deterministically when another instance holds the name.
- Per-instance prefixes confirmed in-process: `83e0_7_` / `83e0_8_` (hex pid + counter).
- Latent race exposed: `KillGraceful` didn't wait → dying child's shm handle lingered → same-slot re-spawn hit `ERROR_ALREADY_EXISTS`. Fixed by adding `WaitForSingleObject(handle, 1000)`.

### Verified results
- `ProxyNamespace.*` — **5/5 PASSED** (unique prefixes, export domain, held-pipe bump, held-shm bump).
- `CrashRecovery.*:PluginIsolation.*` — **50/50 PASSED** (including five known-to-fail + `LiveDropDrainsStaleOutput`).
- FULL suite `build/Debug/hdaw_tests.exe` — **971 tests / 179 suites: 963 passed, 0 failed, 8 skipped, 6 disabled**.
- KillGraceful fix: 5/5 reruns of `RebuildReusesSameSlotWithoutCollision` PASSED (race closed).

## Remaining agenda — briefing for the next session

All items are **low priority** (carried from handoff #5). Do NOT plan now unless the user requests one.

### 1. Beats-vs-seconds ergonomics (old #4)
`paintToProjectEnd` helper / uniform bars-beats acceptance. Frontend speaks beats; clip ValueTree and processors speak seconds. Every boundary-crossing command must convert (lesson 1). An ergonomic helper would reduce this friction. See `docs/architecture.md` → "Time-unit convention".

### 2. Drums role via `RhythmPatternBuilder` (unlocked by #2)
`role:"Drums"` currently uses PhraseGenerator `Euclidean` style, not the rhythm-DSL path. Decide in a future plan whether Drums should route to `generate_rhythm_pattern` (generative toolkit rule in AGENTS.md applies). The plan for #2 deliberately deferred this.

### 3. fm_synth patch `param_N` role defaults (unlocked by #2)
Deferred deliberately — don't ship guessed DX7 algorithms. Would set slot-tree `param_N` props (they survive rebuild via `TrackFXSlot::loadParamsFromTree`; live-forwarding already exists at `AudioEngine.cpp:1076-1131`). If ever pursued: use `setFxSlotInternalParam` (stateLock-guarded, `Track.cpp:777`), not the listener path. Caveat: the default patch is DX7 "init" (all-99 EG), velocity-insensitive — role presets relying on velocity for dynamics won't get them.

## Operational context (unchanged, still true)

All "Operational context a fresh session MUST know" items from handoff #5
remain valid: audio-device environmental failures, message pump, probe
hygiene + pid attribution, real-plugin env guards (NEVER
`HDAW_NO_PLUGIN_ISOLATION` for real plugins), beats-vs-seconds, 3 empty
default tracks, stale-binary traps, master-gain pinning in render tests.
New from this session: `role` defaults on `addInstrumentPart` are stable
(config-tested, not timbre-tested) — keep that distinction if tuning values.
Proxy namespace is now auto-unique per instance (lesson 20 guard complete).

## Where to look (this session's work)

- Plan + gates: `docs/plans/2026-08-20-proxy-namespace-per-instance.md`
- Proxy: `src/proxy/ProxyProcessManager.{h,cpp}`, `ProxyPipe.{h,cpp}`, `ProxySharedMemory.cpp`
- Engine: `src/engine/PluginManager.{h,cpp}`, `AudioEngine.cpp:80`, `ExportManager.cpp:145`
- Tests: `tests/integration/proxy/namespace_collision_test.cpp`, `tests/unit/proxy/crash_recovery_test.cpp:488-496`
- Lessons: AGENTS.md lessons 1–20 (lesson 20 updated with permanent guard), `docs/realtime-safety.md`

Per hdaw-guard: every code change gets a plan with success gates first,
dependency analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`), pitfall-gate scan, and subagent
dispatch with verification.
