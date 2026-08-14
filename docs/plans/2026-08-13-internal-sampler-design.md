# Internal Sampler Instrument (Simpler-style) — Design Spec

> **Document type:** Design spec (output of brainstorming). The task-by-task
> implementation plan is produced next via the `writing-plans` skill; the
> Success Gates / Pitfall Gates / Dependency Map sections below seed that plan.
> **For implementers:** after this spec is approved, run `writing-plans`, then
> `hdaw-guard` before any code change.

**Goal:** Add HDAW's first *internal instrument* — a single-sample,
Simpler-style sampler (Ableton/Bitwig lineage, minimalist-but-powerful) — so a
track can make sound from MIDI without a hosted plugin. It ships as a new
internal FX-chain type for v1, with the DSP/voice engine deliberately decoupled
so it can later be promoted to a first-class instrument slot without a rewrite.

**Tech Stack:** C++17 (JUCE 8), Qt JSON for RPC, React 19 + TypeScript + Zustand,
gtest, Vitest, Playwright.

---

## Design decisions (locked during brainstorming)

1. **Archetype — single-sample Simpler, intentionally capped.** One sample per
   instance, played polyphonically across the keyboard with pitch tracking. No
   multisample keyzone model, no velocity layers, no round robins in v1. This is
   a deliberate YAGNI scope cap — becoming a multisample instrument later is a
   *new* feature, not a migration of this one.

2. **Integration — internal FX-chain type now, first-class instrument slot
   later.** A sampler is an `FX_SLOT` with `fxType="sampler"`, exactly like
   `eq`/`compressor`/`reverb` are internal FX types. MIDI already flows through
   the FX chain (`Track.cpp:507`), so the sampler slot reads incoming MIDI and
   writes audio into the buffer. The `SamplerEngine`/`SamplerVoice`/`SamplerSound`
   classes are **standalone with no FX-slot or ValueTree dependency**, so lifting
   them into a dedicated `INSTRUMENT` slot later is a move, not a rewrite.

3. **Playback modes — Classic + One-Shot + Slicing.** "Powerful" wins on the
   feature surface; all three Simpler modes ship.

4. **Polyphony — polyphonic default + optional Mono/Legato.** Fixed voice pool
   (32) with stealing; mono mode adds note-priority + glide/portamento for
   bass/lead work. Unison/stack is out of scope.

**Recommended (treated as defaults, not re-litigated):**
- Pitch model = root-note transpose via **fractional-read resampling** (a 4-point
  Lagrange interpolator — not bare linear, to honor the quality/fidelity lessons).
- Sample source = drag from the file browser / Audio Pool + file-picker fallback.
- Amplitude envelope = **AHDSR** (Attack–Hold–Decay–Sustain–Release), the
  standard sampler envelope.

---

## Architecture

### Component layout (new files under `src/engine/`)

| Class | Role | Thread |
|-------|------|--------|
| `SamplerSound` | immutable loaded-sample resource (the "tape"): `HeapBlock<float> data[2]`, channels, length, native SR, root note, user start/end, loop start/end, `std::vector<int64_t> slicePoints` | built message thread; read-only on audio thread |
| `SamplerVoice` | one playing instance: active flag, fractional `readPos`, `pitchRatio`, AHDSR phase/gain, per-voice gain/pan, raw `const SamplerSound*`, mode + slice index | audio thread |
| `SamplerEngine` | owns `SamplerVoice voices[32]` + `NoteAllocator` logic + `std::shared_ptr<const SamplerSound>` + pending-sound swap + atomic param mirrors; RT entry `render(buffer, midi)` | audio thread (render) / message thread (load, params) |
| `SliceDetector` | transient + grid slice-point analysis → fills `SamplerSound::slicePoints` | message thread |
| `TrackFXSlot` ("sampler" variant) | thin wrapper: holds a `SamplerEngine`; `process(buffer, midi)` delegates to `engine.render` | audio thread |

The engine has **no dependency** on `TrackFXSlot` or the ValueTree — it takes
plain params/atomics and a `juce::MidiBuffer`. That is the portability contract.

### Voice-engine approach: custom (not JUCE `Synthesiser`)

Considered: (A) JUCE `Synthesiser` off-the-shelf, (B) custom engine,
(C) hybrid. **Chose B.** JUCE's `Synthesiser` is opinionated about voice
stealing and MIDI handling and would fight Slicing (slices→chromatic keys),
Mono/Legato + glide, and per-mode AHDSR. A custom engine matches HDAW's
existing custom-DSP idiom (`PhraseGenerator`, `RhythmPatternGenerator`,
internal FX), integrates cleanly with `stateLock`/automation/modulation, and is
the natural shape for a future instrument slot. Cost: more code; we own
correctness (polyphony, stealing, denormals).

### Realtime contract (where the HDAW lessons bite)

Three threads touch the sampler:

| Thread | What it does | How it's safe |
|--------|--------------|---------------|
| **Audio** (`processBlock`) | iterates `voices[]`, fractional-reads `sound->data`, accumulates, writes buffer | no allocation, `ScopedNoDenormals`, raw `const SamplerSound*` captured at note-on — **never** dereferences a swapped pointer |
| **Message** (param edit, sample load, `prepareToPlay`) | writes atomics; builds new `SamplerSound`; holds `stateLock` for voice-vector-touching ops | mirrors `Track::setFxSlotInternalParam` (lesson 13) |
| **Command/MCP** | RPC set-param / set-sample → message thread | same as all other commands |

**Sample swap (the dangle problem), solved cleanly.** A new sample is **not**
hot-swapped under ringing voices. On the message thread, `loadSample()` builds
the new `SamplerSound`, sets `pendingSound`, and raises `reloadGate` (atomic).
At the **next block start** the audio thread sees the gate, **hard-stops all
voices**, swaps `activeSound = pendingSound`, clears the gate. So a raw
`sound*` held by a voice is valid for that voice's lifetime (voices only start
after the swap completes). This is exactly the `ClipSourceProcessor::activeBuffer`
block-boundary-swap idiom (`ClipSourceProcessor.h:226`), adapted. Cost: changing
the sample mid-play gives a tiny click — acceptable and expected.

**Param edits** (AHDSR, loop points, transpose, mono/glide): plain atomic reads
by voices — **no lock on the audio path**. Only operations that change the
number/identity of voices or the sound take `stateLock` (lesson 13).
`prepareToPlay` recreation follows the `tryEnter()`-or-skip pattern
(`Track.cpp:33`).

### Modes & polyphony behavior

- **Classic:** note-on → voice starts at sample start (or offset), AHDSR attack;
  if a loop region is set, loops while held; note-off → release segment.
- **One-Shot:** note-on → voice plays the sample/slice to end, **ignores
  note-off** (retrigger allowed). The drum/percussion path.
- **Slicing:** `SliceDetector` precomputes slice points (transient detection +
  optional grid), stored on the `SamplerSound`. Slices map **chromatically** to
  successive semitones from a base note; each triggered note plays one slice in
  the current play mode.
- **Poly:** default, pool of 32, steal **oldest** (configurable to quietest).
- **Mono/Legato:** single active voice, note-priority (last/low/high), optional
  glide/portamento (`readPos` retargets at `glide` rate). Toggled by `mono`.

### Resampling & quality (lessons 7/8)

Repitch = fractional read of the buffer at `pitchRatio` (derived from
`playedNote - rootNote + transpose`). Default to a **4-point Lagrange
interpolator** (good quality, cheap) — not bare linear — to avoid the
aliasing/quality regressions lesson 8 flags. Gain staging stays at unity per
voice; track volume/pan/meter apply downstream.

---

## Data model

### ValueTree — a sampler is an `FX_SLOT` with `fxType="sampler"`

Properties (camelCase, matching existing `IDs::` convention):

| Property | Type | Default | Notes |
|----------|------|---------|-------|
| `sampleFile` | String | "" | absolute path; follows clip source-file convention |
| `rootNote` | int | 60 | pitch-tracking root (auto-guessed from filename, else C4) |
| `transpose` | int | 0 | semitones, automatable |
| `mode` | String | "classic" | "classic" \| "oneshot" \| "slicing" |
| `playReverse` | bool | false | |
| `mono` | bool | false | mono/legato mode |
| `glide` | double | 0.0 | seconds, mono only |
| `notePriority` | String | "last" | "last" \| "low" \| "high" |
| `ahdsrAttack`/`Hold`/`Decay`/`Sustain`/`Release` | double | 0.005/0.0/0.1/0.9/0.1 | seconds / 0..1 level |
| `sampleStart`/`sampleEnd` | double | 0.0 / 1.0 | **normalized 0..1** within sample |
| `loopEnabled` | bool | false | |
| `loopStart`/`loopEnd` | double | 0.0 / 1.0 | normalized 0..1 |
| `sliceMode` | String | "transient" | "transient" \| "grid" |
| `sliceGrid` | double | 0.25 | beats (grid mode); converted via playhead BPM |
| `sliceSensitivity` | double | 0.5 | transient threshold |
| `slicePointsOverride` | child list | — | manual slice edits; else recomputed from detection |

**Beats-vs-seconds guard (lesson 1):** all sample-internal coordinates
(`sampleStart`/`End`, `loop*`, slice points) are **normalized 0..1** — never
timeline beats — converted to sample frames once at load. The only beats-value
is `sliceGrid`, converted via playhead BPM exactly as `Track.cpp:531` does.

### Automation / modulation mapping

The sampler implements the existing `TrackFXSlot` automation interface
(`getAutomationParam` / `setAutomationParam`, paramID `100 + slot*100 + idx`).
A small curated set is automatable/modulatable:

```
idx 0 = ahdsrAttack   1 = ahdsrDecay   2 = ahdsrSustain
idx 3 = ahdsrRelease  4 = transpose    5 = sampleStart
```

Everything else is a plain property (set via RPC/MCP/UI, read as atomics).
Modulation routes through the same `paramID >= 100` path the LFO system already
drives (`Track.cpp:591`), so the sampler is a modulation target for free.

---

## FX-slot wiring & sample loading

### The `"sampler"` fxType

`TrackFXSlot` gains a sampler variant holding a `SamplerEngine`:
- **`process(buffer, midi)`:** reads note-on/off (+ sustain-pedal CC 64,
  future), renders active voices, and **overwrites** the buffer (an instrument is
  a source — it clears then writes, matching plugin-instrument behavior). MIDI
  events are consumed by the sampler slot.
- **`prepareToPlay`:** `engine.prepare(sr, blockSize)`; loads `sampleFile` via
  `ProjectPool::getFormatManager()` → builds `SamplerSound` (same
  `createReaderFor` path as `ClipSourceProcessor.h:40`).
- **State/lock:** sample load + slice recompute run on the message thread under
  `stateLock`; the block-boundary swap above keeps the audio path lock-free.

### Sample loading & management

- **Source:** drag-drop from the file browser / Audio Pool onto the sampler slot
  (reuses HDAW's existing drag source + `FileLibraryManager`), a file-picker
  fallback, and an MCP `sampler.setSample` path.
- **Load (message thread):** `createReaderFor` → read fully into
  `HeapBlock<float>[2]` (preload-to-memory, consistent with clips — **no
  streaming-from-disk in v1**, YAGNI) → build `SamplerSound`, auto-guess root
  note from filename (regex `_?([A-G]#?b?\d)`, else 60), auto-detect slices if
  `mode="slicing"`.
- **Save/load:** `sampleFile` path persists in the ValueTree (absolute, matching
  clip convention today). Pool-relative paths are a future `ProjectPool`
  enhancement, out of scope here.
- **Memory:** a single preloaded sample per instance — bounded, acceptable for a
  Simpler.

---

## UI — the Sampler editor (a bottom-panel tab)

Per HDAW's fixed-tile ethos (`AGENTS.md`), the editor is **a tab in the bottom
panel** that activates when a sampler FX slot is selected — no floating window,
lives in the stable frame. Dense horizontal regions, charcoal base + amber
accent, tabular numerals, ~13px base:

- **Left rail:** mode toggle (Classic / One-Shot / Slicing), root-note +
  transpose, poly/mono + glide + note-priority, reverse. Numeric readouts.
- **Center — waveform canvas** (adapted from the existing `WaveformCanvas`
  family) with draggable overlays: `sampleStart`/`sampleEnd` region handles,
  `loopStart`/`loopEnd` handles (when loop on), slice-point lines (when slicing;
  click to audition), and an audition playhead.
  **Thumbnails-are-a-playback-contract** (`AGENTS.md`): the display reflects the
  *effective* region and flips for reverse — not the raw file.
- **Right rail:** AHDSR envelope graph with draggable stage handles + numeric
  fields.
- **Slice strip** (slicing mode only): mode, grid value, sensitivity, detected
  count, re-detect button.

Drag math emits normalized 0..1 coords (atomic writes back to the engine).
Click-to-audition plays a one-shot preview via the existing `AudioPreviewPlayer`
pattern. Micro-transitions ~100–150 ms; `prefers-reduced-motion` respected.

---

## MCP / RPC parity (AGENTS.md — first-class client)

Shared RPC/command layer (UI and MCP share it), with MCP tools mirroring:

- `sampler.setSample(trackId, slotIndex, filePath)`
- `sampler.setParam(trackId, slotIndex, { name, value })` — generic for all props
- `sampler.setMode`, `sampler.setSliceMode`, `sampler.detectSlices`,
  `sampler.triggerSlice` (audition), `sampler.getState` (sound + params + slice
  points)

This also unlocks **generative use** (the generative pillar): MCP seeding of
sampler params, and `PhraseGenerator` able to target a sampler track later —
consistent with how rhythm/phrase generation already works.

---

## Testing strategy

- **gtest (engine):** polyphony + voice stealing (oldest evicted); One-Shot
  ignores note-off; mono/legato note-priority + glide retarget; slice detection
  on a synthetic transient sample + chromatic mapping; AHDSR stage math;
  **sample-swap → all active voices reference the current sound (no dangle)**;
  `sliceGrid` beats→frames at a BPM; automation routing; **restore-after-rebuild
  (lesson 10)** — mutate a sampler param, `rebuildRoutingGraph`, assert the live
  engine survives.
- **Vitest:** editor param logic + drag-handle→normalized math.
- **Playwright E2E:** load sample → set mode → audition slice → assert markers
  render; a drag-stale-closure regression (`docs/pitfalls-frontend.md`).
- **Realtime review:** run the `audio-dsp-review` + `audio-numerics-review`
  skills on the voice engine before merge — denormals, zero audio-thread
  allocation, gain staging (lessons 7/8/13/14).

---

## Success Gates (completion contract — evidence required)

> These seed the implementation plan from `writing-plans`. Each becomes a
> checkbox the implementer must satisfy with evidence.

- [ ] G1: gtest sampler engine suite passes — polyphony, stealing, One-Shot
  note-off, mono/legato + glide, AHDSR math, slice detection + mapping, **sample
  swap no-dangle**, automation routing.
- [ ] G2: **restore-after-rebuild** test (lesson 10) passes — mutate a sampler
  param, `rebuildRoutingGraph`, assert live engine state survives.
- [ ] G3: gtest `sliceGrid` beats→frames conversion at a known BPM is exact.
- [ ] G4: Full engine test suite (`build/Debug/hdaw_tests.exe`, no filter) — no
  regression.
- [ ] G5: `cmake --build build --config Debug` succeeds; new `.cpp` files added
  to the **explicit CMake source lists** (avoid the stale-`.obj` trap, lesson 15);
  verify the test binary timestamp is newer than the build.
- [ ] G6: RPC `sampler.*` methods + MCP `sampler_*` tools registered and covered
  by tests (MCP parity — feature reachable from every surface a human can use).
- [ ] G7: `cd frontend && npm test` passes (new editor component/store tests);
  `npm run build` succeeds.
- [ ] G8: Playwright E2E — load sample → set mode → audition slice → markers
  render; drag-stale-closure regression.
- [ ] G9: Realtime review (`audio-dsp-review` + `audio-numerics-review`) clean —
  no audio-thread allocation, denormals handled, gain staging unity.
- [ ] G10: Version bumped in **both** `CMakeLists.txt` and `frontend/package.json`
  (kept in sync); changelog entry added.
- [ ] G11: Knowledge graph refreshed (`codebase-memory index_repository` mode
  `fast`, project `D-pdf-roo-projects-hdaw3`) so the graph knows the new
  classes/RPC methods.

## Dependency Map

- **Blast radius:** `TrackFXSlot` (new variant), `RoutingManager::addTrack`
  (restores sampler state on rebuild — lesson 10), RPC composition/FX router,
  MCP tools registry, frontend FX-chain + bottom-panel tabs.
- **Reuses (low risk):** `ClipSourceProcessor` preload + block-boundary-swap
  pattern; `ProjectPool::getFormatManager()`; `AudioPreviewPlayer` audition
  pattern; `WaveformCanvas`-style components; the `paramID >= 100`
  automation/modulation routing.
- **New, hot-path (high scrutiny):** `SamplerEngine::render`, `SamplerVoice`
  render loop — audio thread, full realtime discipline (lessons 3/7/8/13/14).
- **Projections affected:** ReadModel (sampler FX_SLOT is part of the existing
  FX_CHAIN fullSync — no new entity type, so delta/fullSync behavior is
  unchanged); audio graph rebuilds via the existing FX-chain path.
- **SPSC paths touched:** none (sampler params are plain atomics + the
  block-boundary sound swap; no new SPSC bridge).
- **Path integrity:** `sampler.setSample` must be a complete chain: RPC parses
  → message-thread load → `SamplerSound` build → block-boundary swap → audio
  thread plays it. `sampler.setParam` → atomic write → voice read. G1/G6 cover
  both chains.

## Pitfall Gates Triggered

- **Lesson 1 (beats vs seconds):** all sample-internal coords normalized 0..1;
  only `sliceGrid` is beats (BPM-converted). G3 enforces.
- **Lesson 3 (processBlock early-out):** N/A — sampler is MIDI-driven, not
  transport-driven, but `render` must early-out when there are no active voices
  and no MIDI (no busy work).
- **Lessons 7/8 (latency/quality):** sampler reports zero added latency;
  Lagrange interpolator (not linear); `ScopedNoDenormals`. G9 enforces.
- **Lesson 10 (rebuild state-restore):** `RoutingManager::addTrack` must restore
  sampler sound + params on rebuild — covered by an explicit test. G2 enforces.
- **Lesson 13 (DSP-state write race):** param writes that touch voice DSP take
  `stateLock`; the block-boundary swap avoids the write-after-free on the sound
  buffer. G1/G9 enforce.
- **Lesson 14 (RT allocation):** voice pool is preallocated; no allocation on
  the audio thread; `slicePoints` is built on the message thread and frozen onto
  the immutable `SamplerSound`. G9 enforces.
- **Lesson 15 (stale `.obj`):** new entry-point/`.cpp` files added to CMake
  source lists; verify binary timestamp. G5 enforces.
- **Frontend pitfalls (stale closures, optimistic + syncSnapshot):** editor
  reads from the store after async RPC, not closure props; drag placement is
  atomic-per-move. G7/G8 enforce.

## Anti-Pattern Scan

- No N-call RPC loops (one batched `setParam` per change, or a single
  `setSample`). No full-tree walks (sampler params are scoped slot reads). No
  `DBG` macro collisions. No raw hex in frontend CSS (reuse existing tokens).
- No audio-thread allocation, locking, or disk I/O in `render`.
- The engine has no ValueTree/FX-slot dependency (portability contract for the
  future instrument-slot promotion).

## Open questions (to resolve in `writing-plans`)

- Exact voice-stealing policy default (oldest vs quietest) and whether to make
  it user-configurable in v1 or hardcode oldest.
- Sustain-pedal (CC 64) and pitch-bend handling — confirm whether v1 includes
  them or defers (design leaves them as "future"; plan should decide).
- Whether `PhraseGenerator` targets the sampler in v1 or is a follow-up.
- Root-note filename-regex: confirm the exact pattern and fallback behavior.
