# Plan — bass synthesizer variation (2026-10-06)

**Status:** investigation complete, DSP scope awaiting sign-off. Derived from a
read-only audit of the four bass-capable internal engines plus the bass guidance
in the workflow docs. Every claim below is cited to `file:line` or was measured
on the live engine (v0.39.4, `/tmp/HDAW_headless_mcp`, HTTP 18765).

**Premise correction:** there is no `growl_synth`. The engine the user means is
`growl_bass` (`GrowlBassEngine`). This plan is about variation *beside* it.

---

## 1. What exists today

| engine | params | architecture | bass idioms it serves |
|---|---|---|---|
| `growl_bass` | 26 (`TrackFXSlot.h:282-310`) | 1 carrier + 1 modulator, **same waveform**, PM (`1/2π` fixed depth) → waveshaper (4 types, drive dB, asymmetry) → ONE 2-pole SVF (LP/BP) → ADSR shared by amp+filter. Unison = 1..4 detuned copies of the FM pair, summed to ch0 then mono-copied (`GrowlBassEngine.cpp:486-488`). | fixed rolling growl, offbeat pump |
| `sub_synth` | 33 (`TrackFXSlot.h:386-427`) | 2 osc (PolyBLEP saw/square) + hard-wired square sub, osc2→osc1 phase-FM, JUCE SVF LP/HP/BP/(LP+HP), optional 24 dB, separate amp + filter ADSRs, glide/legato, 8-voice poly with per-voice filters, one slot LFO with 4 destinations, velocity-sensitive (`cpp:666`) | sub, reese-ish detune, acid |
| `psy_fm` | 38 (`TrackFXSlot.h:338-385`) | 6-op FM, 6 algorithms, 9 presets, mod matrix (5 sources), post-carrier per-voice filter | FM growl, acid, dub sub, stab |
| `fm_synth` | 26 | DX7 6-op (msfa core), SysEx import | DX7 corpus basses |

## 2. Verified gaps

### 2.1 Defects found (bug-fix class — no sign-off needed)

1. **`psy_fm` mod-matrix sources `modWheel` and `velocity` are permanently 0.**
   `PsyFmModMatrix.h:12-13` are the *only* mentions of those fields anywhere in
   the tree — no writer exists (`grep -rn 'velocityValue\s*=\|modWheelValue\s*='`
   → only the declarations). Measured live: with a velocity-127 note and
   `user/hard-clip-bass.json` loaded (route `velocity→op6Feedback` depth 0.5),
   `psy_fm_mod_matrix_debug` reports `sourceValues.velocity = 0`,
   `rawContribution = 0`, `scaledContribution = 0`.
   **Blast radius:** the `acidLead` preset (`modWheel→op6Feedback`) and the
   bank patches `hard-clip-bass` ("velocity-driven bite") and `acid-squelch-bass`
   route through dead sources.
2. **`psy_fm` `barClock` is dead:** `PsyFmEngine::onBarBoundary` (`cpp:170`) has
   **no callers** (grep over `src/`) — so `RatioSweepRateItself` never advances
   and the `riser` preset's "bar clock auto-speeds the sweep LFO" is inert.
3. **`psyarp` never receives the project tempo:** `PsyArpEngine::bpm_` stays
   `120.0` (`h:233`); `TrackFXSlot::setTempo` (`h:623`) forwards to `delay` and
   `drumSynth` only. The arp step rate and `Filter Sweep Bars` are therefore
   wrong at any project BPM ≠ 120.

### 2.2 Capability gaps in `growl_bass` (the "variation" hole)

Verified absent (absent symbol / negative grep on the engine's `.h`/`.cpp`):

- **No saw oscillator at all.** `ModShape = {Sine, Triangle, Square}`
  (`h:15`) and *both* carrier and modulator use it — so the engine cannot produce
  the textbook reese/neuro basis (detuned saws) even with unison.
- **No LFO** of any kind, therefore no tempo-synced wobble (the genre's core
  movement device). Movement must come from HDAW automation instead.
- **No glide/portamento/legato/mono** — no member exists.
- **No HP mode and no second filter stage** (`FilterType = {LowPass, BandPass}`,
  one `processSVF` call at `cpp:471`).
- **No filter key-track** (cutoff is absolute Hz, `cpp:468`).
- **No stereo width / per-voice pan** — mono-copied at `cpp:486-488`.
- **No velocity sensitivity** — velocity is stored (`cpp:256`) and never read.
- **No sub-oscillator, no noise source.**
- **`Formant` is not a filter:** `processFormant` (`cpp:203-237`) computes
  `output += input * (1/(1+freq/2000))` per band — a static gain tilt, not a
  resonant vowel filter, despite the "vocal vowel quality" doc claim.
- **`Ratio Jitter` is non-deterministic:** `juce::Random::getSystemRandom()`
  (`cpp:418`) → two renders of the same project differ (lesson 21 class).

### 2.3 Palette / workflow gaps

- **No bass preset vocabulary.** `growl_bass` has no preset table and no loader
  (unlike `psy_fm`, which has `presetTable()` + `psy_fm_load_preset`).
  `sub_synth` has *only* the 6 mod-LFO presets (`AudioEngineCommands_Fx.cpp:1273`)
  — zero sound presets. So the two dedicated bass engines are 100 % hand-dialed
  every session; the docs' bass recipes are prose param lists
  (`docs/psytrance-va-and-production.md:650,678-681`).
- **The patch library is never factory-seeded** (`ChainLibrary.h:71-75`,
  `cpp:300-309`), so the in-repo bass bank
  (`compositions/psy_fm_bank/`, 20 patches, 5 of them bass) only reaches
  `~/.config/HDAW/patches/user/` on the machine that ran the authoring script —
  it is not shipped content. `_factory/` seeding for chains is code-driven
  (`factoryChainDefs()` → `seedFactoryPresetsIfMissing`, `cpp:269-299`).
- **Exactly one bass-ish factory FX chain** (`Bass Glue`: eq+comp+saturator) —
  processing only, no instrument-scoped bass chains.
- **Bass engine usage across the repo's own briefs** (grep of
  `compositions/*/brief.json`): `psy_fm` 37 mentions / 8 briefs, `sub_synth` 19 / 7,
  `growl_bass` 3 / 3, `fm_synth` 3 briefs. The dedicated growl engine is the
  least-used bass source in practice — consistent with "we need more variation".

## 3. Proposed workstreams

### W1 — fix the dead movement sources (no sign-off; bug fixes proceed)

- `psy_fm`: set `sources_.velocityValue = velocity/127` on note-on; take
  `modWheelValue` from CC1 in the incoming MIDI; call `onBarBoundary(bar)` from
  the per-block playhead already read at `Track.cpp:652`.
- `psyarp`: forward the project tempo in `TrackFXSlot::setTempo`.
- Tests: a velocity-127 vs velocity-20 render must differ through a
  `velocity→op6Feedback` route; a CC1 render must differ through a
  `modWheel→op6Feedback` route; a 140 BPM project must change the psyarp step
  grid; the `riser` sweep rate must advance across an 8-bar boundary.
- Back-compat: default (no such routes / no CC) renders must stay bit-identical.

### W2 — append `growl_bass` capability (DSP; needs sign-off)

Follow the psyarp/psy_fm precedent exactly: **append-only params, defaults
chosen so existing projects render bit-identically**.
Candidates, cheapest-first: Saw/Pulse shapes + Noise · tempo-synced LFO →
cutoff/pitch/drive · glide/legato/mono · HP mode + a second filter stage ·
velocity → drive · stereo unison spread · deterministic ratio jitter.

### W3 — a new bass engine (DSP; needs sign-off)

The `drum_synth` precedent: a new `fxType` in `internalFxTypeNames()`
(`TrackFXSlot.h:118-123`) + `ActiveType` + def table + prepare/process routing +
`isPreservedInstrumentFxType` + MCP/RPC enum + device map + CMake + tests + docs.

Two shapes worth considering:

- **`reese_bass`** — 2–7 detuned saw/supersaw voices (PolyBLEP), hard sync,
  comb/notch, phase-offset unison for stereo width, its own tempo-synced LFO →
  notch/cutoff, glide, mono/legato. This is the missing "neuro/reese/psy-dub
  wobble" voice and the largest single gain in variation.
- **`acid_bass`** — a 303 voice: one osc (saw/square), 18 dB resonant filter,
  env-mod, accent from velocity/poly-aftertouch (couples to the `acid_step`
  MIDI FX's planned slice 2), slide. Overlaps `sub_synth`+`acid_step`, so lower
  marginal value.

### W4 — ship a bass palette (no DSP; can land with W1 or alone)

- A **code-seeded factory bass patch roster** (extend `seedFactoryPresetsIfMissing`
  to a bass patch set, or add a `bassPatches()` def like `factoryChainDefs()`),
  covering `growl_bass` / `sub_synth` / `psy_fm` so every session starts with a
  bass vocabulary instead of hand-dialing.
- 2–4 **factory bass FX chains** beyond `Bass Glue` (e.g. second-filter-pass,
  dub-throw, mid-growl).
- Workflow docs: the bass sub-role table in
  `docs/psytrance-va-and-production.md` §5c and
  `.agents/skills/psy-song-session/roles/sound-selector.md`, plus
  `docs/core-synths-agentic-guide.md` §2.

## 4. APPROVED SCOPE (user sign-off 2026-10-06)

**Chosen: W1 + W4 + W3 (`reese_bass`).** W2 (growl_bass capability append) is
**not** in this pass — the user picked the new-engine option. The new engine sub
choice (reese / acid / psy-dub wobble / hoover) was left open by the user, so the
engine is specced as a **reese/neuro voice with a psy-dub wobble LFO** (the two
lowest-overlap idioms; `acid_step` + `sub_synth` already cover acid).

### 4.1 `reese_bass` parameter contract (40 params, indices 0..39)

A NEW `fxType`, so indices are free (no append-only constraint), but they are
**frozen once shipped** — the device map and tests pin every row.

| # | name | def | min..max | unit | intent |
|---|---|---|---|---|---|
| 0 | Voice Count | 7 | 1..7 | int | detune-width |
| 1 | Detune Cents | 20 | 0..100 | cents | detune-width |
| 2 | Stereo Spread | 0.5 | 0..1 | scalar | detune-width |
| 3 | Osc Shape | 0 | 0..2 (Saw/Square/Tri) | enum | osc-wave |
| 4 | Phase Scatter | 0.5 | 0..1 | scalar | osc-mix |
| 5 | Sub Level | 0.3 | 0..1 | scalar | osc-mix |
| 6 | Sub Octave | −1 | −2..0 | int | osc-mix |
| 7 | Sync Amount | 0 | 0..1 | scalar | fm-metal |
| 8 | Sync Ratio | 1.0 | 1..4 | ratio | fm-metal |
| 9 | Comb Amount | 0 | 0..1 | scalar | formant-vocal |
| 10 | Comb Frequency | 80 | 20..2000 | hz | formant-vocal |
| 11 | Comb Feedback | 0.7 | 0..0.95 | scalar | formant-vocal |
| 12 | Drive dB | 6 | 0..40 | db | distortion-drive |
| 13 | Drive Type | 0 | 0..3 (Tanh/Atan/Hard/Fold) | enum | distortion-drive |
| 14 | Drive Mix | 1 | 0..1 | scalar | fx-mix |
| 15 | Filter Cutoff | 1200 | 20..20000 | hz | filter-sweep |
| 16 | Filter Res | 2 | 0.1..20 | scalar | resonance |
| 17 | Filter Type | 0 | 0..2 (LP/HP/BP) | enum | filter-type |
| 18 | Filter Env Amt | 0.4 | 0..1 | scalar | filter-sweep |
| 19 | Filter Key Track | 0 | 0..1 | scalar | key-vel-track |
| 20 | Filter Attack | 0.01 | 0.001..2 | s | env-shape |
| 21 | Filter Decay | 0.4 | 0.001..5 | s | env-shape |
| 22 | Filter Sustain | 0.3 | 0..1 | scalar | env-shape |
| 23 | Filter Release | 0.2 | 0.001..5 | s | env-shape |
| 24 | Amp Attack | 0.005 | 0.001..2 | s | amp-env |
| 25 | Amp Decay | 0.2 | 0.001..5 | s | amp-env |
| 26 | Amp Sustain | 0.85 | 0..1 | scalar | amp-env |
| 27 | Amp Release | 0.08 | 0.001..5 | s | amp-env |
| 28 | Output Level | 0.35 | 0..1 | scalar | mix-balance |
| 29 | LFO Shape | 0 | 0..3 (Sine/Tri/Sqr/SawDn) | enum | mod-rate |
| 30 | LFO Rate (beats) | 1.0 | 0.0625..8 | beats | mod-rate |
| 31 | LFO Sync | 1 | 0..1 | boolean | mod-rate |
| 32 | LFO Cutoff Amt | 0 | 0..1 | scalar | filter-sweep |
| 33 | LFO Pitch Amt | 0 | 0..1 | scalar | vibrato |
| 34 | LFO Drive Amt | 0 | 0..1 | scalar | distortion-drive |
| 35 | LFO Phase | 0 | 0..1 | scalar | mod-rate |
| 36 | Glide | 0 | 0..2 | s | glide-portamento |
| 37 | Mono Legato | 1 | 0..1 | boolean | polyphony |
| 38 | Pitch Bend Range | 2 | 0..12 | st | pitch-bend |
| 39 | Velocity→Drive | 0.3 | 0..1 | scalar | distortion-drive |

Fixed engine behaviour (not params): PolyBLEP anti-aliasing on every
discontinuity-bearing shape (lesson C precedent); one TPT SVF reusing the
`psyarp`/`growl_bass` SVF arithmetic with an HP branch; a **memoryless soft
ceiling on the voice sum before Output Level** (lesson 45 — 7 voices cannot be
linearly bounded); deterministic per-voice phase scatter derived from the voice
index only (never a clock/pointer); mono/legato glide while `Mono Legato = 1`.

### 4.2 Sequencing

Waves are serialized where they share a file; parallel otherwise. Every build
goes through the repo build lock (never two concurrent builds).

### 4.3 `ReeseBassEngine` API contract

Follows the `InternalFilter` precedent — **the engine owns the param table** and
`TrackFXSlot` derives its `InternalParamDef` list from it, so the advertised
range and the DSP clamp cannot drift:

```cpp
class ReeseBassEngine
{
public:
    static constexpr int kNumParams = 40;      // indices 0..39, frozen at ship
    static constexpr int kMaxVoices = 7;       // unison saws
    struct ParamDef { const char* name; float def; float min; float max; };
    static const std::array<ParamDef, kNumParams>& paramDefs();
    static float clampParam(int index, float value);

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void setTempo(double bpm) noexcept;                 // audio thread, per block
    void setParam(int index, float value) noexcept;     // clamped atomic store
    float getParam(int index) const noexcept;
    int  activeVoiceCount() const noexcept;
};
```

DSP invariants (gate on all of them):

- **Realtime-safe `render()`**: no allocation, no locks, no strings, no I/O,
  no `juce::Random`. Everything sized in `prepare()`.
- **PolyBLEP** band-limiting IN PLACE for Saw/Square (the slice-C precedent —
  no opt-out branch); Triangle is piecewise-linear and needs none.
- **Memoryless soft ceiling on the voice sum, before Output Level** (lesson 45)
  — the same exponential shape as `psyarp`'s `softCeilOsc` (knee 1.0, span 0.5).
- **One TPT SVF**, the `psyarp`/`growl_bass` solve, LP/HP/BP from the same
  coefficients (do not re-derive; `dx/psyarp`'s `processSVF` is the verified
  mapping).
- **Deterministic phase scatter**: `phase_i = scatter * (i / (N-1))`, never a
  clock/pointer/counter.
- **Mono/legato** when param 37 ≥ 0.5: one voice, glide on retarget, release
  only when no keys are held. Poly otherwise (≤ 7 voices, quietest-steal).
- **Comb** is a feedback comb on the summed voice output with a preallocated
  delay line of `sampleRate/20 + 4` samples (the 20 Hz minimum).

### 4.4 Device-map registration (required — `time`/`trap` gates)

`timbre-lib/build_device_map.py` fails its own validation unless EVERY internal
param gets a `unit` from the closed vocabulary, and every param must CLASSIFY or
it is emitted as `tier: "trap"` with `category: "unclassified"` (the honesty
rule). Three edits, all data:

1. `INTERNAL_ENGINES["reese_bass"] = {"file": "src/engine/ReeseBassEngine.h",
   "table": None}` — the `table: None` branch parses the engine header's
   `defs = { { … } };` table (`parse_header_param_defs`, the `InternalFilter`
   precedent). This is why `paramDefs()` must use that literal shape.
2. `intents.json` → `internalEngines.engines.reese_bass` with
   `appliesVia: "set_internal_fx_param"`, `durability: "valuetree"`, the shared
   `durabilityNote`, one broad `categoryRules` entry (category
   `reese-bass`), a `categoryDefaults` fallback (`identity` / `osc-mix`), a
   `unitRules` entry for `Pitch Bend Range → semitones` (the global grammar
   already covers beats/db/hz/cents/ratio/seconds/enum/boolean; the rest fall to
   `scalar`), `intentRules` covering all 40 names exactly once each, and
   `paramNotes` enums for `Osc Shape`, `Drive Type`, `Filter Type`, `LFO Shape`,
   `LFO Sync`, `Mono Legato`.
3. `index.json` regenerates itself (`paramCount` 40; corpus total 258 → 298).

Verification: `python3 timbre-lib/build_device_map.py` prints
`reese_bass total=40 … trap=0 (unclassified=0)`, then `--check` is clean.

## 6. Close-out (2026-10-07) — LANDED, verified on the live engine

### 6.1 What shipped

| Slice | Deliverable |
|---|---|
| W1 | `psy_fm` mod-matrix `velocity` / `modWheel` / `barClock` are real sources (velocity latched on note-on, `modWheel` = MIDI CC1, bar clock driven from the playhead each block and idempotent per bar); `PsyArpEngine::setBpm` + the `TrackFXSlot::setTempo` forward so the arp step grid and beat-based delay follow the project tempo |
| W3 | `ReeseBassEngine.{h,cpp}` — 40 params, 7 detuned PolyBLEP saw/square/tri unison voices + sub sine + hard sync + stereo feedback comb + 4-curve drive + LP/HP/BP SVF (env + key-track) + two ADSRs + tempo-synced wobble LFO + glide/mono-legato + stereo spread + per-channel soft ceiling; wired as `fxType:"reese_bass"` across `TrackFXSlot`, both surfaces, device map, README/docs |
| W4 | 12 factory bass PATCHES (6 `reese_bass`, 4 `growl_bass`, 2 `sub_synth`) seeded into `HDAW/patches/_factory/`; 3 new factory bass FX chains (`Bass Filter Sweep`, `Bass Mid Growl`, `Bass Dub Throw`); docs + skill updates; a new `reese-neuro` branch in `docs/paths/psydub.json` (`evidence: untested`) |

### 6.2 Verified evidence

**Live engine** (`/tmp/HDAW_headless_mcp`, HTTP 18765, version 0.39.4, the binary just built):

| Check | Result |
|---|---|
| `add_track_with_fx {fxType:"reese_bass"}` | `{"trackId":0,"trackID":1,"routed":1,"fxType":"reese_bass"}` |
| `list_fx_params` | 40 params, names/ranges/defaults identical to `ReeseBassEngine::paramDefs()`, pids 100..139 |
| `verify_part` on an 8-note bass line | `audible=1 nonClipping=1 soloRms=0.0813 soloPeak=0.1995` |
| `load_patch {id:"_factory/Psy_Wobble.json"}` | `ok`; readback shows the patch's values (Detune 15, LFO Cutoff Amt 0.85); `verify_part` `soloRms 0.1370` |
| `param_verity {paramName:"LFO Cutoff Amt", steps:[0,1]}` | `anyAudible:true`, both steps audible, `restored:true` |
| `mix_diff` wobble OFF vs ON (`LFO Cutoff Amt` 0.0 vs 0.9, two full exports) | `rmsDb 0.115`, band deltas sub +147.9 / bass +977.3 / body +100.2 / high −30.7 — the wobble moves the low bands, not the highs |
| `list_patches` | 12 `source:"factory"` bass rows + the 20 user `psy_fm` rows |
| `psy_fm_mod_matrix_debug` with `velocity→op6Feedback` | `sourceValue:1`, `rawContribution:0.5`, `scaledContribution:0.5` (was 0 before) |
| same route, CC1 points in the clip, during playback | `sourceValues.modWheel = 1` (was 0 before) |
| same, playing from bar 2 | `sourceValues.barClock = 0.5` (was aliased to the ratio-sweep LFO before) |

**Suites** (`./build-fast.sh test`, all four binaries relinked):

- `hdaw_tests_engine` focused: `ReeseBassEngineTest.*:FxChainPreset.*:DrumSynthEngineTest.*:InternalFxParamClamp.*:ChainLibrary.*:PatchPreset.*` → **72 passed, 0 failed**; `PsyFm*:*PsyArp*:OscAntialias*:InstrumentPart.*` → 106 passed, 1 skipped.
- `hdaw_tests_engine` full: **1406 passed / 14 failed**, and those 14 are the documented environmental set (Windows `E:\samples`/`E:\midi` paths in `timbre-lib/psy_sample_selection.tsv`, the PE/Windows-only scanner tests, mtime-dependent `FileLibraryTest`/`ProjectBackup`). None names the bass work.
- `hdaw_tests_mcp` full: **426 passed / 2 failed** — `McpServer.EngineSettingsStartMcpHttp` (port 18765 was held by this session's own smoke engine; passes when the port is free — re-ran solo: OK) and `McpServer.ExportAudioWithClapPluginDoesNotHang` (see §6.3).
- `hdaw_tests_mcp` focused (`*DeviceParams*:*Patch*:*Chain*`) → 31 passed; `hdaw_tests_frontend` (`*AddFx*:*Parity*`) → 108 passed.
- `python3 timbre-lib/build_device_map.py` → `reese_bass total=40 movement=18 identity=22 trap=0 (unclassified=0)`; **internal params 266 → 306**, corpus 877 → 917 across **22** engines (21 before); `--check` clean.
- `cmake -P cmake/CheckSkillsMirror.cmake` → in sync (15 files).
- Graph refreshed (`python -m graphify update . --force`, 25 036 nodes / 45 621 edges / 1 078 communities); `graphify explain ReeseBassEngine` resolves both the header node and the `TrackFXSlot` member; `.graphify_root` restored to the absolute path.

### 6.3 One unresolved non-bass failure

`McpServer.ExportAudioWithClapPluginDoesNotHang` fails on this box: the test instantiates the first cached CLAP instrument (Surge XT, a real Linux CLAP) as an **isolated** slot and asserts the export is non-silent. Reproduced independently of the test — a live probe of the same path (`add_track_with_fx {pluginId:"/home/clint/.clap/Surge XT.clap"}` → `list_fx` reports 775 params → `audition_plugin`) returns `rms=0 peak=0 audible=0`, and the engine log shows the plugin's lifecycle calls (`save()`, `activate()`) running on the wrong thread. **Nothing in this pass touches the isolation transport, the proxy slot, the CLAP instance or the export manager** (those files are unchanged by this work), so this is a pre-existing condition of the working tree / this Linux box, recorded here rather than fixed — it is outside the bass scope and would need its own investigation.

## 7. Files this pass creates/touches

| Group | Path |
|---|---|
| new engine | `src/engine/ReeseBassEngine.{h,cpp}` |
| registration | `CMakeLists.txt` (engine source list), `tests/CMakeLists.txt` |
| wiring | `src/engine/TrackFXSlot.h` (ActiveType, derived defs, type→ActiveType, prepare push, process dispatch, `setInternalParam` cases, `internalFxTypeNames`) |
| wiring | `src/engine/AudioEngineCommands_Fx.cpp` (`isPreservedInstrumentFxType`), `src/engine/AudioEngineCommands_Composition.cpp` (instrument-name list) |
| surfaces | `src/mcp/McpTools_FxSlot.cpp`, `src/mcp/McpTools_Track.cpp`, `src/mcp/McpTools_FxChain.cpp`, `src/mcp/McpTools_CompositionInstrument.cpp` |
| device map | `timbre-lib/build_device_map.py` (+ generated `timbre-lib/device_map/{reese_bass.params.json,index.json}`) |
| palette | `src/engine/ChainLibrary.cpp` (factory defs), `src/mcp/McpTools_FxChain.cpp` |
| W1 fixes | `src/engine/PsyFmModMatrix.{h,cpp}`, `PsyFmEngine.{h,cpp}`, `PsyArpEngine.h`, `Track.cpp`, `src/common/PsyFmModMatrixView.cpp` |
| tests | `tests/unit/engine/reese_bass_test.cpp`, `psyfm_test.cpp`, `psyarp_engine_test.cpp`, `tests/unit/engine/fx_chain_preset_test.cpp`, `tests/integration/mcp/device_params_test.cpp` |
| docs | this file, `docs/handoffs/2026-10-07-bass-variation.md` + `docs/handoffs/INDEX.md`, `README.md`, `docs/core-synths-agentic-guide.md`, `docs/psytrance-va-and-production.md`, `docs/psytrance-composition-guide.md`, `docs/composition-toolkit.md`, `docs/paths/psydub.json`, `.agents/skills/psy-song-session/{reference.md,roles/sound-selector.md}` and their `docs/skills/` mirror |

