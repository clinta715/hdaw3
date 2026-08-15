# Streaming Handle Sharing — one reader per long file

Date: 2026-08-15. Handoff item #2 from
`docs/handoffs/2026-08-15-drain-seam-and-docs-archive.md` §3.

## Goal

Two clips referencing the same LONG (> `kPromoteToWholeFileMs` = 8 s) file
currently each open their own `AudioFormatReader` inside their own
`StreamingClipSource` (`ClipSourceProcessor::prepareToPlay` /
`switchToSourceFile`). Fix: they share ONE reader (file handle + metadata)
via a `StreamingSoundPool`, mirroring the proven `DecodedSoundPool` pattern.
Per-clip double buffers and background reader threads REMAIN per-clip —
two clips of one file can play at divergent source positions at the same
time, so a single shared window would thrash/starve. This is the HISE model
(`StreamingSamplerSound` shared per file, per-voice `SampleLoader` buffers).

## Success Gates (all must pass with evidence)

- [ ] G1: New suite `StreamingSoundPool.*` / `StreamingPoolDedup.*` passes:
      same-file → same handle + open-count 1; different files separate;
      missing file → null; refcount evict on prune; referenced entry
      survives prune; two `ClipSourceProcessor`s share one handle; engine
      rebuild reacquires without re-open (uses `drainPendingRoutingRebuild`,
      never sleeps); two non-realtime streamers on one shared handle reading
      DIVERGENT positions return correct sample data (lock serialization).
- [ ] G2: `StreamingClipSource.*`, `ClipStreamingE2E.*`, `AudioPoolDedup.*`
      and the FULL `build/Debug/hdaw_tests.exe` pass (816+ tests, no regressions).
- [ ] G3: Audio-thread path untouched: `StreamingClipSource::readNextBlock`
      unchanged in RT contract (no alloc / lock / I/O; atomics + int16 copies).
- [ ] G4: `cmake --build build --config Debug --target hdaw_tests` succeeds;
      test binary is the fresh one (Gate 4 stale-binary check).
- [ ] G5: Version 0.22.4 → 0.23.0 synced in `CMakeLists.txt`,
      `frontend/package.json`, AND `frontend/src/version.ts` (hand-sync).
- [ ] G6: Knowledge graph refreshed (`index_repository`, mode fast) after the
      change (new file + new methods).
- [ ] G7: Anti-pattern scan clean (no audio-thread lock/alloc/I/O in
      `readNextBlock`, no `DBG`, no per-clip N-call loops introduced).

## Dependency Map (verified via codebase-memory + read)

- Blast radius: `StreamingClipSource.h` (reader ownership → handle),
  `ClipSourceProcessor.h` (ctor param + streaming branch),
  `RoutingManager.{h,cpp}` (forward pool to clip procs, :509),
  `MainAudioProcessor.{h,cpp}` (pool member + setter; ctor fwd :138/:613;
  prune site :589), `AudioEngine.{h,cpp}` (ProjectPool placement + wire :59),
  `ProjectPool.h` (owns the pool), `tests/CMakeLists.txt` (new test file).
- Upstream: `RoutingManager::rebuildClipsForTrack` builds every
  `ClipSourceProcessor` (RoutingManager.cpp:509); `MainAudioProcessor::prepareToPlay`
  constructs RoutingManager (MainAudioProcessor.cpp:137-140) and its
  `graph.prepareToPlay` reaches each clip's `prepareToPlay`; `ExportManager`
  builds a standalone RoutingManager with NO pools (ExportManager.cpp:194-199 —
  deliberate, keep).
- Downstream: `processBlock` consumers unchanged (activeBuffer==2 path);
  `getAudioClipSources()` used by tests to reach live processors.
- Projections affected: none (no ValueTree/RPC/frontend change; internal
  resource management only — MCP parity N/A, delta/fullSync N/A).
- SPSC paths touched: none new. The handle's `readLock` is taken ONLY by
  background reader threads, the export render thread, and message-thread
  head fills — never by the realtime audio callback. `readNextBlock` is
  byte-for-byte unchanged.
- God nodes: `ClipSourceProcessor` (high-degree audio node — minimal-diff
  change confined to prepare paths), `RoutingManager` (one ctor param + one
  forward). Elevated risk, contained diff.
- Path integrity: rebuild → ClipSourceProcessor::prepareToPlay →
  streamingPool->acquire → streamer.prepare(handle) → fillBuffer →
  handle->read (lock) — complete chain, verified in code today except the
  acquire/handle links being added by this plan.

## Pitfall Gates Triggered

- **Gate 1/6/10 (rebuild restore):** the pool entry must be REACQUIRED on
  rebuild without re-opening the file. Strong-ref map (like DecodedSoundPool)
  + `pruneUnreferenced` at rebuild start gives exactly this — assert via
  open-count across `rebuildRoutingGraph()` + drain seam. Test asserts LIVE
  processors (getAudioClipSources + handle identity), not just the model.
- **Gate 3 (audio-thread safety):** `readNextBlock` unchanged (no lock/IO).
  The ONLY new lock (`handle->readLock`, a `juce::CriticalSection`) lives on
  background/export/message threads. Documented pre-existing hazard class:
  `prepareToPlay` may run off-message-thread at device restart and already
  performs reader opens + synchronous head fill today; with the pool the
  steady-state prepare does ZERO file opens (cache hit) — strictly safer,
  same stance as DecodedSoundPool.h's threading comment. Mirror that comment.
- **Gate 9 (null guards):** `streamingPool` may be null (export, tests) →
  private-handle fallback, mirroring `decodedPool` null → direct decode.
- **Gate 4 (stale binaries):** new test file registered in tests/CMakeLists.txt.
- **Destructor ordering:** `~StreamingClipSource` body runs `stopPlayback()`
  (joins the worker) BEFORE member destruction destroys `handle_` — the
  background thread can never read through a destroyed handle. Preserve the
  destructor-body join; do not move handle release into `stopPlayback`.
- **Pool lifetime:** `ProjectPool` is declared after `mainProcessor` in
  AudioEngine.h (pool dies first; raw pointers never dereferenced during
  teardown) — adding the streaming pool INSIDE ProjectPool inherits this.
- **Threshold parity:** promotion decision must stay
  `durSec = lengthInSamples / DEVICE sr; durSec * 1000.0 > 8000` — when
  replacing `shouldStream()`'s throwaway probe with handle metadata, keep
  using the device sampleRate parameter, NOT the file's sampleRate, so the
  stream/preload threshold is byte-identical.

## Design

New `src/engine/StreamingSoundPool.h` (mirrors DecodedSoundPool.h layout):

- `StreamingSoundHandle`: `std::unique_ptr<juce::AudioFormatReader> reader`;
  `juce::CriticalSection readLock`; metadata `numChannels` (clamped ≤2),
  `lengthInSamples`, `sampleRate`. Static `open(fm, file) -> shared_ptr`
  (null on unreadable). `read(float* const* ptrs, int numCh, int64 start,
  int num)` takes the lock and delegates to the reader.
- `StreamingSoundPool`: strong-ref `unordered_map<path, shared_ptr>` keyed by
  `juce::File(path).getFullPathName().toStdString()`; `acquire`, 
  `pruneUnreferenced` (use_count ≤ 1 evicts), test hooks `getOpenCount`,
  `getEntryCount`. Copy the DecodedSoundPool.h threading comment, adapted.

`StreamingClipSource`:
- `reader_` → `std::shared_ptr<StreamingSoundHandle> handle_`.
- New overload `prepare(std::shared_ptr<StreamingSoundHandle> handle,
  double sampleRate, int samplesPerBlock)` (pool-acquired or private).
- Legacy `prepare(const juce::File&, juce::AudioFormatManager&, double, int)`
  stays for the 10 existing test call sites + null-pool fallback: opens a
  PRIVATE handle, delegates to the new overload. Promotion/streaming logic
  unchanged (identical thresholds, identical buffer math).
- `fillWholeFile` / `fillBuffer` call `handle_->read(...)` instead of
  `reader_->read(...)`; `numChannels_`/`sourceLength_` initialized from
  handle metadata (identical clamping).

`ClipSourceProcessor`:
- Ctor gains `HDAW::StreamingSoundPool* streamPool = nullptr` AFTER
  `decodedPool` (all existing call sites compile unchanged).
- Streaming branch in `prepareToPlay` and `switchToSourceFile`:
  acquire handle (pool, else `StreamingSoundHandle::open`), decide
  long/short from `handle->lengthInSamples / sr` (device sr — see
  threshold parity), long → `streamer.prepare(handle, sr, block)` (drop the
  now-redundant `shouldStream` probe), short → let the temp handle drop and
  `preloadWholeFile()` as today.
- Test hook `getStreamingHandleForTest()` returning the shared_ptr (identity
  comparison in tests).

Wiring:
- `ProjectPool`: member `HDAW::StreamingSoundPool streamingSoundPool{formatManager}`
  + getter (member order inside ProjectPool: after formatManager init —
  follows decodedSoundPool precedent).
- `AudioEngine.cpp` (:59 site): `mainProcessor->setStreamingSoundPool(
  &projectPool.getStreamingSoundPool());`
- `MainAudioProcessor`: member + `setStreamingSoundPool`; forward to BOTH
  RoutingManager constructions (:138, :613); prune alongside decodedPool
  (:589-590).
- `RoutingManager` ctor param + member; pass to `ClipSourceProcessor`
  construction (RoutingManager.cpp:509).
- ExportManager: UNCHANGED (no pool — deliberate, mirrors DecodedSoundPool
  note at ExportManager.cpp:194).

Tests: new `tests/unit/engine/streaming_pool_test.cpp` (suite
`StreamingSoundPool`), registered in tests/CMakeLists.txt. Long-file WAVs
(>8 s) via the audio_pool_dedup_test.cpp `writeSineWav` helper pattern
(30 s file ≈ 2.6 MB mono 16-bit — acceptable; use 9 s where possible to
keep runtime down, and non-realtime for determinism).

## Steps

1. Task A (subagent): `StreamingSoundPool.h` + `StreamingClipSource` handle
   refactor + `streaming_pool_test.cpp` (pool unit tests + concurrent
   divergent-position correctness) + CMake registration. Gates: new suite +
   `StreamingClipSource.*` + `ClipStreamingE2E.*` pass; build clean.
2. Task B (subagent): `ClipSourceProcessor` + wiring (RoutingManager,
   MainAudioProcessor, AudioEngine, ProjectPool) + engine-level dedup /
   rebuild-reacquire tests (drain seam, no sleeps) + version bump.
   Gates: full `hdaw_tests.exe` passes; grep-verify version triple.
3. Orchestrator: anti-pattern scan of the full diff, final verification,
   graph refresh (index_repository fast), commit(s).

## Version

0.22.4 → 0.23.0 (new engine subsystem). Triple sync: `CMakeLists.txt`,
`frontend/package.json`, `frontend/src/version.ts` (hand-sync all three).
