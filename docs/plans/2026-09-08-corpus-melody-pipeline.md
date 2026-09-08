# Plan — Corpus melody pipeline (read melodies from MIDI, feed the Markov generators)

**Date:** 2026-09-08 · **Status:** Plan (not started) · **Owner:** this session

Follows the completed drum-phrase-bank work (v0.30.0, `RhythmPatternBank.h`,
`percCorpusPhraseProb`). Extends the same corpus pipeline from **rhythm-only**
to **melodic** content, and wires it into the Markov generators. This is a
**score-level generation** feature — no `processBlock`/DSP/audio-thread/
render/export changes — so it lives in the low-blast-radius lane the
sound-engine stability rule prefers.

---

## 1. Goal

1. **Mine melodies** (pitch contours + rhythm) from MIDI files in the corpus
   and curate them into a **key-relative** melody bank (transposable to any key).
2. **Feed the Markov generator(s)**: let melodic roles (arp/stab/pad/lead)
   source from the bank, transposed/voiced into the current harmony — via
   **opt-in default-0** params (the `percCorpusPhraseProb` contract).

Non-goals (explicit): no new audio-engine path, no processBlock change, no
new genre "generator" component for its own sake.

## 2. Current state (what already exists to reuse)

| Piece | Location | Role |
|---|---|---|
| Rhythm corpus pipeline | `tools/analyze_drum_midis.mjs`, `extract_phrase_bank.mjs`, `extract_kit_phrases.mjs`, `curate_bank.mjs` | SMF parse → per-role accent vectors → static bank. **Rhythm only, key-agnostic.** |
| Static rhythm bank | `src/engine/RhythmPatternBank.h` (62 phrases) | `{id, role, bars, grid, dsl, pitch, bpm, source}` DSL strings |
| MIDI analysis | `src/engine/MidiAnalyzer.{h,cpp}` | `MidiFingerprint`, **scale/root detection**, `MidiPattern`s + motifs → PatternPreset JSON. Per-file today, not corpus-batch. |
| Generative melodic Markov | `PhraseGenerator::MarkovMelody` + 20 melodic styles | Phrase-level Markov (stateCount/rhythmGrid), scale-degree key discipline |
| Markov arranger melodic voices | `MarkovArranger.cpp` (arp/stab/pad via role pools + `scaleDegreeToPitch`) | Score-level, in-key melodic layers |
| Opt-in corpus seam | `PsytranceMarkovParams.percCorpusPhraseProb` (default 0) | The exact pattern to copy for melodic voices |

## 3. The conceptual gap

The drum bank stores **accent vectors** (pitch-invariant). Melodies carry
**pitch contours** that must stay in-key. So the melody bank must store
**key-relative scale degrees + rhythm + a scale** (not absolute MIDI notes),
and a **transposition/voicing** step must fit a contour into the current
chord/key per bar. That voicing step is the one genuinely new engine piece.

---

## 4. Phases, dependency map, and success gates

### Phase 0 — Corpus melody availability scan (gated first step)

**Do NOT commit to an extractor before knowing the data is usable** (this
avoids the drum-kit trap where 1,159 candidates were mostly trivial).

- Extend/reuse `MidiAnalyzer` scale+root detection to batch-scan the melodic
  packs (`E:\midi\[1] Drum MIDIs` 808 / PHONK subdirs, plus any melodic source)
  and report: how many files have a usable monophonic/melodic line, key
  distribution, contour length, bar-alignment.

**Success gates (Phase 0):**
- G0.1 A scan script (`tools/scan_melodies.mjs` or an `MidiAnalyzer` extension)
  reports per-pack usable-melody counts, top keys, and median contour length.
- G0.2 Written decision: which packs feed the bank, and a realistic starting
  bank size (>= 20 non-trivial phrases expected, else source-data gap is called
  out explicitly).

### Phase 1 — Melody extractor + `MelodyPatternBank.h`

- New extractor (JS, mirroring `extract_phrase_bank.mjs`) that: parses SMF,
  detects scale/root, converts notes → **scale degrees**, captures rhythm +
  duration + contour, dedupes across files, filters non-trivial phrases
  (reuse `curate_bank.mjs` curation gates).
- New **`src/engine/MelodyPatternBank.h`** (header-only, additive to
  `RhythmPatternBank.h` — do NOT mutate the drum bank or its count tests).
  Phrase row: `{id, role, bars, grid, scale, degreeContour, rhythm,
  dslOrNotes, bpm, source}`.

**Success gates (Phase 1):**
- G1.1 `MelodyPatternBank` enumerates its phrases; count assertion in a new
  `melody_pattern_bank_test.cpp` (scoped to the new bank only, per lesson 9).
- G1.2 A phrase transposes to any of 12 roots and stays in-scale (property
  test over all bank phrases × all keys).
- G1.3 Existing `RhythmPatternBank.*` tests unchanged and green.

### 8.5.1 Phase 1 — Results (2026-09-08) ✅

**Done.** `tools/extract_melody_bank.mjs` (reuses the SMF parser; detects
scale+root with **tonic-support tie-breaking** so relative-major/minor
ambiguity resolves to the real tonic; reduces each bar to a monophonic lead;
converts notes to key-relative {step, degree, octave, dur}; dedupes across
files; per-pack round-robin diversity + per-role caps) mined **87 curated
phrases** (45 lead / 30 bass / 12 chord) from the Phase-0-selected packs,
3,031 notes. Scale distribution is musically realistic (naturalMinor 29,
phrygian 19, major 20, + dorian/locrian/harmonicMinor/blues).

**`src/engine/MelodyPatternBank.h`** (header-only, additive — drum bank
untouched): `MelodyNote`{step,degree,octave,durSteps}, `MelodicPhrase`{id,
role,bars,grid,rootPc,scaleMode,bpm,source,noteOffset,nNotes}, a 12-mode
scale table, and pure transpose helpers (`melodyNotePitch` — register-
preserving NEAREST transposition, wraps delta to [-5,+6] so root-wraps never
jump an octave; `melodyDegreeToPitch` — explicit octave; `melodyNoteInScale`).

**Tests (all green):** `melody_pattern_bank_test.cpp` (6 tests) — G1.1
enumeration (count = 87), G1.2 property test (all 87 phrases × all 12 roots
stay in-scale + in-range, 0 violations), roles/contour sanity, source-key
reconstruction, register preservation. Existing `RhythmPatternBank.*`
unchanged and green (5/5).

**Known limitation:** mode bias corrected for the dominant minor/phrygian
packs via tonic support, but a relative-major (A-minor-as-C-major) reading is
still possible for melodies where the tonic isn't emphasized — acceptable for
a first bank (contours are exact; voicing re-fits to target harmony in Phase 2).

### Phase 2 — Transposition/voicing layer + Markov arranger wiring

- New score-level voicing: fit a bank contour into a bar/chord — modes:
  **diatonic** (map degrees, preserve contour; default), **root-relative**
  (absolute pitch class, for when the source key matters), **chord-tone snap**
  (nearest chord tone) — one function, seeded/deterministic, in the score
  layer (no engine/DSP).
- Wire a **corpus melodic voice** into `MarkovArranger`: when an
  arp/stab/pad/lead role is drawn and the new opt-in param fires, source the
  role's bar from the bank (`bars[curBar % phraseBars]`, transposed), instead
  of the pool-synthesized line.

**Success gates (Phase 2):**
- G2.1 New opt-in params on `PsytranceMarkovParams` (`*CorpusPhraseProb`,
  `melodyTransposeMode`, `melodyContourMutation`) — **all default 0/off**;
  with defaults, `generate()` output is **byte-identical** to the current
  per-seed baseline (property test: same seed+params → same score).
- G2.2 With `*CorpusPhraseProb > 0`, every pitched note in the generated score
  is in-scale (key-discipline invariant, same as existing `scaleDegreeToPitch`
  checks).
- G2.3 RPC + MCP parity: `generatePsytranceMarkov` and the MCP tool accept the
  new params; frontend picker optionally surfaces a melody source.

### Phase 3 (optional, only if the bank is rich enough) — Motif-stitching melodic Markov

A genuinely new topology: learn transition probabilities **between** corpus
melody fragments and stitch them into evolving lines (distinct from the
scale-driven `MarkovMelody`). Layered on `MarkovMelody`/`PhraseGenerator`,
not a fresh standalone component. **Deferred — revisit after Phase 2 evidence.**

---

## 5. Blast radius / dependency analysis

| Touched surface | Upstream callers | Risk |
|---|---|---|
| New `MelodyPatternBank.h` | New (additive). No existing caller. | Low |
| New extractor `tools/*.mjs` | Standalone; no engine dependency | Low |
| Voicing function (score layer) | `MarkovArranger` melodic emission only | Low–Med |
| `PsytranceMarkovParams` new fields | `generatePsytranceMarkov` RPC, MCP tool, `MarkovArranger` | Med (params are the contract) |
| `MarkovArranger` arp/stab/pad emission | `generate()`, per-seed score tests | **Med** — must preserve default-0 byte-identical path |

No audio-thread, SPSC, ValueTree, plugin, or render/export surface is touched.
`RhythmPatternBank.h` and its count tests are deliberately NOT modified.

## 6. Pitfall-gate mapping (hdaw-guard 16)

- **P1 beats-vs-seconds**: melodies carry startBeat (beats) in the score layer
  — no seconds conversion needed; keep the score layer beats-only.
- **P6 O(project) rebuilds**: new bank is header-only/score-layer; no routing
  graph involvement. No incremental-routing concern.
- **P9 clip-count hardcoding**: new tests assert on `MelodyPatternBank`
  enumeration, never absolute clip counts.
- **P10 projection seams**: no ValueTree/processor state added; voicing is a
  pure function. No rebuild-restore concern.
- **P13 DSP-state locks / P23 DSP clamps**: no DSP objects, no recursive
  comb/feedback networks; voicing clamps degree→pitch via the existing
  `scaleDegreeToPitch` contract (-1 on out-of-range).
- **Same-seed determinism** (arranger sequencing contract): the ONLY
  sequencing contract is preserved by keeping all new params default-0 —
  with defaults the draw order is unchanged.

No pitfall gate triggers an engine-path change; this stays score-level.

## 7. Anti-pattern scan

- **Third generator for its own sake** — avoided: the plan adds data + a
  voicing seam + one optional motif-stitch topology, not a redundant component.
- **Param bloat** — avoided: Phase 2 adds a small opt-in set, default-0,
  single-sourced in the params struct.
- **Hardcoded frontend list drift** (already flagged in the handoff for the
  drum bank): if a melody picker is added, plan to add a
  `composition.getRhythmPhrases`-style RPC OR accept and document the sync
  burden.
- **Optimizing before data** (the 1,159-candidate drum-kit lesson): Phase 0
  gates everything on a real corpus scan.

## 8. Testing strategy

- New gtest suites: `melody_pattern_bank_test.cpp`, `melody_transpose_test.cpp`.
- Property tests: all phrases × all 12 keys in-scale; default-params
  byte-identical baseline vs current `generate()` output.
- RPC/MCP: `rhythm_generation_rpc_test.cpp` / MCP coverage extended for the
  new params (mirror `CorpusPhrase*` tests).
- Frontend: `PhraseGeneratorDialog.test.tsx` if a picker is added.
- Run: engine filter + full suite before delivery; frontend `npm test`;
  `run_fast_tests.bat` then full suite.

## 8.5 Phase 0 — Results & Decision (2026-09-08) ✅

**G0.1 — Scan tool + run (done).** `tools/scan_melodies.mjs` (reuses the
SMF parser from `analyze_drum_midis.mjs`, detects scale+root over pitch
classes, classifies `usable` = detectable key (fit>=0.7) + contour>=4 +
avgPoly<=3). Scanned all of `E:\\midi` (17,052 files):

- **12,558 usable melodic files (74%)** — the corpus is melody-rich, far
  beyond the drum-only expectation.
- Top lead/arp packs (monophonic-ish, high contour): Relooped Midi
  Collection (175 @ poly 1.1, contour 12), AKUMU Melody (88 @ 15), MELODIC
  PHRASES (50 @ 14, poly 1.0), GLOSS Melody (23 @ 14), Clark Trap MIDI
  Melodies (46 @ contour 18), Cymatics Apex Trap/Electronic/RnB (454/186/160
  @ contour 12-16, poly 1.6-1.8), Vega/Audiotent/Delta Note (98/91/49 @
  contour 8-10), Synth Lead (99 @ poly 1.0).
- Bass sources: F9 Bassline (123), Avant 140 Bass (136), Bass MIDI (89),
  Clark 808 (40), CC_*_BASS (25-28 each), GHHH Bass (27), PHONK (173).
- **Caveat — key clustering:** top keys skew C/C#/D# major (pack pitching
  bias). Because the bank stores **scale-degree contours** (not absolute
  keys), source-key clustering does NOT hurt playback (it transposes into
  the target key); it only means source-key diversity is irrelevant for the
  bank. Minor/major ambiguity in detection is acceptable — we need a
  consistent scale for the contour, and voicing re-fits to the target harmony.

**G0.2 — Written pack-selection decision (done).**

- **Lead/arp sources (primary):** Relooped, AKUMU Melody, MELODIC PHRASES,
  GLOSS, Clark Trap MIDI Melodies, Cymatics Apex Trap/Electronic/RnB, Synth
  Lead, Vega/Audiotent/Delta Note.
- **Bass sources:** F9 Bassline, Avant 140 Bass, Bass MIDI, Clark 808,
  CC_*_BASS, GHHH Bass, PHONK.
- **Chord/stab sources (secondary):** Chord Stabs MIDI (25, poly 3), Synth
  Chord/Pad (9/8), POP Chord Progressions (35).
- **Realistic starting bank size: 50-70 phrases** across lead/arp/bass/
  chord-stab roles (matches the drum bank's 62 scale), curated multi-bar
  non-trivial phrases via the existing `curate_bank.mjs` gates. 12,558
  usable files are far more than needed — curation, not scarcity, is the
  constraint. **Source-data gap: none.** Phase 1 extractor is unblocked.

## 9. Out of scope (this plan)

- Audio-engine / DSP / processBlock / plugin / export changes.
- New standalone genre Markov component.
- Expanding the *drum* bank further.

---

## Recommendation

Start with **Phase 0** (corpus melody availability scan) — it is cheap,
unblocks the whole plan, and avoids re-committing to a source-data gap. Phase 0
produces the written pack-selection decision (G0.2) before any extractor work.