# HDAW — agent working agreement

**MANDATORY:** before ANY code change in this project, invoke the `hdaw-guard` skill
(`.pi/skills/hdaw-guard/SKILL.md`). Plan-first, pitfall gates, dependency analysis.
Non-negotiable for every task.

**Sound-engine stability rule:** bug fixes to the sound engine proceed without prior
discussion; transparent/reversible/low-blast-radius perf improvements too. Changes
touching `processBlock`, DSP chains, render/export, playback paths, plugin isolation,
or internal/external FX contracts require discussion with the user FIRST, with effort
+ risk notes. Rendering and playback stability outrank new features.

**Current scope:** JUCE 8 desktop DAW, v0.37.0, React 19 + TS frontend (Zustand,
Vite). Engine state via JSON-RPC 2.0 over WebSocket (8766) + HTTP (8765); bundled
SPA or Electron shell. Feature history: `README.md`; per-version changes: git log.

## Documentation map

| Doc | Contents |
| --- | --- |
| [`docs/lessons-learned.md`](docs/lessons-learned.md) | **All 31 lessons, full narratives** (one-line index below) |
| [`docs/architecture.md`](docs/architecture.md) | Build details, key classes, GUI-engine decoupling, beats-vs-seconds |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-thread rules, hardening, plugin isolation, latency/quality |
| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | JUCE pitfalls (scan blacklisting, setProperty no-op, FX clamping) |
| [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md) | Frontend pitfalls (stale closures, optimistic placement) |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | Listener contract, delta-sync limits |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | gtest suite, TransportLoopback seam, MCP architecture |
| [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) | Core-synth CLAPs: devices, patch pipelines, per-engine status (§9) |
| [`docs/psytrance-composition-guide.md`](docs/psytrance-composition-guide.md) | Composition recipes via MCP |
| [`docs/composition-toolkit.md`](docs/composition-toolkit.md) | Generative/randomization/modulation toolkit overview |
| [`docs/build-and-testing.md`](docs/build-and-testing.md) | Build traps (ninja_deps, suppressed regen), sharding, housekeeping |
| [`docs/postmortem-silent-clap-export.md`](docs/postmortem-silent-clap-export.md) | Canonical multi-cause writeup (lessons 11-15) |
| [`docs/handoffs/`](docs/handoffs/) | Session handoffs (completed-work context, not live specs) |
| [`docs/plans/`](docs/plans/) | Current plans |

## Knowledge graphs

**graphify** (`graphify-out/graph.json`, queryable via `graphify query/path/explain`,
MCP `query_graph` etc., `GRAPH_REPORT.md` for God Nodes) — FIRST tool for blast
radius and code discovery. Kept current by a post-commit hook + `--watch`.
Query, don't rebuild (`graphify update .` only when stale). Never invent an edge;
verify with grep. The graph is a snapshot — cross-check critical paths.

**Refresh gotcha (measured 2026-09-22):** on this box `graphify update .` **fails** —
`graphify.exe` is a trampoline that re-execs `python …\Scripts\graphify`, an
extensionless shim that does not exist (`can't open file '…\Scripts\graphify'`), while
`query`/`explain`/`path` work fine in-process. Run the module directly instead:
`python -m graphify update . --force` (the interpreter is recorded in
`graphify-out/.graphify_python`; `--force` is required when a rebuild yields fewer
nodes). The post-commit hook's **detached `watch` rebuild is cache-driven and will not
pick up new/changed files on its own** — after a hook rebuild of a tree containing new
code, 0 of the new symbols were in the graph; the explicit `update --force` extracted all
1318 files and added them (20601 → 20695 nodes). Run the explicit update once when the
tree has new files and the cache is warm afterwards (a later hook rebuild then keeps
them: 20873 → 20963 with `InternalDelay` intact). Verify with `graphify explain
<newSymbol>` before trusting a "rebuilt" log line.

**codebase-memory** MCP — semantic index for "where is X implemented" questions.

## Lessons learned (one-line index — full narratives in [`docs/lessons-learned.md`](docs/lessons-learned.md))

1. **Beats vs seconds is the #1 data-convention bug source** — every boundary crossing converts.
2. **`setProperty` is a no-op on unchanged value** — drive the manager directly or nudge.
3. **`processBlock` must early-out when transport is stopped** — else audible buzz.
4. **Delta-sync can't compute derived state** — mute/solo escalates to fullSync.
5. **`projectEndSample` goes stale on SPSC timing edits** — recomputed in processBlock.
6. **`rebuildRoutingGraph()` is O(project)** — use the incremental path; batch slicing at model level, rebuild once.
7. **Every engine change affects latency** — measure before/after, verify PDC.
8. **Every engine change affects fidelity** — A/B critical listening, check denormals.
9. **Default project ships ZERO tracks** — tests create every track they use; never assume baselines.
10. **Routing rebuild must restore track state** — assert on the LIVE processor, not the ReadModel.
11. **Non-GUI processes MUST start the message pump before JUCE construction** — else silent export + shutdown hang.
12. **Graph mutation from non-message threads parks the pump** (MessageManagerLock, guarded).
13. **DSP-state writes hold `stateLock`** — listeners race prepareToPlay recreation.
14. **Cross-process boundaries truncate and race** — chunk big payloads, bounds-check both sides, hold graphLock on handle swaps.
15. **Stale flags and stale binaries lie** — verify the binary, not the source; play() consumes pending auto-stop.
16. **CLAP lifecycle calls run on the host's main thread** — marshal in the child; render threads pass the thread check.
17. **Audio-device init degrades to output-only** — and device errors log to OutputDebugString, never stderr.
18. **Never instantiate plugins while the pump is parked** — two-phase rebuild.
19. **The CLAP audio thread is the thread running process()** — record real thread ids, never "not X".
20. **Orphaned plugin hosts block the proxy tests** — check for live engines first; unique namespace prefixes prevent collisions.
21. **Render sequence pins the old graph after clear()** — synchronous re-bake closes the handshake; respawn budget ends storms.
22. **WASAPI never calls CoInitialize itself** — ScopedComInit first in every entry point.
23. **Internal FX params clamp at EVERY entry point** — one unclamped value poisoned exports at exactly 0.6 s.
24. **Audition/session states persist through autosave** — verify the SAVED project before diagnosing a render.
25. **A silent render makes every A/B equal** — prove audibility first; never value-initialize a patch buffer dumped verbatim.
26. **Isolated-child bulk state travels via SHM ring, not the control pipe** — log the failure branch of every bounded send; verify against the child's report.
27. **Audit renders are tree copies into fresh children** — live-only writes aren't inputs; parent-local readbacks prove nothing; respect variance floors.
28. **Bare plugin identifiers ('Vavra.clap') resolve against the scan DB** — a .clap suffix is not a path; log the whole load failure branch.
29. **Check the exit code before debugging a crash** — 0x2A (42) = the deliberate
    `engine_restart` tool (`McpTools_Engine.cpp`), and ONLY that: a request
    timeout never produces 42 — it discards the connection and relaunches the
    engine onto a **fresh empty project** (exit 0/1). That timeout is lazy-mcp's
    `requestTimeout`, **default 10 s**, live for the hdaw server; verify the
    override is truly present in `~/.config/lazy-mcp/servers.json` (it has been
    observed missing) — see `docs/testing-mcp.md`. Arm WER LocalDumps; batch
    small, save often.
30. **Batch tree surgery at the LIST level** — removeAllChildren fires the listener per child; swap the container node.
31. **Patch selection needs a variety mechanism** — deterministic ranking repeats; select_patch = cluster-stratified + seeded + ledger.

## Performance rules: batch RPCs, walk the tree incrementally

1. **Consolidate RPC calls — one batch, not N loops.** Every engine mutation fires
   root listeners synchronously; one batched call = one delta + one rebuild + one
   undo unit. N calls = N round-trips and N rebuilds.
2. **Prefer incremental deltas over full re-serialization.** fullSync only for
   restructure/non-clip entities. Derived state (`effectiveMuted`) can't delta.
3. **Don't re-walk the whole ValueTree to touch one node** — indexed access /
   `getChildWithProperty` / held references.

## Feature parity: MCP + RPC (GUI parity not required)

Any user-facing capability MUST be reachable via MCP **and** the frontend JSON-RPC
surface (`namespace.method` dispatched in `src/frontend/router/Router_<Domain>.cpp`,
namespace constants in `src/frontend/FrontendRpc.h`, gated by
`RpcNamespaceCoverage`). Where both surfaces shape the same artifact, put the logic
in `src/common/` — identical payload by construction, not by discipline (worked
examples: core-synth device map, mix_report payload, ParamVerity/ToneVerity).
**Argument names are part of the contract** — mirror the MCP tool's property names
exactly; give each route a twin test asserting the same failure on both surfaces.
Adding a tool requires `node tools/rpc_parity_map.mjs` — the ratchet gate fails
otherwise. GUI parity is NOT required; the agent/MCP surface ships first.

## Composition toolkit (full overview: [`docs/composition-toolkit.md`](docs/composition-toolkit.md))

- **Generative**: PhraseGenerator styles, chord/progression generation, rhythm
  patterns + corpus phrase bank, Markov percussion, humanize/randomize, per-track
  LFO system, song plan + cells (`set_song_plan`/`fill_cells`/`reroll`,
  `params.tileBeats` for long sections).
- **Hardware VA suite**: OsTIrus/Osirus/Vavra/Xenia/JE8086/NodalRed2x/Dexed as
  isolated CLAPs with real firmware (ROMs in `C:\Program Files\Common Files\CLAP\`).
  Patches via `apply_preset` (front door: dispatches by slot + file header) or
  `load_virus_preset` / `load_je8086_preset` / `load_nord_bank`; matrix movement via
  `list_matrix_presets` / `apply_matrix_preset`. **pluginId arguments are BARE names
  ('Vavra.clap') and must resolve against the scan DB (lesson 28).** Loader status is
  evidence-gated per engine — see hardware-va-suite.md §9; confirm via
  `get_fx_capture_status` + a render, never the param list alone.
- **Corpus + variety**: patch libraries ingest sidecar metadata;
  `related_samples` (deterministic ranking) for "find similar";
  **`select_patch`** (cluster-stratified, seeded, ledger-excluded) for "give me a
  different one"; `param_verity_corpus` to audit a slot's parameters;
  `sweep_dx7_patches.py --engine vavra_plugin` to collect dsp vectors.
- **Source material (dev box)**: samples `E:\samples`, MIDI `E:\midi`, patch banks
  `D:\pdf\{Virus Presets,je8086,microwave,NL2x Banks}` and
  `D:\pdf\rhythm-lab.com_waldorf_micro_q` — counts, sidecar state, which packs are
  genre-relevant, and the register-per-pack rule:
  [`docs/psytrance-composition-guide.md`](docs/psytrance-composition-guide.md) §2
  ("Source material locations").
- **Modulation-first**: device's own matrix → onboard FX → HDAW automation/track
  LFO → HDAW internal FX → third-party plugin last.
- **Verification-first**: `param_verity` (audibility), `tone_verity` (envelope/pitch/AM),
  `mix_report {fromPlan:true}` (structure + loudness gates) — verdicts are
  deterministic; never trust "it should work".

## Build

- Configure/build: `cmake --build build --config Debug` (or `build-fast.bat [test|all]`)
- Outputs: `build/HDAW.exe`, `build/HDAW_headless.exe`, `build/hdaw_tests.exe` (flat Ninja layout)
- **Do NOT run `build/Release/HDAW.exe`** — stale binary.
- After editing `CMakeLists.txt` (adding sources/targets): re-run
  `cmake -S . -B build` explicitly — suppressed-regeneration trap.
- Never hard-kill a build (truncates `.ninja_deps` → full rebuild).
- **Frontend:** `cd frontend; npm run build`, then rebuild the C++ project.
  Full details of the traps: [`docs/build-and-testing.md`](docs/build-and-testing.md).

## Disk housekeeping

`scripts/cleanup-stale.ps1` reclaims stale scratch on this dev box: crash dumps +
debugger symbol caches, `%TEMP%` (HDAW param traces, `hdaw_debug.log`, render WAVs,
engine copies, `hdaw_crash_captures\engine_*`), agent chat logs (pi / omp / opencode
/ codex), and re-downloadable caches under `-Aggressive` (`-ModelCache` for
HuggingFace). **Dry-run by default** — `-Apply` deletes. Files held open by a
running process are reported `LOCKED`, which is what protects a live engine's
`hdaw_paramtrace_<pid>.log` (lesson 29's "save often" is the companion habit).
`scripts/cleanup-stale-db.mjs` (`-AgentDb`) is the sqlite companion for
`~/.local/share/opencode/opencode.db` — `VACUUM` reclaims the freelist (`auto_vacuum`
was 0; 6.3 GB of dead pages), `--days N` prunes sessions through the FK cascades,
and it refuses to write while another process holds the DB.

Two invariants when editing either script: `%TEMP%\hdaw_crash_captures` and its `wer`
child are **protected dirs** (`scripts/crash-diag.ps1` registers `wer` as WER's
DumpFolder), and the capture-tree sweep globs `engine_*` only — a bare
`hdaw_crash_captures\*` matched `wer` and deleted it.

## Shell: PowerShell only (no `&&` or `&`)

Windows PowerShell 5.1 — `&&`/`&` are invalid separators. Use `cmd1; if ($?) { cmd2 }`,
`Start-Job { ... }`, or the `workdir` parameter on tool calls. Update bash-legacy
`&&` in docs on sight.

## How frontend changes reach the running app

| Run mode | To pick up frontend changes |
| --- | --- |
| Packaged Electron (`frontend/release/win-unpacked/HDAW.exe`) | **Repackage:** `frontend\build.bat` — app.asar is frozen |
| Browser standalone (`build/HDAW.exe`) | `frontend\build.bat` (forces C++ rebuild when `dist/` newer) |
| Vite dev server (`npm run dev`) | Hard-refresh (Ctrl+Shift+R) |

The packaged app's ENGINE comes from `build/RelWithDebInfo/` — repackage by hand
only after building it. Full table: [`docs/build-and-testing.md`](docs/build-and-testing.md).

## Testing

- **C++ engine (gtest):** `build/hdaw_tests.exe` (`build-fast.bat test`; `all` also
  builds `hdaw_plugin_host.exe` for the isolation suites). Filter:
  `--gtest_filter=Suite.*`. Fast tier: `run_fast_tests.bat`. Full serial baseline
  2026-09-23: **1865 tests / 277 suites — 1825 pass, 39 skipped, 1 failure**:
  `RespawnPath.RealPathPassesThrough`, a deterministic PRE-EXISTING red test
  (Windows path normalisation in `tests/unit/proxy/crash_recovery_test.cpp`, an
  untouched file — full analysis in `docs/testing-mcp.md`). Two more
  environment-dependent hazards are documented there too:
  `PluginIsolation.LargeStateRoundTripThroughProxy` is the historical solo-pass
  flake, and `McpServer.HttpRoundTrip` binds a **fixed port 18765**, so it fails
  whenever a live engine holds it (measured 2026-09-22: 4 failures with an engine
  on the port, 2 with it free). The earlier "1 flake" note (2026-09-21) is stale.
- **Deviceless pattern:** suites needing an audio route fail with `getTrack() ==
  nullptr` when no device — environmental, don't blame your change (lessons 9/17).
- **Frontend (Vitest):** `cd frontend; npm test` · **E2E (Playwright):**
  `npm run test:e2e` — auto-starts engine + Vite; `workers: 1`; clip-position
  assertions must poll with `expect.toPass()`.
- **Engine change test discipline:** identify affected gtest suites before
  finishing; new RPC method/command with no coverage → add a gtest. Full details:
  [`docs/build-and-testing.md`](docs/build-and-testing.md).
