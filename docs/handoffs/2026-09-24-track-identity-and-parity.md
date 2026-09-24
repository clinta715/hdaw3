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
  unresolved 17** (`node tools/rpc_parity_map.mjs`).
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
- **The ledger's 17 `unresolved` are a real work list.** Mostly **missing routes whose command already
  exists** — `project.applyMovementPlan`, `project.applyAutomationPreset`, `project.setMasterFxParam`,
  `project.setMasterFxBypassed`, `composition.placePatterns`, `composition.scaleDegreeToPitch`,
  `session.getClipStates` — each a small route + twin test. Two need a decision, not a rename:
  `add_library`'s route rejects `type="patch"` while the tool creates patch libraries;
  `audio.fm_synthImportSysex` applies LIVE-ONLY where the tool persists the patch.
  Run `node tools/rpc_parity_map.mjs --show-unresolved` for the current list.
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
   `QSettings` writes silently do nothing (so tests read the machine's real values). Recipe:
   `set TEMP=D:\pdf\roo projects\hdaw3\.tmp_suite & set TMP=%TEMP% & powershell -File
   run-tests-sharded.ps1 -Shards 4` (the `%TEMP%` class goes green → 15 failures became 172/172 in one
   focused set). **Never "fix" a test to expect the sandbox.** Detail: `docs/testing-mcp.md`.
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
# focused set (172 tests: ids, parity, waits, settings, regressions)
set TEMP=D:\pdf\roo projects\hdaw3\.tmp_suite & set TMP=%TEMP%
build\hdaw_tests.exe --gtest_filter="Commands.*:ProjectMetadata.*:BusSendRpcTest.*:AddFxParityTest.*:McpCoverageTest.*Track*:McpCoverageTest.*Send*:TrackProperties.*:*Parity*:SongCells.*:AutomationPidRouting.*:FxSurface.*:McpServer.EngineSettingsStartMcpHttp:MatrixRpcParityTest.*"
# full suite (read the FAILED TESTS list, not the total)
powershell -File run-tests-sharded.ps1 -Shards 4
# parity gate (rebuild after: the ledger is compiled into the ratchet test)
node tools/rpc_parity_map.mjs
# knowledge graph (see AGENTS.md for the .graphify_root marker gotcha)
python -m graphify update . --force ; graphify explain <newSymbol>
```
