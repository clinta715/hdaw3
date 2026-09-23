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

**Bus/send architecture — reachable since 2026-09-22** (this section previously
documented a capability gap; it is now closed). `add_bus {busType:"fx"|"group", name,
fxType, busTarget}` creates a bus and returns its `busID`; `add_send {trackId, busTarget,
level, isPreFader}` routes a track into it; `remove_bus` / `remove_send` tear down, and
`remove_bus` cascades (every send targeting it goes in the same undo unit — one `undo`
restores bus + sends). `fxType` must be one of `FxBusProcessor`'s four —
`reverb`, `delay`, `eq`, `compressor`; anything else is rejected by name (an unknown
type would build a silent passthrough). The pre-existing `set_track_send_level` /
`_mode` / `_bypassed` / `get_track_sends` shape and read an existing send. RPC twins:
`project.addBus` / `removeBus` / `addSend` / `removeSend`. Full plan + gates:
`docs/plans/2026-09-22-bus-send-surface.md`.

Measured 2026-09-22 (aether_dub, 16 s of drop1, send 1.0 vs 0.0 into an `fxType:"delay"`
bus): **rms 0.0832 → 0.1156 (+39%, +2.85 dB), bass band 14 094 → 28 662 (+103%), body
3 905 → 8 715 (+123%)** — the return reaches the master, so the dub idiom (one shared
delay/reverb return, ridden per phrase) is now buildable. The bus read/param gap those
measurements walked into is closed: **`list_buses`** (RPC `read.listBuses`) lists every bus
with its `fxType`, and **`list_bus_fx_params` / `set_bus_fx_param`** (RPC
`read.listBusFxParams` / `project.setBusFxParam`) read and shape an fx bus's parameters
(`index` / `name` / `minValue` / `maxValue` / `defaultValue` / `value` vocabulary, shared with
`list_fx_params`) — plan: `docs/plans/2026-09-22-bus-fx-params.md`.

**The delay return is a real feedback delay** (slice C3 of that plan, 2026-09-22): the bus's
`delay` chain now uses the *same* DSP as a track's internal delay — the class `InternalDelay`
(`src/engine/InternalDelay.h`), extracted verbatim from `TrackFXSlot` and shared by both, so a
track delay renders exactly what it rendered before (asserted analytically: taps
1.0 / 0.5 / 0.25 / 0.125 at 1x/2x/3x/4x the delay time for feedback 0.5). Its five params are
real and settable: **Delay Time** (0.01-5 s), **Feedback** (<= 0.99 — the runaway clamp),
**Mix**, **SyncToTempo**, **Division** (0=1/8, 4=dotted-1/8, 6=1/4; derived as
`beats x 60/bpm`, the bus reading the project BPM from the playhead so it follows tempo live
*and* in export). Measured on aether_dub, 16 s of drop1, send at 1.0: Feedback **0.7 vs 0.0**
-> last-2s rms 0.1466 vs 0.1159 (**+26% tail energy**, peak 0.664 vs 0.530) = repeats instead
of one tap; Division **1/8 vs 1/4** -> last-2s rms 0.1373 vs 0.1088, so sync really moves the
taps.

**Set Mix = 1.0 on a send return.** The default 0.5 re-adds the bus input — a doubled dry
signal on top of the track's own dry. 1.0 makes the return pure echo (the classic send-return
wiring). The same applies to a reverb return.

Three verified caveats remain: mutating commands that trigger a routing rebuild (`add_bus`,
`add_send`) can **drop their HTTP response while completing the work** (intermittent —
re-read state with `list_buses` / `get_track_sends` rather than blind-retrying); **sends are
positional** (index = position in the track's `SEND_LIST`, so removing one shifts the rest);
and `export_audio`'s `start`/`end` are **seconds** while `verify_part` takes
`startBeat`/`endBeat` — check the rendered duration before trusting an A/B.

Still available (and often the cheaper choice): per-track FX plus **gestural lane
automation** — `add_automation_lane {trackId, laneName, paramID}` (paramID = `100 +
slot*100 + paramIndex`; built-in lanes are 1 Volume / 2 Pan / 3 Mute, one lane per
paramID) followed by `automation_preset`, whose presets are the gesture vocabulary:
`delayThrow` (the dub throw), `steppedGate` (dub gating), `openClose`, `phaseSweep`,
`macro`, `riser`, `pump`, `subtleLife`, `randomDrift`. `sections[]` entries each carry
their own `preset`, so one call can layer several gestures on one lane
(measured: 768 points from `openClose`+`phaseSweep`; 152 points of `delayThrow` per
track). Note an enabled Volume lane makes automation authoritative for that track —
it then appears in `audit_modulation_coverage`'s `faderOverriddenIds` and
`set_track` volume writes are overridden.

**Guideline: when adding a feature, ask whether the generative/random/modulation
toolkit applies.** New note or parameter editing should offer humanize/randomize;
new content types should consider a generative path; new modulatable parameters
should be wired as modulation targets. Prefer extending these shared utilities
over one-off randomness, so behavior (and its MCP/RPC surface) stays consistent.

