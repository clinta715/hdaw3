# Handoff: Track-Building Automation — "HDAW builds the track, not the LLM"

**Project root:** `D:\pdf\roo projects\hdaw3`
**Current version:** 0.23.1 (in sync in `CMakeLists.txt` + `frontend/package.json`)
**Type:** Think-piece / exploration handoff — NO code changed. Proposals only.
**Date:** 2026-08-18

---

## The question

> After creating a track, how can we make future tracks be done **more by HDAW
> and its tools and less by LLM hand-coding**? What bugs were surfaced along the
> way?

"LLM hand-coding" = the current reality: an agent (or script) assembles a track
by issuing a sequence of ~8–12 fine-grained RPC calls and hand-picking every
value (name, color, volume, instrument, fx slots, clip, notes, automation,
modulation). The building blocks exist; the *workflow* does not. This handoff
maps what exists, the seams, and a concrete path to "one request → a track that
plays".

---

## Current state: what a new track actually is

`project.addTrack { trackType }` → `AudioEngineCommands::addTrack`
(`src/engine/AudioEngineCommands_Tracks.cpp:9`) → `createTrackValueTree`
(`src/engine/AudioEngineCommands.cpp:133`) creates a **bare track**:

- name, volume 1.0, pan 0, muted/soloed false, midiChannel 1
- empty `CLIP_LIST`, empty `FX_CHAIN`, empty automation list
- auto color from palette

**Nothing else.** No instrument, no clips, no fx, no modulation. A generated
MIDI track is **silent** until the user adds an `fm_synth`/`plugin` slot —
there is no default-instrument concept (the default project's "Synth" track is
also silent until you add a slot). See `createDefaultProject`
(`src/model/ProjectModel.cpp:280`): "Synth" ships with an empty FX chain.

### What HDAW already does (the raw material)

| Capability | Where | Notes |
|---|---|---|
| **Arrangement Generator** | `src/engine/ArrangementGenerator.h` + `composition.generateArrangement` (`Router_Composition.cpp:190`) | Closest thing to "HDAW builds a song": creates named tracks (Kick/Hat/Bass/Lead/Chords), clips, notes, in **one undo transaction + one rebuild** (`AudioEngineCommands_Clips.cpp:728`). BUT: adds **no instruments** → silent MIDI tracks. |
| Phrase/Chord/Progression gen | `composition.generatePhrase|generateChord|generateProgression` (`Router_Composition.cpp:89–188`) | Generate into an **existing** track; never create the track. Each call does `addMidiClip` + N `addNote` + **`rebuildRoutingGraph()`** (`Router_Composition.cpp:84`). |
| Rhythm pattern gen | `composition.generateRhythmPattern` (`Router_Composition.cpp:223`) | Same shape — into existing track, own rebuild. |
| `add_track_with_fx` (MCP only) | `src/mcp/McpTools_Project.cpp:260` | Track + one fx slot. No frontend RPC twin, no clips/notes. |
| Track FX / MIDI FX / Modulation / Automation | `project.addFxSlot`, `addMidiFxSlot`, `addLfo`, `addAutomationLane` | All exist and work; all are individual calls. |

### The seams (what makes hand-coding necessary today)

1. **No composite "make me a track" RPC/MCP tool.** Nothing creates
   track + instrument + fx chain + content + automation in one atomic unit.
   `generateArrangement` is the closest and it stops at notes (no instruments).
2. **Per-op rebuilds.** `generateIntoClip` (the shared lambda behind every
   `composition.generate*`) calls `rebuildRoutingGraph()` **per clip**
   (`Router_Composition.cpp:84`). Generating 4 tracks = 4 full O(project)
   rebuilds instead of one — exactly the anti-pattern from AGENTS.md lesson 6.
   A composite that does one `beginTransaction` … N ops … one rebuild is the
   incremental-correct shape.
3. **No instrument-assignment step.** Generated MIDI clips have nowhere to
   sound. The engine has an internal FM synth (`fm_synth` slot) — a
   "default instrument for MIDI tracks" (or role→instrument map: Bass→fm patch,
   Chords→fm patch, Drums→…) is missing.
4. **Frontend/RPC vs MCP drift.** `add_track`/`add_track_with_fx` in the MCP
   server are **independent reimplementations** of track creation (they build
   the ValueTree by hand, `McpTools_Project.cpp:168–181`) rather than calling
   `ProjectCommands::addTrack`. Already drifted: MCP `add_track` sets
   volume **0.85**, engine `addTrack` sets **1.0**. Any "make tracks easier"
   work must route through one implementation (engine command), with MCP =
   thin wrapper (parity rule).
5. **No template system.** No way to define "a track recipe" (name pattern,
   color, instrument, fx chain, midi-fx, modulation, generation params, mixer
   defaults) and instantiate it. Templates are the natural product shape for
   "HDAW does it."

---

## Proposal sketch (prioritized)

### P1 — Composite `project.addTrackWithContent` (RPC + MCP twin)
One call: `{ role | templateId, name?, startBeat, params }` →
1. `addTrack` (single implementation)
2. assign instrument by role (internal `fm_synth` by default; optional
   `pluginId`)
3. generate content into a clip (reuse `PhraseGenerator` /
   `RhythmPatternGenerator` / arrangement part) using **beats** end-to-end,
   converting only at the ValueTree/processor boundary (lesson 1)
4. optional fx chain + one modulation + mixer defaults
5. **one** `beginTransaction` … **one** `rebuildRoutingGraph()` …

Returns `{ trackIndex, clipId, noteCount }`. Frontend `AddTrackMenu` gets an
"assisted track" entry; MCP tool `add_track_with_content` wraps the same
command. Success gates would be: gtest asserting live processor state after the
composite (instrument present, clip routed), E2E journey, MCP parity test.

### P2 — Role→instrument default map
Bass/Lead/Chords → `fm_synth` (or a new simple internal synth), Drums → `fm_synth`
percussive patch or sampler, Audio → pool item. This is what makes P1's output
**audible** — the whole point. Note the FM engine's `loadPatch` doesn't yet
write to the ValueTree (observability gap already flagged in the DX7 handoff) —
P2 should also fix that so the instrument assignment is visible/saved.

### P3 — Track templates
A `TRACK_TEMPLATE` store (project or app-level): recipe JSON → instantiated by
the same composite. Users/LLMs save a configured track as a template instead of
repeating the RPC sequence. Templates make the "assisted" path extensible
without new engine code per role.

### P4 — Fix the per-op rebuild in the existing generators
Route `generateIntoClip` through the batched path (accumulate, single rebuild)
even before P1 lands — pure win, matches lesson 6. Low risk, isolated.

---

## Bugs surfaced during this task (piano-roll de-clutter, committed 44dbaaf)

1. **Clip-picker dropdown silently no-ops when a timeline clip is selected.**
   `PianoRoll.tsx` active-clip precedence is `timelineSelectedId ?? internalClipId`
   (`PianoRoll.tsx:53`). The new `.pr-clip-select` (from the de-clutter) calls
   `loadNotes` → sets `internalClipId`, but if any timeline clip is selected the
   grid **stays on the timeline clip** while the dropdown visibly changes value.
   The E2E test had to `clearSelection` first to make switching work
   (`piano-roll.spec.ts`, "clip picker dropdown switches clips"). This is a real
   interaction bug: user picks a different clip, nothing changes in the grid.
   Fix direction: dropdown selection should also clear/bump the timeline
   selection (or precedence should be last-touch-wins), with an E2E regression
   test. **Pre-existing** (the old clip buttons had the same precedence) but the
   dropdown makes it discoverable.
2. **`npm run build` does not typecheck the frontend source.** Subagent running
   the build reported: "the extra `tsc -p tsconfig.json` (not part of the
   project's build pipeline — `npm run build` typechecks `electron/` only)
   shows ~30 pre-existing errors." So a TS error in `src/` passes the build and
   only surfaces in the browser/playwright. Tooling gap: `build` should run
   `tsc --noEmit` over the app, or the errors should be fixed and the check
   wired into CI. (Gate: CI / local `npm run build` must fail on TS errors.)
3. **Velocity-lane default-collapsed hides an editing surface.** Not a crash,
   but the de-clutter made velocity editing undiscoverable (must know the "Vel"
   toggle). Follow-up idea: auto-open the velocity lane when notes are selected
   (transient context), collapse when selection clears — keeps the decluttered
   default while preserving the workflow.
4. **+CC popover silently ignores a duplicate CC.** Adding a CC that already
   has a lane does nothing with no feedback (`setCcLanes` guard). Minor UX wart
   (pre-existing); a disabled state or toast would clarify.
5. (Test-authoring detail, not a product bug) Playwright `selectOption` rejects
   a raw number — must pass `{ value }`. Noted so it doesn't cost time again.

---

## Decision needed

Do we treat **P1 + P2** (composite track builder + default instruments) as a
planned feature? If yes, the follow-up is a proper plan doc
(`docs/plans/2026-08-18-track-builder.md`) with success gates, then
implementation via hdaw-guard (subagent tasks; C++ command + gtest, RPC + MCP
parity, frontend AddTrackMenu assisted flow, E2E). P4 is a safe standalone fix
worth doing regardless. Bug #2 (build typecheck gap) is cheap to fix and
unblocks CI confidence.

---

## Reference pointers

- Track creation: `src/engine/AudioEngineCommands_Tracks.cpp:9`, `AudioEngineCommands.cpp:133`
- Composite model: `AudioEngineCommands_Clips.cpp:728` (`generateArrangement` — the batched pattern to copy)
- Composition RPCs: `src/frontend/router/Router_Composition.cpp` (esp. `generateIntoClip` lambda at :77)
- MCP track tools: `src/mcp/McpTools_Project.cpp:159` (`add_track`), `:260` (`add_track_with_fx`)
- Default project (silent "Synth"): `src/model/ProjectModel.cpp:280`
- Piano-roll active-clip precedence: `frontend/src/components/PianoRoll.tsx:53`
- Build pipeline note: `frontend/package.json` `build` script typechecks `electron/` only
- Pitfalls that apply to P1–P4: lesson 1 (beats↔seconds), lesson 6 (batched routing), lesson 9 (default-project clip counts), MCP parity rule, Gate 1/10 (state restore on rebuild for any new processor state)