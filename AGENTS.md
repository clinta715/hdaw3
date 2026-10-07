# HDAW — agent working agreement

**MANDATORY:** before ANY code change in this project, invoke the `hdaw-guard` skill
(`.pi/skills/hdaw-guard/SKILL.md`). Plan-first, pitfall gates, dependency analysis.
Non-negotiable for every task.

**Sound-engine stability rule:** bug fixes to the sound engine proceed without prior
discussion; transparent/reversible/low-blast-radius perf improvements too. Changes
touching `processBlock`, DSP chains, render/export, playback paths, plugin isolation,
or internal/external FX contracts require discussion with the user FIRST, with effort
+ risk notes. Rendering and playback stability outrank new features.

**Current scope:** JUCE 8 desktop DAW, v0.39.2, React 19 + TS frontend (Zustand,
Vite). Engine state via JSON-RPC 2.0 over WebSocket (8766) + HTTP (8765); bundled
SPA or Electron shell. Feature history: `README.md`; per-version changes: git log.

## Documentation map

| Doc | Contents |
| --- | --- |
| [`docs/lessons-learned.md`](docs/lessons-learned.md) | **All 51 lessons, full narratives** (one-line index below) |
| [`docs/architecture.md`](docs/architecture.md) | Build details, key classes, GUI-engine decoupling, beats-vs-seconds |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-thread rules, hardening, plugin isolation, latency/quality |
| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | JUCE pitfalls (scan blacklisting, setProperty no-op, FX clamping, the `small`/`rpcndr.h` include-order macro collision, lesson 35) |
| [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md) | Frontend pitfalls (stale closures, optimistic placement) |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | Listener contract, delta-sync limits |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | gtest suite + TransportLoopback + MCP architecture + environmental-failure/flake catalog |
| [`docs/mcp-server-ops.md`](docs/mcp-server-ops.md) | MCP server ops/lifecycle |
| [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) | Core-synth CLAPs: devices, patch pipelines (per-engine status: [`docs/va-suite-status-log.md`](docs/va-suite-status-log.md)) |
| [`docs/va-suite-status-log.md`](docs/va-suite-status-log.md) | Per-engine VA-suite status |
| [`docs/psytrance-composition-guide.md`](docs/psytrance-composition-guide.md) | Composition recipes via MCP |
| [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) | Psytrance production + VA-suite traps |
| [`docs/composition-toolkit.md`](docs/composition-toolkit.md) | Generative/randomization/modulation toolkit overview |
| [`docs/build-and-testing.md`](docs/build-and-testing.md) | Build traps (ninja_deps, suppressed regen), sharding, housekeeping |
| [`docs/postmortem-silent-clap-export.md`](docs/postmortem-silent-clap-export.md) | Canonical multi-cause writeup (lessons 11-15) |
| [`docs/handoffs/INDEX.md`](docs/handoffs/INDEX.md) | Handoff index — chronological table, newest supersedes older statements |
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

**Freshness badge caveat (measured 2026-09-24):** `graphify_status` reports **STALE**
whenever the working tree has uncommitted changes — even *immediately* after a full
rebuild — because with the index-metadata file missing it falls back to a git heuristic
that compares HEAD against the working tree, not the tree the graph was built from. A
`python -m graphify update . --force` re-extracted 1279 files (2026-09-24: 21,285 nodes /
36,799 edges / 987 communities; edges up from 36,601, node count slightly down — hence
`--force`) and the badge still said STALE with the same 21 dirty files. Do not re-run the
update in a loop on the badge alone: verify content currency with `graphify explain
<newSymbol>` or MCP `get_node` — a symbol from an uncommitted file proved the rebuild had
taken the working tree. A clean tree reads FRESH. That CLI refresh also rewrote
`graphify-out/.graphify_root` back to `.` (see the marker gotcha below), so re-apply the
absolute path after every update.

**MCP marker gotcha (measured 2026-09-24):** the `dsh-graphify` DSH plugin resolves
`graphify-out/.graphify_root` **against `graphify-out/` itself**, while graphify's own
readers resolve it against the run-time **CWD**. A relative marker (`.` — what
`watch.py::_graphify_root_marker_value` writes for a relative `graphify update .`)
therefore resolved to `graphify-out`, and every MCP graph query failed with a doubled
path: `graphify-out\graphify-out\graph.json`. The marker now holds this repo's
**absolute** path, which both readers resolve correctly. Because a code rebuild rewrites
the marker verbatim from the `update` argument, a later `python -m graphify update .
--force` can reintroduce `.`; if graphify MCP reports the doubled path again, rewrite
`graphify-out/.graphify_root` with the absolute repo path and reload the profile.

**Patched locally (2026-09-24):** the plugin's `lib/detector.js` now resolves a relative
marker against the **scanned project directory** instead of `graphDir`, which is the
correct semantics — exercised directly against this repo with the marker set to `.`, it
returns the project root. Two consequences: the edit lives in
`~/.dsh/profiles/web/node_modules/dsh-graphify/lib/`, so it is **lost on any
`dsh-graphify` upgrade and must be re-applied**; and it only loads when the harness
restarts, because a live patch reload does not bust Node's ESM module cache. The absolute
marker above is the standing second line of defence while the patch is not loaded.

**codebase-memory** MCP — semantic index for "where is X implemented" questions. Advisory
only: graphify stays authoritative for structure/blast radius. Installed globally
(`npm i -g codebase-memory-mcp@0.11.0` — the npm postinstall must be allowed
(`--allow-scripts=codebase-memory-mcp`) or the 301 MB native runtime is never
downloaded), wired into the DSH profile's `cordis.patch.yml`, and checked with
`index_status` before trusting results.

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
32. **An enabled Volume automation lane owns the parameter in the offline render** — audit isolation must use mute, not `setTrackVolume`.
33. **A per-slot loop must not clear a SHARED chain buffer** — only the last sampler slot survived; first engaged sampler replaces, later ones accumulate (`samplerPreserve_`). Property-only health reads (`hasSound`) can mask a no-sound state.
34. **An accepted-arg-dropped key is a silent no-op** — every parse shape must parse every key (`automation_preset` sections silently dropped `cycles`/`midPoint` → near-silent breakdown); assert the observable effect, not the success payload.
35. **A Windows macro can collide with a JUCE enum name** — `rpcndr.h`'s `small`→`char` vs `BadgeIconType{small}`; a Qt-then-JUCE include order breaks the build. Split the piece: Qt-light header + JUCE-heavy `.cpp` (`src/common/BatchEnd.h`).
36. **JUCE undo boundaries are often deliberately UNPAIRED** — batch atomicity is a FLAG + one choke point (`AudioEngineCommands::transactionBoundary`), never a depth counter.
37. **A windowed render does not predict the full render** — measure the window OUT of a full render and promote its stats before gating (`buildWindowReportPayload`).
38. **The tool boundary has a silent-acceptance class** — refuse unknown keys and non-integral numbers with shared bytes (`requireInt` truncation, unknown expectation keys, over-stated ledger aliases).
39. **A `this`-capturing worker must be stopped and JOINED before the destructor tears anything down** — `std::jthread` joins only during MEMBER destruction (after a dtor body that already freed resources); never `detach()`; make the worker's I/O stop-aware so the join stays bounded (`PluginProxySlot`).
40. **Shared ownership guarantees the OBJECT, not the HANDLE** — lease the pipe/shm object for the exchange (`shared_ptr` from `getPipe`/`getShm`), give the handle exactly ONE closer (`~PipeServer`), and make `stop()` signal + `CancelIoEx` rather than close (a cleared handle leaks; a closed one double-closes against in-flight I/O).
41. **An exchange lock fixes CONCURRENCY, not STALENESS** — serialize each whole request→response transaction on a pipe (one guard spanning send + all replies), but a timed-out reply stays queued, and without a correlation id (and an end-of-response marker) a same-type late reply is indistinguishable from a fresh one: no drain window can be proven complete. The real fix is a per-request protocol id (landed: `requestId` in both structs + a version/legacy guard) or a timeout⇒restart policy.
42. **Two-process protocol: a version guard must be able to DECODE the reply it guards** — a stale v1 child's READY is unparseable under the new framing, so id-matching discards it and the spawn dies by bare timeout; recognise the legacy layout (or the guard is worthless). `--protocol` flags are not a fix: the old child ignores unknown args.
43. **`ScopedJuceInitialiser_GUI` is a process-wide KILL SWITCH** — its LAST teardown stops JUCE's `TimerThread` and deletes the `MessageManager`, after which the pump may no longer be the message thread and every `juce::Timer`/`AsyncUpdater` dies silently; `MessagePumpThread` now pins it (deliberately leaked).
44. **A clamped WAV export hides the true float peak** — size headroom from the RENDER (gtest buffer / `verify_part` solo peak), never the WAV; a file pinned at 1.0000 with `ceilingHitPct` means "≥1.0"; assert a property (halving `Output Level` halves the peak), not an absolute band.
45. **An N-voice instrument summed into ONE slot cannot be linearly bounded** — put a memoryless soft ceiling above the 1–2 voice range (knee above normal voices, asymptote below unity) on the VOICE SUM before the user's output-level param, so normal hits stay bit-identical.
46. **The MCP engine answering your calls may be a stale `%TEMP%` COPY with a different image name** — `taskkill /IM HDAW_headless.exe` misses `HDAW_headless_mcp.exe`; the identity fields are TRUSTWORTHY: `whoami.runningBinaryPath`/`runningMtime` and `engine_info {buildBinaryPath}`'s `stale: true` mean exactly what they say (a mute `--mcp-http` spawn that could not bind the port keeps running while the older `%TEMP%` copy answers — an earlier "false positive" note was a MISDIAGNOSIS, retracted). Settle it in one step: a spawn whose OWN log says `MCP HTTP start failed: failed to listen on ...` is not serving. Confirm by CONTENT (`tool_help` for a changed description, `list_fx_params` for a changed param count) or `Get-CimInstance Win32_Process ... ExecutablePath`; smoke via `python scripts/mcp_call.py run <steps.json>` (stdio, no port).
47. **A windowed offline render never delivers a note-on that falls BEFORE the window start** — verify one-shot parts with the window start at/after the first hit; `verify_part` requires `startBeat > 0`; a zero `soloPeak` on a part audible in the full export is a windowing artifact first, an engine bug second.
48. **A CLI value round-tripped through a settings store you cannot WRITE is silently DROPPED** — `main_headless.cpp`/`main.cpp` wrote `mcp/httpPort` into `QSettings` (unwritable on this box: a `winreg` write returns WinError 5), so `--mcp-http-port 18841` still bound the persisted 18765 while the registry never changed. Pass CLI/API-supplied values to the API DIRECTLY (never round-trip them through a store you cannot prove is writable); a config query must report LIVE state for a running server (`getMcpHttpConfig`), not a persisted snapshot; and `--mcp-http` now fails fast (non-zero exit) when the requested port is not actually served. A CLI value accepted with no effect is the lesson-38 silent-acceptance class — assert the OBSERVABLE effect, not the parse. (Secondary, NOT the cause: Qt's default `QSettings` needs a `QCoreApplication` INSTANCE for the default names to resolve — construct `QSettings(org, app)` with explicit names.)
49. **A broadband envelope follower with a global-peak threshold finds NOTHING on real material** — `SliceDetector::transient` found 0 onsets on 3 of 4 real library loops (a 140bpm drum loop with 51 hits, a hats-only loop with 26, a single hi-hat one-shot) and 23% on a glitch loop. Band-split before onset detection (a quiet element must be judged against its own band's statistics, not the global peak); never reset the envelope after a detection; test with real material, not a sustained tone.
50. **Comparing recall over a MISMATCHED analysis window produces chance-level numbers.** A ground-truth generator capped at 8s while the engine analysed 13.7s compressed the timeline 1.71x and made the recall/precision for those files chance-level. The ground-truth window MUST match the analysed window exactly; report the interior count alongside the ratio.
51. **Qt's `QTextStream::readLine()` over a piped stdin blocks until a 16 KB buffer fills, so an MCP stdio server answers nothing while the client holds the pipe open.** `McpTransportStdio`'s POSIX reader wrapped `STDIN_FILENO` in a `QFile`; Qt issued a 16 KB read and the reader sat in `read(0, …, 16237)` for 8+ s with zero engine syscalls, flushing every queued response only at stdin EOF (strace 2026-10-05). Never read a pipe through a buffering wrapper you do not control — `poll()` + read EXACTLY the available bytes (as the Windows branch already did); a bounded 50 ms timeout keeps `stopped_` live and surfaces `stop()`'s `close(fd)` as POLLNVAL/EBADF. The failure is INVISIBLE to a driver that only reads after EOF — `select()` both pipes while stdin stays open.

52. **A JUCE `AudioProcessorGraph` torn down off the message thread races its own async rebuild and corrupts `NodeStates`' `std::set` — `double free or corruption (out)` on the render thread's `clear()`.** HDAW builds the offline render graph on a dedicated thread, so `rebuild(UpdateKind::sync)` never runs inline: JUCE falls to `updater.triggerAsyncUpdate()`, and the pump's `NodeStates::applySettings` mutates `preparedNodes` UNDER `NodeStates::mutex` while `NodeStates::clear()`/`removeNode()` mutate the same set WITHOUT it (JUCE 8.0.0; `juce_AudioProcessorGraph.cpp:495`/`:1720`/`:1761`). Core-verified at `ExportManager.cpp:683` (`renderThreadFunc` → `renderGraph.clear()`). Diagnose from the log (a render that logs its last `ExportDebug Block N` and never `Export render finished` died IN TEARDOWN) and from a COMPLETE undeleted temp WAV `/tmp/hdaw_render_p<pid>_…` (audio finished, cleanup never ran); arm dumps BEFORE reproducing (`scripts/hdaw-engine-coredump.sh` — `core_pattern=core` + `ulimit -c 0` in a non-interactive shell means the default leaves NOTHING). It is a RACE, so one clean run proves nothing — `scripts/repro-render-teardown-crash.py` crashed at cycle 9 pre-fix, 600+ renders clean post-fix. Fix: drain the message queue (FIFO `CallbackMessage` probe) before `releaseResources()`, after it, and after `clear()`.
53. **A test-harness environment redirect that does NOT bind is worse than none — it reports success while the suite mutates REAL user data.** `test_main.cpp` redirected the user-data root via `USERPROFILE`/`APPDATA`/`LOCALAPPDATA` and logged "user data dir redirected to …", but those are the lever only on **Windows**; on Linux JUCE resolves `userApplicationDataDirectory` as `resolveXDGFolder("XDG_CONFIG_HOME", "~/.config")` — a FILE lookup that ignores the XDG env var, with `~` expanding from **`HOME`** (juce_Files_linux.cpp:135/:121). The redirect bound to nothing and the suite wrote into the real `~/.config/HDAW` (17 test-generated patch presets leaked into the live library), and it only redirected when the root looked *unwritable*, which on a dev box is never. ASSERT the **effective** root, not the intent you set; resolve the platform's real mechanism; make tests **hermetic by default** (always redirect, mirror reads, explicit escape hatch) — writability is not the property you want. Companion trap: a helper calling a **non-idempotent** tool twice (`mcpIsError` + `mcpValue` around `save_patch`) leaked a file per call — call mutating tools ONCE per assertion.
54. **A CLAP host that collapses N output ports into ONE summed port makes strict plugins return `CLAP_PROCESS_ERROR` — and discarding the `process()` status turns that into pure silence.** `CLAPPluginInstance` summed every output port's `channel_count` into one `clap_audio_buffer_t` with `audio_outputs_count = 1`; Surge XT (3 ports: Output/Scene A/Scene B) rejected the 6-channel layout with status **0 = ERROR** and wrote nothing, for months, invisibly. Found with a control host, not by reading HDAW: `tools/clap_min_host.c` (~340 lines of C, no JUCE) renders one plugin and prints peak **and status** — `MINHOST_OUTCH=6` -> `status=0 PEAK=0.000`, `MINHOST_MULTIPORT=1` -> `status=1 PEAK=0.47`. Rules: hand a plugin **exactly as many ports as it declared** (`audio_outputs_count == count(plugin,false)`, never 1-with-summed-width); **never discard a plugin's return status** (throttled loud log on `CLAP_PROCESS_ERROR`); "it works for the plugins I tried" is not port-contract evidence — the summed layout was a **no-op** for the 6 single-port plugins (bit-identical renders) and fatal for the one multi-port plugin that validates its layout. Companion trap: `params->flush(plugin, nullptr, nullptr)` (null event lists are illegal per spec and clap-helpers dereferences them) **segfaults** against Surge XT — pass valid empty lists. Driver: `tools/clap_port_matrix.py`; hermetic net: `tests/unit/engine/clap_port_layout_test.cpp`.

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

**Parity ledger CLOSED (updated 2026-10-06b): 328 tools / 430 methods / mapped 314 /
mcp-only 14 / unresolved 0.** (Previous entry, 2026-10-06: 325 tools / 427 methods /
mapped 311 / mcp-only 14 / unresolved 0 — the slot-scoped PATCH release added
+3 tools: `save_patch` / `load_patch` / `list_patches`, each with its
`project.*` RPC twin.) (Earlier, 2026-10-01: 319 tools / 421 methods /
mapped 305 / mcp-only 14 / unresolved 0 — the 2026-10-05 batch-tool release added
+5 tools: `add_buses` / `add_sends` / `set_fx_params` / `set_bus_fx_params` /
`set_lfo_params`.) The route-addition recipe: one shared `src/common/`
shaper both surfaces call + a twin test asserting the same behaviour on both
surfaces + `node tools/rpc_parity_map.mjs` regeneration — the ledger tracks
names/routes only, so an argument-only change needs no regeneration but DOES
need the twins.

**DEPRECATED (2026-09-23):** this parity rule still binds the **engine** surfaces —
`src/mcp/` and the JSON-RPC router in `src/frontend/router/` (namespace constants
in `src/frontend/FrontendRpc.h`, parity ledger, `RpcNamespaceCoverage` gate, twin
tests) remain live and must still be maintained. The Electron **client** is now a
separate project and is no longer a delivery target.

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
  evidence-gated per engine — see `docs/va-suite-status-log.md`; confirm via
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
  ("Source material locations"). **Linux box (2026-10-04):** `/mnt/nvme2/samples` +
  `/mnt/nvme2/midi` only — no patch banks, no VA-suite CLAPs (§2 has the counts).
- **Key-fit gate**: `key_check` (RPC `composition.keyCheck`) compares a candidate key against
  the project's current scale — it defaults to `get_scale` — and returns
  `unison | relative | parallel | consonant | neutral | conflicting` plus the pitch-class
  overlap behind the verdict (the root interval alone is not the answer: A minor ↔ C major is
  `relative`, while a tritone root sharing 4/7 of its classes is rescued to `neutral`). Accepts
  a human key (`"F minor"`), `root`+`scaleMode`, or `analyze_midi_file`'s `scaleType`, and
  **errors rather than guessing** when the candidate's key cannot be determined. This is the
  pre-listen gate at the palette step — the call that answers *"will this clash and sound
  sour?"*.
- **Modulation-first**: device's own matrix → onboard FX → HDAW automation/track
  LFO → HDAW internal FX → third-party plugin last.
- **FX-chain construction**: DEFAULT order is source → shape → glue → space →
  level (deviate for a role-specific/measured reason). On an instrument/MIDI
  track, build the instrument slot FIRST (internal synths ARE FX slots) — an
  audio-clip track has no instrument slot and starts with processing FX, and a
  bus/return uses `add_bus {fxType}`. Then
  `load_fx_chain` a factory roster chain (it PRESERVES instrument slots and
  replaces the FX slots after them) or hand-build with `add_fx` batched in one
  `begin_batch`/`end_batch` (stdio) — N `add_fx` calls are N rebuilds; MIDI FX
  (`add_midi_fx`) sit before the audio chain; shared delay/reverb goes on a
  bus/send with **Mix = 1.0**; params in REAL units (`list_fx_params {trackId,
  slotIndex}` readback — carries `valueNormalized` too — `set_internal_fx_param`
  write), addressed by `paramIndex`/`paramName`/**`intent`** (the Device
  Parameter Map's intent id; ambiguous intents are refused with candidates, and
  `unit` on a device-map row says what the number means — units/ranges are
  per-device, never portable across engines). Routed **sidechain**
  (`set_fx_sidechain`,
  compressor slots only, one source, acyclic) is the real kick→bass ducking pump —
  use it INSTEAD of a Volume-lane pump, not both. Recipe + sidechain detail:
  [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5.
- **Verification-first**: `param_verity` (audibility), `tone_verity` (envelope/pitch/AM),
  `mix_report {fromPlan:true}` (structure + loudness gates), `verify_window` (render
  the whole project, gate ONE beat window's promoted stats), `render_and_verify`
  (render + `mix_verdict`-identical verdict in one call) — verdicts are
  deterministic; never trust "it should work".
- **Archaeology + batching**: `query_notes` / `query_clips` (interval-overlap
  beat windows, absolute beats, clip-clamped spans); `set_notes_gain` /
  `set_clips_edit` (batch, one undo unit); `set_fx_params` / `set_bus_fx_params` /
  `set_lfo_params` (batched FX/bus/LFO param writes, PARTIAL-APPLY with per-write
  error rows, one undo unit); `begin_batch` / `end_batch` (long-lived
  stdio session, one named undo unit); `tool_help` (one tool's exact `tools/list`
  entry); `whoami` (engine + transport + session project + batch state).
- **Agent transport, one shared engine**: DSH-hosted agents have no `mcp()` proxy
  tool — they reach the SAME per-session engine over HTTP with
  `python scripts/hdaw_mcp_http.py {tools|whoami|desc|schemas|call|run}`
  (e.g. `call whoami '{}'`), the HTTP twin of the stdio `scripts/mcp_call.py`.
  `mcp_call.py` spawns a FRESH engine per invocation, so a role must never use
  it; and never launch a stdio engine / `mcp-launch.bat` (Windows) /
`mcp-launch.sh` (Linux) from a role — that kills
  the shared engine every other agent is attached to.
- **Time windows in either unit**: every window-taking tool/call accepts the `*Beat`
  and `*Sec` spellings plus its own key — disagreeing spellings are refused
  (`src/common/WindowUnitArgs.h`; `docs/testing-mcp.md` § "Time windows").

## Build

- **Linux (native, 2026-10-04 port):** `./build-fast.sh [test|all|debug]` or
  `cmake --preset linux` — full package list, layout, and platform deltas in
  [`docs/build-and-testing.md`](docs/build-and-testing.md) § Linux. The plugin
  isolation layer is ported (AF_UNIX SEQPACKET + shm_open), not compiled out.
- **Agent builds (Windows): use `dsh-build-fast.bat [test|all|debug]`** — sandbox-safe
  wrapper (no PowerShell/.NET calls, auto-detects VS-bundled cmake/ninja).
  `build-fast.bat` remains for human use. Same interface.
- **Ninja is the preferred generator** — the build dir is Ninja-configured;
  incremental rebuilds use direct mtime tracking (no .sln scan). `/Z7` embedded
  debug info (CMakeLists.txt) eliminates C1041 PDB contention under parallel cl.
  `dsh-build-fast.bat ninja` reconfigures (one-time).
- **sccache is OFF and stays OFF** — PCH (`target_precompile_headers`) makes
  nearly every TU non-cacheable (/Fp /Yc fingerprint mismatches). Do not enable
  `-DHDAW_USE_SCCACHE=ON` unless running a no-PCH workflow.
- **Concurrent builds are unsafe** — check before launching a build: if another
  build is running on the same `build/` dir, abort. Never hard-kill a build
  (truncates `.ninja_deps` → full rebuild).
- **Sandbox behavior (DSH ConstrainedLanguage):** native commands (`cl`,
  `cmake`, `ninja`, `git`, `python`) work fine. `esbuild`/`tsx`/`tsdown` fail
  with `spawn EPERM` (frontend builds need an unsandboxed terminal). `pnpm`
  writes outside workspace need `danger-full-access`. ACL fix for `D:\`:
  `icacls "E:\build\hdaw3" /grant "DOMAIN\user:(OI)(CI)(WO)"`.
- Configure/build: `cmake --build build --config Debug` (or `dsh-build-fast.bat`)
- Outputs: `build/HDAW.exe`, `build/HDAW_headless.exe`, `build/hdaw_tests_{engine,mcp,frontend,platform}.exe` (flat Ninja layout; layered libs `hdaw_common`/`hdaw_engine`/`hdaw_surface`/`hdaw_proxy` + `hdaw_juce`)
- **Do NOT run `build/Release/HDAW.exe`** — stale binary.
- After editing `CMakeLists.txt` (adding sources/targets): re-run
  `cmake -S . -B build` explicitly — suppressed-regeneration trap.
- **Skills mirror:** `.agents/skills/` (live tree the loader reads) and
  `docs/skills/` (doc-map mirror) must stay byte-identical — enforced by the
  `check_skills_mirror` target (`cmake/CheckSkillsMirror.cmake`, run by
  `build-fast.sh test|all`). Edit ONE tree or the other and keep both in sync.
- **Frontend:** `cd frontend; npm run build`, then rebuild the C++ project.
  **DEPRECATED (2026-09-23):** the Electron frontend is a separate project as of this
  date — do NOT build it (`npm run build`, `frontend\build.bat`). Engine-only
  verification: the four `build/hdaw_tests_*.exe` gtest binaries + the MCP surface. The source tree
  remains in-repo for reference.
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

**Native Windows 11 dev box:** MSVC (VS 18 Community), VS-bundled CMake, Ninja,
Node and Python are installed; **WSL/MSYS are NOT required by any documented
workflow**. Build from a plain shell with `build-fast.bat test|all|debug` — it
bootstraps MSVC and resolves the VS-bundled CMake itself (no PATH setup needed).
The shell is **Windows PowerShell 5.1 / cmd**: `&&`/`&` are invalid separators —
use `cmd1; if ($?) { cmd2 }`, `Start-Job { ... }`, or the `workdir` parameter on
tool calls, and update bash-legacy `&&` in docs on sight. `cmake --build` never
re-runs CMake here (`CMAKE_SUPPRESS_REGENERATION=ON`): after editing
`CMakeLists.txt`, run `cmake -S . -B build` explicitly.

**Sandbox write-denial gotcha (measured 2026-09-24):** when every `pwsh`/`bash` tool
call fails *before running* with `SetNamedSecurityInfoW failed (Win32 5):
grantWrite(<workspace>)`, the ACL runner cannot apply its Low-integrity label:
`SetNamedSecurityInfoW(…, LABEL_SECURITY_INFORMATION …)` needs the object's
**WRITE_OWNER** right, and `D:\` project directories here grant the user only `Modify`
(via Authenticated Users) — enough to rewrite a DACL as owner, but not to label it. Only
the shell (ACL restricted-token) path is affected; `read`/`write`/`edit` are not. Grant
the owner the right once per workspace — it persists in the DACL, and the runner then
provisions normally (capability ACE `S-1-4-…:(W,D,DC)`, `Everyone:(DENY)(DC)`,
`Mandatory Label\Low`, and `TMP`/`TEMP` rewritten to a private `%TEMP%\dsh-<id>`):

```powershell
icacls "<workspace>" /grant "<DOMAIN>\<user>:(OI)(CI)(WO)"
```

Directories under `%USERPROFILE%` normally carry FullControl and never hit this; any
other workspace granting only `Modify` will.

**ConstrainedLanguage consequence (measured 2026-09-24):** once the sandbox provisions
successfully, confined shell commands run in **PowerShell ConstrainedLanguage** — the
restricted token triggers PowerShell's lockdown, so `New-Object`, `Add-Type`, COM, and
other .NET type creation fail with `Cannot create type. Only core types are supported in
this language mode` (an unsandboxed shell stays `FullLanguage`). Core cmdlets, property
access, and native commands (git, python, cmake, the test binaries) are unaffected, so
builds and tests still run — but ACL/registry/WMI diagnostics, which lean on .NET types,
need an approved unsandboxed shell. This surfaced only after the WRITE_OWNER fix above:
while provisioning failed, the confined child was not genuinely restricted.

## How frontend changes reach the running app

**DEPRECATED (2026-09-23):** the Electron frontend is a separate project — do NOT
build or repackage it (`frontend\build.bat`, `npm run build`, `npm run dev`). The
table below is retained for reference; engine-only verification
(the four `build/hdaw_tests_*.exe` gtest binaries + the MCP surface) is the live path.

| Run mode | To pick up frontend changes |
| --- | --- |
| Packaged Electron (`frontend/release/win-unpacked/HDAW.exe`) | **Repackage:** `frontend\build.bat` — app.asar is frozen |
| Browser standalone (`build/HDAW.exe`) | `frontend\build.bat` (forces C++ rebuild when `dist/` newer) |
| Vite dev server (`npm run dev`) | Hard-refresh (Ctrl+Shift+R) |

The packaged app's ENGINE comes from `build/RelWithDebInfo/` — repackage by hand
only after building it. Full table: [`docs/build-and-testing.md`](docs/build-and-testing.md).

## Testing

- **C++ engine (gtest):** `build/hdaw_tests_engine.exe` + `_mcp`/`_frontend`/`_platform`
  (2026-09-29 split; `build-fast.bat test` builds all four, `test <target>` one; `all` also
  builds `hdaw_plugin_host.exe` for the isolation suites). Filter per exe:
  `--gtest_filter=Suite.*`. **Authoritative baseline 2026-09-28: 2146 tests —
  2107 passed, 39 skipped, 0 failures.** Canonical full run:
  `powershell -NoProfile -ExecutionPolicy Bypass -File run-tests-sharded.ps1 -Shards 2` (`Bypass`
  is required — the `.ps1` is unsigned), 20 min wall, every shard `ran == intended`:
  848/848, 959/959, 339/339. The earlier 2026-09-28 runs were INCOMPLETE (1814 passed, one dead
  shard) because `PsytranceComposition.PsyDubFiveMinutes` intermittently died: that was a
  **pre-existing `PluginProxySlot` worker-lifetime UAF**, root-caused with CDB (the AV re-reads
  `this->slotId` after the slot was freed) and FIXED (lesson 39), followed by the proxy pipe/shm
  lease + single-closer handle hardening (lesson 40), the pipe exchange serialization +
  callback-map snapshot (lesson 41), the protocol correlation-id completion of the staleness fix
  (lesson 41), and the JUCE message-pump ownership pin (lesson 42) — see the entries in
  [`docs/testing-mcp.md`](docs/testing-mcp.md). Superseded baseline: 2026-09-26, 287 suites /
  2027 tests, 1988 passed / 39 skipped / 0 failures, 27.1 min (shards 850/850, 855/855, 322/322).
  Fast tier: `run_fast_tests.bat`
  — the old ~3.3 min figure for it is stale (the tier is ~1900 tests at
  ~0.5-0.9 s each); use the shard runner for full-suite numbers. Every run is
  sandbox-safe because the test harness self-isolates: `tests/test_main.cpp`
  redirects temp (`TMP`/`TEMP`), the user-data root
  (`USERPROFILE`/`APPDATA`/`LOCALAPPDATA`) and `QSettings` (INI store under
  `<repo>/.tmp_tests/…`) when the defaults are unwritable. The baseline failures
  previously documented here no longer reproduce —
  `RespawnPath.RealPathPassesThrough` (the old Windows-path red, now
  platform-gated), `PluginIsolation.LargeStateRoundTripThroughProxy` (the old
  solo-pass flake, root-caused and fixed) and `McpServer.HttpRoundTrip` (the old
  fixed-port-18765 hazard) all pass in the authoritative run. The remaining
  environmental classes are catalogued in `docs/testing-mcp.md`: the sandbox
  write-denial class is handled by the harness self-redirect above, and the
  load-sensitive `McpJobs.AnalyzeTuningWaitFalsePollMatchesSynchronousResult`
  passes solo and in the full run but is timing-sensitive under heavy load.
- **Deviceless pattern:** suites needing an audio route fail with `getTrack() ==
  nullptr` when no device — environmental, don't blame your change (lessons 9/17).
- **DEPRECATED (2026-09-23):** the Electron frontend is a separate project — do NOT
  run its suites. Commands retained for reference: **Frontend (Vitest)**
  `cd frontend; npm test` · **E2E (Playwright)** `npm run test:e2e` (auto-starts
  engine + Vite; `workers: 1`; clip-position assertions must poll with
  `expect.toPass()`). Engine-only verification: the four `build/hdaw_tests_*.exe` gtest
  binaries + the MCP surface.
- **Engine change test discipline:** identify affected gtest suites before
  finishing; new RPC method/command with no coverage → add a gtest. Full details:
  [`docs/build-and-testing.md`](docs/build-and-testing.md).
