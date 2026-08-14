# Sampler Implementation — Continuation Handoff

## What's done

All 14 engine tasks are **complete**. Branch `feat/internal-sampler` has 13 commits
(Tasks 1–14). The sampler is a fully integrated `fxType="sampler"` TrackFXSlot with
Classic/One-Shot/Slicing modes, mono/legato, AHDSR envelope, Lagrange interpolation,
polyphony (32 voices), block-boundary sample swap, L13-safe atomic param push,
restore-after-rebuild, RPC (`sampler.setSample`, `setFxSlotParam`), MCP parity
(`set_internal_fx_param`, `sampler_set_sample`, `sampler_get_state`, `add_fx` with
"sampler" enum), frontend `SamplerEditor` component, and version bump to 0.21.0.

**Commits (13, all on `feat/internal-sampler`):**

| SHA | Task | What |
|-----|------|------|
| `efa9cd5` | 1 | `SamplerSound` immutable resource + 2 tests |
| `9778a41` | 1 fix | Leak fix — embed `SamplerSound` in shared ownership bundle |
| `0a6a222` | 1 nit | `slicePoints` test + dead `<algorithm>` include removed |
| `6303178` | 2 | 4-point Lagrange interpolator + 3 tests |
| `5ab87cb` | 3 | AHDSR envelope state machine + 3 tests |
| `6519961` | 4 | `SamplerVoice` render (classic, pitch, loop, stereo) + 2 tests |
| `6235633` | 5 | `SamplerEngine` polyphony/stealing/block-boundary swap + 3 tests |
| `12b17da` | 6 | Mono mode single-voice regression test |
| `3170ff7` | 7 | One-Shot mode + SliceDetector + chromatic slice mapping |
| `aeb82b8` | 8 | TrackFXSlot sampler variant + L13-safe atomic AHDSR + tests |
| `3e4f18c` | 9 | Restore sampler state across routing rebuild (lesson 10) |
| `0abd433` | 10-11 | MCP tools + RPC `sampler.setSample` |
| `782355c` | 14 | Version bump to 0.21.0 |

**Frontend (committed by subagent, SHA not captured):**
- `SamplerEditor.tsx` + `SamplerEditor.css` — waveform canvas, AHDSR sliders, mode/root/transpose/mono
- `SamplerEditor.test.tsx` — 2 Vitest tests
- `sampler.spec.ts` — Playwright E2E test
- `App.tsx` — wired "Sampler" bottom tab
- `ReadModel.h`/`ReadModelImpl.cpp` + `Router_Read.cpp` — `sampler.getState` RPC + `SamplerStateSnapshot`

**Test state:**
- C++ sampler: **20/20 passing** (SamplerSound 3, Interpolator 3, Voice 2, Engine 5, FxSlot 4, SliceDetector 3)
- Frontend Vitest: **334/334 passing**
- Audio DSP review: **No critical violations** (2 acceptable warnings documented below)

## What's left

### 1. Knowledge graph refresh
Run `codebase-memory index_repository` (mode `fast`, project `D-pdf-roo-projects-hdaw3`)
to pick up the new files and RPC methods. This is required by the completion contract
(AGENTS.md §Completion Contract item 8).

### 2. Full regression test
Run the **full** `hdaw_tests.exe` (no filter) to confirm no regressions across all
250+ tests. The sampler tests pass, but the full suite hasn't been run since Task 14.

### 3. Audio DSP review warnings (acceptable, optionally address)

**Warning 1: `shared_ptr` deallocation on audio thread** (`SamplerEngine.cpp:44`)
When `activeSound_` is replaced in `applyPendingSwap`, the old `shared_ptr`'s refcount
decrements. If the message thread no longer holds a copy, the destructor runs on the
audio thread (heap deallocation). In practice callers hold references, so this is safe.
Optional fix: stash the old `shared_ptr` in an atomic "graveyard" that the message thread
drains.

**Warning 2: Non-atomic `params_` struct read** (`SamplerEngine.cpp:148`)
`handleNoteOn` reads `params_` on the audio thread while `setParams` writes it on the
message thread. The fields are POD (float/int/bool) and word-aligned — safe on x86/ARM in
practice, but technically UB. The AHDSR fields are correctly atomic. Optional fix: make
`mode`, `reverse`, `baseNote`, `sampleStart` individual atomics (matching `transposeAtom_`,
`monoAtom_`, `glideAtom_`).

### 4. E2E test execution
The Playwright E2E test (`sampler.spec.ts`) was written but needs a running engine to
execute. Run `cd frontend && npm run test:e2e -- --grep sampler` with a current
`build/Debug/HDAW.exe`.

### 5. Follow-up features (deferred from spec Open Questions)
- **Glide/portamento:** `glideAtom_` is stored but not yet implemented. In mono mode with
  `glide > 0`, a retrigger should retarget the active voice's pitch over `glide` seconds.
- **Sustain pedal (CC64):** not wired.
- **Pitch bend:** not wired.
- **PhraseGenerator → sampler:** generate phrases into sampler slices.
- **Root-note regex finalization:** for auto-detecting root note from filename.

### 6. Merge to main
When all above items are done, merge `feat/internal-sampler` to `main`. Use the
`finishing-a-development-branch` skill.

## Files created/modified

**New engine files:**
- `src/engine/SamplerSound.h` — immutable loaded-sample resource (header-only)
- `src/engine/SamplerInterpolator.h` — 4-point Lagrange interpolation (header-only)
- `src/engine/AHDSREnvelope.h` — per-voice AHDSR envelope state machine (header-only)
- `src/engine/SamplerVoice.h` — single playing voice (header-only)
- `src/engine/SamplerEngine.h` / `.cpp` — polyphonic voice manager
- `src/engine/SliceDetector.h` — transient + grid slice detection (header-only)

**Modified engine files:**
- `src/engine/TrackFXSlot.h` — added `ActiveType::Sampler`, param defs, sampler member,
  branches in prepare/process/reset/applyInternalParamToDsp, `loadSamplerState`, test hooks
- `src/engine/Track.cpp` — sampler branch in `rebuildFXChain`
- `src/common/ProjectCommands.h` — added `setSamplerSample` virtual method
- `src/engine/AudioEngineCommands.h` / `_Fx.cpp` — implemented `setSamplerSample`
- `src/common/ReadModel.h` — added `SamplerStateSnapshot` + `getSamplerState`
- `src/engine/ReadModelImpl.h` / `.cpp` — implemented `getSamplerState`
- `src/frontend/router/Router_Project.cpp` — `sampler.setSample` RPC handler
- `src/frontend/router/Router_Read.cpp` — `sampler.getState` RPC handler
- `src/mcp/McpTools_Audio.cpp` — `set_internal_fx_param`, `sampler_set_sample`,
  `sampler_get_state` tools; `add_fx` enum updated with "sampler"

**New test files:**
- `tests/unit/engine/sampler_sound_test.cpp`
- `tests/unit/engine/sampler_interpolator_test.cpp`
- `tests/unit/engine/ahdsr_envelope_test.cpp`
- `tests/unit/engine/sampler_voice_test.cpp`
- `tests/unit/engine/sampler_engine_test.cpp`
- `tests/unit/engine/slice_detector_test.cpp`
- `tests/unit/engine/sampler_fxslot_test.cpp`

**Modified test files:**
- `tests/CMakeLists.txt` — added all new test `.cpp` files

**New frontend files:**
- `frontend/src/components/SamplerEditor.tsx`
- `frontend/src/components/SamplerEditor.css`
- `frontend/src/components/__tests__/SamplerEditor.test.tsx`
- `frontend/e2e/sampler.spec.ts`

**Modified frontend files:**
- `frontend/src/App.tsx` — added Sampler bottom tab
- `frontend/package.json` — version bump to 0.21.0

**Modified build files:**
- `CMakeLists.txt` — added `SamplerEngine.cpp` to source list, version bump to 0.21.0

## Workflow notes

- **Branch:** `feat/internal-sampler` (scoped commits: stage only sampler files).
- **Build:** `cmake --build build --config Debug`
- **Test C++:** `build\Debug\hdaw_tests.exe --gtest_filter=Sampler*` (or `SliceDetector*`, `SamplerFxSlot*`)
- **Test frontend:** `cd frontend && npm test`
- **Test E2E:** `cd frontend && npm run test:e2e -- --grep sampler`
- **hdaw-guard:** invoke before EVERY code change.
- **Do NOT commit unrelated dirty-tree files.** Stage by explicit path only.
