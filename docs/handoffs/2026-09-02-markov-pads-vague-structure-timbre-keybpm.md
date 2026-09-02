# Handoff — Markov pads + vague macro-structure + timbre key/BPM (v0.26.0)

Date: 2026-09-02 · Branch: main · Version bumped 0.25.1 → 0.26.0
Follows: 2026-09-01-psytrance-composition-handoff.md (markov generator created there)

## What landed

### 1. Markov pads — thick chords + rhythmic gating (`PsytranceMarkovGenerator`)
- Pads emit triad/7th chord stacks (root/third/fifth + optional seventh,
  velocity-tiered), not thin root+fifth lines.
- Gated pads pulse on an 8th- or 16th-note grid with accent-on-downbeat
  velocity shaping; ungated pads sustain the full window.
- New `PadVariant` action (previously an unwired enum entry): toggles
  voicing (triad ↔ 7th), pulse grid (8th ↔ 16th), and a secondary harmony
  (alternate chord center + `padSecondaryShift` degrees, 3–4).
- `NoteLengthVariant` stays scoped to bass/arp/stab. Key discipline
  (1 home key + ≤1 secondary key) untouched and still tested.

### 2. Vague macro-structure (user ask: "build-ups/breakdowns, but not deterministic")
- `sectionCycleBars` is a **gravity well, not a grid**: each section draws its
  span seeded — {0.5×, 0.75×, 1×, 1.25×, 1.5×} × base, even-rounded, min 8 —
  biased by character (sparse/breakdown short, peak sustains).
- **Forced progress**: a state may renew itself once inside the base cycle;
  past that the boundary force-advances sparse→build→peak→breakdown(→build).
  Worst-case span ≈ 2.5× base.
- **Audible transitions**: the window where the section changes never emits
  `Keep` — section-appropriate structural/FX fallback runs (via the existing
  viability `fallback` chain).
- **Staleness ramp**: >6 windows (12 bars) without Add/Remove/Swap ramps
  +0.3 onto wSwap/wRemove (full by 10 windows) so elements never sit too long.
- Tests: `SectionTimingJittersAndCaps` (jitter + over-stay cap),
  `SectionTransitionsAreAudible` (no silent section change across seeds),
  `SectionChangesOnlyOnSlowBoundaries` now asserts 2-bar window alignment
  instead of the old fixed %16 grid.

### 3. Timbre pipeline key/BPM (user ask: key-aware sample search)
- `timbre-lib/lib_analyze.py`: emits `key` + `bpm` into `timbre_index.json`
  and per-file `.timbre.json`. Filename tag first (`Am`, `F#m`, `C min`,
  `G# Minor`, `Bb Maj`, `128 BPM`, `126_BPM` — underscore-aware boundaries,
  sharp/flat spelling preserved), else audio estimation (Krumhansl chroma,
  corr > 0.5 gate; onset tempo, 40–220 BPM guard, 0.0 = unknown). Cached
  records missing the fields get a lazy backfill on re-run.
- `FileLibraryManager::applyTimbreSidecar` ingests sidecar key/bpm (sidecar
  key OVERWRITES the native chroma guess; sidecar bpm only fills bpm<=0).
- `FileLibraryManager::extractAudioMetadata`: BPM fallback via
  `BpmDetector` (aubio) on the same decoded buffer used for chroma, when
  metadata has no tempo. Silence stays 0.0.
- Tests: `SidecarKeyAndBpmIngested`, `SidecarWithoutKeyBpmLeavesDefaults`,
  `AudioBpmFallbackDetectsClickTrain` (41/41 FileLibrary+BpmDetector green).
- `timbre-lib/backfill_keybpm.py`: key/BPM backfill for existing sidecars
  (~0.2 s/file, Windows python, pure librosa). **All 248 psy-pack sidecars
  backfilled** (236 key / 236 bpm / 12 honest neither — kicks, claps, hats).

## What we tried that did not stick (lessons)
- **WSL agent-kernel venv is not the ML runtime.** Installed
  librosa/torch/transformers into the prime-agent kernel venv to run the
  analyzer; worked, but the user correctly flagged it as the wrong home —
  fully rolled back (incl. setuptools restore to 84.0.0). The ML stack lives
  on **Windows python `py -3.14`** (numpy 2.5.2, torch 2.13.0+cpu,
  librosa 1.0.0, transformers 5.16.1). Run the analyzer/backfill through
  `cmd.exe /c py -3.14 ...` with Windows-style paths. HF CLAP model is cached
  on the Windows side.
- **librosa 0.11 vs 1.0 tempo API**: `librosa.beat.tempo` is gone in 1.0 —
  code tries `librosa.feature.rhythm.tempo` and falls back. The Windows 1.0
  run is also more honest: a beatless sine gets bpm 0.0 instead of 0.11's
  spurious ~128.
- **`llama_cpp` has no py3.11 wheel here and CMake build fails** —
  `lib_analyze.py` now imports `llm_stage` lazily so `--no-llm` runs without
  it (on either platform).
- **RLM child `timbre-key-bpm-pipeline` stalled in needs_input** without
  writing any code; the timbre work was implemented directly instead. The
  earlier `markov-pad-update`/`markov-pad-code` children did deliver and were
  merged/superseded; all children deleted at session end.
- **Backfill bug caught by spot-check**: first pass used an elif chain, so a
  filename key suppressed the filename BPM (`145bpm B` got key=B, no bpm).
  Fixed to independent field handling; second pass updated the stragglers.

## Pending (next engine session)
1. **Rescan psy packs in HDAW** — sidecar mtimes are now newer than audio, so
   `scan_library` per pack re-ingests key/BPM automatically (23 packs,
   ids via `list_libraries`).
2. Optional: full BPM coverage for the ~3,012 never-sidecar'd files →
   `remove_library` + `add_library` + `scan_library` per pack (native chroma
   + aubio fallback re-extract, ~20–50 min total), or let it happen naturally
   as files get touched.
3. Render A/B: regenerate a markov composition (fresh seed) and listen to the
   new section dynamics; verify builds/breakdowns land at drifting, non-uniform
   bars in the step log.
4. Library counts (2026-09-02): 23 psy packs, 3,260 audio files, 248 with
   sidecars, 3,012 never CLAP-analyzed (earlier runs stride-sampled ~14/pack).

## Junk cleanup done this session
- Deleted stray literal-named `C:\Tempuild_hdaw.bat` (old quoting-bug artifact).
- `*.bak` added to `.gitignore`; `HDAW_headless_debug_broken_20260918.exe.bak`
  and `models.bak` stay on disk, now ignored.
