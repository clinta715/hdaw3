# Async routing-rebuild drain seam (2026-08-15)

## Goal

Replace the four flaky `juce::Thread::sleep(50)` waits in
`tests/unit/engine/audio_pool_dedup_test.cpp` with a deterministic,
self-marshaling drain seam — `AudioEngine::drainPendingRoutingRebuild()` —
so engine tests can settle the coalesced async routing rebuild before
touching live processors.

## Why safe

- **JUCE AsyncUpdater semantics (verified,**
  `build/_deps/juce-src/modules/juce_events/broadcasters/juce_AsyncUpdater.cpp`):
  `handleUpdateNowIfNeeded()` is main-thread-only
  (`JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED`), runs `handleAsyncUpdate()`
  synchronously iff pending (`shouldDeliver.exchange(0)` — exactly-once
  vs the pump's own delivery), else is a no-op.
- **No-park path:** on the message thread, `rebuildRoutingGraph` skips the
  `MessageManagerLock` (`needsPark == false`, MainAudioProcessor.cpp:609-620)
  — no deadlock, no lesson-18 plugin-instantiation-under-park trap.
- **The graph bake settles too:** `rebuildRoutingGraph` calls
  `graph.prepareToPlay()` under `graphLock` (MainAudioProcessor.cpp:631);
  on the message thread the graph's `topologyChanged(UpdateKind::sync)`
  escalates to an inline `handleAsyncUpdate()` render-sequence rebuild
  (juce_AudioProcessorGraph.cpp:1862), so node `prepareToPlay` (the pooled
  acquire) runs synchronously inside the drain.
- **Exactly-once:** the `shouldDeliver` atomic exchange means the drain and
  the pump's own dispatch never double-deliver; a no-op when nothing is
  pending.
- **Private inheritance blocked pure test-side access** (compiler-verified
  C2247/C2248 in a previous attempt: `AudioEngine` privately inherits
  `juce::AsyncUpdater`, AudioEngine.h:26, so tests cannot call
  `handleUpdateNowIfNeeded()` directly) — hence ONE minimal public method is
  the designed seam, honoring the handoff's "do NOT add engine APIs
  casually" by exposing a self-marshaling drain rather than raw internals.
  The marshal mirrors the `runOnMessageThread` precedent
  (MainAudioProcessor.cpp:36-70): null/own-thread inline fallback,
  `callFunctionOnMessageThread` otherwise; caller must not hold a
  `MessageManagerLock`.

## Changes

1. `src/engine/AudioEngine.h` — public declaration of
   `drainPendingRoutingRebuild()` (after the transport/record public block,
   before the private listener section), with the full contract comment.
2. `src/engine/AudioEngine.cpp` — implementation directly after
   `handleAsyncUpdate()`; marshals `handleUpdateNowIfNeeded()` onto the
   message thread via `callFunctionOnMessageThread` (capture-less lambda +
   local `Ctx`), inline flush when already on the message thread or when no
   MessageManager exists.
3. `tests/unit/engine/audio_pool_dedup_test.cpp` —
   - `ClipAndSamplerShareOneDecodeAcrossRebuild`: sleep → drain; comment
     rewritten to describe the seam; a second drain added immediately
     BEFORE the explicit `rebuildRoutingGraph()` so the
     addFxSlot/setSamplerSample-queued rebuild lands first (closes the
     "benign" second window; the live-processor read can no longer race a
     RoutingManager swap). No EXPECT/ASSERT changed — decode count staying
     1 across extra rebuilds is the suite's contract.
   - `EngineWiresPoolAndRebuildReacquiresWithoutRedecode`: three sleeps →
     drains (after clip adds, and after the two explicit rebuilds so the
     queued bake/updates flush before the assertions); comments updated to
     reference the drain.
4. `docs/testing-mcp.md` — new subsection "Async routing-rebuild drain
   seam" (race, seam contract, MUST-drain-never-sleep rule).
5. This plan document.

No version bump — no behavioral change, no production call sites (test
seam only). No CMakeLists changes.

## Success gates

- [x] Build: `cmake --build build --config Debug --target hdaw_tests`
  succeeds; AudioEngine.cpp and audio_pool_dedup_test.cpp recompile.
- [x] `--gtest_filter=AudioPoolDedup.*` — 11/11 pass.
- [x] Determinism: 10 consecutive runs of `AudioPoolDedup.*` — all pass.
- [x] `--gtest_filter=TrackFxRebuildRace.*:AudioGraphSurface.*` — 7/7 pass.
- [x] `rg -n "Thread::sleep" tests/unit/engine/audio_pool_dedup_test.cpp`
  → empty.
- [x] `git diff --stat src/` → exactly AudioEngine.h + AudioEngine.cpp
  (19 + 18 insertions, no other file touched under src/).
- [x] Full suite `build\Debug\hdaw_tests.exe` — 816 tests, 0 failures.

## Gate results (actual output)

- Build (target hdaw_tests):
  `hdaw_tests.vcxproj -> D:\pdf\roo projects\hdaw3\build\Debug\hdaw_tests.exe`
  (only pre-existing STL4029 shared_ptr-atomic deprecation warnings;
  `AudioEngine.cpp` and `audio_pool_dedup_test.cpp` recompiled — the binary
  carries the change, per lesson 15).
- `AudioPoolDedup.*` (single run):
  `[  PASSED  ] 11 tests.` (1535 ms total)
- Determinism, 10 runs:
  `Run 1..10: [  PASSED  ] 11 tests.` — 10/10 PASS.
- `TrackFxRebuildRace.*:AudioGraphSurface.*`:
  `[  PASSED  ] 7 tests.` (30803 ms total)
- `rg -n "Thread::sleep" tests/unit/engine/audio_pool_dedup_test.cpp`:
  no matches (rg exit code 1).
- `git diff --stat src/`:
  `src/engine/AudioEngine.cpp | 19 ++++++++`, `src/engine/AudioEngine.h | 18 ++++++++`,
  `2 files changed, 37 insertions(+)`.
- Full suite:
  `[==========] 816 tests from 155 test suites ran. (520020 ms total)`
  `[  PASSED  ] 816 tests.` (plus 5 pre-existing DISABLED tests)
  Pre-check per lesson 20: no stale `HDAW*.exe` / `hdaw_plugin_host.exe`
  processes were live before the run.
