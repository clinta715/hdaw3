# Incremental Routing — Task 4 Complete (default flipped ON)

> Successor to `docs/handoffs/2026-08-15-incremental-routing-task2-3-complete.md`.
> Predecessor commits: `f2af89a` (Tasks 2/3). This handoff records Task 4
> (latency/quality gates + flip default). The incremental-routing feature is
> now the **default** path for clip mutations; the full-rebuild path survives
> behind `HDAW_FORCE_INCREMENTAL_ROUTING=0`.

## What changed

| File | Change |
|------|--------|
| `src/engine/AudioEngine.cpp:63-73` | `initialize()` lambda flipped: unset/empty env → **ON**; `0`/`false`/`FALSE` → OFF (escape hatch); log message updated. |
| `src/engine/AudioEngine.h:194` | `incrementalEnabled_ = true` (declared default consistent). |
| `src/engine/MainAudioProcessor.h:44` | New `getRoutingGraphLatencySamples()` (const, inline) — `AudioProcessorGraph::getLatencySamples()` readback for the T4-G1 latency probe. |
| `tests/unit/engine/incremental_routing_engine_test.cpp` | `FlagPlumbingReadOnceAtStartup`: `flag("")` case flipped `EXPECT_FALSE` → `EXPECT_TRUE` (unset now = ON); comment updated. |
| `tests/unit/engine/incremental_routing_ab_test.cpp` | NEW (T4): `ProduceWavPair` (A/B render, max diff = 0) + `RealDeviceLatencyStable` (latency triple). |
| `tests/CMakeLists.txt` | Registers `incremental_routing_ab_test.cpp`. |

## Gate results (measured, 2026-08-15)

- **T4-G1 latency on real device** (`IncrementalRoutingAB.RealDeviceLatencyStable`,
  Focusrite USB Audio, active console session): device output latency
  `3840→3840` samples (and `441→441` on rerun), graph latency `0→0`, output
  channels `2→2`; OFF-path (full-rebuild) graph latency `0`. **No topology
  latency shift** — incremental vs. full rebuild equivalent (lesson 7).
- **T4-G2 quality A/B** (`ProduceWavPair`): the same edit sequence (6
  overlapping crossfaded clips + move-into-overlap + last-position remove) was
  rendered through the **production `ExportManager` pipeline** from both the
  incremental-path tree and the full-rebuild-path tree. **max diff = 0
  (bit-identical)**; the user A/B-listened both WAVs on the Focusrite and
  confirmed they sound fine (no clicks/pops/DC/phase).
- **T4-G3 default flipped**: unset/empty `HDAW_FORCE_INCREMENTAL_ROUTING` →
  incremental ON; `=0`/`false`/`FALSE` → full-rebuild path.
- **T4-G4 suite + build green with default flipped**: `cmake --build build
  --config Debug` succeeds; `hdaw_tests.exe` full suite **849/849 PASSED**
  (163 suites, ~8 min), 5 pre-existing DISABLED, 0 failures. Every unflagged
  engine test now exercises the incremental path by default and still passes.
  Incremental suites (16): Spike 3 + RemoveMove 3 + Engine 7 + AB 2 + Bake 1,
  all PASS.

## Constraints / contracts now in force

- **Default is incremental ON.** `HDAW_FORCE_INCREMENTAL_ROUTING=0` (or
  `false`/`FALSE`) is the kill-switch to the pre-existing full-rebuild path
  (`handleAsyncUpdate` → `rebuildRoutingGraph`), reproduced exactly
  (`FlagOffPathIsUnchangedFullRebuild`).
- Flag is read **once** at `AudioEngine::initialize()` — tests that need a
  specific mode must set the env var before `initialize()` (`ScopedIncrementalFlag`).
- Incremental-safe ops: append-adds, last-position removes, and edits/moves of
  those clips (incl. last-position cross-track moves — `FlagOnCrossTrackMoveEquivalent`);
  structural ops (middle inserts/removes, track ops, slicing, load, tempo,
  stretch) force `forceFullRebuild_`.
- Every `RoutingManager` mutation under `graphLock` (+ pump-park off the
  message thread); the drain is the message-thread no-park path. SPSC clip-param
  push preserved on top of the incremental crossfade recompute.

## Known deviations / notes

- T4-G1's "burst of 128 incremental adds" was originally run as a **32-clip
  batched burst** because 128 per-op commands were a lesson-6 time cliff
  (pump thread ~80 s CPU). **Fixed** (2026-08-15): `UpdateKind::none` on
  incremental graph mutations + one end-of-batch `graph.rebuild()` reduces
  128 per-op drain to ~1.6 s. The `IncrementalRoutingBake.OneRebuildPerBatch128Ops`
  test regresses this. The latency/equivalence claim is proven at both 32
  (batched) and 128 (per-op with deferred bake).
- `tests/CMakeLists.txt` is an explicit source list (no GLOB) — new test files
  must be registered or they silently never compile.

## Handoff to next session

The incremental-routing feature (Tasks 1-4) is **complete and shipped by
default**. Remaining follow-ups are standing items, not gates:
~~lesson-6 incremental-routing improvement (avoid N message-thread bakes for
per-op commands)~~ **DONE** (2026-08-15): `UpdateKind::none` on incremental
graph mutations + one end-of-batch `graph.rebuild()`. 128 per-op drain now
completes in ~1.6 s (was ~80 s). See
`docs/handoffs/2026-08-15-incremental-routing-perop-bake-followup.md`.
The per-run pipe/shm namespace guard for plugin-isolation tests (lesson 20)
is still outstanding. Version bump (0.20.0) and release notes are separate
work — see `README.md` feature history.