# Plan: Sampler percussion renders as digital silence in offline export

Date: 2026-08-13
Status: Complete (all gates passed)

## Goal

Fix the internal Sampler FX producing pure digital silence in offline export
(and after any fresh project load / `graph.prepareToPlay` that follows an FX
chain rebuild) by preserving the `SamplerEngine` instance — and the staged
sample it holds — across repeated `TrackFXSlot::prepare` calls.

## Root cause (verified)

1. `RoutingManager::rebuildFromValueTree()` builds each Track:
   - `buildTrackProcessor` → `Track::prepareToPlay` (fxChain empty at this point),
   - then `rebuildFXChain(fxChainTree)` → for a `sampler` slot:
     `slot->prepare(fxSpec)` (creates `SamplerEngine` #1) then
     `slot->loadSamplerState(slotTree)` which stages the sample into engine #1
     via `sampler->setSound(...)` (sets `pendingSound_` + `reloadGate_`).
2. `graph.prepareToPlay()` (ExportManager.cpp:212, MainAudioProcessor.cpp:71/521)
   calls `Track::prepareToPlay` → `slot->prepare(fxSpec)` AGAIN.
3. `TrackFXSlot::prepare` (TrackFXSlot.h:366) unconditionally does
   `sampler = std::make_unique<SamplerEngine>()` → engine #1 (with the staged
   `pendingSound_`) is destroyed. `loadSamplerState`'s fallback (`stagedSound_`)
   is NOT used because engine #1 existed at load time. Result: the rebuilt
   engine has no sound → `render()` clears the buffer → **silence**.

Live playback only worked because samples were staged via `setSamplerSample`
→ `rebuildTrackFX` AFTER the last `graph.prepareToPlay`, so no re-prepare
followed the staging. Confirmed empirically: a fresh `load_project` (which runs
rebuild + `graph.prepareToPlay`) yields `hasSound:false` even while playing,
whereas the same project previously showed `hasSound:true`.

## Fix

In `TrackFXSlot::prepare`, `ActiveType::Sampler` case: only create the
`SamplerEngine` if none exists; always call the idempotent
`SamplerEngine::prepare(sr, blockSize)` (it only stores `sr_` and re-prepares
voices — it preserves `pendingSound_`/`activeSound_`/`reloadGate_`). Params
push and `stagedSound_` adoption unchanged.

## Success Gates

- [x] **Gate 1** — New gtest `SamplerFxSlot.ReprepareKeepsLoadedSound` added to
  `tests/unit/engine/sampler_fxslot_test.cpp`: `prepare` → `setSamplerSoundForTest`
  → `prepare` again → `process` a note-on → assert non-zero audio (and
  `samplerEngineForTest()->currentSound() != nullptr`). Fails on old code,
  passes on new code.
- [x] **Gate 2** — `build\Debug\hdaw_tests.exe --gtest_filter=SamplerFxSlot.*:SamplerEngine.*`
  passes; full `hdaw_tests.exe` suite passes (no regressions).
- [x] **Gate 3** — `cmake --build build --config Debug` succeeds; verify
  `build\Debug\HDAW_headless.exe` timestamp is newer than the edit (Gate 15:
  stale binary / stale .obj guard).
- [x] **Gate 4** — End-to-end with the freshly built binary: fresh
  `build\Debug\HDAW_headless.exe` (headless WebSocket mode) loads
  `compositions\techno_polyrhythm_160bars.hdaw`, mutes melodic tracks
  7–10 via RPC, exports a 5 s percussion-only section; output WAV has non-zero
  RMS (proves the sampler survives the export graph's build+prepare). The same
  test with the previous binary produced pure silence (RMS 0.0).
- [x] **Gate 5** — Full ~303 s composition re-rendered; percussion audibly
  present in the output WAV.

## Dependency Map

- **Blast radius**: `TrackFXSlot::prepare` is called only by
  `Track::prepareToPlay` (Track.cpp:44). `Track::prepareToPlay` is called by
  `RoutingManager::buildTrackProcessor` (RoutingManager.cpp:115), by the JUCE
  `AudioProcessorGraph` when `graph.prepareToPlay` runs (ExportManager.cpp:212,
  MainAudioProcessor.cpp:71 and :521), and from `Track::prepareToPlay`'s own
  graph re-bake. No other callers.
- **Upstream**: `Track` (fxChain), `RoutingManager` (rebuild paths),
  `AudioEngineCommands_Fx::setSamplerSample` (→ `rebuildTrackFX`),
  `McpTools_Audio` (sampler_get_state reads engine via `samplerEngineForTest`).
- **Downstream**: `SamplerEngine::render` on the audio thread via
  `TrackFXSlot::process`. ReadModel/frontend unaffected (frontend reads the
  `sampleFile` property, not engine state).
- **Projections**: audio graph only. **SPSC paths**: `setSound` (message
  thread) → `render` `applyPendingSwap` (audio thread) — unchanged contract.
- **God nodes**: `TrackFXSlot` is a hub (sampler + FX + plugin hosting) — the
  change is a one-line guard; low risk. No cross-community change.

## Pitfall Gates Triggered

- **Gate 1/6/10 (state not restored on rebuild)**: exactly this bug. Fix
  restores processor state (the loaded sample) across the rebuild+prepare
  seam. Regression test asserts the live processor path (slot `process` → audio
  out), not the ReadModel. Gate 4 closes the "masked by live SPSC path" form.
- **Gate 3 (audio-thread safety)**: `prepare` runs on the message thread under
  `Track`'s `stateLock` (Track.cpp:33); the change is a `if (!sampler)` guard
  on the message thread. No audio-thread code touched.
- **Gate 13 (stateLock)**: unchanged — `prepare` is still invoked under
  `Track::prepareToPlay`'s `stateLock`.
- **Gate 4/15 (stale binary)**: end-to-end verification runs the freshly built
  binary (never `build/Release`), and binary timestamp is checked.

## Anti-patterns

None introduced. Single-file minimal diff; no new RPCs, loops, or raw hex.

## Steps

1. Edit `src/engine/TrackFXSlot.h` `prepare()` Sampler case (line ~364):
   guard engine creation with `if (!sampler)`, keep the idempotent
   `sampler->prepare(...)` call, params push, and `stagedSound_` adoption.
2. Add `SamplerFxSlot.ReprepareKeepsLoadedSound` to
   `tests/unit/engine/sampler_fxslot_test.cpp` (already in the test build;
   tests/CMakeLists.txt:46).
3. Build + run tests: `cmake --build build --config Debug`; then
   `build\Debug\hdaw_tests.exe --gtest_filter=SamplerFxSlot.*:SamplerEngine.*`;
   then full `build\Debug\hdaw_tests.exe`.
4. End-to-end verification with a fresh headless engine over WebSocket
   (port 8770): `project.loadProject` → `project.setTrackMuted` (7,8,9,10) →
   `export.audio` (start 0, end 5, wav 24-bit/48 kHz) → analyze WAV RMS.
5. Full 303 s re-render; verify percussion; deliver.

## Verification commands

- `cmake --build build --config Debug`
- `build\Debug\hdaw_tests.exe --gtest_filter=SamplerFxSlot.*:SamplerEngine.*`
- `build\Debug\hdaw_tests.exe`
- Fresh-engine WS drive + WAV RMS analysis (orchestrator, after build)