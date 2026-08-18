# Plan: scale ExportManager render-bake timeout with project size

## Goal
Fix the spurious `export failed: Render graph bake timed out after 15000ms - export aborted.`
failure on large projects by making the bake-wait timeout in
`ExportManager::renderThreadFunc` scale with project size, while keeping the
existing `HDAW_EXPORT_BAKE_TIMEOUT_MS` env override.

## Root cause (confirmed by runtime reproduction)
`ExportManager::renderThreadFunc` waits up to a FIXED 15 s for the
render-sequence bake, which is delivered by the JUCE message pump
(`AudioProcessorGraph`'s `LockingAsyncUpdater` → `Pimpl::handleAsyncUpdate` →
`RenderSequenceBuilder`). On a 13-track / 771-clip project the bake
legitimately takes ~17–21 s, so every export fails at the 15 s deadline.
Evidence:

- Deterministic repro: load `projects/polywave_shift.hdaw` → every
  `export.audio` fails at ~17 s with the bake-timeout message (verified 8×).
  Default 3-track project exports fine (~19 ms).
- With `HDAW_EXPORT_BAKE_TIMEOUT_MS=120000` the identical export SUCCEEDS in
  ~21 s and produces the correct 6 s WAV (1,058,504 bytes).
- Thread dump at 6 s into the export: the pump thread is legitimately inside
  `AudioProcessorGraph::Pimpl::handleAsyncUpdate` →
  `juce::RenderSequenceBuilder::build` (baking, not stuck).
- `tests/unit/engine/export_volume_bypass_test.cpp:46-49` already documents:
  "The render-sequence bake for a 771-clip graph exceeds the 15 s default bake
  window in Debug builds; the production export scripts set this to 120 s."
  The default was never fixed.
- Repeated failing exports (8×) do NOT crash the engine and leak no
  plugin-host children → the handoff's "residual engine deaths after ~6
  exports" was the now-fixed disconnect bug; this bake-timeout is the
  remaining export-failure bug (every probe render of the loaded polywave
  project spuriously fails, misread as engine instability).

## Fix
`src/engine/ExportManager.cpp`, in the bake-wait block of `renderThreadFunc`:

- When `HDAW_EXPORT_BAKE_TIMEOUT_MS` is set, honor it exactly as today
  (explicit override wins).
- When NOT set, scale the default from the project's clip count (mirror the
  `calculateProjectDuration` traversal over `treeCopy`):
  `scaled = 10000 + 50ms * totalClips`, clamped to [15000, 120000].
  - 0 clips → 15000 ms (behavior unchanged for small projects).
  - ~771 clips → ~48 s (≈2.4× headroom over the measured ~21 s).
  - Cap 120 s bounds a genuinely wedged pump so a hang still fails.
- Extract the computation as a public static on `ExportManager` (e.g.
  `static uint32_t computeBakeWaitMs(const juce::ValueTree& projectTree)`) so
  it is unit-testable. Declare it in `ExportManager.h`.

## Success Gates (all must pass with evidence)
- [ ] G1: New unit test: `computeBakeWaitMs` returns 15000 for a small/empty
      project; > 15000 for a ~800-clip project; ≤ 120000 for a huge project
      (cap).
- [ ] G2: New integration test: build a project with ~800 MIDI clips
      (`ProjectModel::createMidiClipEmpty`), `startExport` with NO env
      override, wait for completion, assert success + non-trivial output WAV.
      (On this machine this FAILS before the fix and passes after.)
- [ ] G3: `build/Debug/hdaw_tests.exe --gtest_filter='*Export*'` passes
      (ExportManager / MCP export matrix / FrontendServer export tests).
- [ ] G4: Full engine suite passes (902 tests, exit 0).
- [ ] G5: Runtime: fixed `build/Debug/HDAW.exe` (verify timestamp > source),
      no env override, load `polywave_shift.hdaw` → `export.audio` succeeds
      (the exact repro that failed before).
- [ ] G6: Diff scan — no new anti-patterns, no unrelated changes.

## Dependency Map
- Blast radius: `ExportManager.cpp` bake-wait block + `ExportManager.h`
  (one public static helper). No signature changes to `startExport`.
- Upstream callers of `startExport` (all unchanged): `McpExportTool.cpp:109`,
  `Router_Export.cpp:97`, tests.
- Downstream: `onComplete`/`onProgress` + export result shape — unchanged.
- Projections: none (no ReadModel / audio graph / frontend snapshot change).
- SPSC paths: none new — the O(clips) tree scan + timeout math run once on the
  export render thread before the wait loop (render thread already sleeps in
  the wait loop; not the audio thread).
- Env var: `HDAW_EXPORT_BAKE_TIMEOUT_MS` remains the explicit override.

## Pitfall Gates Triggered
- Gate 2 (unimplemented path): covered by G2/G3 (full RPC → export → bake →
  file write path exercised).
- Gate 4 / Gate 15 (stale binary): G5 verifies the BINARY (timestamp probe),
  not just source.
- Gate 11 (message pump): unchanged — the scaled wait still requires the pump
  to deliver the bake; a dead pump still times out (bounded by the cap).

## Anti-patterns
- None introduced. No per-loop RPCs, no `setProperty` side-effects, no new
  CSS, no processor state, no ID allocation.

## Steps
1. Edit `src/engine/ExportManager.cpp`: add `ExportManager::computeBakeWaitMs`
   (file-local logic, declared public static in the header); use it as the
   fallback when the env override is absent; update the timeout comment.
2. Add `static uint32_t computeBakeWaitMs(const juce::ValueTree& projectTree);`
   to `src/engine/ExportManager.h`.
3. Add `tests/unit/engine/export_bake_timeout_test.cpp`:
   - unit test: formula floor/scaling/cap;
   - integration test: ~800 MIDI clips, `AudioEngine` + real `ExportManager`,
     no env override, `startExport`, `waitForExport`, assert success + WAV size.
4. Add the new `.cpp` to `tests/CMakeLists.txt`.
5. Build Debug, run the new test, then `*Export*`, then the full suite.
6. Rebuild `HDAW.exe`, runtime-verify G5 against the polywave project, clean
   up any engine/plugin-host processes afterwards.
