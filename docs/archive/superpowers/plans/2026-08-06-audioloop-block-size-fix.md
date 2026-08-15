# Fix: PluginHost::audioLoop scratch buffers never resize to PREPARE block size

> **Status:** Plan — implementation dispatched to a subagent.
> **Related prior plan:** `2026-08-03-plugin-isolation-fixes.md` (crash/isolation hardening, already shipped).

## Problem

The isolated Vital instrument produces a single high-pitched distorted squeak
at the wrong position instead of three clear tones. Root cause is a block-size
mismatch inside the child process, not the MIDI/audio routing (that path was
verified intact).

## Goal

Make `PluginHost::audioLoop()` size its scratch `inputBuffer`/`outputBuffer` to
match the block size delivered by the PREPARE message, so `plugin->processBlock`
operates on the same block width the transport (and the plugin's `prepareToPlay`)
use. Fix the audible distortion without regressing the isolation/hardening work.

## Root cause (verified by reading source)

`src/proxy/host/PluginHost.cpp`, `audioLoop()`:

- Line 365/366 build `inputBuffer`/`outputBuffer` with `preparedBlockSize`,
  which at thread start is the **constructor default 512** (`PluginHost.h:44`).
- The audio thread starts immediately in `run()` (`PluginHost.cpp:183`) before
  the parent's PREPARE message arrives.
- PREPARE (handled in `controlLoop()` at `PluginHost.cpp:224`) later sets
  `preparedBlockSize = 441` (real device block per logs `bufS=441`) and calls
  `plugin->prepareToPlay(44100, 441)`.
- The buffers are **never resized**, so `processBlock(inputBuffer, midiBuffer)`
  (`PluginHost.cpp:401`) is handed a **512-sample** buffer even though:
  - the ring transfer reads/writes `preparedBlockSize` (441) samples per channel
    (lines 379-381, 429-431), and
  - the plugin was configured for 441-sample blocks.

Consequences for a synthesizer like Vital that advance internal phase by the
buffer width: it renders 512 samples toward the 441 the transport consumes,
yielding a ~1.16x pitch-up, plus a stale/garbage tail on the un-written
471..511 columns and wrong MIDI sampleOffsets — the distortion and the "wrong
place" timing.

The existing `AudioRoundTripWithPassthrough` (block 512) and other tests do not
catch this because passthrough copies exactly the 441 filled columns and the
512-based ring math is self-consistent.

## Success Gates (all must pass, with evidence)

- [ ] **Gate 1 — Bug reproduced:** New gtest `PluginIsolation.ResizesScratchBuffersToPreparedBlockSize`
      FAILS against the current code: it drives a `__blocksize__` probe (product of
      a new built-in in `PluginHost.cpp` that fills the buffer with
      `(float)buffer.getNumSamples()`), sends PREPARE `blockSize=441`, writes a
      441-per-channel block, and asserts `readOutput()[0] == 441.0f`. Before the
      fix the probe reports the stale 512 width → assertion fails.
- [ ] **Gate 2 — Fix verified:** The same test PASSES after the fix
      (`readOutput()[0] == 441.0f`), proving `processBlock` now receives a
      441-sample buffer.
- [ ] **Gate 3 — No proxy regression:** `PluginIsolation.*` suite passes
      (passthrough, crash, kill-mode, recovery, shared-memory tests unchanged).
- [ ] **Gate 4 — Full suite:** `build/Debug/hdaw_tests.exe` passes with no
      regressions.
- [ ] **Gate 5 — Clean build:** `cmake --build build --config Debug` compiles
      (Debug; do NOT rebuild/touch `build/Release`).

## Dependency Map

- **Upstream:** `ProxyProcessManager::spawnPluginHost` → child `run()` →
  `audioLoop()`; PROBE fixed. Peer consumes via `PluginProxySlot::prepareToPlay`
  → parent sends PREPARE → `controlLoop()` PREPARE case sets `preparedBlockSize`.
- **Downstream:** resized buffers feed `plugin->processBlock`; the ring transfer
  loops already use `preparedBlockSize`, so they are unchanged.
- **Projections affected:** none (child-process internal; no ReadModel/audio
  graph / frontend snapshot).
- **SPSC paths touched:** the child input/output rings — buffer width must match
  `preparedBlockSize` for correct framing. No change to the ring protocol.
- **Path integrity:** PREPARE → `prepareToPlay` already wired; the lone gap is
  scratch-buffer sizing. No graphify/graph query needed — direct 2-file scope,
  verified by reading source (no invented edges).

## Pitfall Gates Triggered

- **Gate 3 (audio-thread safety):** `audioLoop` is the child's dedicated worker
  thread (not the DAW's realtime callback) and already `Sleep(0)`/yields. Buffers
  resize **only when `getNumSamples() != preparedBlockSize`** (a one-time event at
  PREPARE), never per block, so no per-callback allocation is introduced. Note the
  boundaries: `preparedBlockSize` read in the loop is already a racy-by-design
  read (set once by PREPARE before audio flows); resize is gated on the same read.
- **Gate 2 (no silent path):** the `__blocksize__` probe is a built-in exactly
  like the existing `__passthrough__`/`__crash__`; its `processBlock` writes every
  sample so any buffer width is observable.
- **Gate 8/9:** not applicable (no CSS/IDs).

## Anti-patterns to avoid

- Do NOT rebuild the ring every iteration (allocate only on width change).
- Do NOT change the ring protocol or `computeShmSize` layout.
- Do NOT touch `processBlock` in the parent (`PluginProxySlot`) — the parent's
  write/read framing is already correct at the real block size.

## Steps

1. **Add probe** — in `PluginHost.cpp` anonymous namespace, add a
   `BlockSizeProbeProcessor : juce::AudioPluginInstance` registered for path
   `"__blocksize__"` in `loadPluginByPath()`. Its `processBlock` fills every
   channel/sample with `(float)buffer.getNumSamples()`.
2. **Add failing test first** — in
   `tests/integration/proxy/isolation_integration_test.cpp` add
   `PluginIsolation.ResizesScratchBuffersToPreparedBlockSize` following the
   `AudioRoundTripWithPassthrough` template: spawn `__blocksize__` on a fresh
   slot, send PREPARE `{44100.0, 441, 2}`, wait for shm header init, write a
   441-block, read output, assert `output[0] == 441.0f`. Use an unused slot id
   (e.g. 9130) to avoid collisions.
3. **Build + run the single test** — confirm it FAILS (reproduces bug). Capture
   the failing output.
4. **Apply fix** — in `audioLoop()`, before the ring-read block (or at top of
   the `while` body), if `inputBuffer.getNumSamples() != preparedBlockSize`,
   call `inputBuffer.setSize(preparedNumChannels, preparedBlockSize)` and
   `outputBuffer.setSize(preparedNumChannels, preparedBlockSize)`
   (`keepExistingContent=false` since the ring fully overwrites).
5. **Build + run the single test** — confirm it PASSES.
6. **Run `PluginIsolation.*`, then the full `hdaw_tests.exe`** — confirm no
   regressions (Gates 3, 4, 5).
7. **Report** — files changed, before/after test output, full-suite summary.

## Commands (PowerShell — no `&&`)

- Build tests: `cmake --build build --config Debug --target hdaw_tests`
- Run one: `build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.ResizesScratchBuffersToPreparedBlockSize`
- Run suite: `build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*`
- Run all: `build\Debug\hdaw_tests.exe`

## Out of scope

- HDAW.exe / frontend / packaging (no UI or engine-surface change; DllMain/CLAP hosting unaffected).
- The RealTime-safety pitfall entries in docs (no signal-path change to the parent graph).