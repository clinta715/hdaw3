# Plan — BUG-3: Percussion phrase style ignores density + multi-pitch output

## Evidence
`PhraseGenerator.cpp:516` Percussion branch: fixed voices {36:4, 42:4, 38:2} with euclidean onsets across the whole clip. `density` ignored (96 requested → ~10 placed); multi-pitch output (36/38/42) on single-sample sampler tracks plays the same sample at wrong pitches.

## Fix
1. Density-aware per-voice hit counts: derive each voice's euclidean k from `density` (e.g. kick voice ≈ 1 hit/beat when density ≥ lengthBeats; hats 2/beat), clamped to the 16th-note grid.
2. Doc note in the tool description + composition guide: Percussion is multi-pitch — pair with multi-sampler key-range setups (`set_sampler_key_range`) for role-split kits.
3. Unit test in phrase-generator tests: density scaling (96-beat clip, density 96 → ≈96 notes; density 24 → ≈24) and pitch containment.

## Gates
- Note count scales with density; pitches within [lowNote, highNote]; existing phrase tests green.

## Effort/risk
0.5d. Risk LOW — isolated style branch.


---

## STATUS (2026-09-12)

**SHIPPED**: Percussion branch density-aware (voices split density 4:4:2, euclidean per voice, density 0-per-voice skips cleanly). Unit test `PhraseGenerator.PercussionDensityScalesNoteCount` (96→~96 notes, 48→~48, pitch containment) — green.
