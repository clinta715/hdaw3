# Plan: agentic batch commands — corrected scope (2026-09-22, revision 2)

**Status:** planned, not started. Revision 2 REPLACES the original
`select_palette` / `verify_layer` / `verify_mix` proposal after a code audit found
one item already shipped, one arithmetically infeasible, and one with an input
shape that cannot express its gates.
**Owner:** agent session 2026-09-22. **Risk:** LOW (one new command + two additive
payload enrichments + one environment fix; no DSP, no `processBlock`, no graph
topology, no render-path change).
**Related:** `docs/plans/2026-09-22-param-verity-pipeline.md` (ParamVerity/Phase 2
`tone_verity`, today), `docs/plans/2026-09-21-rpc-parity-retrofit.md` (the parity
rule + `mix_verdict` completion), `docs/skills/psy-song-session/` (the workflow
roles this serves), `docs/testing-mcp.md` (the transport budget).

## Problem (as measured, not as assumed)

The psy-song-session roles burn round-trips on orchestration. Three claims in the
original plan were checked against the code:

1. **"`verify_mix` merges mix_report + audit_modulation_coverage +
   audit_song_structure + diagnose_intro_blast into one verdict."**
   **Already shipped** as `mix_verdict` (2026-09-21): `buildMixVerdict`
   (`src/common/MixVerdict.cpp:25`) composes `audible` / `clipping` /
   `loudness(drop-vs-build)` / `structure` / `modulation` / `introBlast` into
   `{ok, gates{}, issues[], warnings[]}`, with MCP `mix_verdict`
   (`src/mcp/McpTools_AudioRead.cpp:434`, schema
   `{filePath, fromPlan, bpm, dropBuildRatio, introSeconds, sections[]}`) and RPC
   `audio.mixVerdict` (`src/frontend/router/Router_Audio.cpp:164`).
   The **only** gate it lacks is per-role tuning.

2. **"`select_palette` batches 10 roles × (patch + track + chain + preset +
   tone_verity) into one call."** **Infeasible as specified.** `tone_verity`
   solo-renders (`AudioEngineCommands_Composition.cpp:1770`), `renderTrackWindow`
   is a synchronous block-wait offline export (`:365`), and the offline graph
   builds **every non-folder track** (`RoutingManager.cpp:119-123`), so each
   render spawns one isolated child per plugin slot **project-wide**, with a
   **12 s** warmup each for virus-family descendants (`PluginHost.cpp:980-1057`).
   Ten renders over a growing palette ≈ 11 min inside one call. The live HTTP
   transport's client deadline was **OMP's 30 s default** (per-server `timeout`
   absent; `OMP_MCP_TIMEOUT_MS` unset), so the batch is ~22× over budget — not the
   "10 s" the original plan cited. The plan's own cited remedy (AGENTS.md lesson 29)
   is "batch **small**, save often".

3. **"`verify_layer` returns one `{pass, gates:[{name,pass,measured,expected}]}`
   verdict for a track."** The verdict contract **already exists** on
   `tone_verity` (`src/common/ToneVerity.h` → `evaluateToneExpectations` fills
   `expectations` / `pass` / `expectationsChecked`; MCP returns
   `{pass, expectationsChecked, expectations[]}`), and the per-track modulation
   data already exists in `modulationCoverageJson(...).tracks[{trackId,…}]`
   (`src/common/ModulationCoverage.h`). So the delta is **one payload row**, not a
   tool. Worse, the proposed signature took `{trackIndex, role, window}` while the
   surface's argument names are `trackId` / `windowSeconds` (AGENTS.md:
   "argument names are part of the contract").

Additional defect found while auditing the premise itself: the plan asserted
"10 s per-call timeout → exit-42 restarts". Exit 42 has exactly one producer —
the deliberate `engine_restart` tool (`src/mcp/McpTools_Engine.cpp:137`;
`main_headless.cpp:138/201` return `app.exec()`, `mcp-launch.bat` has no 42
branch). A timeout is a **discarded connection + relaunch onto an empty project**.
Corrected in `AGENTS.md` (lesson 29), `docs/lessons-learned.md` §29,
`docs/skills/psy-song-session/reference.md`, `docs/testing-mcp.md`.

## Existing assets (reuse — do NOT rebuild)

| Asset | Location | Role here |
| --- | --- | --- |
| `buildMixVerdict` | `src/common/MixVerdict.cpp:25` | the aggregator; Slice 1 extends it |
| `analyzeTuning` | `src/common/TuningAnalysis.h` | per-role spectral analysis, already `src/common` (parity-ready) |
| `modulationCoverageJson` | `src/common/ModulationCoverage.h` | per-track rows incl. `faderOverriddenIds`; already in the verdict |
| `analyzeToneWav` / `evaluateToneExpectations` | `src/common/ToneVerity.h` | the per-track verdict contract; Slice 2 adds one row |
| `verifyPart` | `composition.verifyPart` (`Router_Composition.cpp:1192`; engine `:1469`) | 2 renders; the precedent for a render-bearing verdict — and the reason not to add more |
| `addInstrumentPart` | `AudioEngineCommands_Composition.cpp:725-948` (begin `:809`, one `rebuildRoutingGraph` `:915`, end `:941`) | the ONE undo unit + ONE rebuild pattern `select_palette` must copy |
| `generateArrangement` | `src/engine/AudioEngineCommands_Clips.cpp` | existing **multi-track, one transaction** precedent |
| `clearNotes` | `src/engine/AudioEngineCommands_Midi.cpp` | the **LIST-level node-swap** idiom (lesson 30: never fire the listener per child) |
| Song Brief `palette` / `paletteTrackMap` | `docs/skills/psy-song-session/brief.schema.json` (stored verbatim in the `SONG_PLAN` node) | the persistence target for the palette record |
| `FileLibraryManager::selectPatch` | `src/engine/FileLibraryManager.h:151` | cluster-stratified + seeded + ledger variety (lesson 31); returns `{path,name,libraryId,clusterId,tags,poolSize,usedCount,seed}` |
| `McpJobs` + `wait:false` | `mix_report` `McpTools_AudioRead.cpp:579-591`, `analyze_tuning` `McpTools_Tuning.cpp:46-53`, `scan_plugins` (async-default) `McpTools_ProjectSaveLoad.cpp:247-266` | the async escape hatch for anything that can exceed 10 s. Reply is always `{jobId, state:"running", pollWith:"…"}`; terminal states are `finished` / `failed`; `kMaxRetainedJobs = 64` |
| `audio.jobStatus` (exists) | `src/frontend/router/Router_Audio.cpp:153-162` | the RPC poller for `audio.*` async jobs — already emitted as `mixReport`'s `pollWith`; no new RPC method needed. `poll_job` stays MCP-only (`rpc_parity_map.inc:155`) |
| `audition_patch` | MCP tool | palette-time audibility (stages its own probe clip) — what `tone_verity` cannot do pre-notes |

**God-node note (required):** every slice adds to `AudioEngineCommands` (308
edges) or `ProjectCommands` (254) — the two highest-degree hubs in
`graphify-out/GRAPH_REPORT.md`. Additive methods only; no signature changes.

## Decisions taken (each reversible — flag and I flip it)

1. **`select_palette` ships RENDER-FREE** (one undo unit, one rebuild, LIST-level
   batch, seeded `select_patch`), with audibility left to `audition_patch`.
   Rationale: §Problem 2; a render inside a batch is incompatible with the
   transport budget until Slice 0 is verified applied.
2. **No `verify_layer` tool.** The verdict contract exists on `tone_verity` and the
   modulation row is a payload addition (Slice 2). A new tool would duplicate a
   shipped contract and add a parity row for no new capability.
3. **No `verify_mix` tool.** Extend `mix_verdict` (Slice 1) instead of shipping a
   fourth verdict tool whose gates are a subset — the harness proxies MCP lazily
   precisely to avoid schema-surface bloat.
4. **Async before enrichment.** `mix_verdict` has no `wait:false` while its
   sibling `mix_report` does, and it FFTs the whole file — it is already
   timeout-fragile on long renders. Slice 1 adds async *first*, then the tuning
   gates (which only make it slower).

## Non-goals

- Any new **render-bearing** batch tool (a "layer gate" that renders) until Slice 0
  is verified applied — it cannot fit the budget on a virus-heavy project.
- `verify_layer` / `verify_mix` as tool names; `diagnose_intro_blast` gaining an RPC
  route (its gate is already inside `mix_verdict`).
- Changing `tone_verity`'s render count (Slice 2 adds a payload row to the
  existing single render), `processBlock`, DSP, `RoutingManager`, or the export path.
- Rewriting the role procedures wholesale — only the surface lists and the calls
  the new commands replace.

---

## Slice 0 — the transport budget (APPLIED 2026-09-22)

**Goal:** make the request budget a known, adequate number on **both** transports,
so every later call is judged against a true deadline.

**What was actually wrong** (two independent paths, one diagnosis each):

| Path | Who uses it | Was | Now |
| --- | --- | --- | --- |
| HTTP `hdaw-http` → `127.0.0.1:18765/mcp` | the OMP harness (this session) | no `timeout` in the repo-root `.mcp.json`, `OMP_MCP_TIMEOUT_MS` unset → **OMP's 30 s default** | `"type":"http"` + `"timeout":900000` committed in `.mcp.json` |
| stdio `hdaw` (lazy-mcp → `mcp-launch.bat`) | the pi chain | documented `requestTimeout: 900000` + `healthMonitor.idleTimeout: 0` **absent** (file mtime predated the 2026-09-15 fix) → **10 000 ms default** | re-applied (backup `servers.json.bak-20260922-pre-timeout-fix`) |

The original plan's "10 s per-call timeout" was therefore **wrong for the live path**
by 3×, and its "exit-42" mechanism was wrong outright. The conclusion is unchanged:
both budgets are far below the ~11 min a render-bearing batch needs.

**Never** set `timeout: 0`/`requestTimeout: 0`: HTTP/SSE carry no socket-idle
timeout, so an unresponsive engine would block the agent indefinitely.

**Success gates**
- [x] `.mcp.json` is valid JSON with `hdaw-http.timeout == 900000` and `type == "http"`
      (verified; `git diff` shows 3 insertions).
- [x] `~/.config/lazy-mcp/servers.json` carries `requestTimeout: 900000` +
      `healthMonitor.idleTimeout: 0` at both scopes (verified via `jq`-equivalent read).
- [ ] **Still open — the empirical one:** with the engine up, a deliberately long but
      healthy call (full-length `mix_report {wait:true}` on a ≥300 s render) completes
      *after* 30 s with the **same engine pid** before and after (`engine_info`). This
      is the only check that proves the client actually re-read the config; a config
      the running client never reloaded is indistinguishable from an ignored one.
      Requires `/mcp reload` or a new session first.
- [ ] Record the measured cut-off in `docs/testing-mcp.md` (the transport table
      currently records the configured value, not a measurement).

**Pitfall gates:** lesson 29 (a timeout is a discarded connection + relaunch, exit
0/1 — **not** 42); "config on disk ≠ config in effect" (lesson 15's family: verify
the thing that runs, not the file that describes it).

## Slice 1 — `mix_verdict`: async + per-role tuning gates

**Goal:** make `mix_verdict` the Mix Verifier's single measurement call, replacing
`analyze_tuning` × N roles (today: kick / bass / arp / lead / hats = 5 calls, each
with its own `wait:false` + poll round-trip).

**Why async is part of the fix, not a bonus:** `mix_verdict` reads and FFTs the whole
file while `mix_report` — which does the same work — already offers `wait:false`. On
a full-length render that work exceeds the transport's **30 s** default cut-off
(Slice 0), so today the tool is a footgun whose sibling tool isn't. Tuning gates make
it slower still, so async lands first. `wait` therefore defaults to **`true`**
(compat): the two pinned tests call it synchronously (`mcp_coverage_test.cpp:2285`,
`:2332`), `mix_report` sets the same precedent, and flipping it would hand
`{jobId, state:"running"}` to every existing caller as if it were a verdict — a worse
failure than the timeout it avoids. Callers opt in with `wait:false`.

**Measured 2026-09-22 (revises this slice's urgency, not its shape):** `mix_verdict
{fromPlan:true}` on a **303 s / 87 MB** render returned all six gates in **1.0 s** —
the whole-file FFT is far from the 30 s deadline, so the async retrofit is a
robustness nicety here, not a live hazard. What *does* exceed the deadline on this
project is `export_audio` (303 s render), `auto_gain_tracks` (11 solo renders) and
`auto_gain_to_target` batches — all of which dropped the client socket while the
engine kept working (`docs/testing-mcp.md` → "Long calls", added 2026-09-22). If
Slice 1 is reordered, the async conversion is better spent on the **render-bearing**
tools than on the verdict.

**Files**
- `src/common/MixVerdict.{h,cpp}` — new optional 9th parameter `tuning` (a
  `QJsonObject`) + `tuning` gate rows built from the shared `analyzeTuning`,
  **skipped when empty** exactly like `structureAudit` (`MixVerdict.cpp:81`). This
  is the only shape that keeps the strict pin safe: `mcp_coverage_test.cpp:2319`
  asserts `EXPECT_EQ(rpc.payload.toObject(), v)` — **MCP-vs-RPC equality, not a
  golden key set** — so a gate that both surfaces compute identically (or both skip)
  cannot break it. Note the `modulation` gate is supplied **unconditionally** by both
  call sites today (`McpTools_AudioRead.cpp:501-502`, `Router_Audio.cpp:204-205`),
  so `tuning` must be opt-in to keep the no-args payload unchanged.
- `src/mcp/McpTools_AudioRead.cpp:434` — add `wait` (default `true`) + `tuningRoles`
  to the schema; the async branch is the existing 4-line convention (capture **by
  value**): `const bool wait = a.value("wait").toBool(true); if (!wait) { const int id
  = McpJobs::instance().submit("mix_verdict", [captures]() { … }); … }` → reply
  `{jobId, state:"running", pollWith:"audio.jobStatus"}` (`mix_report` is the exact
  precedent at `:579-591`; `scan_plugins` is the third precedent, `wait` defaulting
  to **false** — do NOT copy that default here).
- `src/frontend/router/Router_Audio.cpp:164` — mirror the arg names; **`audio.jobStatus`
  already exists** (`:153-162`, and `audio.mixReport`'s `pollWith` is already the
  string `"audio.jobStatus"`), so this slice adds **no RPC method** and the route's
  `-32602` contract is already pinned (`missing or non-numeric param: jobId`,
  `unknown jobId`). Reuse the `buildPayload` lambda pattern (`:139-143`) so sync and
  async cannot drift.
- `src/frontend/FrontendRpc.h` — **no change**: `audio` already exists (`:37`); a new
  sub-method under an existing namespace needs no constant and no
  `allMethodNamespaces()` entry (`rpc_namespace_coverage_test.cpp` only covers
  namespaces).
- `docs/skills/psy-song-session/roles/mix-verifier.md` — surface list += `mix_verdict`;
  step 5 becomes "run the tuning gate via `mix_verdict {fromPlan:true, tuningRoles:[…]}`"
  (the role file currently names neither `mix_verdict` nor the audits its own
  procedure calls — a doc defect this slice closes).

**Success gates**
- [ ] `mix_verdict` without `tuningRoles` returns a payload with an unchanged key set
      — asserted by the existing `EXPECT_EQ(rpc, mcp)` pins still passing
      (`mcp_coverage_test.cpp:2285`, `:2332`).
- [ ] `mix_verdict {tuningRoles:["kick","bass"]}` adds exactly `gates["tuning_kick"]`
      / `gates["tuning_bass"]` with the same measurement fields `analyze_tuning`
      reports for the same file+role (assert equality against the tool's payload).
- [ ] Async contract, mirroring the pinned precedent
      (`mcp_jobs_test.cpp:137-185`, `tuning_rpc_test.cpp:189-213`,
      `mcp_coverage_test.cpp:2245-2279`): the `wait:false` reply returns in <100 ms
      with `state=="running"` and `pollWith=="audio.jobStatus"`; after polling,
      `state=="finished"` and `status.result == syncPayload`; a bogus jobId is the
      existing `-32602` on both MCP (`poll_job`) and RPC (`audio.jobStatus`).
- [ ] gtest only. Suites: `McpCoverageTest` (the two `MixVerdict*` tests), `McpJobs*`,
      `RpcParityRatchet.*`; no new test file needed if the above extend existing cases.
- [ ] **Blast radius:** no `processBlock` / DSP / `RoutingManager` / render / playback
      file in the diff. (Graph: `buildMixVerdict` depends only on `MixReportJson` +
      `MixReportAnalyzer` + `analyzeBlast`; consumers are `registerAudioReadTools` and
      `dispatchAudio`. Slice 1 touches no engine file at all.)

**Pitfall gates:** Gate 2 (unimplemented path — the tuning gate must be asserted
against `analyze_tuning`'s own numbers, not just present); lesson 29 (async is the
mitigation, not an optimisation); lesson 25 (a silent render makes every tuning
verdict vacuous — keep the existing `audible` gate first and report tuning as
`inconclusive` when `rms <= 1e-4`).

## Slice 2 — `tone_verity`: per-track modulation row

**Goal:** close the Layer Agent's "modulation exists on this layer" self-gate
(`roles/layer-agent.md:115-128`) without a second tool call, using the payload that
already exists.

**Files**
- `ProjectCommands::ToneVerityResult` (`src/common/ProjectCommands.h:1044-1072`) —
  carry the row: the payload builder takes **only the result**
  (`src/common/ToneVerity.cpp:435`), so the engine must put the filtered row into the
  result (e.g. a `QJsonObject modulationRow` + `bool hasModulationRow`) — the builder
  cannot see the ValueTree. Adding a builder *parameter* instead would change the
  signature at both call sites (`McpTools_AudioRead.cpp:656`, `Router_Audio.cpp:63`);
  prefer the result field.
- `ProjectCommands::ToneVerityParams` (`:1022-1036`) — optional `includeModulation`.
- `src/common/ToneVerity.cpp:448-476` — emit `modulation` unconditionally only when
  the row is populated (the existing `error` key at `:477-478` is the precedent for a
  conditional key). Current key set: 28 unconditional + `error`.
- Engine `verifyTone` (`AudioEngineCommands_Composition.cpp:1770`) — select
  `tracks[]` by `trackId == p.trackIndex` from `modulationCoverageJson(...)` and copy
  it into the result; **no extra render** (the function already does exactly ONE solo
  `renderTrackWindow` at `:1794-1797` and already guards silence via
  `baselineAudible` + the lesson-25 error at `:1835-1837` — keep both).
- `src/mcp/McpTools_AudioRead.cpp:603` + `Router_Audio.cpp:39-64` (`audio.verifyTone`)
  — mirror the flag; `docs/…/roles/layer-agent.md` self-gate cites the row.

**Success gates**
- [ ] Without the flag the payload gains no key (the 28-key set is unchanged).
      SAFE by audit: **no test pins the `tone_verity` payload at all** (`tone_verity`
      /`verifyTone` appear only in `tone_verity_engine_test.cpp` and
      `tone_verity_test.cpp`, which assert C++ struct members; `audio.verifyTone` is
      not exercised by any test).
- [ ] **Do NOT add an auto-checked expectation.** `tone_verity_test.cpp:155-171` pins
      `expectationsChecked == 2` / `== 0`, and `tone_verity_engine_test.cpp:90` pins
      `== 1`. A new expectation is only safe while it stays NaN-sentinel-gated
      (absent ⇒ not counted); an always-on one BREAKS all three. This slice adds
      **no** expectation.
- [ ] With the flag, `modulation.trackId == trackId` and its `needsAttention` /
      `lfos{total,enabled}` / `automation{lanes,enabledLanes}` values equal the
      `audit_modulation_coverage` row for the same track (assert equality).
- [ ] **Absent-row case is asserted separately:** `modulationCoverageJson` omits
      tracks with **zero clips** (the global rule is "every SOUNDING track"), so a
      freshly created palette track has no row. The gate must distinguish
      "no row" (omit the key / report why) from "row with `needsAttention:false`" —
      a silent `{}` would read as a pass.
- [ ] Render count unchanged: exactly ONE `renderTrackWindow` per `tone_verity` call
      (assert through the existing engine-test seam — the flag must not add a render).

**Pitfall gates:** Gate 2 (the row must be asserted against
`audit_modulation_coverage`'s own values, not merely present); lessons 27/29 (never
add a render to buy a payload row); Gate 1 does not apply (no new processor state, no
rebuild path).

## Slice 3 — `select_palette` (the only new command)

**Goal:** replace the Sound Selector's palette bookkeeping (`sound-selector.md`
steps 1–4, 7–8: `set_tempo`/`set_scale`, 10× `add_track_with_fx`, ~10 chain loads,
~10 preset loads, ~10 mod presets, per-param readbacks, shortlists, `paletteTrackMap`)
with one render-free command that returns the palette record — **not** a verifier.

**Behaviour**
- Input: `{briefId?, roles:[{role, engine, pluginId?, chain?, preset?, modPreset?,
  libraryIds?}], seed?, tempo?, scaleRoot?, scaleMode?}`.
- Per role: create the track (`add_track_with_fx`/`add_fx` equivalent internals),
  load the FX chain, load the preset (`apply_preset` dispatch), stage the modulation
  default (`apply_sub_synth_mod_preset` equivalent), and pick alternates with
  `FileLibraryManager::selectPatch(libraryIds, role, seed, excludePaths, useLedger,
  method, error)` — reporting its `{path,name,libraryId,clusterId,tags,poolSize,
  usedCount,seed}` so the shortlist is reproducible (lesson 31). The ledger is the
  JSON file `AppData/HDAW/libraries/patch_selection_ledger.json`, keyed
  `"<role>|<libraryIds joined by ,>"` (`FileLibraryManager.cpp:1607-1608`) — it
  resets when the role pool is exhausted, and `usedCount`/`seed` are the evidence.
- **ONE** transaction (`beginTransaction` … `endTransaction`) and **ONE**
  `rebuildRoutingGraph()` at the end (the `addInstrumentPart` shape; `generateArrangement`
  is the multi-track reference), NOT per track (lesson 6 + performance rule 1).
  Track/node creation batches at the LIST level — `clearNotes`
  (`AudioEngineCommands_Midi.cpp`) is the idiom to copy (lesson 30).
- **No render.** Audibility stays with `audition_patch` / `audition_plugin`
  (`trackIndex < 0`); the Sound Selector's FORBIDDEN list already bars note writes,
  which is why `tone_verity` cannot verify a palette-time track.
- Returns `palette` + `paletteTrackMap` with the exact fields `sound-selector.md`
  step 8 demands (trackIndex, instrument, committed preset/chain, 2–3 alternates,
  modulation default, audition evidence placeholder) plus `seed` / `usedCount` per
  role, in one undo unit — persisted into the **Song Brief** `palette` /
  `paletteTrackMap` (`docs/skills/psy-song-session/brief.schema.json`, stored
  verbatim in the `SONG_PLAN` node) so the record survives a reload and the layer
  agents can consume it.
- RPC twin: `composition.selectPalette` — the **name-derived** mapping means
  `tools/rpc_parity_map.mjs` classifies `select_palette` as `mapped` automatically
  (`camel("select_palette") == "selectPalette"`, generator rule 1 at
  `tools/rpc_parity_map.mjs:17,96,197`). No `ALIASES` entry needed; no
  `FrontendRpc.h` change (`composition` exists, `:41`).

**Success gates**
- [ ] Live processor assertion, not ReadModel: after `select_palette`, every role's
      track exists **in the audio graph** (`getMainProcessor()->getTrack(idx)`)
      with its instrument FX slot present — the Gate 1/10 discipline. Seed tracks
      0..N explicitly (lesson 9: the default project ships zero tracks), and call
      `engine.drainPendingRoutingRebuild()` before reading live processors.
- [ ] **One** `rebuildRoutingGraph()` for the whole call (count it in the test, or
      assert via the incremental-path instrumentation) — not one per role.
- [ ] **One** undo unit: a single `undo` reverts the entire palette (track count,
      FX slots, param writes) — asserted on the live processor after undo.
- [ ] Variety: two calls with the same seed return the **same** picks; two calls
      with seed 0 (time-seeded) differ; the ledger never repeats a patch until the
      role pool is exhausted, and `usedCount`/`seed` are reported.
- [ ] Parity: `composition.selectPalette` payload identical to the MCP payload;
      argument-name twin test asserts the **same failure text/class** on both
      surfaces for a bad role.
- [ ] Ledger: `node tools/rpc_parity_map.mjs` regenerates cleanly (expect **297
      MCP tools / 385 RPC methods** — the header counts are a **comment, not an
      assert**, so the real gate is G1) and `RpcParityRatchet.*` is green: G1
      `EveryLiveToolIsClassified` (`rpc_parity_ratchet_test.cpp:78` — fails until the
      ledger carries the new tool), G2 `NoStaleLedgerRows` (`:95`), G3
      `MappedTargetsResolveOnTheDispatchSurface` (`:110`), G4
      `EveryNonMappedRowHasAReason` (`:139`), G5 `ReviewQueueIsReported` (`:156`).
      `RpcNamespaceCoverage` needs **no** change (no new namespace), and
      `mcp_server_test.cpp:2756` (`ToolSurfaceRegistersCuratedTools`) is SAFE — its
      curated list is a superset check plus `EXPECT_GT(names.size(), 200)` (`:2774`),
      and it does not enumerate every tool. Never rename the curated entries (the
      launcher string-checks `audit_song_structure`).
- [ ] **Zero renders:** no `renderTrackWindow` / `export_audio` / child spawn in the
      call path (assert no proxy child is spawned).
- [ ] `docs/skills/psy-song-session/roles/sound-selector.md` surface +=
      `select_palette`; steps 2–4/7 collapse into it.

**Pitfall gates**
- Gate 1 / lesson 10 (rebuild restores track+slot state — assert on the live
  processor after the rebuild the command itself triggers).
- Gate 2 (fully implemented path: preset load verified by the returned patch name,
  the FM/Virus-family trap where a load "queues" without applying).
- Gate 6 / lesson 20/21 (no render ⇒ no child-spawn storm; keep it that way).
- Lesson 9 (explicit track seeding in tests), lesson 30 (batch at the LIST level;
  do not fire the listener per child), lesson 31 (variety discipline), lesson 23
  (param writes stay on the clamped `set_fx_param` / `set_internal_fx_param` path).
- Gate 12 does not apply as long as the one rebuild uses the existing command-layer
  path (which already handles the pump park); if the implementation calls
  `graph.clear()`/connect directly, Gate 12 applies in full.

## Steps

0. Slice 0: re-apply + verify the lazy-mcp override (or record the decision to stay
   at 10 s and hold every new call under it).
1. Slice 1: `wait:false` via `McpJobs` reusing the **existing** `audio.jobStatus`
   (no new RPC method); then the optional `tuning` gate as a 9th `buildMixVerdict`
   parameter (skipped when empty, like `structureAudit`); then the role-file surface
   update. The payload-pinning audit is already done (see Slice 1 gates) — just keep
   the two `EXPECT_EQ(rpc, mcp)` pins green.
2. Slice 2: `tone_verity` modulation row carried through `ToneVerityResult` (the
   builder cannot see the tree); no new expectation, no new render.
3. Slice 3: `select_palette` (new command + RPC + MCP tool + ledger regen + tests).
4. `node tools/rpc_parity_map.mjs` after Slice 3 (G1 fails until the ledger has the
   new tool), then `graphify . --update`, then the full targeted suite sweep.
5. Update `AGENTS.md`'s composition-toolkit / parity notes and the role files only
   where this plan says so (no new convention is introduced — the
   JSON-payload-built-in-`src/common` rule is unchanged).

## Open questions for the user

1. **Slice 0, the one thing I could not verify:** the applied configs are on disk and
   valid, but neither has been proven *in effect* — that needs the engine up and a
   >30 s call with a pid check (Slice 0's last open gate). Do you want me to run it
   (start the engine + a long `mix_report`), or will you let the next real session
   prove it?

**Decided (was question 2):** `wait` keeps defaulting to **`true`** — see Slice 1's
rationale (pinned sync callers; `mix_report` precedent; async stays opt-in).
