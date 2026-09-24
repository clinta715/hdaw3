# Handoff — track surface parity, stable ids (B1/B2), native-Windows tooling

**Read this first.** It supersedes `docs/handoffs/2026-09-23-remaining-issues.md` (kept as history for
the pre-session state; its "remaining issues" list is closed or superseded below). Everything here was
verified against the tree at `49a531b`.

## 1. State

- **HEAD `49a531b`** at the time of writing, plus the documentation commit that follows it (this file
  and the "SUPERSEDED" banner on the old ledger). This session's code work is committed; nothing of
  it is dirty.
- **Uncommitted files that belong to ANOTHER session — leave them alone**: `.gitignore` (an
  `.agents/` junction entry), `AGENTS.md` (graphify/DSH-plugin findings), `docs/skills/README.md`,
  `docs/skills/psy-song-session/SKILL.md`. Do not sweep them into an unrelated commit; their author
  is mid-flight.
- Parity ledger after this session: **307 tools / 396 methods / mapped 278 / mcp-only 12 /
  unresolved 17** (`node tools/rpc_parity_map.mjs`) — and after the **2026-09-24 second wave**
  (§2b): **307 / 403 / mapped 285 / mcp-only 12 / unresolved 10**.
- Project version 0.37.0. Build: `build-fast.bat test|all|debug` (it bootstraps MSVC **and resolves
  cmake itself** — a plain shell works).

## 2. What landed (oldest → newest)

| commit | scope | verified by |
| --- | --- | --- |
| `e0a4095` | the pre-existing 73-file batch, committed here at the user's instruction (base for everything below) | full suite |
| `ce56bc4` | **tooling**: native-Windows-first scripts + docs; `time-sync.cmd` opt-in, cmake auto-discovery, bash-only twins retired, **no frontend build/test gate anywhere** | scripts executed; `cmd /c scripts\time-sync.cmd` exits 0 silently |
| `2717288` | **track parity closed**: `set_track` covers all 13 `project.setTrack*` properties, 2 folder-move tools, `project.addTrackWithFx`, MCP's removal guard mirrored on RPC, one creation payload, `trackId` rename (+ `folderId`/`inputMonitor`/`midiChannel`), retired keys rejected; **plus the parity generator fixed** | 54 parity tests + ratchet |
| `1484d93` | the unreachable FM pid branch (300–308) deleted; `FmModParamIDs` gone; 3 stale comments fixed | `AutomationPidRouting.LfoTarget306…` |
| `02b9a26` | the two vacuous `SongCells` tests now fill a real track | `SongCells.*` |
| `40ebe3b` | **design B1**: stable `trackID`/`sendID` (tree-derived allocator, load-time backfill, duplicate re-stamps) echoed on both surfaces | 144 tests incl. save/load + backfill |
| `2306fb8` | `McpServer` CLAP tests parse the payload instead of regex-matching it | 13 CLAP plugins render again (peaks 0.065…0.459) |
| `970ae8b` | handoff: shard-abort + render-starvation observations | — |
| `8675ed8` | **the last fixed-port test is gone** (`setMcpHttpConfig` accepts `port == 0`, never persists 0, reports the bound port) | `McpServer.EngineSettingsStartMcpHttp` + `HttpTransport.*` |
| `0f2f5ad` | **parity ledger classified**: 96 `unresolved` → 278/12/17, every mapped row naming the shared entry point | regenerated + ratchet |
| `7b39c0c` | **design B2**: `trackID`/`folderID`/`sendID` accepted as arguments on the track + send CRUD | 172-test focused set |
| `c8919f9`, `c2e0591`, `49a531b` | docs: ephemeral port, the sandbox-write trap, the B3 plan, handoff close-out | — |

Focused verification at HEAD: **172/172** (Commands, ProjectMetadata, BusSendRpcTest, AddFxParity,
McpCoverage track/send, TrackProperties, the parity ratchet, SongCells, AutomationPidRouting,
FxSurface, MatrixRpcParity, the HTTP runtime test).

## 2b. 2026-09-24 second wave — the seven no-route tools (UNCOMMITTED at the time of writing)

§5's first bullet, closed in one pass: the MCP tools `automation_preset`, `apply_movement_plan`,
`set_master_fx_param`, `set_master_fx_bypassed`, `place_patterns`, `scale_note`,
`session_get_clip_states` now each have a JSON-RPC twin.
(`project.applyAutomationPreset`, `project.applyMovementPlan`, `project.setMasterFxParam`,
`project.setMasterFxBypassed`, `composition.placePatterns`, `composition.scaleDegreeToPitch`,
`session.getClipStates`.)

- **The shared-entry-point half** (a previous session, left uncommitted and inherited here):
  `src/common/{AutomationPresetRequest,MasterFxAccess,MovementPlanJson,PlacePatternsRequest,ScaleNote,SessionClipStateJson}.h`
  — each header is the ONE entry point both surfaces call, so text/payload parity is by construction.
  `src/mcp/McpTools_{Automation,CompositionGenerate,CompositionPattern,FxSlot,Session}.cpp` were slimmed
  to call them (-265/+53 in that directory). All six headers are header-only: no CMake registration.
- **The route half**: seven `if (m == "…")` branches (Router_Project ×4, Router_Composition ×2,
  Router_Session ×1). The JSON-payload routes hand the client the parsed structure of the shared compact
  JSON (read.getFxSlots precedent); the two master-FX routes hand back the shared TEXT as the payload
  string, because that text carries the clamped value lesson 23 requires. `session.getClipStates`
  needed `SessionManager` in the dispatcher (not reachable from `ProjectCommands&`) — `Router_Session.h/.cpp`
  + the single call site `FrontendRouter.cpp:114`.
- **Evidence**: `tests/unit/frontend/missing_route_parity_test.cpp` (12 tests) — failure parity asserts
  `-32602` + byte-identical text; payload parity asserts route payload == parsed tool text (master FX:
  identical string). Ledger **307 tools / 403 methods / mapped 285 / mcp-only 12 / unresolved 10**.
  Focused set **147/147** (`MissingRouteParityTest.*:RpcParityRatchet.*:RpcNamespaceCoverage.*:Commands.*:
  ProjectMetadata.*:BusSendRpcTest.*:TrackProperties.*:SongCells.*:AutomationPidRouting.*:FxSurface.*`),
  build exit 0. The new test file required an explicit `cmake -S . -B build` (suppressed regeneration).
- **Full-suite gate (sharded 4-way + serial, measured at the end of this wave): 1967 tests / 284
  suites — 1916 pass / 39 skipped / 12 distinct failures, every one environmental and none in the
  wave's diff** (11 match a class in `docs/testing-mcp.md`: 6× `RaveSettings.*` and
  `FrontendServer.SettingsNamespaceExposesMcpHttpConfig` (QSettings/machine state), `McpServer.ApplySongPlan`
  / `SongPlan.TemplateRoundTripDoesNotApply` / `McpCoverageTest.FxChainPresetRoundTrip` (`%APPDATA%`
  template/preset writes), `PluginIsolation.LargeStateRoundTripThroughProxy` (solo-pass flake); the 12th,
  `TransportSurface.StartStopRecording`, is the deviceless-input class — this box exposes no capture
  endpoint, so `AudioRecorder::startRecording` returns false). **The AGENTS.md baseline (1865/277) is
  stale by 90 tests / 6 suites of already-committed work** — HEAD alone is 1955/283, and
  `RespawnPath.RealPathPassesThrough` (the documented 1-failure baseline) no longer fails: it is
  platform-gated.
- **Two measured deviations, both kept** (do not "fix" them back):
  1. **The parity generator could not see a chained predicate.** `tools/rpc_parity_map.mjs` matched
     `if (m == "X")` only, so `if (m == "setMasterFxParam" || m == "setMasterFxBypassed")` made BOTH
     methods invisible (the first regeneration reported 401 methods / 12 unresolved / 283 mapped). The
     scanner now matches the whole condition and harvests every literal in it — a future chained branch
     is covered. This is a second flavor of §4.6: *the generator is part of the gate; a new route shape
     must be regenerated AND read.*
  2. **MCP schema validation runs before the tool handler** (`src/mcp/McpServer.cpp:113` →
     `McpSchema.cpp`), so a missing or out-of-range REQUIRED argument is rejected there with
     `invalid params: …` while the route reaches the shared validator and answers its own text. Both are
     `-32602` and both refuse — only the wording differs on that input class. The twin tests assert
     byte-identical text for schema-satisfying args, and code+exact route text for the rest.

## 3. The two designs in one paragraph each

**B1 — identity.** Every TRACK/SEND carries a stable `trackID`/`sendID`, minted at creation by a
**tree-derived** allocator (`max existing + 1` — never a counter, because a counter is global state an
offline render model resets; that exact bug reused clip ids in 2026-08-23). `scanAndSyncTrackIDs()`
backfills a pre-B1 file on load (collect-then-assign so it can never mint a colliding id, never
undoable), `duplicateTrack` re-stamps the copy, and the ids are echoed in `TrackSnapshot.trackID` /
`SendSnapshot.sendID`, the shared send rows (`src/common/SendJson.h`), the creation payloads
(`TrackJson.h`, `AddTrackWithFx.h`) and `read.getTrack`/snapshot. **`trackID`/`sendID` are identities;
the pre-existing `trackId`/`sendIndex` stay positional addresses** — the case difference is deliberate.

**B2 — accepting them.** The surfaces now take the id as an OPTIONAL alternative to the positional
argument (`trackId`/`trackID`, `folderId`/`folderID`, `sendIndex`/`sendID`). One rule, in
`src/common/StableRefResolve.h` (Qt-free) with thin per-surface wrappers: the stable id wins; an
unknown id is an error naming it; a positional argument naming a different entity is an error naming
both; positional-alone is byte-for-byte the old behaviour; presence is decided by `contains()`, never
by the value (an explicit `trackId: 0` is the first track). **No "guess which number they meant"** —
an index and an id can be numerically identical, so that rule would be untestable and would mutate the
wrong track. **Deliberately NOT wired** on the fx/automation/plugin tools that take `trackId` (they
keep the index) — see §5.

## 4. Findings fixed here (each was reproduced before the fix)

1. **`move_track` forward moves were doubly wrong** — the MCP tool spliced at the un-decremented index,
   so the order differed from `project.moveTrack` *and* the shared permutation ran on a mismatched
   tree; a folder `childIds` / `SONG_PLAN cellTrack` could end up pointing at the wrong track.
   Untested before (existing assertions only moved backward).
2. **`list_fx` ↔ `read.getFxSlots` were divergent**, not just duplicated (`slot`/`type` vs
   `slotIndex`/`fxType`, `pluginName` on one side).
3. **An LFO targeting an internal EQ param allocated on the audio thread per sample**
   (`Coefficients::makePeakFilter` is `*new Coefficients(...)`). All four EQ rebuild sites now use the
   non-allocating `ArrayCoefficients` + array assignment (bit-identical coefficients).
4. **`removeSend` did not remap LFO targets** — an LFO on `2000 + sendIndex` went stale; it is now
   parked at `-1` (inert but preserved) in the same undo unit.
5. **The documented FM targets 300–308 were unreachable** (the `>=100` audio-FX compound claims those
   pids first) — branch deleted, docs corrected.
6. **The parity generator was lying about the project namespace**: it registered only the FIRST
   `dispatchX(` per namespace branch, so once `FrontendRouter` intercepted `removeTrack`/
   `addTrackWithFx` before `dispatchProject`, regenerating relabelled every `project.*` row as
   `settings.*`; methods resolved in the intercept chain were invisible.
7. **Three `McpServer` CLAP tests regex-matched the MCP payload** (`"trackId":N`) — the shared JUCE
   builder emits `"trackId": N`, so all 13 plugins reported "could not add track". Now parsed.
8. **Two `SongCells` tests were vacuous** — they filled cells wired to a track that did not exist
   (zero-track default project), `addMidiClip` no-oped, and `fillOneCell` still reported success. The
   batch's new guard exposed them; the guard stayed.
9. **Qt's `slots` macro broke the build** via a `src/common/` shaper parameter named `slots`.

## 5. Open work, and the decision each one needs

- **B3 — durable refs → ids.** Plan written, **not built**:
  `docs/plans/2026-09-23-durable-refs-to-stable-ids.md`. It migrates folder `parentId`/`childIds`,
  `SONG_PLAN cellTrack` and the lane/LFO send addresses to ids and deletes
  `remapTrackPositionalRefs`/the shift machinery. Needs a decision on four questions in that doc —
  most important: **a lane send address is `2000 + sendIndex`, so a send id above 999 would collide
  with the bus range `3000+`**; recommend scoping B3 to refs 1–3 and treating the send address
  separately. B1+B2 already make a held id durable and usable, so B3 is robustness + deletion.
- **The ledger's 10 remaining `unresolved`** (the seven routes this bullet previously named landed in
  the second wave — §2b). Run `node tools/rpc_parity_map.mjs --show-unresolved` for the live list.
  Two need a **decision**, not a route: `add_library`'s route rejects `type="patch"` while the tool
  creates patch libraries; `audio.fm_synthImportSysex` applies LIVE-ONLY where the tool persists the
  patch. Two are near-miss twins: `fm_synth_get_state` (different data source) and `apply_preset`
  (name-derived `matrix.applyPreset` is a different family — it needs its own front door). The rest are
  **no route today, with the missing capability named**: `fm_synth_load_preset` / `sub_synth_import_sysex`
  / `audition_patch` (raw patch writes have no dispatch), `load_plugin_preset_file`,
  `get_master_fx_params` (no master-FX read accessor exists — the bus routes address BUS_LIST),
  `list_clip_takes` (nothing enumerates TAKE_LIST / activeTake).
- **B2's scope cut (candidate B2b):** the fx/automation/plugin tools that take `trackId` do not accept
  `trackID`. An agent holding an id must resolve it for those. Extend the same resolver when it
  matters.
- **`*FX` CLAP editions cannot process audio** — cross-repo (the plugin builds); our side already
  filters them out of `list_plugins` and rejects them with an explicit message
  (`src/common/FxPluginIdCheck.h`). No repo-side work.
- **MCP HTTP config side effect worth knowing:** `mcp/httpEnabled=true` persisted in `QSettings` means
  **every** engine instance starts a loopback HTTP server on the configured port; port 0 is accepted
  while enabling and never persisted (the bound port is stored instead).

## 6. Traps for the next agent (each cost real time here)

1. **Qt keyword macros vs `src/common/`.** `qtmetamacros.h` defines `slots`/`signals`/`emit`/`foreach`;
   a parameter named `slots` in a header included from a Qt TU is erased → a build failure with
   `syntax error: '.'`. Name it `fxSlots`/`slotsList` (see `src/common/SendJson.h`).
2. **MCP text payloads are NOT byte-compact.** The shared builders use
   `juce::JSON::toString(v, true)` — one line, but a space after `:` and `,`. **Parse** payloads in
   tests (`QJsonDocument::fromJson`), never `contains("\"k\":v")` / regex them.
3. **After regenerating the ledger, read it.** The ratchet verifies a mapped target EXISTS, never that
   it means the same thing; check the namespace column (`project.*` rows must not read `settings.*`)
   and remember `FANOUT` exists for one-tool-many-routes.
4. **The sharded runner's "failed" count is ~2× the real one** (it counts `[ FAILED ]` lines and gtest
   prints each failure twice): read the `FAILED TESTS` list, then re-run those solo. A shard can also
   **stop silently mid-test** — compare per-shard test counts against a previous run before trusting
   "0 failed" on a short shard. Heavy runs starve renders
   (`export failed: Render graph bake timed out after 15000ms`) — those pass solo.
5. **A sandboxed shell loses the child's writes outside the working tree**: `%TEMP%` →
   `Access is denied`, `%APPDATA%` preset/template writes → `failed to write template file …`, and
   `QSettings` writes silently do nothing (so tests read the machine's real values). Recipe — **set both
   vars to the literal path**: `cmd /c "set TEMP=D:\pdf\roo projects\hdaw3\.tmp_suite&& set
   TMP=D:\pdf\roo projects\hdaw3\.tmp_suite&& powershell -File run-tests-sharded.ps1 -Shards 4"`.
   `set TMP=%TEMP%` in the same cmd line is a TRAP (measured 2026-09-24): cmd expands `%TEMP%` at parse
   time, so TMP keeps the sandbox-denied path and the save/load class stays red — 8 unrelated failures
   (`SongCells.CellsPersistAcrossSaveLoad`, 6× `ProjectMetadata.*`,
   `BusSendRpcTest.ListBusesMatchesMcpAndTheSavedProject`) that all go green once TMP is set explicitly.
   **Never "fix" a test to expect the sandbox.** Detail: `docs/testing-mcp.md`.
6. **The deviceless pattern** (no usable audio route — disconnected RDP): suites fail fast with
   `getTrack() == nullptr`; environmental, not your change (lessons 9/17).
7. **Stale binaries lie**: verify the binary, not the source (lesson 15). Useful scratch pattern for
   agent-driven builds: a wrapper that calls `vcvars64.bat` and the VS-bundled cmake explicitly — but
   keep it **outside `%TEMP%`**, because `scripts/cleanup-stale.ps1 -Apply` sweeps `%TEMP%` and will
   delete your wrapper mid-session (that happened here).
8. **`PluginIsolation.LargeStateRoundTripThroughProxy`** is the historical solo-pass flake; check
   `docs/testing-mcp.md` before blaming it.
9. The repo's `docs/pitfalls-*.md`, `docs/lessons-learned.md` and the `hdaw-guard` skill remain
   authoritative — this handoff only adds what is newer.

## 7. How to re-verify

```
build-fast.bat test                     # or `all` to also build hdaw_plugin_host.exe (isolation suites)
# focused set (ids, parity, waits, settings, regressions; the parity subset alone is 147 tests)
# NOTE: set BOTH vars to the literal path — `set TMP=%TEMP%` here keeps the sandbox-denied path (§6.5)
cmd /c "set TEMP=D:\pdf\roo projects\hdaw3\.tmp_suite&& set TMP=D:\pdf\roo projects\hdaw3\.tmp_suite&& build\hdaw_tests.exe --gtest_filter=Commands.*:ProjectMetadata.*:BusSendRpcTest.*:AddFxParityTest.*:MissingRouteParityTest.*:McpCoverageTest.*Track*:McpCoverageTest.*Send*:TrackProperties.*:*Parity*:SongCells.*:AutomationPidRouting.*:FxSurface.*:McpServer.EngineSettingsStartMcpHttp:MatrixRpcParityTest.*:RpcNamespaceCoverage.*"
# full suite (read the FAILED TESTS list, not the total)
powershell -File run-tests-sharded.ps1 -Shards 4
# parity gate (rebuild after: the ledger is compiled into the ratchet test)
node tools/rpc_parity_map.mjs
# knowledge graph (see AGENTS.md for the .graphify_root marker gotcha)
python -m graphify update . --force ; graphify explain <newSymbol>
# ^ measured 2026-09-24 (second wave): a SILENT NO-OP in this environment — exit 0 in 0.63 s,
#   the "Re-extracting code files in . (no LLM needed)..." banner, ZERO files written
#   (nothing under graphify-out/ newer than the 07:37 build), and `graphify explain
#   setMasterFxParamToolText` still reports "No node matching". The graph therefore stays at
#   HEAD 4afe1fc until the post-commit hook rebuild (plus this explicit update) is verified with
#   `explain <newSymbol>` — never trust the banner or the exit code.
```
