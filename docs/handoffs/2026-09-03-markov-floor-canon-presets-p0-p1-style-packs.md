# Handoff — Markov floor canon + factory FX presets + ontology P0/P1 + style-pack architecture decision

Date: 2026-09-03 · Branch: main · Version: 0.27.0 (this work is UNCOMMITTED on top; bump 0.28.0 at commit)
Follows: 2026-09-02-markov-pads-vague-structure-timbre-keybpm.md (markov generator two-tier/themes lineage)

## What landed (all verified, none committed)

### 1. Floor canon — kick/bass breakdown-only removal (`PsytranceMarkovGenerator`)
- User observation: markov renders bounced the kick in/out as mere variation.
  Genre canon: the floor only drops as build-up/breakdown tension.
- `canRemoveTarget`: kick+bass removable ONLY in `breakdown` sections
  (section tier off ⇒ floor never removed). Covers evict-on-max (both lists
  filter through it). Breakdown removal weights the floor ×8 (bias, not
  force); the transition INTO build prefers re-adding kick, then bass
  (seeded 75%). Broken-kick flips (Breakbeat/RhythmVariant) stay variation.
- User decisions: protect bass AND kick; drop biased-not-guaranteed.
- Tests: `FloorRolesOnlyDropInBreakdown`, `BreakdownPrefersFloorAndDropReturn`
  (fixed seeds 100–139).

### 2. Factory FX-chain presets + preset-toolkit discoverability
- Gap was: `ChainLibrary` supported `_factory/` ids but never listed or
  seeded them; no doc told composing agents any preset tools existed.
- `ChainLibrary::listPresets()` scans `_factory/` then `user/` (factory
  first, deterministic sort); `ChainPreset::isFactory` added. Ctor seeds 8
  psytrance per-role chains (create-if-missing, NEVER overwrites — user
  edits survive): Kick Punch, Bass Glue, Hat Air, Pad Shimmer, Acid Lead,
  Arp Width, Stab Snip, Riser Sweep. Internal FX only; param indices/units
  verified against `TrackFXSlot.h` defs tables (see table in
  ChainLibrary.cpp `factoryChainDefs`).
- `list_fx_chains` gained additive `source: factory|user`; `load_fx_chain`
  resolves `_factory/<File>.json` ids and factory names; `delete_fx_chain`
  refuses factory ids (pre-existing). Router_Project chain-list RPC got the
  same additive field. Guide §5 gained "Preset toolkit" (roster + full tool
  table incl. plugin `.fxp`/`.syx`, `automation_preset` shapes, FM/cluster).
- Tests: `ChainLibrary.*` 10/10 (seeding, no-overwrite, delete-refusal,
  def-range asserts); FxChain suites 17/17. NOTE: running the MCP coverage
  test seeds the REAL `%APPDATA%/HDAW/chains/_factory/` — intended.

### 3. P0 — three-group ontology + real fades + perc holds
- Ontology predicates (generator, anonymous namespace): `isFloorRole`
  (kick,bass), `isCoreRole` (arp,stab,pad,bass — bass is floor AND core on
  purpose), `isPercRole` (kick,hat,clap,+snare,rim in P1), `isFxRole`
  (riser,down). Guide §4B "Three-group element ontology".
- **Volume fades are engine-written, not advisory**: non-floor AddLayer
  (bar>0) emits fade-in (core 4 bars, perc 2 bars), RemoveLayer/evict emits
  fade-out (2 bars); floor + bar-0 stay hard-edged. Command layer writes
  them to real Volume lanes (paramID 1) inside the generatePsytranceMarkov
  transaction (ONE undo unit with clips): enable-on-write for
  fader-authoritative lanes (same contract as applyAutomationPreset), DROP
  the factory hold-pair (unity at 0s/16s — would ramp fade-outs back to
  unity), upsert-by-time append (accumulates across cycles). Blocked writes
  counted in additive `automationsSkipped` (MCP + RPC). `filterCutoff`
  automations remain ADVISORY (target paramID depends on the track's FX
  chain) — agents apply via set_automation_points.
- Perc pattern hold: `kPercPatternHoldBars = 32` (superseded by themes in
  P1 but the constant/gating shape persists).
- Trap found: default Volume lanes ship a factory hold-pair — see above.

### 4. P1 — snare/rim voices + percussive THEMES
- `snare` (pitch 38) / `rim` (37) palette roles; perc pool = 5
  (`maxPercTracks ≤ 5`, `maxTracks ≤ 9`); wired through validation, clip
  writing, fades, MCP schema, RPC parse. Start state unchanged (kick+hat+
  bass+arp) — snare/rim arrive via AddLayer only.
- **PercTheme**: 16-step uint8 velocity grids × hat/snare/rim + kickBroken.
  Theme 0 = canonical offbeat-8th opener (accent baked into grid). Themes
  1..T-1 (T = rngInt(2,3), drawn ONCE at a fixed draw position) derived by
  seeding `RhythmPatternGenerator::Params` (grid=16, bars=1; hat 4–12 hits
  vel 88–104/64–84, snare 2–5 vel 60–80/45–60 + optional 2/4 accent 90,
  rim 1–4 vel 50–90; kickBroken ~35%).
- `RhythmVariant` = rotate the WHOLE theme (targetRole **"theme"** —
  convention change from "hat"/"kick"), gated by `themeAge ≥ 32`;
  `Breakbeat` toggles the CURRENT theme's kickBroken in place (persists per
  theme), gated by `kickClock ≥ 32` (reset by Breakbeat OR rotation).
- Filler fallback: an ACTIVE voice whose theme grid is empty (theme 0 has
  no snare/rim) plays canonical filler (snare 2/4 backbeat, rim steps
  7/15) at velocity **100** — outside every seeded theme range so unit
  rotation stays observable and an added layer is never a silent fade
  target.
- Retired: per-window ±6 hat accent jitter + density flourish (they
  jittered bars within a theme, defeating unit rotation). Clap stays
  hardcoded 2/4 (theme-independent for now).
- Tests: `SnareRimRolesGenerate`, `ThemeRotatesAsUnit` (40-seed sweeps,
  non-vacuous), `PercPatternHoldEnforced` rewritten for rotation semantics;
  VariantsOccur/MicroActionsDominate/KeyDiscipline/Validation extended.

### Verification state
- `PsytranceMarkov*` 25/25 · `Automation*:PsytranceGenerator*:GenerativeMarkov*`
  39/39 · `ChainLibrary.*` + FxChain suites 10/10 + 17/17.
- `build-fast.bat all` clean; all binaries fresh 2026-09-03 12:54.
- **NOT done**: full ~13-min suite (run before commit); version bump;
  knowledge-graph refresh (codebase-memory MCP not attached this session;
  no new files/RPC methods, low urgency).
- **Packaged Electron app.asar is stale** (pre-dates floor canon) —
  `frontend\build.bat` or `build-fast package` before testing in Electron.

## Architecture decision (user-agreed, for the next context)

The user asked: monolithic `generate()` vs components + external
customizable thing (originally "python script"; clarified: NOT python
internally — some extensible external mechanism per style). The balance
agreed:

**Mechanism in C++ components, style in JSON packs, strategy in the
agent/MCP layer, Python only as external tooling/MCP client** (precedent:
`timbre-lib/lib_analyze.py` is offline tooling; never embed a script VM —
lessons 11–22 are all boundary bugs).

1. **Component refactor** (next step, before P2):
   - `MarkovArranger` — orchestrator/state machine (section tier, limits,
     cadence, action selection, fade emission)
   - `PercussionEngine` — themes, grids, rotation (extract P1)
   - `HarmonyEngine` — riff identity, chord-tone derivation (P2's home)
   - `TextureEngine` — long-form FX material, dub bursts (P3's home)
   - Each takes a style-parameter struct; `PsytranceMarkovGenerator` becomes
     a thin orchestrator. Genre breadth = parameter presets, not new
     generators. Hazard: seeded draw order will shuffle — same-seed
     determinism stays guaranteed (tests are self-comparisons), but a
     given seed's score may change; acceptable, document it.
2. **Style packs (the "external extensible thing")**: JSON genre packs the
   engine loads — action weights, hold constants, section transition
   tables, theme-generation ranges, fade lengths, role palette, drum
   pitches, progression defaults. Factory packs + user packs, stored like
   ChainLibrary/PatternLibrary (factory seeding, delete-refusal, source
   field). Psytrance = pack #1, extracted from the current constants.
   Agents can write/iterate packs over MCP — style iteration without
   recompiles.
3. **Long game the design must anticipate**: an agent combining a future
   DnB generator with the psytrance generator via MCP. Implication: keep
   components genre-agnostic (psytrance specifics live in the pack, not
   the component), keep per-genre generators as pack+component
   combinations, keep cross-generator combination an AGENT-level move
   (multiple MCP calls), not engine coupling.
4. **P2 on the new structure**: HarmonyEngine — riff-centric core: accept
   an imported/generated central riff (`analyze_midi_file`/`import_pattern`
   exist but are disconnected from generation), pitched roles derive
   voicings/countermelodies from its chord tones (chord-tone machinery
   exists: kChordTones, PhraseGenerator Counterpoint/CallResponse),
   secondary core elements fade in as variation (fades exist since P0).
5. **P3**: TextureEngine — long-form textures (timestretch across N bars —
   `McpTools_Clip.cpp` has stretch; `ProjectModel::sliceClipAtTimes` for
   slice/rearrange), riff-style cutoff/amp automation over textures, dub
   bursts (delay Feedback 0–0.99 + SyncToTempo) as generator actions.

## Uncommitted-tree caution

`git status` contains PRE-EXISTING dirty files from other sessions that
are NOT part of this work: ExportManager.{cpp,h}, MainAudioProcessor.cpp,
PluginManager.cpp, Track.cpp, ReadModelImpl.cpp, AutomationPanel{,.test}.tsx,
ModulationPanel{,.test}.tsx, package.json, package-lock.json,
docs/adr-automation-model.md. This session's files:
PsytranceMarkovGenerator.{h,cpp}, ChainLibrary.{h,cpp},
AudioEngineCommands_Composition.cpp, ProjectCommands.h,
McpTools_CompositionGenerate.cpp, McpTools_FxChain.cpp,
Router_Composition.cpp, Router_Project.cpp, psytrance_markov_test.cpp,
chain_library_test.cpp, docs/psytrance-composition-guide.md, README.md.
Stage selectively at commit time.
