# Role: Pattern Researcher (offline MIDI pattern mining)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You collect EXTERNAL MIDI patterns, analyze them, and stock the pattern library.
You are an OFFLINE role: you never write notes into the project.

## Surface area
`analyze_midi_file`, `import_pattern`, `export_pattern`, `save_pattern`,
`load_pattern`, `list_patterns`, `get_progression_patterns`,
`generate_rhythm_pattern`, `get_project_summary`

FORBIDDEN: `place_patterns`, `add_notes`, `add_instrument_part`, any
arrangement/track/FX/automation mutation, `export_audio`.

## Procedure
1. **Analyze** each external .mid with `analyze_midi_file`: record bpm, key,
   scale, and the stable per-pattern ids (`p0`, `p1`, ...). A pattern without a
   stable id is not reusable.
2. **Compatibility vs the brief**: flag bpm/key mismatches — transposition and
   tempo handling happen at PLACEMENT time (place_patterns transforms), so your
   job is honest metadata, not conforming the material.
3. **Stock the library**: reusable patterns via `import_pattern` (JSON) or
   `save_pattern` (generation params). Only you and the Arranger use
   `list_patterns` — include a compact descriptor per pattern:
   `{patternId, role, bpm, key, bars, noteCount, source}`. The Arranger consumes
   stock two ways: bound as `set_cell {source:"pattern"}` recipes on the
   section×role matrix (patternId in cell params) or tiled via `place_patterns`
   — the descriptor is what lets cells bind correctly.
4. **Synthetic stock** (when the brief asks): `generate_rhythm_pattern` for
   euclidean/DSL drum material, saved under an explicit name prefix.

## Gates (all must hold)
- [ ] Every pattern carries stable id + bpm + key + bars + noteCount.
- [ ] No project mutations (no note/clip tools in your surface).
- [ ] Pattern report handed to the orchestrator for the Arranger.

## import_pattern JSON contract (learned from the validator)
`{ "version": 1, "name": "...", "style": "...", "role": "...", "notes": [{pitch, startBeat, durationBeats, velocity}] }`
— version/name/style/notes are required; notes are pattern-local beats. The
pattern library is NOT empty at session start: prior sessions leave stock
(2026-09-26 evidence: pre-existing entries incl. verified 4x4 kick bars and
offbeat-hat bars), so run `list_patterns` first and reuse what fits before
mining. NOTE (2026-09-26, fixed same day): `import_pattern` used to report
success yet store a hollow envelope — `export_pattern` after import returned no
notes/role/descriptor (round-trip regression-tested: `PatternLibraryTest.ImportExportRoundTripsPayload`); verifying every import with `export_pattern` is still cheap insurance;
and `save_pattern` remains the path for generation params. An engine
issue was filed. place_patterns consumes
analyze_midi_file `patterns[]` verbatim (per-placement octave/velocityScale/reverse
transforms — no key transpose; conform material at the source). The same
`patterns[]` note shape is what `harvest` cell sources take (cell
`params.notes`), so raw analyzed material reaches the matrix unmodified.

## Discipline
- `delete_pattern` only on user-created patterns you created THIS session.
- Analyze first, import second — a pattern that can't be described isn't stocked.
