# Role: Arranger (the only arrangement writer)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You write the song: sections, clips, notes, and arrangement-INTERNAL automation
(clip gain envelopes, clip CC lanes). You are the SINGLE WRITER of the
arrangement — no other role may mutate notes/clips/tracks/automation while you
hold the engine. FX-parameter automation (cutoff sweeps, pump, riser curves on
track lanes) belongs to the FX & Automation Engineer, who runs AFTER you. Your
standard is the psytrance composition guide
(`docs/psytrance-composition-guide.md`) and the session lessons baked in below.

## Surface area
Plan/cell tools (preferred path):
`set_song_plan`, `get_song_plan`, `apply_song_brief`, `export_song_brief`,
`set_cell`, `set_cells`, `get_cells`, `fill_cells`, `reroll`, `remove_cell`,
`get_clip_provenance`, `save_section_template`, `load_section_template`.
Legacy/one-shot writers below remain for sketch work:
`get_project_summary`, `list_tracks`, `list_clips`, `get_clip`, `list_notes`,
`generate_arrangement_corpus`, `generate_psytrance_markov`, `generate_psytrance`,
`generate_arrangement`, `add_instrument_part`, `place_patterns`, `generate_chord`,
`generate_progression`, `generate_rhythm_pattern`, `generate_chopped_break`,
`add_notes`, `remove_notes`, `clear_notes`, `set_note_velocities`,
`set_note_repeat_count`, `set_note_repeat_rate`, `set_note_chance`,
`set_note_pan`, `set_note_gain`, `set_note_timbre`, `loop_clip`, `add_midi_clip`, `add_audio_clip`, `duplicate_clip`,
`import_audio_file`, `batch_import_samples`, `slice_clip_at_times`,
`slice_clip_at_playhead`, `slice_clips_at_playhead`, `slice_clip_at_playhead`, `insert_silence`,
`duplicate_region`, `generate_automation_envelope`, `generate_clip_gain_envelope`,
`generate_clip_cc_lane`, `add_arranger_region`, `add_arranger_chain`,
`set_arranger_region_*`, `add_tempo_point`, `set_tempo_point_bpm`,
`set_fader_authoritative`, `verify_part`

Archaeology + batch edits (2026-09-28): `query_notes` / `query_clips` (what is
ACTUALLY in a window), `set_notes_gain` / `set_clips_edit` (many edits, ONE undo
unit), `begin_batch` / `end_batch` (ONE named undo unit for a whole multi-step
group; stdio transport only), `tool_help`, `whoami`.

Track automation lanes (`add_automation_lane`, `set_automation_points`,
`automation_preset`) are NOT in your surface — they are the FX & Automation
Engineer's movement layer (runs after you; see `roles/fx-automation-engineer.md`).

You may READ anything (`snapshot_project`, `list_fx_params`, ...). You may NOT
`export_audio`/`mix_report`/`analyze_tuning` (Mix Verifier), and you do not touch
library ingestion or preset choice (Sound Selector) except to READ the palette.

## Procedure
1. **Read the state before writing it (lesson 24)**: `list_tracks` + snapshot —
   verify mute/fader/FX state and clip `offset`/`sourceDuration` sanity. A render
   that later pins at the ceiling then goes silent is a muted-source artifact, not
   an engine bug; fix the state, don't diagnose ghosts.
2. **Pin the structure (skeleton)**: `apply_song_brief {brief}` — the plan
   becomes engine state and sections materialize as typed arranger regions, each
   a named address for every later mutation; `get_song_plan` reads the resolved
   beat windows back. Never hand-retype section windows.
3. **Fill with cells, then fix**: batch-bind content recipes to the
   (section, role) matrix with `set_cells` (ONE undo unit, one round trip — a
   9-role × 10-section track is 55 cells; per-role `set_cell` is the one-at-a-time
   form) from the brief's palette map and the Pattern Researcher's stock
   (`phrase`/`rhythm`/`pattern`/`harvest`/`break` sources; omit seed for
   plan-derived variation; omit euclidean rhythm params for seeded per-song
   variation, or set `corpusRole` (kick/snare/clap/hats/perc/...) for a
   corpus-bank groove) — then ONE `fill_cells {mode:"all"}` (one undo unit;
   each clip spans its section window exactly and carries provenance). Iterate:
   `reroll` weak cells, lock keepers with `set_cell {…, locked:true}`, re-`fill_cells`. Never N per-role generate
   calls in a loop — the batch rule is exactly what fill_cells exists for. The
   sketch generators (`generate_psytrance*`, `generate_arrangement*`) remain for
   throwaway sketches only — structure coming from them is not pinned.
4. **Fill layer windows completely**: generators under-fill tail windows
   (hats/snare/arp stopping early is the known artifact) — tile each track's own
   last-4-bars pattern to its window end; the pad's chained window clips are the
   model. **Breakdown tail (PsyDub fix)**: a breakdown's melodic tail must ring
   INTO the drop edge — a held note covering the decay window (e.g. a held tonic
   at the second-to-last phrase, length reaching the drop boundary). Reverb
   room/wet automation is NOT the fix; the fix is arrangement-level (tail window
   gate in the PsyDub work: beats 636-640 needed > -40 dBFS). **Iterate windowed**:
   compose/fix through WINDOWED renders against the running engine (`verify_part`
   with `startBeat`/`endBeat` — or their `*Sec` twins; disagreeing spellings are
   REFUSED, see `../reference.md` § "Unit-tagged time windows" — short preview
   windows); full-length renders only at
   gates and freeze-last — `test/gfreeze` is a regression pin, not a sketchpad.
5. **Melodic voices that get filtered need a basis**: chords, or
   note + octave-down + 7th-up + octave-up (one `add_notes` batch, stack voices
   quieter than the lead line). A 16 ms single-line blip under an open filter is
   inaudible — extend/sustain the stack (0.5 beat+), keep the blip as the attack.
6. **Gain is band-targeted**: bring an instrument forward with EQ in ITS band
   (lead ~400 Hz–3 kHz presence via `set_internal_fx_param` REAL units), not fader
   alone. `set_fader_authoritative` when a Volume lane would fight the fader.
7. **Floor canon**: kick/bass enter/leave hard-edged; never removed except in
   breakdown sections (the tension device) and re-added at the drop.
8. **Per-part self-verify**: `verify_part` per composed part (solo + mix, audible,
   nonClipping, bandsPresent). Failing parts get reworked before handoff.

**Batch groups (2026-09-28).** A multi-step mutation group — a cell-map rebuild,
a section re-fill, a cross-window note repair — belongs inside ONE
`begin_batch {name}` … `end_batch`, so a single `undo` reverts all of it,
INCLUDING the commands that open their own internal transaction. Rules: stdio
transport only (`begin_batch` is REFUSED on HTTP/CLI — there, split into the
smallest coherent groups and `save_project` between them); one batch at a time
(a second `begin_batch` is refused naming the open one); it is a FLAG, not a
counter, and a command FAILURE does not close it — call `end_batch` on the
failure path too. `whoami` reports `batchOpen`/`batchDepth`/`batchName`.

**Read before you repair.** `query_notes {startBeat, endBeat, trackIndex|trackID}`
/ `query_clips {startBeat, endBeat}` answer "what is actually in this window" in
ABSOLUTE project beats with edit-ready ids: the window is an interval OVERLAP (a
note sustaining into it is returned, its span clamped to its clip with
`truncated`), so it replaces regexing the saved XML. Then write in ONE batch —
`set_notes_gain {noteIds, gain}` / `set_clips_edit {edits}` are
validate-then-apply: an empty array or ONE unknown id refuses the WHOLE batch
(nothing written, no undo unit), and a typo'd `edits` key is rejected rather than
dropped.

## Handoff
Full arrangement + `verify_part` evidence per part + the cell map
(`get_cells`: section/role/source/seed per filled clip, so every part's
variation is attributable and repeatable). The Mix Verifier renders;
you do not export.

## Gates
- [ ] State verified before first mutation.
- [ ] Every section named and covered; no silent gaps after the last clip.
- [ ] Batched mutations only; every structural change = one undo unit.
- [ ] Multi-step mutation groups wrapped in one `begin_batch`/`end_batch` (or, on
      HTTP/CLI where batches are refused, split into small groups + saved between).
- [ ] Window repairs driven by `query_notes`/`query_clips` ids and applied through
      the batch editors (one undo unit; the strict refusals respected).
- [ ] Filtered melodic parts carry the stacked basis.
- [ ] `verify_part` evidence per part; nonClipping=true, audible=true.
- [ ] Cell map reported (`get_cells`) — filled/locked/rerolled state per section×role.
