# Handoff: plugin-slot param persistence — A + B + C shipped and gated (2026-09-21)

**STATUS (end of session, 2026-09-21): A, B and C are DONE and gated; version bumped
to 0.37.0 and the tree is commit-ready.** This header supersedes the "B half-built /
C not started" framing of the original title (kept below as the working record).

- **A (durability)** — G1/G2/G3-G5 green, G6 real Vavra monotonic, G7 drain trace.
- **B (opt-in live probe)** — G8 fast tier + G8 real Vavra
  (`ledger(AmpVolume=0) -> 0.0034411 rms, raw live(AmpVolume=1) -> 0.0289078 rms,
  usedLiveParamState 0 -> 1`), MCP + RPC parity asserted on both surfaces.
- **C (honesty)** — the four false-claim gate headers corrected (plus the dump gate's
  "LIVE render" wording), five live docs + two live plans corrected, and the gates
  strengthened rather than merely reworded: `NodalRed2x` Cutoff effect is now
  **asserted** (3.1-8.2x separation, self-calibrated against a measured same-input
  noise floor), `Xenia`'s effect is **not resolvable** (spread > separation) and is no
  longer claimed, `OsirusBootPatchAwakening` is framed as the ROM boot-patch guard,
  and `VavraHostParamsLiveReachability` asserts host-side staging plus the parent-side
  flush trace (`P1 stageParam` -> `P3F FLUSHED`).
- **G9** — `FxMidiInjection.*` real-plugin sharded 4-way: **22/23**. The one failure
  (`XeniaEditBufferDumpChangesOfflineRender`, `export failed: Could not create output
  file`) is a **cross-shard temp-file collision**, not a regression — the same test
  passes solo in 25 s. Registry/RPC sweep: **123/123**.

**Two open items:** (1) `%TEMP%\hdaw_render_<trackIndex>_<counter>.wav` is not
process-unique, so sharded runs can collide — the one-line fix (pid in the name) is in
the render path and needs sign-off; (2) backlog #4 (RPC-parity retrofit + the inert
`param_N` decision for RPC `project.setFxSlotParam` on plugin slots).

Written 2026-09-21. Self-contained: what the session established, what is in the
working tree (uncommitted), what is left, and the traps.

Plans this handoff tracks (both are in the repo, uncommitted):
- `docs/plans/2026-09-21-vavra-live-param-delivery.md` — **DIAGNOSED** (root cause + trace evidence; supersedes the earlier Vavra-specific reading).
- `docs/plans/2026-09-21-plugin-param-persistence.md` — the A + B + C implementation plan, gates G1–G10.

---

## 1. The finding (why any of this exists)

Live host-param writes to a **plugin** FX slot reach the **LIVE isolated child
only** and are never persisted into the tree. Every render the audit surface
uses — `audition_plugin`, `verify_part`, `export_audio` — is an **offline export
of a tree copy into a FRESH child** (`renderTrackWindow` →
`ExportManager::startExport`), so a live-only write is invisible to it and to
save/load.

This is **not Vavra-specific**. There is no emulator/wrapper delivery defect: the
trace proved the write lands (`P1 stageParam` → `P3F FLUSHED` → child `C1 SET` →
`WARM` → `DRAINED` → `IDLE clock`). The earlier "Vavra live-path gap" was a
**measurement-harness artifact** (the render A/B compared different children, and
the evidence came from a fresh-child boot-noise delta).

Consequence for the user goal (transparency): the documented
`appliesVia: set_fx_param` route for Vavra/JE8086/OsTIrus **did not do what it
said**. A is the fix.

User decision (2026-09-21): ship **C + A + B**.

---

## 2. Shipped in the working tree — A (durability) — gates PASSED

Uncommitted (all of HDAW's changes are uncommitted; nothing pushed).

| File | Change |
| --- | --- |
| `src/common/ParamOverrideLedger.{h,cpp}` **(new)** | one `idx=val;idx=val` grammar: `formatParamOverride` / `parseParamOverrides` / `mergeParamOverride` / `removeParamOverride`; `ParamOverridePair = std::pair<int,float>` |
| `src/engine/ExportManager.cpp` | `parseAppliedParamOverrides` now delegates to the shared parser (signature unchanged — a frozen test seam) |
| `src/mcp/McpTools_Matrix.cpp` | `writeAppliedParamOverrides` uses `formatParamOverride` (still **replace** semantics) |
| `src/common/ProjectCommands.h` | 3 new virtuals: `setPluginParam`, `clearPluginParamOverrides`, `getPluginParamOverrides` |
| `src/engine/AudioEngineCommands_Fx.cpp` / `.h` | the impl (live write + ledger **merge**, 0..1 clamp, live-cache bounds guard when a slot exists) |
| `src/mcp/McpTools_FxSlot.cpp` | `set_fx_param` plugin branch → the shared command, returns `ok overrides=N`; **new tool `clear_fx_param_overrides`**; `list_fx_params` gains an `overridden` flag |
| `src/frontend/router/Router_Plugin.cpp/.h` | `dispatchPluginParam` takes `AudioEngine&`; `pluginParam.setParam` → the shared command (slot resolved by `pluginID`); `getParams` gains `overridden` |
| `src/frontend/router/Router_Project.cpp` | new RPC `project.getPluginParamOverrides` / `project.clearPluginParamOverrides` (RPC parity for the MCP clear tool) |
| `src/proxy/PluginProxySlot.{h,cpp}` | *B groundwork*: `hostWritten_` bitset set by `stageParam`; `getHostWrittenParams()` |
| `src/engine/TrackFXSlot.h` | *B groundwork*: `getLiveHostWrittenPluginParams()` (delegates to the proxy; empty for internal/in-process) |
| `src/engine/AudioEngineCommands_Composition.cpp` | `renderTrackWindow(..., bool seedLiveParams = false)` + the seeding block at `~:459` — **compiles, not yet driven by any caller** |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | new source + test registrations |

Tests added: `tests/unit/common/param_override_ledger_test.cpp`,
`tests/integration/mcp/plugin_param_persist_test.cpp`, plus
`FxMidiInjection.VavraHostParamPersistedWriteAffectsExport`.

### Gates that actually ran and PASSED

| Gate | Result |
| --- | --- |
| G1 `MatrixPresetsTest.*` | **12/12** (replace semantics + parse seam preserved) |
| G2 `ParamOverrideLedgerTest.*` | **11/11** |
| G3/G4/G5 `PluginParamPersistTest.*` | **6/6** — incl. `EXPECT_EQ(ledgerViaMcp, ledgerViaRpc)` (parity by construction) |
| G6 real Vavra end-to-end | **1/1** — `base=0.0173994`, `AmpVolume=0 → 0.00357402`, `AmpVolume=1 → 0.00841921` (monotonic) |
| G7 child drain >256 | **PASSED via trace** — `C1 DRAINED calls=256 pr=256 pw=256` **then** a second chunk applying `idx=256…`; pre-fix the tail was silently dropped while `paramSetReadPos` advanced past it |

Commands:

```powershell
build\hdaw_tests.exe --gtest_filter=ParamOverrideLedger.*
build\hdaw_tests.exe --gtest_filter=MatrixPresetsTest.*
build\hdaw_tests.exe --gtest_filter=PluginParamPersistTest.*
$env:HDAW_REAL_PLUGIN_TESTS='1'
build\hdaw_tests.exe --gtest_filter=FxMidiInjection.VavraHostParamPersistedWriteAffectsExport
```

Contract kept: `apply_matrix_preset` = **replace** the whole ledger (one preset
per render); `setPluginParam` = **merge** ONE index. Do **not** reroute
`apply_matrix_preset` through `setPluginParam` — it would break the replace gate.

---

## 3. IN PROGRESS — B (opt-in live-state probe). Compiles; ~40% wired.

Intent: let a windowed render reflect **what you currently hear** (live-only
writes that were never persisted), **opt-in and default OFF**, so the default
probe stays tree-derived and matches a real `export_audio`.

Done (compiles, `build-exit=0`):
- `PluginProxySlot` `hostWritten_` tracking + `getHostWrittenParams()`
- `TrackFXSlot::getLiveHostWrittenPluginParams()`
- `renderTrackWindow(..., bool seedLiveParams = false)` merges live host-written
  pairs into each plugin slot's `appliedParamOverrides` on the **tree copy**
  (`src/engine/AudioEngineCommands_Composition.cpp:459`)

Left (the whole surface, nothing of this exists yet):
1. `ProjectCommands::AuditionParams` — add `bool liveParamState = false;` (near
   `src/common/ProjectCommands.h:828`).
2. `ProjectCommands::AuditionResult` — add `bool usedLiveParamState = false;`
   (near `:840`) so a caller can never mistake the two modes.
3. `AudioEngineCommands::auditionPlugin` — pass `params.liveParamState` into its
   `renderTrackWindow(...)` call (`src/engine/AudioEngineCommands_Composition.cpp:1334`)
   and copy `r.used...` into the result.
4. `renderTrackWindow` itself should report whether seeding happened (add a
   `usedLiveParamState` out-flag to `RenderWindowResult`).
5. MCP `audition_plugin` — add the `liveParamState` argument + schema
   (`src/mcp/McpTools_CompositionInstrument.cpp:191`).
6. RPC `composition.auditionPlugin` — same argument
   (`src/frontend/router/Router_Composition.cpp:987`); it builds `AuditionParams`
   at `:1009`.
7. **G8 gate**: with `liveParamState=true`, a raw
   `PluginParamService::setParam(...)` write (no ledger) changes the window; with
   the default (false) it does not. Also assert the same **before/after** a
   `clear_fx_param_overrides` (proves the two channels are distinguishable).

Do **not** make `seedLiveParams` default true anywhere. The default probe must
stay tree-derived (that is the honesty property this whole plan exists to
protect).

---

## 4. NOT STARTED — C (honesty pass)

The old harness produced false claims. Correct them; do not preserve them.

### 4a. Gate headers in `tests/unit/engine/fx_midi_injection_test.cpp`

| Test (line) | The false claim | The truth (measured) |
| --- | --- | --- |
| `XeniaHostParamsChangeRender` (:854) | claims a moved host param "audibly changes the live render" | the render is a **fresh export child**; the Δ was **5.1e-4** boot noise (the claimed 6.1e-3 does not reproduce). The write goes to the live child only (trace-confirmed). |
| `VavraHostParamsLiveReachability` (:996) | "two renders of the UNCHANGED patch (same child) land in one of two modes" → a same-child bimodality story | **false premise**: each render is a different child. What the gate actually proves is S1–S4 delivery to the LIVE child (which is real) — say that, and say that it says nothing about renders. Replace the parent-local-cache "reachability" assertion with the real flush evidence (`P1 stageParam` → `P3F FLUSHED` → `C1 SET` → `C1 DRAINED`). |
| `NodalRed2xHostParamsChangeRender` (:1841) | same shape as Xenia | Δ = **2.7e-4** = fresh-child boot noise, not a param effect |
| `OsirusBootPatchAwakening` (:1430) | frames the awakening as a `setParam` effect | it is the **ROM boot-patch fix** (load factory A-0 into the edit buffer); the gate only asserts `rms > 0.001` on the first audition. Reword, do not delete the gate (it is a real regression guard). |

### 4b. Docs

- `docs/hardware-va-suite.md` §9 — the Vavra `set_fx_param` row and the
  "host params don't move renders" reading. Now: **`set_fx_param` (and its RPC
  twin `pluginParam.setParam`) persists through `appliedParamOverrides`, so it
  does reach `export_audio` / `audition_plugin` / `verify_part`.** Note the
  opt-in live-state probe (`liveParamState`, default OFF) and the
  `clear_fx_param_overrides` escape hatch.
- `docs/hardware-va-suite.md` line 25 (the "F-A … load_virus_preset queues but
  does NOT change Osirus renders" line) — reconcile with lesson 26 / the §9
  correction already recorded; make sure the Vavra clause no longer reads as a
  delivery failure.
- `docs/core-synths-agentic-guide.md` — the "third structural limit" section.
- `docs/psytrance-composition-guide.md` — the FX/param persistence recipe.
- `docs/skills/psy-song-session/roles/fx-automation-engineer.md` — same.

### 4c. Also update

- `docs/plans/2026-09-21-plugin-param-persistence.md` — status + a §0 Progress
  table with the gate numbers **is already updated** (this session). Keep it in
  sync as B/C land.

---

## 5. Remaining gates + commit plan

- **G8** live-state probe gate (see §3.7).
- **G9 regression sweep** (not yet run for this slice):
  - `FxMidiInjection.*` with `HDAW_REAL_PLUGIN_TESTS=1` (~600 s serial; 4 shards ≈ 307 s via `scripts/run-tests-parallel.sh 4 FxMidiInjection`)
  - registry/RPC-surface sweep (`McpCoverageTest` / `ToolRegistry*` / `DeviceParams*` / `DeviceParamsRpcTest*` / `PsyFmRpcTest*`) ≈ 162 tests / ~426 s
  - `MatrixPresetsTest.*` + `PluginParamPersistTest.*` + `ParamOverrideLedgerTest.*` (already green)
  - full suite (~13 min) **before delivery**
- **G10** docs (§4).

**Commit:** both repos. HDAW currently has 19 modified + 7 untracked files
(including this handoff).
One caveat: `AGENTS.md` shows as modified but that change is **not mine** (a
harness/environment doc update about the graphify MCP tools) — decide with the
user whether to include it or leave it out of the slice commit.
gearmulator (`D:\pdf\gearmulator-git`) holds only intentionally-untracked scratch
(`.clap.bak-*`, `build_*_clap*.bat`, `probe-out/`, `source/jeTrace.h`,
`jeStateProbe/`, `jeUserPatchProbe/`) — **do not commit** unless asked.

---

## 6. Traps for the next session

- **`IDs` is a GLOBAL namespace**, not `HDAW::IDs` (`src/model/ProjectModel.h:8`).
  `HDAW::IDs::…` does not compile.
- **The RPC namespace is `pluginParam`, not `plugin`** (`method::PluginParam`,
  `src/frontend/FrontendRpc.h`). Plugin-param methods are `pluginParam.getParams`
  / `pluginParam.setParam`.
- **`set_fx_param` on a plugin slot with an unresolved instance**: the live param
  list is empty, so the range check is skipped and the write is **persisted
  only** (deliberate — lets a param be pre-staged, and the replay reports
  out-of-range entries as `skippedBeyondCache`). Non-empty list ⇒ range-checked.
- **Fixture plugin ids resolve to no live instance.** `add_fx {pluginId}`
  creates a plugin-typed slot; `PluginParamService::getParams` returns `{}`, so
  live-path assertions are impossible without a real plugin. Unit-level tests
  must exercise the command layer / RPC surface; MCP↔RPC live parity belongs in
  a real-plugin gate.
- **`renderTrackWindow` has 7 internal call sites** (`:925`, `:955`, `:986`,
  `:1004`, `:1334` audition, `:1418`, `:1426`). `seedLiveParams` is last with a
  default, so they all still compile — but only the audition path should ever
  pass `true` unless a caller explicitly wants live state.
- **Real-plugin gates need `HDAW_REAL_PLUGIN_TESTS=1`**; a skipped gate looks
  green. Vavra gates ~10–14 s each plus a 12 s OS warmup.
- **Lesson 25 — prove audibility first.** Always assert a non-silent baseline
  (`rms > 1e-4`) before judging a param delta; the Osirus silence manufactured a
  false "does not change the render" conclusion.
- **Fresh export children vary ~2× in level on identical input** (flagged
  separately as an export-determinism fidelity item). Do not build a
  single-render A/B on a small Δ; use the monotonic ≥2× separation the Vavra
  gates use, or repeated windows.
- **Trace tooling:** `HDAW_TRACE_PARAM=1` → `%TEMP%\hdaw_paramtrace_<pid>.log`
  (parent `P1/P2/P3E/P3F`, child `C1`). Verified present in both
  `build\hdaw_tests.exe` and `build\hdaw_plugin_host.exe`.
- **Build:** `CMAKE_SUPPRESS_REGENERATION=ON` ⇒ a new `.cpp` needs an explicit
  `cmake -S . -B build` (helper:
  `C:\Users\hapbt\AppData\Local\Temp\opencode\configure_and_build_both.bat`);
  otherwise `build_tests_fast.bat`. One build at a time. Flat Ninja output at
  `build\hdaw_tests.exe` (no `build/Debug/`). A truncated `build/.ninja_deps`
  turns a no-op build into ~285 s — delete it.
- **The isolated child's drain is now chunked** (256/chunk). If you touch
  `PluginHost.cpp`'s param drain again, keep the invariant: **`paramSetReadPos`
  must never advance past an entry that was not applied.**
- **`appliedParamOverrides` writes trigger NO graph rebuild** (the FX_SLOT
  listener early-returns for anything but `param_` / `psyFmMatrix` /
  `psyFmSweepRate`). Safe and cheap; keep it that way.

---

## 7. Next move (ordered)

1. Finish B wiring (§3.1–3.6) and add the **G8** gate.
2. Do the C honesty pass (§4a test headers first — they encode false claims;
   then §4b/4c docs).
3. Run **G9** (focused sweep, then full suite).
4. Keep `docs/plans/2026-09-21-plugin-param-persistence.md` §0 in sync as B/C land;
   then commit both repos (ask about `AGENTS.md`).
5. Then backlog **#2 — RPC parity retrofit**:
   `docs/plans/2026-09-21-rpc-parity-retrofit.md`, suggested order
   `Router_Matrix` → `Router_Rave` → `Router_Pool` → `Router_Tuning`.
   One item inside it was found this session: RPC `project.setFxSlotParam` still
   writes the **inert** `param_N` for plugin slots, while MCP `set_fx_param`
   routes plugin slots to the param service. Decide: error / redirect / document.

---

## 8. Deviation note

`hdaw-guard` mandates subagents for implementation; subagents are disabled by the
user's global config, so this session worked inline. Graphify MCP tools were not
exposed in the session, so blast-radius analysis used grep + direct reads.
Both deviations are also recorded in `docs/plans/2026-09-21-plugin-param-persistence.md` §6.
