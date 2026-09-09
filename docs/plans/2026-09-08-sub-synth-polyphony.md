# Plan: SubtractiveSynthEngine Polyphony (param 24) — ✅ COMPLETE 2026-09-08

## Result
`sub_synth` is now polyphonic via param 24 "Polyphony" (0=mono default, 1=poly,
8 voices). Mono path bit-identical (all 11 pre-existing tests pass unchanged);
5 new poly tests pass; MCP round-trip verified live on the engine.

## Gate results (all pass)
- [x] Gate 1 (mono regression): 11 pre-existing SubtractiveSynthEngine tests
      pass with default settings.
- [x] Gate 2 (poly): PolyTwoNotesSoundSimultaneously (count 2),
      PolyNoteOffReleasesOnlyItsVoice (break-early render loop — do NOT
      hardcode release-length windows in voice tests),
      PolyVoiceStealingCapsAtEight, MonoDefaultStaysMonoWhenPolyNotesOverlap,
      ModeSwitchClearsSound.
- [x] Gate 3 (wiring): param 24 defs -> MCP set_internal_fx_param -> tree ->
      live setPolyphony, verified via get_internal_fx_param on the engine.
- [x] Gate 4: build OK (both binaries) + McpCoverageTest 79/79.

## Test-lesson learned
Voice-release tests must render until the voice DIES (break-early loop over
blocks), never a hardcoded sample window — the release envelope length is a
user param and activeNoteCount counts releasing voices (mono-consistent
contract). A fixed 20x512 window was timing-fragile.

## Design (as implemented)
- `polyVoices_[8]` + per-voice SVF pair + `poly_` atomic; mono path (`voice_`,
  `filter_`/`filterHp_`, heldNotes) untouched and bit-identical.
- Per-sample DSP extracted into `renderVoiceSampleCore(Voice&, SVF&, SVF&, float&)`;
  `advancePitch`/`advanceEnvelope` take `Voice&`. Mono wrapper applies
  outputLevel; poly sums cores * 0.5 * outputLevel (solo poly note = mono level).
- Poly note-on: first free voice, else steal quietest (min env*velocity).
  Poly note-off: release the voice playing that note; sustainHold per voice;
  pedal-up releases all held poly voices. Pitch bend applies to all voices.
- Legato/portamento stay mono-mode features (poly note-on always allocates).
- setPolyphony clears BOTH banks on switch (no orphan voices/stale filters).
- Wiring: defs {24, "Polyphony", 0, 0, 1}; prepare push; applyInternalParamToDsp
  case 24. MCP set/get_internal_fx_param are defs-driven — automatic.
- Debug accessors (tests): polyVoiceNoteForTest/polyVoiceEnvForTest/
  polyVoiceReleasingForTest.

## Files
- src/engine/SubtractiveSynthEngine.{h,cpp} — poly bank, core extraction
- src/engine/TrackFXSlot.h — param def 24 + prepare push + apply case
- tests/unit/engine/subtractive_synth_test.cpp — 5 poly tests
