# Plugin-slot param persistence + live-state probe (A) + honesty pass (C)

Status: **A DONE + VERIFIED (uncommitted); B IN PROGRESS (compiles, surface
unwired); C NOT STARTED.**
Owner: session 2026-09-21
Driven by: `docs/plans/2026-09-21-vavra-live-param-delivery.md` (root cause:
host-param writes reach the LIVE child only and never persist into the tree, so
every tree-copy render — export, `audition_plugin`, `verify_part` — cannot see
them; the "Vavra live gap" is not Vavra-specific).

User decision (2026-09-21): **C + A + B**.

Handoff for the remainder:
`docs/handoffs/2026-09-21-plugin-param-persistence-handoff.md`.

## 0. Progress (2026-09-21)

| Slice | State | Evidence |
| --- | --- | --- |
| A durability | **done, gates green** | G1 `MatrixPresetsTest.*` **12/12**; G2 `ParamOverrideLedgerTest.*` **11/11**; G3/G4/G5 `PluginParamPersistTest.*` **6/6** (incl. `EXPECT_EQ(ledgerViaMcp, ledgerViaRpc)`); G6 real Vavra **1/1** — `base=0.0173994`, `AmpVolume=0 → 0.00357402`, `AmpVolume=1 → 0.00841921`; G7 trace — `C1 DRAINED calls=256 pr=256 pw=256` then a second chunk applying `idx=256…` (pre-fix the tail was silently lost) |
| B live-state probe | **done, gates green** | `AuditionParams::liveParamState` (**default OFF**) + `AuditionResult::usedLiveParamState`; `renderTrackWindow(..., seedLiveParams)` merges the LIVE host-written cache (isolated proxies only) into the tree copy and reports `usedLiveParamState`; MCP `audition_plugin` and RPC `composition.auditionPlugin` both accept `liveParamState` and both report `usedLiveParamState`. G8 real Vavra: `ledger(AmpVolume=0) -> 0.0034411 rms, raw live(AmpVolume=1) -> 0.0289078 rms, usedLiveParamState 0 -> 1` (8.4x monotonic; live cache verified 0.0 -> 1.0; persisted ledger and live tree both unchanged). Fast tier `Audition.LiveParamStateFlagReportsRealityNotRequest`. A channel unregressed: `PluginParamPersistTest.*` 6/6, `ParamOverrideLedger.*` 11/11, G6 still green |
| C honesty pass | **done** | 4 test headers + their inline messages rewritten in `fx_midi_injection_test.cpp`. `VavraHostParamsLiveReachability`: the parent-local-cache "reachability" assertion is replaced by host-side staging (parent cache + `getLiveHostWrittenPluginParams`) and the **flush evidence** (`P1 stageParam` -> `P3F FLUSHED`) read from THIS process's trace file — only when `HDAW_TRACE_PARAM=1`, and it says so loudly when tracing is off (deviation from handoff §4a: the child-side `C1 SET`/`C1 DRAINED` lines go to the CHILD's per-pid trace file and `ParamTrace` caches the env flag at first use, so they cannot be asserted from the parent process). `Xenia/NodalRed2xHostParamsChangeRender`: the bogus live-render delta replaced by host-side staging + the durable `appliedParamOverrides` round trip — NodalRed2x's Cutoff effect asserted at 3.1-8.2x separation, **Xenia's effect NOT asserted** (same-input spread 0.0056 exceeded the separation 0.0023, so a threshold there would measure noise). `OsirusBootPatchAwakening` reframed as the ROM boot-patch regression guard. Docs corrected: `hardware-va-suite.md` (Vavra device row, §9 reading, capability rows 2/3, F-A reconciliation, §9 evidence-table reading), `core-synths-agentic-guide.md` (device table + structural limit #3), `psytrance-composition-guide.md`, `skills/psy-song-session/roles/fx-automation-engineer.md` |
| G9 sweep / G10 docs / commit | **done** | Fast focused tier 28 tests / 5 suites: 26 passed + 2 real-plugin skips, 0 failed. Real-plugin `FxMidiInjection.*` sharded 4-way: **22/23** — the single failure `XeniaEditBufferDumpChangesOfflineRender` was `export failed: Could not create output file`, a **cross-shard temp-file collision** (`%TEMP%\hdaw_render_<trackIndex>_<counter>.wav` is not process-unique), proven non-regression by passing solo in 25 s. **Fixed** (sign-off 2026-09-21): the target is now `%TEMP%\hdaw_render_p<pid>_<trackIndex>_<counter>.wav`, and the identical 4-shard sweep re-runs **24/24**. Registry/RPC-surface sweep **123/123** (`McpCoverageTest`, `ToolRegistry*`, `DeviceParams*`, `DeviceParamsRpcTest*`, `PsyFmRpcTest*`). G10: 6 doc locations + 2 live plans corrected (see the C row). Version bumped to **0.37.0** (`CMakeLists.txt` + `frontend/package.json` + `README.md` + `AGENTS.md`) and lesson 27 added. Open: the temp-file pid-namespacing fix (render path — needs sign-off) |

## 1. Design

### A — durability: the ledger becomes the durable channel for plugin params

`IDs::appliedParamOverrides` already exists and is already replayed into every
fresh export child by `ExportManager::replayAppliedParamOverrides`. Today only
`apply_matrix_preset` writes it. A extends the same channel to plain plugin-param
writes:

- new shared formatter/parser `src/common/ParamOverrideLedger.{h,cpp}` (parity by
  construction: `ExportManager::parseAppliedParamOverrides` and
  `McpTools_Matrix::writeAppliedParamOverrides` both delegate to it);
- new commands on the shared command layer (so MCP **and** RPC get them):
  - `setPluginParam(track, slot, paramIndex, normalized)` → live write **+** merge one
    ledger entry;
  - `clearPluginParamOverrides(track, slot)` → drop the property;
- MCP `set_fx_param` (plugin branch) and RPC `plugin.setParam` route through it;
- new MCP `clear_fx_param_overrides` + RPC `project.clearPluginParamOverrides`;
- `plugin.getParams` / `list_fx_params` report per-index `overridden` so live vs
  persisted state is visible (transparency goal);
- **latent-bug fix:** the isolated child's paramSet drain applies at most 256
  entries per pass and advances `paramSetReadPos` past the rest
  (`PluginHost.cpp` `SettledParam settled[256]`) → any bulk write >256 silently
  loses the tail. Chunk the drain so nothing is dropped (needed for bulk ledger
  replay; JE8086 applies 461 params in one preset).

Contract kept: `apply_matrix_preset` keeps **replace** semantics (its gate
asserts one entry after re-apply); the new `setPluginParam` **merges** one index.
Both write the same property; documented in the tool descriptions.

### B — live-state probe (opt-in, default OFF)

`renderTrackWindow` is used by `auditionPlugin` / `verifyPart` / `measure_*`.
Add an opt-in `liveParamState` flag: when set, seed the tree copy's plugin-slot
ledger from the **live** host-written param cache before `startExport`, so a
windowed render reflects what you currently hear.

- host-written tracking: `PluginProxySlot` marks `stageParam` indices
  (`hostWritten_` bitset) with a read accessor;
- exposed via `TrackFXSlot::getLiveHostWrittenPluginParams()` (empty for internal
  / in-process FX — only isolated proxies have a parent-local live cache);
- default OFF on purpose: the DEFAULT probe must stay tree-derived so it matches a
  real `export_audio` (a probe that silently includes live-only state would
  re-create the very trap this plan fixes); the result reports
  `usedLiveParamState` so a caller can never mistake one for the other.

Implementation contract (as shipped, 2026-09-21):

- `usedLiveParamState` is `true` **only when the copy actually absorbed ≥ 1
  live-only pair** (≥ 1 plugin slot had a non-empty live cache). It NEVER echoes
  the request: `liveParamState=true` on a slot with no live writes (fixture
  plugin id, internal FX, in-process plugin) honestly reports `false`. Gated by
  `Audition.LiveParamStateFlagReportsRealityNotRequest`.
- the merge is **last-write-wins** (`mergeParamOverride` replaces the value for a
  matching index), which is what makes "you currently hear it" beat the persisted
  ledger in the copy.
- the seed writes the **tree COPY** with a `nullptr` undo manager — a live-state
  probe must never persist itself into the live tree, and must never enter undo
  history. Both are asserted.
- **precondition: no concurrent graph rebuild.** A rebuild re-creates the FX slots
  and therefore empties the live cache (it is live-only state by design — making it
  survive a rebuild is what channel A is for). The audition path satisfies this: it
  rebuilds synchronously *before* rendering. Documented at the call site.

### C — honesty pass

Correct the false claims produced by the old harness: gate headers
(`fx_midi_injection_test.cpp`), `docs/va-suite-status-log.md`,
`docs/core-synths-agentic-guide.md` (the "third structural limit"),
`docs/psytrance-composition-guide.md`,
`docs/skills/psy-song-session/roles/fx-automation-engineer.md`. Replace the
parent-local-cache "reachability" assertion with real flush evidence.

## 2. Dependency analysis (grep — graphify tools unavailable in this session)

| Symbol | Callers / consumers |
| --- | --- |
| `setFxSlotParam` | MCP `set_fx_param` (internal branch), `Router_Project.setFxSlotParam`, `Router_Sampler`, `McpTools_Sampler`, `AudioEngineCommands_Fx` (matrix/sub-synth/DX7 apply) — **untouched** (new command added instead) |
| `PluginParamService::setParam` | `McpTools_FxSlot` (plugin branch), `Router_Plugin` (`plugin.setParam`), `McpTools_Matrix:390`, tests |
| `ExportManager::parseAppliedParamOverrides` | `ExportManager.cpp`, `matrix_presets_test.cpp` (static seam — signature frozen) |
| `appliedParamOverrides` | `McpTools_Matrix::writeAppliedParamOverrides`, `ExportManager::replayAppliedParamOverrides`, `matrix_presets_test.cpp`, `fx_midi_injection_test.cpp` |
| FX_SLOT tree listener | `AudioEngine.cpp:1249-1306` — early-returns for any property other than `param_`/`psyFmMatrix`/`psyFmSweepRate`, so the new ledger writes trigger **no rebuild** |

## 3. Pitfall scan

- **Lesson 2 (setProperty no-op):** rely on no listener side-effect — safe.
- **Lesson 6 (rebuild cost):** verified the FX_SLOT listener early-returns for
  `appliedParamOverrides`; no `rebuildRoutingGraph` on write.
- **Lesson 23 (clamping):** normalize/clamp to 0..1 at the command boundary.
- **Lessons 11/12/18 (message pump / graph mutation):** new command runs on the
  command thread, writes a tree property and stages into the proxy SHM ring — no
  graph mutation, no plugin instantiation.
- **Performance rule 1 (batch):** a UI slider drag writes one property per call;
  the existing `TreeDeltaAccumulator` coalesces. Merge is a single property write.
- **Anti-pattern:** no new engine files where an existing one fits — the command
  impl goes into `AudioEngineCommands_Fx.cpp`; only the shared ledger helper is a
  new `src/common/` file (**requires an explicit CMake configure** —
  `CMAKE_SUPPRESS_REGENERATION=ON`).

## 4. Success gates

| # | Gate | Evidence |
| --- | --- | --- |
| G1 | `MatrixPresetsTest.*` still green | replace semantics + parse seam preserved |
| G2 | `ParamOverrideLedgerTest.*` (new) | parse/merge/remove, malformed tokens, order stability |
| G3 | `PluginSlotParamPersistTest.*` (new, fixture plugin) | MCP `set_fx_param` writes+reports the ledger; `clear` removes it |
| G4 | RPC parity | `plugin.setParam` and MCP `set_fx_param` produce an **identical** ledger (`EXPECT_EQ`) |
| G5 | `plugin.getParams` reports `overridden` | new integration assertion |
| G6 | real Vavra end-to-end | `set_fx_param` on `Ch N AmpVolume` → `audition_plugin`/export is quieter (param now reaches the rendered child); non-silent baseline asserted first (lesson 25) |
| G7 | child drain >256 | `HDAW_TRACE_PARAM=1` shows `C1 DRAINED calls=>256` for a >256-entry write (pre-fix it caps at 256) |
| G8 | live-state probe | **PASSED (2026-09-21)** — real Vavra, ONE slot, two channels: persisted ledger driven to 0.0 (LOW) + raw `PluginParamService::setParam` to 1.0 (HIGH). Default render follows the ledger (`usedLiveParamState=0`, 0.0034411 rms); `liveParamState=true` follows the live cache (`usedLiveParamState=1`, 0.0289078 rms → 8.4x monotonic, well above the ~2x fresh-child noise). Structural: live cache 0.0 → 1.0 for every index, persisted ledger unchanged by the raw write, live tree unchanged by the probe. Fast tier: `Audition.LiveParamStateFlagReportsRealityNotRequest` (flag reports reality, not the request) |
| G9 | regression sweep | registry/RPC-surface sweep + `FxMidiInjection` Vavra gates green |
| G10 | docs | honest claims in the 6 locations above; no remaining "same-child"/"Xenia verified" falsehoods |

## 5. Out of scope

- Rendering the LIVE graph (no device in tests).
- Emulator/wrapper changes (proven unnecessary — delivery works).
- Export determinism (fresh-child boot variance ~2× on identical input) — flagged
  in the diagnosis doc as a separate fidelity item. **Measured 2026-09-21** (during
  the C honesty pass): Vavra within-run same-input spread **4.1e-07**
  (0.0169436 vs 0.016944; 3.3e-05 on a second run); Xenia same-input spread
  **0.0010-0.0056** on levels of ~0.05 (up to ~9%), and one render moved **~17%
  BETWEEN runs**. Consequence: a single-render A/B is only trustworthy at multi-x
  separation (NodalRed2x 3.1-8.2x, Vavra ledger 2.4x, Vavra live probe 8.4x).
  Xenia's param effect sits below its own variance, so no gate claims it.

## 6. Deviation note

`hdaw-guard` §Execution Model mandates subagent tasks for implementation;
subagents are disabled by the user's global config, so this session works inline.
Graphify tools are not exposed in this session, so blast-radius analysis used
grep + direct reads. Both deviations are recorded here.
