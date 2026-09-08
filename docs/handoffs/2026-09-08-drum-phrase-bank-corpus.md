# Handoff — 2026-09-08: Corpus-derived drum phrase bank

## What shipped (v0.30.0)

Mined real drum grooves from a MIDI corpus (`E:\midi`) and wired them into the
drum pattern generators end-to-end (engine, RPC, MCP, UI).

### The bank
- `src/engine/RhythmPatternBank.h` — **62 multi-bar drum phrases** (1/2/4/8-bar)
  across kick/snare/clap/hats/perc/ride, each `{id, role, bars, grid, dsl, pitch,
  bpm, source}`. Header-only (adding a phrase = adding a row, no rebuild).
  Roles: kick, snare, clap, hats, perc, ride. `rhythmPhraseCount() == 62`.

### Analysis pipeline (reusable, in `tools/`)
- `analyze_drum_midis.mjs` — standalone SMF parser + role/pitch stats.
- `extract_phrase_bank.mjs` — multi-bar phrase extraction for single-instrument
  packs (role from filename); dedupes across files.
- `extract_kit_phrases.mjs` — role-**from-pitch** extraction for full-kit packs.
- `curate_bank.mjs` — filters to multi-bar/cross-file/non-trivial, capped per
  role, emits C++ array lines. Flags: `--min-bars`, `--min-hits`, `--cap`.
- `emit_pattern_bank.mjs`, `extract_role_bank.mjs`, `phrase_view.mjs`, `corpus_scan.mjs`.

### Engine
- `RhythmPatternGenerator` — additive `generatePhrase(id)`, `generatePhraseByRole(role, idx)`,
  `applyPhrase(Params&, id)`, `applyPhraseByRole(Params&, role, idx)`. Phrase drives the
  DSL voice alone (pulses off). Pure/score-level — NO `processBlock`/DSP/audio-thread change.
- `PercussionEngine` — `PercTheme` gained optional multi-bar `hatPhrase`/`snarePhrase`
  grids; `writeWindowNotes` indexes `bars[curBar % phraseBars]`. New `corpusPhraseProb`
  on `PercussionStyle` — **OPT-IN (default 0)** so the legacy euclidean-only theme draw
  stream stays byte-identical (verified: `SnareRimRolesGenerate`/`ThemeRotatesAsUnit` pass).
- `PsytranceMarkovParams.percCorpusPhraseProb` (default 0) wired through `MarkovArranger`.

### Surfaces (full parity)
- RPC: `generateRhythmPattern` (`phrase`/`phraseRole`/`phraseIndex`),
  `generatePsytranceMarkov` (`percCorpusPhraseProb`).
- MCP: `generate_rhythm_pattern` + `generate_psytrance_markov` (schema + handler).
- UI: `PhraseGeneratorDialog` Rhythm mode Corpus Phrase picker (hardcoded id list —
  NOTE: this drifts from the C++ bank; a `composition.getRhythmPhrases` RPC would
  remove the duplication if the bank grows further).

## Tests (all green)
- `RhythmPatternBank.*` (5) — incl. `BankEnumeratesAllPhrases` = 62.
- `RhythmGenerationRpc.CorpusPhrase*` (3) + `PsytranceMarkov.CorpusPhraseMultiBarPercussion`.
- `McpCoverageTest.GenerateRhythmPattern*` (4) + `GeneratePsytranceMarkovCorpusPhrase`.
- Frontend `PhraseGeneratorDialog.test.tsx` (4).

## Gotchas for future work
- **The build hang was NOT CMake** — `build-fast.bat` calls `scripts\time-sync.cmd`
  (`wsl.exe -e bash sudo ntpdate`), which can hang in this environment before compiling.
  Bypass with a direct `vcvars64.bat` + `cmake --build build --target hdaw_tests` (log to
  file). That is the reliable incremental build path here.
- `extract_kit_phrases.mjs` over-extracts melodic 808/pad parts as `perc` (pitch 60 = middle C).
  If mining more full-kit packs, add a melodic-confidence filter.
- Frontend phrase list is hardcoded — keep in sync with the bank, or add the RPC.

## Not done / candidates
- `composition.getRhythmPhrases` RPC to kill the frontend/bank duplication.
- Melodic-confidence filter in the kit extractor.
- Bank is at 62; further packs (808, PHONK) are mostly melodic basslines — low value.