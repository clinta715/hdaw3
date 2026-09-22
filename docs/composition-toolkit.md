# Composition toolkit — generative, randomization, modulation, hardware VA

Moved out of AGENTS.md (2026-09-22). The canonical deep-dive for the
hardware VA suite is hardware-va-suite.md (§9 = per-engine measured
status); the psytrance recipes live in psytrance-composition-guide.md.
This file preserves the toolkit overview + loader status verbatim.

## Generative composition, randomization & modulation

**Render output convention (standing):** all composition renders — final
track exports, verification windows, and the WAVs fed to `mix_report` /
`analyze_tuning` — go to the repo-root `compositions/` directory
(`D:\pdf\roo projects\hdaw3\compositions\`, gitignored via `/compositions/`).
Do not write render output into `tools/`, the home dir, or other scratch
locations; MRT2 one-shot *sound design* samples (the raw sound palette)
stay in `tools/mrt2/sounds/`, but anything rendered from a project goes to
`compositions/`.

HDAW is a *generative* DAW, not just a recorder. Assisted creation is a core
product pillar and should be reached for wherever it fits:

- **Generative composition** lives in `PhraseGenerator` (`src/engine/PhraseGenerator.h`):
  scale-aware phrase styles (Standard, Arpeggio, BassLine, ChordStab, Pad, Lead,
  RandomWalk, Buildup), single-chord and chord-progression generation, scale
  modes, chord types/voicings/inversions. Exposed over RPC as
  `composition.generatePhrase/generateChord/generateProgression` (and matching
  MCP tools), surfaced in the UI by the **Compose tab** (TransportBar 🎵 /
  Ctrl+Shift+G; a docked bottom-panel tab — the old `PhraseGeneratorDialog`
  modal was retired into it, no more auto-close-on-generate).
- **Rhythm / drum patterns** come from `RhythmPatternGenerator`
  (`src/engine/RhythmPatternGenerator.h`): two euclidean pulses
  (polyrhythm) plus a rhythm-DSL voice (`E(k,n[,rot])`, groups).
  Exposed over RPC as `composition.generateRhythmPattern` (and MCP
  `generate_rhythm_pattern`), surfaced in the UI by the "Rhythm" mode of the
  Compose tab. A **corpus-derived drum phrase bank**
  (`src/engine/RhythmPatternBank.h`, 62 multi-bar phrases across
  kick/snare/clap/hats/perc/ride) feeds `generatePhrase(id)` /
  `applyPhrase(...)` factories (RPC `phrase`/`phraseRole`/`phraseIndex`;
  MCP mirrors). Markov percussion (`PercussionEngine` hat/snare theme voices)
  can source from the bank via opt-in `percCorpusPhraseProb` on
  `generate_psytrance_markov` (default 0). The bank is grown by the reusable
  `tools/` corpus pipeline (`extract_phrase_bank.mjs` single-instrument,
  `extract_kit_phrases.mjs` role-from-pitch full-kit, `curate_bank.mjs` →
  C++ rows).
- **Randomization / humanization** — note timing, velocity, and pitch
  humanize in the piano roll (`NoteGrid`) and clip editor (`ClipEditor`).
- **Modulation** — a per-track LFO system (`ModulationManager` /
  `LFOModulationSource`, track `MODULATION_LIST` ValueTree, `rebuildModulation`)
  that modulates parameters in the audio engine. The sub_synth's internal LFO
  additionally ships six factory **mod presets** applied atomically
  (`apply_sub_synth_mod_preset` MCP / `project.applySubSynthModPreset` RPC /
  FX Chain Mod button).
- **Song plan + cells** (plan/cell workflow —
  `docs/plans/2026-09-11-song-composition-workflow.md`) — deterministic
  structure, seeded content: a root `SONG_PLAN` ValueTree node + section-typed
  arranger regions pin the skeleton (section kinds = `PsytranceSectionKind`,
  4/4); cell recipes (phrase/rhythm/break/pattern/harvest) fill per-section
  windows in ONE undo unit with clip **provenance** (`genTool/genSource/
  genSeed/genParams`). Surfaces: MCP `set_song_plan`/`apply_song_brief`/
  `set_cell`/`fill_cells`/`reroll`, matching `composition.*` RPC, and the
  Compose tab ▸ **Song Plan** panel; `mix_report` accepts `fromPlan: true`;
  section templates persist under `AppData/HDAW/section-templates`. Variation
  comes from re-seeded content, never from structure drift.

- **Hardware VA suite (gearmulator CLAPs)** — OsTIrus (Virus TI), Osirus
  (Virus A/B/C), Vavra (microQ), Xenia (Microwave), JE8086 (JP-8000),
  NodalRed2x (Nord Lead 2x), Dexed (DX7) run as isolated CLAPs with their real
  firmware (installed in `C:\Program Files\Common Files\CLAP\` with ROMs).
  Injection tools: `send_fx_midi` (PC/CC/note/sysEx), `load_virus_preset`
  (CC0 bank + PC), `load_dexed_cartridge` (.syx). Per-plugin **matrix presets + morph chains** exist
  for all five devices (`timbre-lib/matrix_presets/`); apply them via the
  `list_matrix_presets` / `apply_matrix_preset` MCP tools (xenia/nord/je8086
  verified live; per-engine measured status in `docs/hardware-va-suite.md` §9).
  Audition workflow:
  inject → `save_project` → `export_audio` → measure — the preset lives in the
  live plugin state; the save persists it into the tree for offline renders.
  Constraint: the serializer's size-regression guard protects plugin states
  across load→save cycles (see docs/plans/2026-09-12-plugin-state-durability.md).
  **Patch pipelines:** every device with a bank library has a decoder writing
  searchable sidecars — `virus_patch.py` (`.virus.json`), `nl2x_patch.py`
  (`.nl2x.json`), `je8086_patch.py` (`.je8086.json` + an exploded per-patch tree),
  `microq_patch.py` (`.vavra.json`); FileLibraryManager ingests all four (register
  the folder as a *patch* library). **Loaders are evidence-gated:** `load_nord_bank`
  queues and changes bytes but by EAR the renders stayed near-identical across 14 real
  patches (2026-09-18 ear pass — delivery gap, see
  docs/handoffs/2026-09-18-gearmulator-custom-builds.md), `load_virus_preset` (CC0+PC)
  queues but does NOT change Osirus renders on the current build (preset-load ext
  absent; finding F-A — under investigation), JE8086 DT1 dumps **do** apply since
  2026-09-20 (wrapper retargets UserPatch → temp performance; `load_je8086_preset`),
  as do its 461 parameters — confirm a dump via `poll_fx_capture` + render, not the
  param list (§9), Vavra exposes no host parameters and its
  SysEx injection is MEASURED NOT APPLYING (2026-09-16/17: queued but state
  unchanged; channel filter excluded — see
  docs/plans/2026-09-16-matrix-preset-engine-fixes.md). **Prefer the device over a plugin for movement:**
  own modulation matrix → onboard FX → HDAW automation/track LFO → HDAW internal FX →
  third-party plugin last (plugin FX add CPU, latency, isolation and state-round-trip
  risk). Device matrix, per-device FX recipes and caveats:
  `docs/hardware-va-suite.md`.

**Guideline: when adding a feature, ask whether the generative/random/modulation
toolkit applies.** New note or parameter editing should offer humanize/randomize;
new content types should consider a generative path; new modulatable parameters
should be wired as modulation targets. Prefer extending these shared utilities
over one-off randomness, so behavior (and its MCP/RPC surface) stays consistent.

