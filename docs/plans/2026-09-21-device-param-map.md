# Plan: Device Parameter Map + `list_device_params` (core-synth param transparency)

**Date:** 2026-09-21
**Status:** complete (slice 1) — all gates pass (G1–G10)
**Related:** `docs/hardware-va-suite.md` (capabilities + §9), `docs/core-synths-agentic-guide.md`,
`docs/psytrance-composition-guide.md` §4D, `docs/plans/2026-09-16-matrix-presets.md`,
`docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md`

## Goal

Make the core-synth (hardware-VA CLAP) parameter surface usable by agents: a per-engine
**Device Parameter Map** generated from evidence that already exists, a read-only
`list_device_params` MCP tool that serves it, and playbook truth-fixes so agents stop
ignoring live param surfaces.

## Why

- All five param-bearing engines now publish host params (JE8086 461, Vavra 7557,
  OsTIrus 6939 / Osirus 3086, Xenia 2151, NodalRed2x 362) — but the agent playbooks
  still say *"Only JE8086 publishes host parameters"* (`docs/skills/psy-song-session/SKILL.md:40`),
  *"microQ/Vavra → no host parameters"* and *"Vavra → no working apply path"*
  (`roles/fx-automation-engineer.md:25,48`), and *"DT1 dumps do NOT apply"* (`:22`).
  `SKILL.md:43` is also text-corrupted.
- The only MCP surface is `list_fx_params` — a raw N-entry dump with no intent. An agent
  cannot ask "what on this device can do a filter sweep, and is it export-durable?".
- The usefulness evidence exists in five incompatible formats (matrix sheets, offset maps,
  `je8086_param_index_map.json`, `hardware-va-suite.md` §3/§7, `psytrance-composition-guide.md` §4D)
  but nothing joins them.

## Definition of "useful" encoded by the map

A parameter is useful for a job only if it is **reachable** (name/index/range) AND
**effective** (audible past the ±2% isolated-child noise floor) AND **durable** (survives
save → load → offline render via `JPAR`/`presetSysex`) AND **honest** (not a bit-alias,
no-offset ghost, or destructive op). This slice encodes reachability/route/honesty from
existing evidence and carries `audibility: "unknown"` honestly; audibility probing is slice 2.

## Scope

1. Truth-fix the stale/corrupted playbook claims (`SKILL.md`, `fx-automation-engineer.md`).
2. `timbre-lib/build_device_map.py` (deterministic generator) →
   `timbre-lib/device_map/<engine>.params.json` (schema `hdaw.device.param.map.v1`),
   plus a hand-maintained `timbre-lib/device_map/intents.json` (intent vocabulary,
   name-grammar rules, per-engine traps/overrides).
3. `src/mcp/McpTools_Device.cpp` → read-only `list_device_params`
   (index mode when `engine` is omitted; filtered engine mode otherwise).
4. Playbooks point at `list_device_params`.
5. **RPC parity** — `device.listParams` serves the same artifact with the same
   filters, via `src/frontend/router/Router_Device.cpp`, with the shared loader in
   `src/common/DeviceParamMap.{h,cpp}` so MCP and RPC cannot drift (the standing
   RPC-parity rule, AGENTS.md → "Feature parity: MCP + RPC").

## Out of scope

- Audibility probing / noise-floor sweeps (slice 2).
- Any engine/DSP/`processBlock`/render/playback/plugin-isolation change.
- Waldorf alias-collapse formalization beyond recording known traps with reasons.
- Frontend UI — no UI control for this. RPC + MCP parity only; GUI parity is
  explicitly not required (AGENTS.md).

## Success Gates (all must pass) — **ALL PASS (2026-09-21)**

- [x] **G1 generator** — `python timbre-lib/build_device_map.py` emits the 5 engine maps +
      intents index; deterministic (byte-identical on rerun); every param entry has
      `category`, `tier`, `intents`, `stages`, `source`; schema/sanity validation passes;
      prints a per-engine count summary; exits non-zero on validation failure.
      → `--check`: **"device map is up to date"**, exit 0.
- [x] **G2 registration** — `list_device_params` appears in `tools/list` with an
      `inputSchema` (gtest). → `DeviceParamsTest.ToolIsRegistered` OK.
- [x] **G3 index mode** — omitting `engine` returns `{engines[], intents[], stages[]}` (gtest).
      → `DeviceParamsTest.IndexModeReturnsVocabulary` OK.
- [x] **G4 engine mode + filters** — `category`/`intent`/`stage`/`tier`/`limit` filter
      correctly; unknown engine errors cleanly and lists available engines (gtest).
      → `EngineModeReturnsMap` / `FiltersNarrow` / `LimitTruncates` / `NoMatchHint` /
      `ErrorPaths` OK.
- [x] **G5 tests** — gtest suite `DeviceParamsTest.*` passes. → 7/7 OK.
- [x] **G6 build** — `hdaw_tests` builds clean (new `.cpp` in `src/mcp`, header decl,
      registration call, test in `tests/CMakeLists.txt`). → `build-exit=0` after an explicit
      reconfigure (see the build-trap note below).
- [x] **G7 docs** — zero remaining occurrences of the stale phrases; corrupted line gone.
- [x] **G8 no engine blast radius** — diff touches only new/2 wiring files under
      `src/mcp/`, `src/common/`, `src/frontend/` (router + method constant),
      `CMakeLists.txt`, `tests/`, `timbre-lib/`, `docs/`. No
      `processBlock`/DSP/`RoutingManager`/render/playback/plugin-isolation file.
- [x] **G9 parity** — the map is agent-reachable via the tool; playbooks reference it.
- [x] **G10 RPC parity** — `device.listParams` dispatches through `frontend::dispatch` and
      returns the same payload/filters/`matched` semantics as the MCP tool (fixture-backed
      gtest `DeviceParamsRpcTest.*`). → 4/4 OK. Also verified against the REAL corpus:
      611 params across 5 engines, correct schema/tier/route/durability.

**Gate run (2026-09-21):** `hdaw_tests.exe --gtest_filter=DeviceParamsTest.*:DeviceParamsRpcTest.*`
→ **11 tests from 2 suites, all PASSED** (exit 0).

## Dependency Map (graphify + grep verified)

- **Registration chain:** `registerAllTools` (`src/mcp/McpTools.cpp:98`) →
  `registerAudioDomain` (`McpTools_Audio.cpp:8`) → `registerFxTools` (`McpTools_Fx.cpp:8`)
  → `registerMatrixTools`. New `registerDeviceTools` declared in
  `src/mcp/McpTools_Private.h` and called from `registerAllTools`.
- **Dispatch is generic by name** (`McpServer.cpp:108-120`: `tools_.contains(name)` →
  `t.handler(args)`). No per-tool switch → stdio/HTTP/loopback need no changes.
- **Upstream callers:** none (purely additive).
- **Downstream consumers:** MCP clients (agents). No ReadModel, no audio graph, no SPSC,
  no ValueTree.
- **Path-resolution precedent:** `resolveMatrixDir()` (`McpTools_Matrix.cpp:64-114`) —
  env-first, then cwd/exeDir candidates, cached keyed on the env value. Mirror with
  `HDAW_DEVICE_MAP_DIR`.
- **RPC chain (parity):** `frontend::dispatch` (`FrontendRouter.cpp:34`) switches on the
  `namespace.method` prefix → `ns == method::Device` → `dispatchDevice`
  (`src/frontend/router/Router_Device.cpp`). Both surfaces delegate to the shared
  `src/common/DeviceParamMap.{h,cpp}` (`resolveDeviceMapDir`, `readDeviceMapFile`,
  `deviceMapEngines`, `filterDeviceParams`, `validEngineId`), so payload + filters are
  identical by construction. `frontend/src/rpc/client.ts` needs no per-method change
  (it exposes a generic call).
- **God nodes in scope:** none. **Community boundaries crossed:** none.
- **Projections affected:** none. **SPSC paths touched:** none.

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** trace name → handler → bounded file read → JSON out;
  covered by G2–G4.
- **Gate 4 (build/packaging):** add the new `.cpp` to the `src/mcp` list
  (`CMakeLists.txt`, after `McpTools_Matrix.cpp`) and the test to `tests/CMakeLists.txt`
  (~line 31). `timbre-lib/device_map` is data and, like `matrix_presets`, is not in
  `electron-builder.yml` `extraResources` — consistent with the existing matrix tools.
- **Gate 9 (validation):** reuse the `validEngineId` pattern; size-bound the sheet read;
  null-guard the engine pointer.
- **Gate 15 (stale binaries):** verify via a real gtest run; the project's current
  `hdaw_tests.exe` must exit before linking (one build at a time).
- **Not triggered:** 1, 3, 5, 6, 7, 8, 10, 11, 12, 13, 14, 16 — no ValueTree, processor,
  DSP, graph, frontend, or plugin-lifecycle code is touched.

## Anti-patterns checked

No full-tree walks, no N±1 RPC loops, no CSS. The sheet read is bounded (≤1 MiB) like
`loadJsonBounded`.

## Steps

1. `timbre-lib/device_map/intents.json` — intent vocabulary + name-grammar + per-engine traps.
2. `timbre-lib/build_device_map.py` + run it → `timbre-lib/device_map/<engine>.params.json`.
3. `src/mcp/McpTools_Device.cpp` + `McpTools_Private.h` decl + `McpTools.cpp` call + CMakeLists.
4. `tests/integration/mcp/device_params_test.cpp` + `tests/CMakeLists.txt`.
5. RPC parity: `src/common/DeviceParamMap.{h,cpp}` (shared loader),
   `src/frontend/router/Router_Device.{h,cpp}`, `method::Device` in `FrontendRpc.h`,
   the `FrontendRouter.cpp` branch, `CMakeLists.txt` entries,
   `tests/unit/frontend/device_params_rpc_test.cpp`.
6. Docs truth-fix + playbook pointers + AGENTS.md parity/build-trap rules.
7. **Explicit CMake reconfigure**, then build + run gates.

## Verification commands

```
python timbre-lib/build_device_map.py     # run twice; compare hashes (determinism)
cmake -S . -B build                       # REQUIRED: regeneration is suppressed here
cmake --build build --target hdaw_tests
build\hdaw_tests.exe --gtest_filter=DeviceParamsTest.*:DeviceParamsRpcTest.*
```

## Implementation record (2026-09-21)

- **G1 (generator) — DONE.** `timbre-lib/device_map/intents.json` (intent vocabulary +
  name grammar + per-engine overrides) and `timbre-lib/build_device_map.py` emit the 5
  engine maps + `index.json`. 611 params: je8086 52 (37 movement / 13 identity / 2 trap),
  nodalred2x 17 (10/7/0), xenia 86 (83/3/0), virus 93 (85/8/0), vavra 363 (54/22/287 —
  277 are `off_N` unnamed dump fields, 10 are FX bit-aliases). `unclassified == 0` on every
  engine; every trap carries a named reason. Byte-identical on rerun (`--check` clean).
- **G7 (docs) — DONE.** `docs/skills/psy-song-session/SKILL.md`,
  `roles/fx-automation-engineer.md`, `docs/hardware-va-suite.md` (6 spots, incl. the
  "Vavra: no host params" section and the Virus/F-A claims) and
  `docs/psytrance-composition-guide.md` corrected. Remaining stale-phrase hits are dated
  handoffs/plans that quote the superseded claims as history (intentionally not rewritten).
- **hdaw-guard deviation.** The guard mandates implementing in subagent tasks; subagents are
  DISABLED by the user's global config (`"agent": {"explore":{"disable":true},
  "general":{"disable":true}}`), so slice 1 was implemented directly in the orchestrating
  session, with this plan + success gates as the substitute control.
- **G10 (RPC parity) — DONE (standing rule).** `device.listParams` added:
  `src/common/DeviceParamMap.{h,cpp}` (shared resolver/reader/filter/validator — also hoists
  the previously duplicated `validEngineId`), `src/frontend/router/Router_Device.{h,cpp}`,
  `method::Device` in `FrontendRpc.h`, the `FrontendRouter.cpp` branch, and
  `tests/unit/frontend/device_params_rpc_test.cpp`. Both surfaces now call one
  implementation, so payload and filter semantics (`matched`/`truncated`/`hint`) cannot
  drift. An earlier draft shipped MCP-only; superseded by the user's directive to maintain
  RPC parity as a general rule.
- **Build trap found (blocked G6 on first attempt).** This tree is configured with
  `CMAKE_SUPPRESS_REGENERATION=ON`, so `build.ninja` contains **no** `RERUN_CMAKE` statement
  and `cmake --build` never re-runs CMake. The first slice-1 build compiled `McpTools.cpp`
  (its call site) but never `McpTools_Device.cpp` → `LNK2001: unresolved external symbol
  mcp::registerDeviceTools`. Fix: an **explicit `cmake -S . -B build`** before building (now
  in the verification commands). Recorded in AGENTS.md → "the suppressed-regeneration trap".
- **Gate run + regression evidence (2026-09-21).**
  `--gtest_filter=DeviceParamsTest.*:DeviceParamsRpcTest.*` → **11/11 PASSED** (exit 0).
  Regression sweep `--gtest_filter=ToolRegistry*:McpCoverage*:RpcSurface*:McpServer*:` plus the
  two new suites → **158 tests from 5 suites, all PASSED** (exit 0) — adding a tool to
  `registerAllTools` broke no registry/coverage/RPC-surface assertion. Real-corpus sanity:
  611 params / 5 engines, schemas + tier + route + durability as designed. `graphify update .`
  refreshed after the structural change.
- **Incidental finding (not slice 1) — RESOLVED 2026-09-21.** `FxMidiInjection.VavraHostParamsChangeRender`
  failed the full-suite re-run on a threshold margin (`|Δrms| = 9.99868e-06` vs a `1e-5` threshold).
  Investigation superseded the threshold framing — but the story it was replaced WITH was also wrong
  (corrected 2026-09-21, `docs/plans/2026-09-21-vavra-live-param-delivery.md`): there is no "live child
  rendering in two modes", because every render in this harness is an offline export of a TREE COPY into
  a FRESH child. The old threshold was still a **false pass** (1e-5 sat inside the harness's own
  variation — measured same-input spread 4.1e-07 for Vavra, and one Xenia render moved ~17% between
  runs), and the sibling "controls" were artifacts (Xenia's claimed 6.1e-3 does not reproduce,
  NodalRed2x's 2.7e-4 is not resolvable, Osirus's 0→0.047 is the ROM boot-patch fix). What IS true:
  the params are not dead, and the durable channel carries them —
  `VavraHostParamPersistedWriteAffectsExport` persists the write via `appliedParamOverrides` and replays
  it into every fresh export child (monotonic), while `VavraHostParamsLiveReachability` now asserts only
  host-side staging + the parent-side flush trace (`P1 stageParam` -> `P3F FLUSHED`) and claims nothing
  about renders. So there is **no live-path gap** — the earlier reading was a measurement artifact, not a
  regression from slice 1, and not an engine change.

