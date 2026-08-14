# Sampler Implementation — Session Handoff

## What's been done

Two design docs + 7 commits on branch `feat/internal-sampler`, implementing the
first 5 of 14 engine tasks for HDAW's first internal instrument — a
single-sample, Simpler-style sampler (Classic/One-Shot/Slicing modes, poly +
optional mono/legato, internal FX-chain type for v1).

**Design & plan (completed, approved):**
- `docs/plans/2026-08-13-internal-sampler-design.md` — full design spec (locked
  decisions, architecture, RT contract, data model, UI, MCP, testing strategy,
  success gates).
- `docs/plans/2026-08-13-internal-sampler-plan.md` — 14-task TDD implementation
  plan with full code.

**Commits (7, all on `feat/internal-sampler`):**

| SHA | Task | What |
|-----|------|------|
| `efa9cd5` | 1 | `SamplerSound` immutable resource + 2 tests |
| `9778a41` | 1 fix | Leak fix — embed `SamplerSound` in `detail::SamplerSoundStorage` shared_ptr |
| `0a6a222` | 1 nit | `slicePoints` test + dead `<algorithm>` include removed |
| `6303178` | 2 | 4-point Lagrange interpolator + 3 tests (plan's formula was broken — implementer correctly replaced with closed-form cubic Lagrange) |
| `5ab87cb` | 3 | AHDSR envelope state machine + 3 tests (compute-then-increment ordering) |
| `6519961` | 4 | `SamplerVoice` render (classic, pitch, loop, per-channel stereo) + 2 tests |
| `6235633` | 5 | `SamplerEngine` polyphony/stealing/block-boundary swap + 3 tests (no-dangle genuinely verified; fixed uninitialized `voiceOrder_[]` UB) |

**Test state:** 14/14 sampler tests passing; `SamplerEngine.SampleSwapStopsAllVoices`
genuinely verifies the post-swap voice points at the new sound (not a weak proxy).

## Key concern from Task 5 (unresolved)

`setParams()` pushes the AHDSR envelope to all voices from the message thread
while the audio thread may be rendering them — a **lesson-13 DSP-state write
race**. It's not triggered by the current tests (no `setParams` calls), but
**Task 8 (FX-slot wiring)** must address it: either wrap param writes in
`stateLock` (matching `Track::setFxSlotInternalParam`), or use an SPSC param
queue. The spec's design says "atomics for plain params" but the envelope struct
push is not atomic. This needs a concrete fix when Task 8's implementer wires
the FX slot.

## What's left (Tasks 6–14 + final review)

All work is on branch `feat/internal-sampler`. The working tree on this branch
has **unrelated uncommitted in-flight work** (engine edits, docs reorg, frontend
changes) that was present when the branch was created — scoped commits policy
applies (stage only sampler files, never `git add -A`).

**Task 6: Mono/Legato** — stop-other-voices on noteOn in mono mode + test.
Simple; modify `SamplerEngine.cpp`. Already partially implemented (the mono
branch exists in `handleNoteOn`); needs a targeted test and possibly `v.stop()`
instead of `v.noteOff()` for instant mono-steal.

**Task 7: One-Shot + SliceDetector + slicing** — the big mode-2 task. Create
`src/engine/SliceDetector.h` (transient + grid detection), wire slice→chromatic
mapping in the engine, add slicing-mode start to `SamplerVoice` (`startSlice`).
Tests for detection, mapping, and One-Shot ignoring noteOff.

**Task 8: TrackFXSlot "sampler" variant** — THE integration task. Modify
`TrackFXSlot.h` (`ActiveType::Sampler`, `getParamDefsForType("sampler")`,
`sampler` member, branches in prepare/process/reset/applyInternalParamToDsp +
`loadSamplerState`); modify `Track.cpp::rebuildFXChain` sampler branch. **Must
address the L13 race**: param/param writes that touch voice DSP need `stateLock`
or atomic envelope param push. Also: the sampler slot OVERWRITES the buffer
(instrument = source, `buffer.clear()` then `sampler->render()`). Automatable
params: indices 0–5 (attack/decay/sustain/release/transpose/sampleStart),
mapping to `paramID >= 100` automation scheme. Tests: process produces audio from
MIDI, params route, `loadSamplerState` round-trips through ValueTree.

**Task 9: Restore-after-rebuild (lesson 10)** — mutate a sampler param, call
`rebuildRoutingGraph`, assert the live engine survives (sampler sample + params
restored). This is a targeted integration test, modeled on
`track_mixer_state_test.cpp`.

**Tasks 10–11: RPC + MCP parity** — `sampler.*` RPC methods in
`AudioEngineCommands_Fx.cpp` + `sampler_*` MCP tools in `McpTools_Project.cpp`.
Follow the `set_fx_param` / `add_fx` pattern exactly.

**Tasks 12–13: Frontend** — `SamplerEditor.tsx` (waveform canvas + AHDSR graph +
drags), store slice, Playwright E2E. Deferred until engine is solid.

**Task 14: Finish** — version bump (both `CMakeLists.txt` + `package.json`),
realtime review (audio-dsp-review + audio-numerics-review skills on the voice
engine), knowledge graph refresh, changelog.

**Final: full-suite code review + finishing-a-development-branch.**

## Workflow notes

- **Branch:** `feat/internal-sampler` (created from `main` with dirty working
  tree; scoped commits policy: stage only sampler files).
- **Build:** `cmake --build build --config Debug` (never run
  `build/Release/HDAW.exe`).
- **Test:** `build\Debug\hdaw_tests.exe --gtest_filter=Sampler*` for sampler;
  full `build\Debug\hdaw_tests.exe` (no filter) for regression.
- **hdaw-guard:** invoke before EVERY code change (AGENTS.md mandate). Report
  gate results.
- **Two-stage review:** spec compliance first (don't trust implementer reports),
  then code quality. Tasks 1–4 were consolidated (spec review was thorough enough
  to serve as quality review for the small standalone headers). Task 5 onward use
  the full two-stage on the risky integration units.
- **Context budget note:** this was ~40 subagent dispatches for tasks 1–5. Plan
  for context usage accordingly.
- **Do NOT commit unrelated dirty-tree files.** Stage by explicit path only.
- **If build fails for unrelated dirty-tree reasons:** STOP and escalate — don't
  try to fix unrelated code.

## Task-by-task dispatch guide (from the plan)

The full plan `docs/plans/2026-08-13-internal-sampler-plan.md` has TDD code for
every task (Step 1 failing test → Step 2 verify fail → Step 3 implement → Step
4 verify pass → Step 5 commit). Some implementations have known issues noted
below — read the plan's "Notes" sections before dispatching.

| Task | Files to create/modify | Key concern |
|------|----------------------|-------------|
| 6 | `SamplerEngine.cpp` (edit), `sampler_engine_test.cpp` (edit) | `v.stop()` not `v.noteOff()` for instant mono-steal |
| 7 | `SliceDetector.h` (new), `SamplerEngine.cpp` (edit), `SamplerVoice.h` (edit: `startSlice`), `slicer_engine_test.cpp` (edit), `slice_detector_test.cpp` (new) | transient detection algorithm; chromatic slice→note mapping |
| 8 | `TrackFXSlot.h` (edit), `Track.cpp` (edit), `sampler_fxslot_test.cpp` (new) | **L13 race** — wrap param writes in `stateLock`; sample load via `ProjectPool`; `buffer.clear()` + render |
| 9 | `sampler_fxslot_test.cpp` (edit) | model on `track_mixer_state_test.cpp` rebuild harness |
| 10 | `AudioEngineCommands_Fx.cpp` + router (edit), `sampler_rpc_test.cpp` (new) | follow `set_fx_param`/`add_fx` handler pattern |
| 11 | `McpTools_Project.cpp` (edit), `sampler_mcp_test.cpp` (new) | follow `set_fx_param` tool registration |
| 12 | `SamplerEditor.tsx` (new), store, vitest | reuse `WaveformCanvas`; no raw hex CSS |
| 13 | `sampler.spec.ts` (new) | Playwright; poll with `expect.toPass()` |
| 14 | `CMakeLists.txt`, `package.json`, README | version bump both places; realtime review + KG refresh |
