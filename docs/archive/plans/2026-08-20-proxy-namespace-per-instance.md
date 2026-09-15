# Plan — Lesson-20 namespace gaps: per-instance unique prefix + held-name slot bump (2026-08-20)

Source: `docs/handoffs/2026-08-19-part-templates-shipped.md` §"#3 (RE-SCOPED)".
The per-PROCESS prefix (`<pid-hex>_`, `b9545d1`) already shipped; this plan
closes the remaining evidence-based gaps: in-suite collisions inside ONE
process, and the static `export_` domain shared by every offline render.

## Goal

Make every `ProxyProcessManager` instance own a **unique OS name namespace by
construction** (pipes/shm/state files), for both live and offline domains, and
add a bounded **slot-bump retry** in `spawnPluginHost` when a pipe/shm name is
detected as held at spawn time — eliminating the in-suite and cross-export
collision vectors of AGENTS.md lesson 20.

## Success Gates (all must pass to declare done)

- [ ] **Gate A** — new test asserts two `ProxyProcessManager` instances in one
      process get distinct prefixes and distinct built pipe/shm names; two
      `PluginManager` instances likewise; an offline copy's prefix carries the
      `export_` domain label AND a unique suffix.
- [ ] **Gate B** — orphan simulation: hold the manager's would-be slot-1 pipe
      name with a squatter `PipeServer`, spawn through the manager with the
      out-param → spawn succeeds on a BUMPED slot (child alive), no hang.
      Second case: hold the slot-1 shm name (manually created `ShmRegion`) →
      spawn bumps likewise.
- [ ] **Gate C** — FULL `build/Debug/hdaw_tests.exe` run (no filters): 0
      failures, including the five CrashRecovery tests
      (`AutoRespawnAfterCrash`, `RespawnDuringActiveProcessing`,
      `DestroyedProxyIsDeregistered`, `OfflinePluginDomainIsolatedFromLive`,
      `PluginIsolation.UniqueSlotIdPerInstance`) and
      `PluginIsolation.LiveDropDrainsStaleOutput`.
- [ ] **Gate 14 (cross-process protocol)** — names still round-trip through
      spawn args (`--pipe=`/`--shm=`); `src/proxy/host/main.cpp` untouched
      (child uses whatever names it is given — verified parent-only change).
      Gate B's spawned child reaching READY on the bumped name proves the
      round-trip.
- [ ] **Build gate** — `cmake --build build --config Debug` clean; test binary
      timestamp newer than the edit (lesson 15 stale-`.obj` trap).
- [ ] **Hygiene gate** — no raw `DBG` (use `HDAW_LOG`), no new files missing
      from `tests/CMakeLists.txt`, diff scan shows no audio-thread/processor
      state changes.

## Dependency Map (grep-verified; graph snapshot had no spawnPluginHost edges)

- **`spawnPluginHost` upstream:** `PluginManager::createPluginInstance`
  (`src/engine/PluginManager.cpp:631`), `PluginManager::respawnIsolatedSlot`
  (`:954`), ~35 direct test call sites in
  `tests/integration/proxy/isolation_integration_test.cpp` +
  `tests/unit/proxy/crash_recovery_test.cpp:283,308`. Signature stays
  source-compatible via a defaulted out-param → zero test churn.
- **`setProxyNamespacePrefix` upstream:** `AudioEngine.cpp:80` (REMOVE —
  ctor default supersedes it), `ExportManager.cpp:145` (KEEP — gains unique
  suffix automatically), `crash_recovery_test.cpp:463` (KEEP — assertions
  updated to read the real prefix).
- **`namePrefix` downstream consumers:** `makePipeName`/`makeShmName`
  (`ProxyProcessManager.cpp:330,334`) and `PluginProxySlot::stateFilePrefix`
  (`PluginProxySlot.cpp:35` → state files at `:763,:785,:860,:872`). All read
  the prefix; none assume a format — except
  `CrashRecovery.OfflinePluginDomainIsolatedFromLive` which hardcodes
  `hdaw_proxy_state_[export_]<slot>.bin` (`crash_recovery_test.cpp:490-492`)
  → update to use the new `PluginManager::getProxyNamespacePrefix()` getter.
- **Projections / SPSC / audio thread / ValueTree:** none touched. No
  processor state, no graph mutation, no plugin lifecycle calls.
- **God nodes:** none. **Community boundaries crossed:** proxy ↔ engine
  (existing seam, contract unchanged: parent builds names, child consumes
  them from its command line — `src/proxy/host/main.cpp:10-14`).

## Pitfall Gates Triggered

- **Gate 9 (ID namespace)** — the change itself. Addressed by per-instance
  unique prefixes + bump-on-collision.
- **Gate 14 (cross-process)** — pipe/shm names cross the process boundary via
  spawn args. Child is name-agnostic (verified). Keep names short:
  `\\.\pipe\hdaw_plugin_` (19) + prefix (~12) + slot ≤ ~40 chars « 256 limit.
- **Gate 4/15 (stale binaries)** — rebuild Debug, verify `hdaw_tests.exe`
  timestamp before trusting Gate C.
- Gates 1/3/6/10/12/13/16 — N/A (no rebuild-path state, no audio thread, no
  graph mutation, no DSP writes, no lifecycle calls).

## Design

1. **`proxy::ProxyProcessManager`** (`src/proxy/ProxyProcessManager.{h,cpp}`)
   - New `static std::string makeUniqueNamespacePrefix(const std::string& domainLabel)`
     → `domainLabel + <pid-hex> + "_" + <process-wide atomic counter> + "_"`.
   - Constructor sets `namePrefix = makeUniqueNamespacePrefix("")` — every
     instance is unique by default (fixes bare-`ProxyProcessManager` tests,
     direct-`PluginManager` tests, and every future manager).
   - `makePipeName`/`makeShmName` become **public const** (Gate A/B tests need
     to predict names).
   - `spawnPluginHost(const std::string&, uint32_t slotId, uint32_t* actualSlotId = nullptr)`:
     wrap the defensive-kill + pipe-create + shm-create section in a bounded
     retry loop (≤ 8 attempts). On pipe-create failure OR shm-create
     failure/collision: log via `HDAW_LOG` (include `GetLastError()`), stop
     any partially-created pipe, `++slotId`, retry. Always write the final
     slot to `*actualSlotId` when non-null. Defaulted param keeps all ~35
     existing call sites compiling unchanged.
   - Update the `setNamePrefix` doc comment: raw setter (escape hatch); the
     ctor default is already unique; `PluginManager::setProxyNamespacePrefix`
     is the domain-label API and always appends uniqueness.
2. **`proxy::PipeServer::start(DWORD* errorOut = nullptr)`**
   (`src/proxy/ProxyPipe.{h,cpp}`) — surface `GetLastError()` on failure so
   spawn can log the collision code.
3. **`proxy::ShmRegion::create`** (`src/proxy/ProxySharedMemory.cpp`) — after
   a successful `CreateFileMappingA`, treat `GetLastError() == ERROR_ALREADY_EXISTS`
   as FAILURE (close handle, return false): today a same-size existing region
   is silently OPENED and shared — worse than a hard failure.
4. **`HDAW::PluginManager`** (`src/engine/PluginManager.{h,cpp}`)
   - `setProxyNamespacePrefix(p)` → `proxyProcessMgr->setNamePrefix(ProxyProcessManager::makeUniqueNamespacePrefix(p.toStdString()))`.
     Callers declare the domain label; uniqueness is enforced by construction
     (no caller can regress to a static namespace).
   - New `juce::String getProxyNamespacePrefix() const` (forwards to
     `proxyProcessMgr->getNamePrefix()`; empty string when isolation compiled
     out).
   - `createPluginInstance` (`:627-639`) and `respawnIsolatedSlot` (`:952-954`)
     pass `&slotId` / `&newSlotId` to `spawnPluginHost` and use the
     (possibly bumped) value for everything downstream (PluginProxySlot ctor,
     crash callbacks, `liveProxySlots`, `registerSlotTrackIndex`).
5. **`AudioEngine.cpp:80`** — remove the explicit `%x_` call; the ctor
   default already carries pid + instance counter. Replace with a comment
   pointing at the new guarantee.
6. **`ExportManager.cpp:145`** — keep `setProxyNamespacePrefix("export_")`;
   it now yields `export_<pidhex>_<n>_` per offline copy (fixes the static
   `export_` domain across overlapping exports/processes). Refresh the
   surrounding comment.
7. **Tests**
   - `crash_recovery_test.cpp` `OfflinePluginDomainIsolatedFromLive`: rebuild
     the expected state-file names from
     `livePm.getProxyNamespacePrefix()` / `offlinePm->getProxyNamespacePrefix()`
     instead of the hardcoded `""` / `"export_"`.
   - NEW `tests/integration/proxy/namespace_collision_test.cpp` (register in
     `tests/CMakeLists.txt` next to `isolation_integration_test.cpp`):
     - Gate A: distinct prefixes/names across two managers; offline-copy
       prefix starts with `export_` and differs from the live manager's.
     - Gate B (pipe): squatter `PipeServer` on `mgr.makePipeName(1)` →
       `spawnPluginHost("__passthrough__", 1, &actual)` succeeds with
       `actual == 2`, child alive; cleanup.
     - Gate B (shm): squatter `ShmRegion::create(mgr.makeShmName(1), ...)` →
       spawn bumps; cleanup.
     - Record the observed `GetLastError()` code for the held-pipe case in
       the test comment (empirical confirmation of the collision mechanism).

## Out of scope

- `hdaw_plugin_scanner.exe` (own exe + pedal file, no shared namespace).
- Counter wraparound (uint32 process-wide counter never wraps in practice).
- AGENTS.md lesson text (historical record; the handoff note will record the
  closure of lesson 20's standing follow-up).

## Verification commands

```
cmake --build build --config Debug
build/Debug/hdaw_tests.exe --gtest_filter=ProxyNamespace.*   # Gates A+B
build/Debug/hdaw_tests.exe --gtest_filter=CrashRecovery.*:PluginIsolation.*
build/Debug/hdaw_tests.exe                                    # Gate C (FULL)
```
