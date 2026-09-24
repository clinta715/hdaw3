# Handoff: what remains after the mixer-return / automation / key work (2026-09-23)

Context for the next session. Everything listed here was verified against the tree today;
"documented" means a repo file already records it, "reported only" means it exists only in
conversation and still needs writing down.

## Status after the 2026-09-23 completion session

Every numbered item below was taken up in the 2026-09-23 completion session. This section is the
outcome ledger for that session; the section that follows is the original (pre-work) text and is
kept as history. Where the two disagree, this section wins.

1. ✅ **shipped** — options (b) + gate. The shared predicate `PluginManager::isShadowFxEdition`
   filters `*FX` CLAP editions out of `list_plugins` (`kind:effect` AND `kind:all`) and out of
   `PluginManager::getEffectPlugins`; `add_fx` rejects them — and any unresolvable id — on BOTH
   surfaces through the new `src/common/FxPluginIdCheck.h` (RPC gate at the `FrontendRouter`
   dispatch intercept, i.e. `project.addFxSlot`). The MCP-only `add_track_with_fx` is gated the
   same way. Guide updated. Tests: `ShadowFxEditionPredicate.*`, `AddFxParityTest.*`. Option (a)
   (make these editions process audio) remains an open, documented cross-repo limitation.
2. ✅ **shipped** — `Threshold -6.0 / Ratio 2.0` in BOTH tables (`src/engine/TrackFXSlot.h` +
   `src/common/BusFxDefs.h`), pinned equal by `BusFxParam.DefTableMatchesTrackFxDefs`. This is the
   measured non-destructive remedy.
3. ✅ **shipped** — new `Damping` param (index 5) in `src/engine/InternalDelay.h`: a one-pole
   lowpass INSIDE the feedback path only, hard-bypassed at 0 (the default, so existing impulse
   tests stay bit-exact); state sized in `prepare()`, coefficient re-derived in `setParam`. The
   `BusFxDefs` delay table and the track surface derive the param automatically. Test:
   `TrackFxDelay.DampingDarkensOnlyTheFeedbackRepeats`.
4. ✅ **shipped** — send levels are automatable: `paramID = 2000 + sendIndex`, decode order
   3000 → 2000 → 1000 → 100 in `Track.cpp`; encoder in `ReadModelImpl::getAutomatableParams`.
   Tests: `AutomationSendBusPids.*`.
5. ✅ **shipped** — bus FX params are automatable: `paramID = 3000 + busID*8 + paramIndex`.
   Audio-thread delivery via new `FxBusProcessor::setAutomationValue` (atomic + dirty flag),
   consumed in `processBlock` under `dspStateLock.tryEnter()`; control path uses Gate-13
   tryEnter/dirty-on-skip. Send/bus handles are registered in the rebuild path (via
   `RoutingManager` + the new `Track.h` API).
6. ✅ **shipped** — the legacy send arg mismatch is gone: the RPC arg was renamed to `trackId` on
   the three setters AND on `read.getTrackSends` (MCP unchanged); `MixerStrip.tsx` + tests
   migrated. Twin tests `BusSendRpcTest.LegacySendRoutesRejectTrackIndexOnBothSurfaces` plus an
   extended `RouteKeysMirrorToolPropertyNames`. Trap retired. The same rename was applied on RPC
   for `removeTrack` / `moveTrack` / `duplicateTrack` (⇒ `trackId`).
7. ✅ **shipped (design A)** — `AudioEngineCommands_Helpers.h`: removal/move index maps + ONE-walk
   remap of folder `parentId`/`childIds` + `SONG_PLAN` `cellTrack`; structured
   `{ok, removed, shifted[]}` payloads byte-mirrored MCP/RPC for `removeTrack` / `moveTrack` /
   `removeSend`; MCP `remove_track` routed through the shared command (undo byte-identical);
   `fillOneCell` sentinel guard now errors instead of filling the wrong track; and `removeSend`
   remaps surviving automation lanes (`2000+sendIndex`) and DELETES the removed send's own lane
   (no FX-slot precedent — deliberately the safe choice). Tests: `Commands.RemoveTrack*`,
   `Commands.MoveTrack*`, `McpCoverageTest.RemoveTrack*` / `MoveTrack*`,
   `AutomationSendBusPids.RemoveSend*`, `BusSendRpcTest.RemoveSend*`.
   ⏳ **deferred** — design B (additive stable `trackID`/`sendID` wire ids, ~15-18 files). Design A
   made it easier: the shift payload is where a stable-id echo would attach.
8. ✅ **shipped** — root cause found: Qt's default **15 s keep-alive** aborted the connection
   during a long synchronous rebuild (`lastActiveTimer` restarts only on request READ).
   `TransportHttp::start` now sets a **900 s** keep-alive. Test:
   `HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900`. Traps `bus-response-dropped` /
   `rebuild-commands-drop-response` retired with the lesson retained.
9. ✅ **shipped** — fixed at the single source (`ReadModelImpl::getFxSlots`):
   `TrackFXSlot::paramCount()` now reports internal defs size / live instance count (in-process OR
   isolated proxy `GET_PARAM_COUNT` — the 6939-param case) / 0 while unloaded. Test:
   `FxSurface.ParamCountReportsInternalDefsNotTreeChildren`; trap entry added.
10. ✅ **shipped** — optional `soloOnly` (+ a `mixMeasured` honesty echo) added on MCP + RPC; it
    skips the mix render (halves cost + avoids plugin warmup). Tests:
    `VerifyPart.SoloOnlySkipsMixRenderHonestly`,
    `McpCoverageTest.VerifyPartSoloOnlyMatchesRpcTwin`. Also fixed in passing: test hygiene around
    `EngineSettingsStartMcpHttp` (no other change there).
11. ✅ **shipped** — optional `expectBackbeat` (default TRUE = bit-for-bit behavior) on both
    surfaces; skipping keeps the gate truthfully `true` and adds
    `dropChecks.backbeatChecked:false`. The 4 path-JSON `gate` entries were deliberately NOT
    edited (still valid). Tests:
    `SongStructureAudit.DubOneDropFailsDefaultPassesWithoutBackbeatExpectation`,
    `McpCoverageTest.AuditSongStructureExpectBackbeatFalseMatchesRpcTwin`.
12. ✅ **shipped (documented)** — new `Engine launch: locks and LNK1104` section in
    `docs/build-and-testing.md`.
13. ✅ **shipped** — `McpServer.HttpRoundTrip` now uses an OS-assigned ephemeral port, with one
    production line: `TransportHttp::start` refreshes `port_ = serverPort()` after a successful
    listen; `HttpTransport.StartStopLifecycle` likewise.
    ⏳ **remaining** — `McpServer.EngineSettingsStartMcpHttp` still uses a fixed 18766; fixing it
    needs production config plumbing (`setMcpHttpConfig` rejects port 0).
14. ✅ **shipped** — `RespawnPath.RealPathPassesThrough` is now platform-gated (Windows assert
    under `_WIN32`, POSIX under `#else`); production behavior confirmed contract-correct
    (refuse-to-spawn on a foreign-platform path); note updated in `docs/testing-mcp.md`.
15. ✅ **shipped** — trap `no-bus-read-tool` retired (RESOLVED: `list_buses` shipped).
16. ✅ **walked** — both branches proven against a live engine over MCP stdio and recorded as ledger entry `walk_minimal_3track` (`branch_stats` updated; `never_walked` is now empty): movement-plan → `measured_ok` (audit gate `attentionRequiredIds == []`, 3/3 covered); rides node walked as a node — arc-pass + return-ride `measured_ok` (tone_verity centroid 264→284; send A/B `rmsDb` −3.59; 64-point macro lane on paramID 2000), staging-pass `proven_with_caveat` (movement-plan's default Volume lanes claim fader authority — the documented interaction).
17. ✅ **shipped** — trap `no-sends` retired (RESOLVED: buses/sends are creatable + listable AND
    send levels are automatable as of this batch).

### New findings (2026-09-23 session)

- **Qt keyword macros mangle identifiers.** `qtmetamacros.h:43-44` defines `slots`/`signals`
  (also `emit`/`foreach`). Symptom: `error C2513: 'auto': no variable declared before '='` plus
  misleading gtest noise. Documented in `docs/pitfalls-juce.md`.
- **Render-suite bake starvation + self-inflicted link failure.** In a heavy 563-test run, 14
  render tests failed with `export failed: Render graph bake timed out after 15000ms`, and ALL
  passed in isolation. Separately, running TWO `build-fast.bat` invocations at once produced
  `LNK1104`/`LNK4076`. Both documented in `docs/testing-mcp.md` / `docs/build-and-testing.md`.
- **Disconnected-RDP deviceless environment.** The audio route can be absent even though
  `Win32_SoundDevice` reports healthy hardware. Live-graph suites then fail with
  `(track)/(rm) == nullptr` in 40-70 ms instead of ~1900 ms, including suites untouched by any
  change (`InternalFx`, `AudioPoolDedup`, `MasterGain`, `AudioEngineReadFacadeTest`). Documented in
  `docs/build-and-testing.md`. **Consequence: the 2026-09-23 full-suite run is
  environment-limited and its device-dependent failures are NOT regressions.**
- **Parity-debt inventory (reported, NOT fixed).** 13 `setTrack*` RPC routes still take
  `trackIndex` vs MCP `set_track`'s `trackId`; `moveTrackIntoFolder`/`OutOfFolder` have NO MCP
  tool; 6 RPC-only track setters; `add_track_with_fx` has no RPC route (ledger `unresolved`);
  `remove_track` has `dryRun`/`force` while `project.removeTrack` has neither; `move_track`
  returns `ok` vs RPC `Null`.
- **`get_track_sends` has TWO hand-built JSON shapers** (MCP inline vs RPC `toJson(SendSnapshot)`)
  held equal only by a test — candidate for the `src/common` shared-shaper pattern (the documented
  drift class).
- **LFO `targetParamID` (MODULATION_LIST) shares the pid space** and could durably encode
  `2000+sendIndex`; `set_lfo_param` advertises only 1/2/3/100+ targets — worth an inventory check
  in the design-B follow-up.
- **`duplicateTrack` copies `parentId`/`childIds` verbatim** (pre-existing): duplicating a folder
  clones `childIds`; duplicating a child leaves a dangling `parentId` claim.
- **The batch is UNCOMMITTED (67 files: 64 modified, 3 new)** and **graphify indexes committed
  state only** — refresh the graph after committing (`python -m graphify update . --force`, then
  `graphify explain <newSymbol>`).
- **The Electron frontend is DEPRECATED (2026-09-23)** — no build/test gates for it; recorded in
  `AGENTS.md`, `docs/build-and-testing.md`, `docs/testing-mcp.md`. The engine-side JSON-RPC surface
  + the parity ratchet remain live.

## What landed (so it is NOT open)

Seven commits, tree clean at handoff:

| Commit | Delivers |
| --- | --- |
| `70ab519` | bus/send **creation** (`createBus`/`createSend`, MCP + RPC twins) |
| `45de9cf` | session findings: the graphify refresh gotcha, test-infra traps |
| `4c51ec9` | the corrected return-ride recipe (send levels are not automatable) |
| `3eed79e` | bus FX params + `list_buses` + **the shared delay DSP** (real Feedback/Mix/SyncToTempo) |
| `ab78da0` | **`set_bus_target`** (re-parent) + the two-phase bus rebuild it exposed |
| `584983a` | the stale test baseline + the `aether_dub_returns` ledger entry |
| `382f4eb` | **`export_audio` can no longer silently succeed** + **`key_check`** |

Also fixed in passing: two *pre-existing* engine defects (an unprepared
`FxBusProcessor::processBlock` corrupting memory; `getSampleRate()` being 0 so rebuilds never
re-prepared freshly added nodes), a latent routing mis-wire (a bus whose parent sat later in
`BUS_LIST` silently fell back to master), and dead `RoutingManager::addBus` removal.

## Open — needs your approval first (stability rule: FX/plugin/DSP contract)

1. ✅ **shipped** — **`*FX` CLAP editions silently silence a track.** A qualified id (`CLAP-VavraFX-…`) loads,
   reports `pluginFormat` and params, and kills the audio (proven by bypass A/B: `0.0595 RMS
   audible=1` → `0/0`). A *bare* id (`"VavraFX"`) leaves an inert slot with `pluginFormat: ""`
   and **no error**. Fix is either making them process audio or dropping them from `kind:effect`
   so no agent can pick one. Reported + partly written up; **not** in the path trees.
2. ✅ **shipped** — **The compressor default is destructive**: `Threshold -20 dB / Ratio 4.0` on a hot synth
   clamped a bassline to 1/6 of its level (the pad with no compressor measured 3.6× louder at the
   same pitches). Mentioned in `docs/paths/dub_electro.json` and the ledger; no fix proposed.
3. ✅ **shipped** — **No filter *inside* the delay's feedback loop.** The new `filter` bus darkens the whole
   return statically; the classic dub move (repeats getting progressively darker) still needs a
   filter in the loop. This is the one musical gap left from the returns work.

## Open — contract/surface gaps (documented, unfixed)

4. ✅ **shipped** — **Send levels are not automatable.** `setSendLevel` is reachable only from the command and the
   graph build; the automatable paramID space is `1/2/3 / ≥100 track FX / ≥1000 MIDI FX`. A
   per-phrase throw must still come from a per-track delay slot. Trap:
   `send-levels-not-automatable`. Likely fix: a send paramID range (e.g. `2000 + sendIndex`,
   mirroring the `≥1000` convention).
5. ✅ **shipped** — **Bus FX params are not automatable either** — bus `param_N` has no paramID, so no lane can
   ride a bus's delay time or reverb size. Explicit non-goal of slice C; the param-index space was
   deliberately left open for it.
6. ✅ **shipped** — **Legacy send routes disagree across surfaces**: `setTrackSendLevel/_Mode/_Bypassed` take
   `trackIndex` on RPC but `trackId` on MCP (a real argument-name contract break; the new routes
   use `trackId` on both). Trap: `legacy-send-arg-mismatch`.
7. ✅ **shipped (design A; design B ⏳ deferred)** — **Positional track and send ids.** `remove_track` shifts ids above it, so held references go
   stale; `removeSend` shifts sends. Documented (trap `one-lane-per-param`'s neighbourhood + the
   returned id-shift note); stable ids remain unfixed.

## Open — latent engine / tool issues

8. ✅ **shipped** — **The dropped HTTP response on rebuild-triggering commands.** `add_bus`, `add_send` and
   `set_bus_target` intermittently lose their response while **completing the work** (observed
   across three commands, including once *not* reproducing — so it is timing-dependent, not
   deterministic). Traps: `rebuild-commands-drop-response`, `bus-response-dropped`. Root cause
   (rebuild running on the HTTP handler thread) **not investigated**.
9. ✅ **shipped** — **`list_fx` reports `paramCount: 0` for a plugin that exposes 6939 params.** Reported only —
   **not written into any repo doc**; needs an entry (it makes `paramCount` useless as a "did the
   slot load" signal).
10. ✅ **shipped** — **`verify_part` cost scales with plugin instances, not window length** (two attempts exceeded
    120 s on a one-CLAP project, killing the cheap window-probe recipe). Trap:
    `probe-cost-scales-with-plugins`. The fallback (render once at a guessed-safe master and
    rescale) works but wastes a render. **Shipped 2026-09-23: `verify_part {soloOnly:true}`**
    (RPC `composition.verifyPart {soloOnly:true}`) skips the full-mix render — the plugin-spawn
    cost is paid once, not twice — and the mix metrics then report `mixMeasured:false` instead
    of a fake pass. Default (`false`) is unchanged.
11. ✅ **shipped** — **`audit_song_structure`'s `allDropsHaveBackbeat` is psytrance-shaped** — a faithful dub
    one-drop fails it by design. Documented in both path trees + `2026-09-21-mcp-dogfood-composition.md`.
    **Addressed 2026-09-23: pass `expectBackbeat:false`** on either surface
    (`audit_song_structure` / `composition.auditSongStructure`) to skip the gate —
    `gates.allDropsHaveBackbeat` reports `true` with `dropChecks.backbeatChecked:false` and the
    drops stay listed informationally. Default (`true`) is unchanged.
12. ✅ **shipped** — **Engine-launch friction (process hazard).** A manual copy of the engine to `%TEMP%` died with
    exit `0x7FFFFFFF` even with `PATH` set, so live tests launched from `build/` — which locks the
    exes and causes `LNK1104` on the next build (hit twice today). The launcher's own
    `mcp-launch.bat` copy-and-verify path is the correct route and should be used instead of
    re-deriving it.

## Open — test infrastructure (documented, unfixed)

13. ✅ **shipped (partial — ⏳ `EngineSettingsStartMcpHttp` still fixed-port)** — **`McpServer.HttpRoundTrip` binds a fixed port 18765** (`mcp_server_test.cpp:115,122`), so it
    fails whenever a live engine holds it — measured: 4 failures with an engine on the port, 2
    without. Should use an ephemeral port.
14. ✅ **shipped** — **`RespawnPath.RealPathPassesThrough` is deterministically red on Windows** — it asserts
    `resolveRespawnPath("/usr/lib/MyPlugin.clap")` round-trips unchanged, in
    `tests/unit/proxy/crash_recovery_test.cpp:655` (untouched since 2026-08-20). Independent of
    any change; either fix the expectation for Windows or gate it by platform.

## Open — stale doc to fix (one line)

15. ✅ **shipped** — **`no-bus-read-tool` is now FALSE.** The psydub trap still says no tool lists buses, but
    `list_buses` shipped in `3eed79e`. It is actively misleading (it tells the next agent the tool
    does not exist). Retire/rewrite it the way `export-dir-must-exist` was retired.

## Open — creative / path work

16. ⏳ **attempted** — outcome recorded in `docs/paths/ledger.json` (see `branch_stats`). **`movement-plan` is the only never-walked branch** in the ledger, and the new **`rides`**
    node's three options (`arc-pass`, `return-ride`, `staging-pass`) have never been walked *as a
    node* — only `arc-pass`'s mechanism was proven, and only `shared-return` is in
    `branch_stats.measured_ok`.
17. ✅ **shipped** — `docs/paths/dub_electro.json` has a trap named `no-sends` that predates this work and may
    need re-examining now that sends exist (unverified today).

## In flight at handoff

- **Full confirmation suite** → `build/full_suite3.log` (244 run / 0 failed when this was
  written; expected to end like the 09-23 baseline: 1 pre-existing `RespawnPath` failure).
- **Knowledge-graph refresh** — `KeyConflict` is absent from `graphify-out/graph.json` because the
  post-commit hook's rebuild is cache-driven (documented trap in `AGENTS.md`). Run
  `python -m graphify update . --force`, then `graphify explain KeyConflict` to confirm.

## How to re-verify this work

```
build/hdaw_tests.exe --gtest_filter=BusSetTarget.*:KeyConflict*:KeyCheck*:McpCoverageTest.ExportAudio*:BusFxParam.*:BusSendCreate.*:BusSendRpcTest.*:TrackFxDelay.*
```
(13 + the bus/filter/delay suites; last run 0 failures.) Parity ledger after any new tool:
`node tools/rpc_parity_map.mjs` — currently 307 tools / 396 methods / mapped 202.

---

# Follow-up session (2026-09-23, evening) — the six leftover items above, taken up

Scope: the six "New findings" items (design B, parity debt, `duplicateTrack` folder refs,
the `get_track_sends` shaper dedup, the LFO `targetParamID` pid space, the Gate-3
`makePeakFilter` tension), plus two user directives — *use the native Windows toolchain,
not WSL/MSYS* and *we do not build the frontend*.

## Shipped

| commit | what |
| --- | --- |
| `ae2e880` | **one move path**: `move_track` routes through `AudioEngineCommands::moveTrack`, so the splice, the range rule and the durable-ref remap cannot drift; `duplicateTrack` no longer clones `childIds`/`parentId` verbatim (the copy drops `childIds` and registers itself once in its folder's CSV, same undo unit); `TrackHeaders.tsx`'s double-decremented forward drop fixed |
| `508e6f0` | **`removeSend` parks LFO targets**: `MODULATION_LIST` is walked in the same undo unit (removed send's target → `-1`, inert but preserved; survivors decrement; nothing outside `2000..2999` touched), and the three docs that advertised the unreachable FM targets / a stale modulation claim are corrected |
| `e69362e` | **one shared send/fx-slot shaper** (`src/common/SendJson.h`) behind both surfaces — and the `list_fx` ↔ `read.getFxSlots` pair they revealed as *divergent* (not merely duplicated) is now one canonical vocabulary, `paramCount` on every slot |
| `b770b77` | **Gate 3**: every internal-FX EQ coefficient rebuild uses `ArrayCoefficients` + array assignment instead of the allocating `Coefficients::makePeakFilter` wrapper — the per-sample allocation an LFO on an internal EQ param used to cause is gone (bit-identical coefficients) |
| `ce56bc4` | **native-Windows tooling**: `time-sync.cmd` opt-in (no `wsl.exe` on every build), `build-fast.bat` resolves cmake itself, `run-tests-parallel.sh` / `probe-c2c/engine.sh` / `timbre-lib/*.sh` retired for the PowerShell+Python twins, AGENTS.md + the guard skill state the native toolchain, and **no frontend build/test gate anywhere** (all three `hdaw-guard` copies) |
| `2717288` | **track parity closed**: `set_track` covers all 13 `project.setTrack*` properties (every write through the shared command), `move_track_into_folder` / `move_track_out_of_folder` + `project.addTrackWithFx` exist, `project.removeTrack` runs the MCP `dryRun`/`force` guard, one creation payload shape, and the 13 routes take `trackId` (+ `folderId`/`inputMonitor`/`midiChannel`) with the retired `trackIndex` rejected on both surfaces — **plus the parity gate itself fixed** (see finding 6) |
| `1484d93` | the unreachable FM pid branch (300..308) deleted; `FmModParamIDs` removed; the three stale comments corrected |
| `02b9a26` | the two vacuous `SongCells` tests now fill a real track (finding 8) |

## New findings (all fixed above unless noted)

1. **`move_track` forward moves were wrong on two counts.** The MCP tool re-inserted at the
   *un-decremented* index on a forward move, so it produced a different order than
   `project.moveTrack` **and** then ran the command's permutation on a tree that did not match
   it — a folder `childIds` entry could survive pointing at the wrong track. Untested: the
   existing assertions only moved backward.
2. **`list_fx` and `read.getFxSlots` were divergent**, not just duplicated (`slot`/`type` vs
   `slotIndex`/`fxType`, `pluginName` on one side only). One vocabulary now.
3. **`removeSend` did not remap LFO targets.** An LFO on `2000 + sendIndex` went stale after a
   splice and a re-created send inherited it (the lane fixup had covered lanes only).
4. **LFO targets `2000+`/`3000+` already worked and were unvalidated** — the UI offered them
   while the tool text said "1/2/3/100+"; and the documented FM targets 300..308 were
   *unreachable* (the `>=100` compound claims those pids first). Decode-order trap, now documented
   and de-coded.
5. **An LFO targeting an internal EQ param allocated on the audio thread per sample**
   (`Coefficients::makePeakFilter` is `*new Coefficients(...)`). The new bus consume inherited the
   convention; all four eq sites now use the non-allocating array form.
6. **The parity generator was lying about the project namespace.** It registered only the FIRST
   `dispatchX(` call per `method::X` branch, so once `FrontendRouter` intercepted
   `removeTrack`/`addTrackWithFx` *before* falling through to `dispatchProject`, regenerating
   relabelled every `project.*` route as `settings.*`; and methods resolved in the intercept chain
   were invisible (they read as "no route exists"). Fixed; the generator now also supports a
   `FANOUT` classification so a one-tool-many-routes capability (`set_track`) is stated instead of
   parked in the review queue. **Always eyeball a regenerated ledger: the ratchet only verifies
   that mapped targets exist.**
7. **Qt's `slots` macro bit a shared header.** Naming a `src/common/` shaper parameter `slots`
   erased the identifier in every Qt-including TU (build failure). `SendJson.h` documents it.
8. **Two `SongCells` tests were vacuous**: they filled cells wired to track 1 in a project that
   ships **zero** tracks, `addMidiClip` no-opped, and `fillOneCell` still reported success. The
   batch's new guard (never fill a cell whose track is missing) exposed them — and that guard is
   correct; the tests were fixed.
9. **`build-fast.bat` could not find cmake on a plain shell** (it bootstrapped MSVC but not cmake),
   and `time-sync.cmd` spawned `wsl.exe` + printed "WSL clock synced" on *every* build of a native
   box where no build input crosses a filesystem view.

## Still open

- **Design B1 shipped** (this session): every TRACK/SEND is stamped with a stable
  `trackID`/`sendID` at creation (tree-derived allocator — max existing + 1, never a counter, so an
  offline render model cannot clobber the id space), a `scanAndSyncTrackIDs()` backfill runs on load
  for files written before B1, `duplicateTrack` re-stamps the copy (a copy must not inherit its
  source's identity), and both surfaces echo the ids: `TrackSnapshot.trackID`,
  `SendSnapshot.sendID` → `read.getTrack`/snapshot + `SendJson.h` rows, `TrackJson.h` /
  `AddTrackWithFx.h` creation payloads. **`trackID`/`sendID` are identities; `trackId`/`sendIndex`
  stay positional addresses.**
  **B2** (resolvers accept id-or-index, so the shift payloads stop being load-bearing) and **B3**
  (durable refs — folder `parentId`/`childIds`, `SONG_PLAN` `cellTrack`, lane `2000+sendIndex`, LFO
  targets — migrate to ids) remain; B1 alone is what makes a held id survive a removal, which is the
  bug class behind findings 1/3.
- **The ledger's review queue is a CLASSIFICATION debt, not a reachability list.** 96 rows are
  `unresolved`; several are reachable but name-unmatched (`add_fx` → `project.addFxSlot`,
  `set_fx_bypass` → `project.setFxSlotBypassed`, …) and some are MCP-only by design
  (`get_project_summary` ≈ `read.snapshot`). `node tools/rpc_parity_map.mjs --show-unresolved`
  is the work list; classify from the router, never from the name.
- The `*FX` CLAP editions still cannot process audio (documented cross-repo limitation, item 1 of
  the earlier section) and `McpServer.EngineSettingsStartMcpHttp` still binds a fixed port.
- The full-suite re-baseline of this session's commits: run
  `powershell -File run-tests-sharded.ps1 -Shards 4`. Note the runner counts each `[  FAILED  ]`
  **line** and gtest prints every failure twice, so its "failed" total is ~2× the real count —
  read the `FAILED TESTS` list, and re-run those solo before blaming a change
  (`PluginIsolation.LargeStateRoundTripThroughProxy` is the historical solo-pass flake).
