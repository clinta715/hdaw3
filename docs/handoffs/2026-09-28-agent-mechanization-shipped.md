# Handoff — agent mechanization shipped (S1–S7 + follow-ups)

**Date:** 2026-09-28 · **Continues:**
[`2026-09-28-v0.39.2-backlog-closeout.md`](2026-09-28-v0.39.2-backlog-closeout.md)
(whose §5 OPEN item 1 was the mechanization proposal; this session implements it)
· **Plan:**
[`docs/plans/2026-09-28-agent-mechanization.md`](../plans/2026-09-28-agent-mechanization.md)
— marked IMPLEMENTED by this close-out.
**Scope:** every slice of the mechanization plan + the review-driven follow-ups
(S1, S1b, S2, S3, S4, S5, S6, S7 and S2b/S3b/S3d/S4b/S6-followups/S7b) landed
end-to-end: ten new MCP tools with parity twins, a beat-window archaeology
shaper, edit batches as one undo unit, unit-tagged time windows on both
surfaces, render→measure→compare tools, and the `whoami`/`--project` lifecycle
bootstrap.

## 1. State

- **Parity ledger:** 317 tools / 419 RPC methods, **mapped 303 / mcp-only 14 /
  unresolved 0** (`node tools/rpc_parity_map.mjs`). The prior close-out's
  `307 / 411 / 295 / 12 / 0` line is superseded.
- **Composition:** no engine/DSP path was restructured; every change is a new
  `src/common/` shaper plus an MCP tool and/or RPC route calling it. `mix_report`
  / `mix_verdict` / `mix_diff` bytes are unchanged by S4 (it *composes* their
  inputs, it does not rewrite their payload).
- **Validation:** the authoritative full sharded run was executed by the
  orchestrator; its result is the ONE `Full sharded run (final tree):` line in
  §5. Per-slice focused gates are named in §3.

## 2. Shipped — tool + route inventory

**New MCP tools (10):** `query_notes`, `query_clips`, `set_notes_gain`,
`set_clips_edit`, `tool_help`, `whoami`, `verify_window`, `render_and_verify`,
`begin_batch`, `end_batch`.

**New RPC routes (8):** `read.queryNotes`, `read.queryClips`,
`project.setNotesGain`, `project.setClipsEdit`, `project.beginBatch`,
`project.endBatch`, `composition.verifyWindow`, `export.renderAndVerify`.

Ledger classification: `tool_help` and `whoami` are deliberate **MCP_ONLY**
rows (the RPC surface has no tool registry, and cannot report its own
transport) — the `engine_info` precedent. The other eight are exact mapped
twins. `begin_batch`/`end_batch` were *un-aliased* this session: they used to
map to the raw `project.beginTransaction`/`endTransaction`, which overstated
equivalence; they now call the same `ProjectCommands::beginBatch`/`endBatch`
entry points the routes do (§3 S7).

## 3. Per-slice summary

**S1 / S1a — central unit annotation.** `src/common/ToolUnits.h` (header-only)
classifies the unit of every numeric MCP schema property with no per-tool edits:
an explicit override table, name/token suffix rules, and a scalar
(dimensionless) classification, each carrying its live instance list. Injected
once by `McpServer::registerTool`. Gated by `tests/unit/mcp/tool_registry_test.cpp`
GATES A–E: coverage (`?unclassified` never survives), examples-per-tool,
no-clobber, registry-count, and GATE E's hand-audited `kExpectedUnits` ledger
that re-derives every unit-bearing field from the live `tools/list`.

**S1b — `tool_help {name}`.** `registerToolHelpTool`
(`src/mcp/McpTools_Engine.cpp`) returns the exact `tools/list` entry — both read
the SAME stored `McpToolDef` and the same `toolunits::buildToolExample`, so the
payload cannot drift (`ToolRegistry.ToolHelpReturnsTheExactToolsListEntry`).
MCP_ONLY, like `engine_info`; unknown name refused in-band with one shared text.

**S2 — beat-window archaeology.** `src/common/ProjectQuery.{h,cpp}` is the ONE
implementation behind `query_notes`/`query_clips` and their twins
`read.queryNotes`/`read.queryClips` (`Router_Read.cpp` shares
`HDAW::readBeatWindowArgs`). The window is an INTERVAL OVERLAP in PROJECT
(absolute) beats, with the returned span clamped to its clip and a `truncated`
flag when the clip cut the tail. Follow-up S2b hardened the same shaper.

**S3 — batch mutations.** `ProjectCommands::setNotesGain` /
`setClipsEdit` are the single entry points (`AudioEngineCommands_Midi.cpp`,
`AudioEngineCommands_Clips.cpp`); the shared strict parser is
`src/common/BatchEditJson.h`, so the MCP tools and the
`project.setNotesGain`/`project.setClipsEdit` routes emit the same error bytes
by construction. Validate-then-apply: an empty array or any unknown id refuses
the whole batch with nothing written and no undo unit. (`edits` items are
`additionalProperties:false`, so a typo'd key is rejected, not dropped.)
`BatchEditRpcTest.*` covers payload parity, one-undo-unit, partial-edit, and the
non-integral-id refusals.

**S4 — render→measure→compare.** `verify_window` (+ `composition.verifyWindow`)
renders the WHOLE project through the shared launcher
(`src/common/RenderLaunch.h`) and measures only the requested window
(`src/common/VerifyWindowJson.h`), with `src/common/MixReportJson.cpp`
`buildWindowReportPayload` promoting the WINDOW's stats to the payload root so
`targets` gates the window, not the file. `render_and_verify`
(+ `export.renderAndVerify`) uses `src/common/RenderAndVerify.h` and the same
`src/common/MixVerdictInputs.h` the `mix_verdict` tool uses, so their verdict is
byte-identical. `verify_window`'s expectation keys are strict
(`src/common/RenderToolArgs.h`). Pinned by `VerifyWindowParity.*` /
`RenderAndVerifyParity.*` (`tests/unit/engine/verify_window_test.cpp`).

**S5 — lifecycle bootstrap.** `whoami` (`registerWhoamiTool`,
`src/mcp/McpTools_Engine.cpp`) shares `buildEngineInfoPayload` with
`engine_info` so the two cannot drift
(`EngineTools.WhoamiMatchesEngineInfoForSameArgs`), and adds transport +
session project path/name/counts. `HDAW_headless --mcp-stdio --project <file>`
(`src/common/HeadlessArgs.{h,cpp}`, `src/main_headless.cpp`) loads the project
after engine init + plugin scan; a malformed `--project` is a hard exit, never a
silent empty project. `scripts/mcp_call.py` gained `--engine-args` to prove the
bootstrap end-to-end.

**S6 — unit-tagged time windows.** `src/common/WindowUnitArgs.h` is the ONE
resolver, run at BOTH dispatch choke points (`McpServer::handleToolsCall` before
schema validation, and `FrontendRouter::dispatch`): the musical spelling, the
`*Sec` twin, and the tool's bare key read per an optional `unit`; disagreeing
spellings are refused; the resolved value is written back into the key the
handler already reads. The same spec table DECLARES every accepted spelling into
the stored schema, so validator and resolver cannot drift. Route-specific specs
cover the routes that rename their keys. Pinned by `WindowUnitParityTest.*` /
`WindowUnitResolver.*`. **S6c closed the unit-echo gap:** the last four windowed
tools (`add_arranger_region` / `set_arranger_region_bounds` / `transport` /
`seek`) set `echoUnit=false` in S6 because their payload was a bare id / status
line; S6c CUT THOSE PAYLOADS OVER to a JSON object carrying the value AND the
unit (`{"regionID":<id>,"unit":<u>}` / `{"ok":true,"unit":<u>}`) and flipped all
four to `echoUnit=true`, so NONE is unresolved.

**S7 — workflow transactions.** `begin_batch`/`end_batch`
(`src/mcp/McpTools_Transport.cpp`) delegate to `ProjectCommands::beginBatch` /
`endBatch`; the shared `src/common/BatchEnd.{h,cpp}` parses → SEALS → optionally
verifies, so the tool and the `project.beginBatch`/`project.endBatch` routes
cannot drift. The atomicity guarantee is the single choke point
`AudioEngineCommands::transactionBoundary`
(`src/engine/AudioEngineCommands_Undo.cpp`): while a batch is open every command
undo boundary is suppressed, so the whole batch is one undo unit
(`BatchEditRpcTest.BatchCollapsesInternallyTransactionalCommandsIntoOneUndo`).
State is a FLAG, never a counter; one batch at a time; the MCP tool is
stdio-gated (the route is not — the one recorded asymmetry). `end_batch`'s
optional `verify` hook seals first and never un-seals on a verification failure.

## 4. New traps this session

1. **Windows `rpcndr.h` defines `small` as `char`**, while
   `juce_PushNotifications.h` declares `enum BadgeIconType { none, small, large }`
   — a `src/common` header that pulls engine/JUCE headers AFTER Qt headers breaks
   the build. Fix pattern: Qt-light header + a `.cpp` with JUCE/engine includes
   first (`src/common/BatchEnd.{h,cpp}` is the reference).
2. **JUCE's `beginNewTransaction` is often deliberately UNPAIRED** (`createBus`
   /`createSend` join) so a depth counter leaks — the batch flag + one boundary
   choke point is the safe design.
3. **Windowed renders do not predict full-render sums** (plugin state re-bakes
   per window) — windowed verification must measure OUT of a full render.
4. **`buildMixReportPayload` puts whole-file metrics at the ROOT**, so gating a
   window requires promoting the window's stats
   (`buildWindowReportPayload`), not reusing root fields.
5. **Accepted-argument silent classes fixed:** `requireInt`/`optInt`
   truncation, unknown expectation keys (`verify_window`), and ledger aliases
   that overstate equivalence (`begin_batch` was aliased to
   `project.beginTransaction`).
6. **Each `scripts/mcp_call.py call` invocation is a FRESH engine** (stateless)
   — use `run <steps.json>` for a multi-step proof.

## 5. OPEN

1. **Route-keyed window specs.** Any RPC route that renames its window keys is
   covered by the resolver's per-surface spec (plan §4 lists the route-only
   bulk/composite methods that keep their existing spelling and get no spec).
2. **Pre-existing surface divergences noted but NOT fixed:**
   `add_track` returns `{trackId,routed,trackID}` while RPC
   `project.addTrack` returns a bare index; `get_waveform_peaks` returns 200
   peak values regardless of an integral `numBins`.

Full sharded run (final tree): **2140/2140 executed — 2101 passed, 39 skipped, 0 failures**, every
shard `ran == intended` (960/960, 845/845, 335/335), 19 min wall — re-run after the engine fixes in §7–§9.
Earlier the same run was INCOMPLETE (1814 passed, one dead shard):
`PsytranceComposition.PsyDubFiveMinutes` intermittently died. That turned out to be a pre-existing
`PluginProxySlot` worker-lifetime use-after-free (reproduced on a pristine HEAD `f1551e4` build,
root-caused with CDB, fixed in §7); the test now passes inside shard 1. Baseline moved in
[`AGENTS.md`](../../AGENTS.md) (2140 tests, 0 failures); see also `docs/testing-mcp.md`.

## 6. Docs touched

- `docs/handoffs/2026-09-28-agent-mechanization-shipped.md` (this file) +
  `docs/handoffs/INDEX.md`.
- `docs/lessons-learned.md` (lessons 35–39).
- `AGENTS.md` (parity ledger, toolkit bullets, pitfalls pointer, index rows, testing baseline).
- `docs/pitfalls-juce.md` (the `small` include-order trap).
- `docs/plans/2026-09-28-agent-mechanization.md` (status → IMPLEMENTED, counts
  reconciled).
- `docs/testing-mcp.md` (the plugin-proxy UAF entry + the runner-exclusion wart).

## 7. Post-close-out engine fix — `PluginProxySlot` worker-lifetime UAF

While chasing the pre-existing `PsytranceComposition.PsyDubFiveMinutes` instability (the sharded
runner died inside it; the test touches no mechanization surface), a CDB second-chance access
violation was captured:

    hdaw_tests!proxy::PluginProxySlot::getStateInformation+0x9c   mov edx,dword ptr [r15+1A0h]

`uf`/`ln` show `mov r15, rcx` at entry (saving `this`) and the faulting read is `this->slotId` — the
slot object had already been FREED while its own background work was still running.

Mechanism: `startStateRetryWorker` launches a `std::jthread` capturing `[this]` that sleeps up to
~31 s and then calls `publishStateToRing` / `sendStateInternal` / `verifyStateApplied` →
`getStateInformation` (3 × multi-second bounded pipe attempts). `~PluginProxySlot` neither stopped
nor joined it: the destructor BODY killed the child, released resources and dropped the shm handle
while `std::jthread`'s implicit join only runs during MEMBER destruction (after that body), with
members declared after the thread (`crashed`, `childAlive`, …) destroyed before it. A `detach()`ed
`editorWatcherThread` captured `this` the same way. Reproduced on a pristine HEAD `f1551e4` build →
pre-existing, not caused by the mechanization work.

Fix (`src/proxy/PluginProxySlot.{h,cpp}`): the dtor now `stopTimer()`s, raises `stopping_`,
`request_stop()`s and JOINS the retry worker, then raises `editorWatchStop_` and JOINS the editor
watcher (no `detach()` remains in `src/proxy`) — all BEFORE any child/resource teardown. The worker
takes its `std::stop_token` as a lambda PARAMETER; the send/publish/verify paths early-out on
`stopping_`/`crashed`/`!childAlive` (deliberately NOT `getStateInformation` itself, so crash-recovery
capture on a dead child still works), bounding the join to one in-flight bounded op (~100 ms normal).
Flags are declared before the threads and the threads last, so reverse destruction kills the threads
first and the flags they read last.

Evidence: `PluginIsolation.DestroyWhileStateRetryWorkerRuns` (5 cycles, ~640 ms/cycle) and
`DestroyWhileEditorWatcherRuns` (1.04 s), both with the measured destruction time inside the
assertion; `PluginIsolation.*` 53/53; `CrashRecovery.*` + `ProxyNamespace*.*` 16/16; canary 5/5 green;
canonical shards complete on THAT build (2130/2130, 0 failures — §8 re-ran the suite after the lease
fix and records 2135/2135). Lesson 39 records the rule.

Not fixed (unverified code-read concerns only, no runtime evidence): `getPipe`/`getShm` hand out raw
`ChildInfo` pointers that a kill may free mid-use, and external callers (save/export threads) may read
a slot's state while a rebuild destroys it. **§8 is the follow-up: the lease/first of these was then
confirmed reachable and fixed (`aa5e05d`); the remaining open races are listed there.**

## 8. Second engine fix — proxy pipe/shm leases + single-closer handle discipline (`aa5e05d`)

Continuation of the investigation in §7 (the user asked to pursue the two "unverified follow-ups").
The raw-lease and stop-vs-I/O halves of follow-up #1 were confirmed by code read and are FIXED here;
follow-up #2 (external state readers vs slot destruction) remains OPEN — see the list at the end of
this section.

1. **Raw-pointer leases.** `ChildInfo::pipe`/`shm` were `unique_ptr` and `getPipe`/`getShm` returned
   RAW pointers used after the map mutex dropped; `killPluginHost` erases the entry (KillHard inside
   the lock; KillGraceful after `TerminateProcess` + a 1 s wait), freeing the object. The reachable
   cross-thread pair is the slot's OWN background workers — the state-retry worker
   (`sendStateInternal`/`verifyStateApplied` → `getStateInformation`) and the editor watcher
   (`waitForEditorClosed`) — against a kill on the domain's owning thread
   (`respawnIsolatedSlot` under `graphLock`, `spawnPluginHost`'s defensive pre-kill, `~ProxyProcessManager`).
2. **`stop()` vs in-flight I/O.** `PipeServer::stop()` closed the handle with no synchronization while
   bounded `overlappedRead/Write` re-read the member handle after multi-second waits (`CancelIo(hPipe)`,
   `GetOverlappedResult(hPipe,…)`), on plain non-atomic members.

Fix: `shared_ptr<PipeServer>`/`<ShmRegion>` owned by `ChildInfo` and RETURNED as leases (held for the
whole exchange); `hPipe` atomic and loaded ONCE per operation; `stop()` raises `stopped_` and calls
`CancelIoEx` ONLY — it must not clear the handle (the destructor's exchange would then see INVALID and
LEAK it) nor close it (double-close against in-flight I/O); `~PipeServer` is the sole closer
(Cancel → Disconnect → Close) and runs at the last lease release. `spawnPluginHost` already retries a
held name with a bumped slot id (8 attempts). The audio path is unchanged (non-owning `shmHandle`,
Gate 3) with an owning `shmLease_` held by the slot.

Evidence: `PluginIsolation.PipeLeaseSurvivesKill`, `StopRacesInFlightBoundedRead` (a kill lands while a
`__slowstate__` read is in flight), `EditorWatcherVsKill`, `ShmLeaseSurvivesKill`,
`PipeHandleNotLeakedAcrossKillCycles` (20 spawn→lease→kill→release cycles with `GetProcessHandleCount`),
`ProxyNamespace.SpawnBumpsSlotWhenShmNameHeld`; the isolation/crash-recovery/namespace set is
**74/74**; PsyDub canary single run **PASS (15.3 min)**; canonical shards complete on this build —
**2135/2135 executed, 2096 passed, 0 failures**. Lesson 40 + the invariant section in
`docs/realtime-safety.md`.

**Still OPEN (documented in `docs/realtime-safety.md`, NOT resolved by this patch):**
1. **No per-slot exchange serialization** — leases keep the objects alive but nothing prevents two
   callers interleaving a request/response pair on the same pipe (`A-send, B-send, A-receive`).
2. **`checkAllChildren()` reads `perSlotCrashCallbacks` outside the mutex** (the
   `for (auto id : crashedSlots)` lookup runs after the scoped lock) while
   `setSlotCrashCallback`/`removeSlotCrashCallback` mutate the map under the mutex — and
   `removeSlotCrashCallback` is called from `~PluginProxySlot` on the message thread, so the health
   monitor can look up an entry while the message thread erases it (iterator invalidation → UAF).
   Code-evident, no runtime evidence yet.
3. **External state-reader affinity is UNVERIFIED** — save/export-prepass reads and slot destruction
   currently share the message thread for a live domain and the offline export domain starts no timer,
   which makes an overlap unlikely, but nothing enforces it. **→ FIXED in §9 (items 1 and 2);
   item 3 remains open.**

## 9. Third engine fix — pipe exchange serialization + callback-map snapshot

Items 1 and 2 of §8's OPEN list, both confirmed by code read, plus the message-pump hazard the fix
itself introduced:

1. **Exchange serialization.** `PipeServer` had no exchange-level lock, so concurrent users of one
   slot's pipe (the 100 ms message-thread `pollProgramCount`, UI editor calls, the background
   state-retry worker, the editor watcher) could interleave `A-send, B-send, A-receive` and consume
   each other's replies. Added `PipeServer::Exchange` (RAII): ONE guard spanning a full
   request→response transaction including `STATE_CHUNK` continuations; blocking acquisition for
   worker/command paths, `std::try_to_lock` + skip for the periodic `pollProgramCount` (a blocking
   acquisition there would stall the JUCE message pump while a state exchange holds the lock across
   seconds of retries) and `try_lock`-per-iteration for the editor watcher's unrequested-await loop;
   one-way `sendHeartbeat` under the same lock; connection/`connected` transitions owned by the
   guarded path. Documented LOCK ORDER: LEASE first, then the Exchange; never call back into
   `ProxyProcessManager` while holding it; `stop()` takes neither lock (it signals + `CancelIoEx`s,
   which is what unblocks a waiting exchange). Reentrancy: non-recursive mutex,
   `verifyStateApplied` calls the already-guarded `getStateInformation` sequentially (plus an
   NDEBUG-only lock-owner tripwire). Every production pipe I/O now runs inside an Exchange — the
   src-wide grep for raw `->send*/receive*` returns no matches.
2. **Callback-map snapshot.** `checkAllChildren()` looked up and invoked `perSlotCrashCallbacks`
   AFTER releasing `mutex`, while `set`/`removeSlotCrashCallback` mutate the map under it (and
   `removeSlotCrashCallback` runs from `~PluginProxySlot` on the message thread) → iterator
   invalidation. Now `invokeCrashCallbacks(ids)` snapshots the `std::function`s under the lock and
   invokes them OUTSIDE it (callbacks re-enter `PluginManager`/`CrashRecoveryManager`, so the lock
   must not be held during the call). Contract documented in `ProxyProcessManager.h`: a callback
   removed concurrently with a sweep may still fire once (snapshot semantics).

Evidence: `PluginIsolation.*` **79/79** (74 + `ConcurrentExchangesDoNotMisattribute`,
`UnsolicitedEditorClosedIsRoutedNotConsumed`, `DesyncedPipeDiscardsUnexpectedReply`),
`FxMidiInjection/InternalFx/Clap` 35 passed (env skips expected), `CrashRecovery.*` +
`ProxyNamespace*.*` 16/16 plus `CrashCallbackSweepSnapshotsUnderTheLockAndInvokesOutside`; PsyDub
canary PASS (616.9 s); canonical shards complete — **2140/2140 executed, 2101 passed, 0 failures**
(960/845/335). Gate 3 checked: the guard/mutex is never touched by `processBlock`/
`flushStagedParams` (they read only the non-owning `shmHandle`).

**STILL OPEN — stale replies after a timeout (needs a protocol decision, NOT fixed).** A bounded
receive that times out deliberately leaves its reply queued, and `ProxyMessage`/`ProxyResponse` are
exactly 256 bytes with no correlation id and no end-of-response marker (a `GET_STATE` answer is a
header plus N chunks declared in the header). After a timeout the queued-message count is therefore
unbounded and a same-type late reply cannot be distinguished from a fresh one. What the code does
today is best-effort: an unsolicited `EDITOR_CLOSED` is routed to its callback, a timeout sets an
internal `desynced_`, and unexpected reply types are logged/discarded while desynced —
`DesyncedPipeDiscardsUnexpectedReply` pins that behaviour, and it is explicitly NOT a correctness
guarantee. Real options: (i) add a correlation id echoed by the child (`data[244]`→`[240]` in both
structs, touching every message path and the chunk math) or (ii) make a timeout fatal/desyncing and
restart the connection before another exchange. §8's item 3 (external state-reader affinity) also
remains UNVERIFIED.

**Run note:** one canonical attempt was invalidated by an orphaned `hdaw_tests.exe` left by a
cancelled verification run — it produced two `RenderSequenceRelease.*` failures and a shard death at
`PsytranceComposition.NewPacksLongRenderWithFxAutomation`. Both failing tests pass solo, and the
clean re-run above is green; treat a red result while any orphan test/plugin process is alive as
contaminated (kill leftovers first).
