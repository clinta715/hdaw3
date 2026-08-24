# Handoff: Voltage DnB MCP composition session (2026-08-23)

## Context

Built "Voltage DnB — Ghost Protocol" end-to-end purely through the HDAW MCP
surface (no code changes, no frontend involvement): 3:54, 174 BPM, 4/4,
G minor, 168 bars, 10 tracks.

- Project: `projects/Voltage DnB - Ghost Protocol.hdaw`
- Export: `projects/Voltage DnB - Ghost Protocol.wav` (24-bit/44.1k, ~60 MB,
  verified non-silent mid-file)
- Assets used: internal FM synth + DX7 cartridges
  (`D:\pdf\Dexed Presets\80's Librairy\{synths,synplus,strings,starma}.syx`),
  internal sampler + Lekebusch WAVs (`E:\samples\_lekebusch\SAE 01/02/03/05`),
  style/pattern analysis from `E:\midi\Voltage Vol.5 - MIDI`
  (*BS - Parallel Rhythm*, *LD - Ghost Sequence*, *LFO - Night Light*).
- Every part verified audible via `verify_part`; final export rendered clean.

What worked well (keep as-is): `analyze_midi_file` (fingerprint + motifs +
regen params), `fm_synth_import_sysex` (returns the full 32-voice list — made
precision preset picks trivial), `generate_rhythm_pattern` euclidean/polyrhythm
layering, `generate_automation_envelope` (all 11 shapes), `verify_part`,
`set_note_velocities` bulk randomize-as-humanize, `duplicate_clip`/`loop_clip`.

## Bugs surfaced (need fixes)

Ordered by severity. All reproducible over MCP/RPC; per AGENTS.md test
discipline each fix should ship with a gtest in the MCP tool / MIDI FX suites.

### BUG 1 — `generate_arrangement` writes every note at velocity 0 (Critical)

- **Symptom:** after `generate_arrangement` (seed 174, style 2/DnB),
  `get_clip` on the generated Bass clip showed all ~4500 notes across all six
  generated clips at `velocity: 0` — the entire skeleton was silent.
- **Workaround:** bulk `set_note_velocities` per clip with
  velocityMin/velocityMax ranges (doubled as humanization).
- **Suspect:** velocity parameter never plumbed from the arrangement generator
  into note creation (RhythmPatternGenerator / arranger path), or a default
  of 0 where 80–100 was intended.
- **Fix + test:** pass real velocities; gtest asserts every note of a
  generated arrangement has velocity > 0.

### BUG 2 — `generate_arrangement` ignores `targetTrackIds` for hat roles (High)

- **Symptom:** called with
  `{"Kick":0,"Snare":1,"ClosedHat":2,"OpenHat":3,"Bass":6,"Chords":9}`.
  Kick/Snare/Bass/Chords honored, but the hats landed on **newly created**
  tracks 10/11 ("Closed Hat"/"Open Hat") instead of my sampler tracks 2/3 —
  which then had no instrument and would have been silent.
- **Workaround:** `move_clip` both hat clips onto tracks 2/3, then
  `remove_track` the orphans.
- **Suspect:** role-key lookup by exact internal name (e.g. expects a
  different hat key than `ClosedHat`/`OpenHat`); when unmatched it silently
  creates a track even though an explicit index was supplied.
- **Fix + test:** when a role appears in `targetTrackIds`, always use that
  index (name irrelevant); only create a track for roles absent from the map.
  Unknown role keys should error, not fall through. Gtest: map every role to
  pre-made tracks, assert no new tracks appear and clips land on the mapped
  indices.

### BUG 3 — Arpeggiator MIDI FX outputs silence on sustained notes (High)

- **Symptom:** Stab track (Chords clip, held chord notes) through
  `arpeggiator` → `verify_part` returned `soloRms=0, audible=0`. Tried
  pattern 0 (up) with gate 0.9 and rate 0.25 — still fully silent.
  Bypassing the slot restored audio immediately (`soloPeak=0.155`).
- **Workaround:** removed the arp, replaced with `humanize`
  (velocity 0.3) — audible and useful.
- **Suspect:** arp retrigger/latch logic drops notes that are already held
  (no new note-on inside its window), or note-length handling breaks for long
  durations. Only verified failing on long/sustained notes; short-note arp
  behavior untested this session.
- **Fix + test:** minimal gtest — track + arp (defaults) + one held note
  (≥ 4 beats) → render, expect non-zero output. Then cover patterns 0–5 and
  gate extremes.

### BUG 4 — `add_automation_lane` silently no-ops on paramID collision with a built-in lane (Medium)

- **Symptom:** `add_automation_lane("Pad Volume", paramID=1)` returned `ok`,
  but `list_automation_lanes` showed only the built-in `Volume` (paramID 1,
  disabled) lane — no "Pad Volume". A subsequent
  `generate_automation_envelope` on "Pad Volume" failed with
  `lane not found`.
- **Workaround:** generate directly onto the built-in `Volume` lane, then
  `set_automation_enabled(Volume, true)`.
- **Related gotcha:** built-in lanes are disabled by default, and
  `generate_automation_envelope` happily writes points onto a disabled lane —
  the automation exists but does nothing until enabled. Easy silent failure.
- **Fix + test:** `add_automation_lane` should error (or return the existing
  lane) when the paramID is taken. Consider auto-enabling a lane when the
  first envelope point is generated, or at least log a warning. Gtest:
  colliding add must not return ok; envelope points on a disabled lane either
  apply or are refused loudly.

### BUG 5 — `audition_plugin` rejects internal FX slots (Low)

- **Symptom:** `audition_plugin(trackIndex=6, slotIndex=1)` on the internal
  FM synth → `slot is not a plugin`. The tool's purpose (de-risk silent
  plugins) doesn't cover the internal synths/samplers, which are exactly what
  MCP composition sessions use most.
- **Workaround:** `verify_part` per track after building parts (adequate but
  indirect — no per-preset audition before committing to a patch).
- **Fix:** support internal `fm_synth`/`sampler` slots in the audition path.

### BUG 6 — `generate_arrangement` response omits clip ids / role mapping (Low, DX)

- **Symptom:** response is `tracks=6 clips=6 notes=4523 seed=174` — no ids.
  I guessed clipId 0 for the kick and got `clip not found` (kick was 1;
  snare 4; ids non-sequential because euclidean clips minted ids in between).
  Required a `list_clips` sweep and per-track guessing to target velocity
  fixes.
- **Fix:** return `[{role, trackId, clipId, noteCount}]` so callers can chain
  `set_note_velocities`/edits without discovery calls.

### Observation (AMBIGUOUS — verify before acting)

- `verify_part` reported `bandsPresent=0` for kick, bass, lead, and pad
  despite `audible=1` and healthy RMS (snare reported 1). Either the bitmask
  semantics are undocumented/misleading or band detection is off for
  low-frequency material. Not investigated; someone who knows the intended
  semantics should check `bandsPresent` computation and document it.
- `generate_rhythm_pattern` hard-caps `bars` at 16 (schema-documented, clean
  error). Fine, but long sections require manual chunking — see suggestions.

## Streamlining suggestions

1. **Bulk note creation RPC/MCP tool (`addNotes`).** The lead motif cost 16
   sequential `add_note` calls (plus 8 more for each variation). AGENTS.md's
   own batch-RPC rule exists precisely to avoid N-loops, yet the note API has
   no batch form. One call taking an array of `{pitch, start, duration,
   velocity}` would collapse motif entry and reduce delta/rebuild churn.
2. **Close the analyze → pattern-library loop.** `analyze_midi_file` already
   returns bar-aligned patterns/motifs with notes, and
   `import_pattern`/`save_pattern`/`list_patterns` exist — but there is no
   bridge. A tool (or an extra field in the analysis result) that saves an
   analyzed motif as a pattern preset would make "import style from MIDI" a
   first-class resequencing workflow: analyze → save motif → `load_pattern`
   per section with variation. This session imported the *style* manually
   (motif rebuilt by hand from analysis output) because the bridge is
   missing. (The user-facing "pattern-remix" pillar is currently hard to
   reach from MCP.)
3. **Canonical automation paramID lookup.** FX-slot automation paramIDs are
   `100 + slotIndex*100 + paramIndex` (I computed 204/223/102 by hand). A
   `resolveParamId(trackId, slotIndex, paramName)` helper — or returning
   `paramID` directly in `list_fx_params` output — removes the arithmetic
   foot-gun.
4. **Arrangement generator velocity/humanize params.** Once BUG 1 is fixed,
   expose `velocityMin/Max` (+ optional swing already exists) so generated
   skeletons arrive performance-ready instead of needing a per-clip
   velocity pass.
5. **Auto-split long rhythm generations.** Let `generate_rhythm_pattern`
   accept `bars > 16` by internally chaining 16-bar clips (same params,
   contiguous starts), or document the `loop_clip` composition pattern for
   section-length euclidean layers. 32/64-bar sections currently need manual
   chunking (I made 9 calls for the percussion grid).
6. **Slice-mode percussion workflow.** One sampler track with
   `detect_sampler_slices` + slice mode could host a whole Lekebusch kit
   (pitch → slice mapping), collapsing my 6 sampler tracks to 1–2 and making
   kit swaps a single sample-load. Worth a cookbook example once slice
   auditioning (`trigger_sampler_slice`) is proven.
7. **MCP composition cookbook.** Capture the verified pipeline as a doc
   (`docs/`): analyze MIDI → set key/tempo/scale → tracks+FX → arranger
   skeleton (with velocity fix) → euclidean layering → FM patches via sysex →
   MIDI FX → envelope automation → per-section `verify_part` (seek first!) →
   export + silence check. Include the pitfalls above; it doubles as a
   regression checklist for the tool surface.

## Suggested gtests (with fixes)

- `ArrangementGenerator.GeneratedNotesHaveVelocity` (BUG 1)
- `ArrangementGenerator.TargetTrackIdsHonoredForAllRoles` /
  `...UnknownRoleKeyErrors` (BUG 2)
- `MidiFx.ArpeggiatorHeldNoteProducesAudio` (+ pattern/gate matrix) (BUG 3)
- `Automation.AddLaneParamIdCollisionErrors` and
  `Automation.EnvelopeOnDisabledLaneIsLoud` (BUG 4)
- `McpTools.GenerateArrangementReturnsClipIds` (BUG 6)

## Session facts for whoever picks this up

- No source files were modified this session — pure MCP project work; nothing
  to build or test beyond the fixes above.
- The project file is a good fixture for tool-surface regression: it contains
  euclidean clips, sysex-loaded FM patches, 8 automation lanes (2 with
  generated envelopes incl. disabled-lane case), 3 MIDI FX slots, and one
  humanize substitution where the arp was removed.
- Stab track (`trackId 9`) intentionally has no arpeggiator — do not "restore"
  it until BUG 3 is fixed.
