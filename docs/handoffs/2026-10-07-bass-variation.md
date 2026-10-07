# Handoff — 2026-10-07: bass variation — the internal `reese_bass` engine + a shipped bass palette

**Status:** shipped and verified on the live engine. Plan with the full evidence table:
`docs/plans/2026-10-06-bass-variation.md` (§6 close-out). Nothing here is a proposal —
every claim below was measured this session.

**Ask:** "we need more bass synthesizer variation besides just growlsynth".
**Answer:** a new internal instrument `reese_bass` (the reese/neuro + psy-dub wobble voice
that no existing engine could reach), a shipped palette of 12 factory bass patches and 3
bass FX chains, and four defects fixed that were silently disabling movement on existing
engine/bass patches.

---

## 1. What shipped

### 1.1 `reese_bass` — a new internal instrument (40 params)

`src/engine/ReeseBassEngine.{h,cpp}` (new, global namespace like `GrowlBassEngine`).
Per-note signal path:

```
7 detuned unison oscillators (PolyBLEP saw/square, naive triangle)
  [+ hard-sync crossfade] [+ sine sub at Sub Octave]   -> linear-panned stereo sum
-> stereo feedback comb (Comb Amount > 0 only)
-> stereo drive (4 curves; velocity + LFO add up to 24 dB)
-> stereo TPT SVF (LP/HP/BP) with filter env + key track + LFO cutoff
-> amp envelope
-> per-channel memoryless soft ceiling (knee 1.0, span 0.5) -> Output Level
```

Why it exists (verified absences in the existing engines):

| engine | what it cannot do |
|---|---|
| `growl_bass` | **no saw oscillator at all** (`ModShape = Sine/Tri/Square`, shared by carrier AND modulator), no LFO, no glide/legato/mono, no HP mode, one filter stage, velocity stored but never read, `Formant` is a static gain tilt not a filter, `Ratio Jitter` uses `juce::Random::getSystemRandom()` (non-deterministic) |
| `sub_synth` | unison is a **fixed** 2 detuned copies at 7 cents (not a parameter), no hard sync, no comb |
| `psy_fm` | FM only — no saw wall, no filter key-track before this month's slice, no wobble LFO |

So "many detuned saws" and "a tempo-synced wobble" were both unreachable. Those are the
two things `reese_bass` is built around.

Design discipline (all pinned by tests):

- **Conservative defaults**: `Sync Amount` 0, `Comb Amount` 0 and all three LFO amounts 0
  are **bit-exact bypasses** of their whole stage — so a default slot is the plain detuned
  saw wall, and nothing in those stages costs CPU unless used.
- **`Mono Legato` defaults to 1** (mono, glide on retarget, envelopes keep running on a
  legato overlap) — the correct default for a bass. `0` = poly, 2 note slots.
- **PolyBLEP** band-limiting IN PLACE for Saw/Square (the slice-C precedent; naive
  aliasing is a defect, not a feature). Triangle is piecewise-linear.
- **Memoryless soft ceiling on the voice sum, before `Output Level`** (lesson 45):
  7 detuned voices cannot be linearly bounded, so the knee sits above 1–2 voices and the
  asymptote below unity.
- **Deterministic**: no random source anywhere, including the unison phase scatter
  (`phase_i = scatter·i/(N−1)`), so two renders of the same project are bit-identical.
- Realtime-safe: `prepare()` does all allocation; `render()` takes no lock, allocates
  nothing, formats no strings.
- `paramDefs()` is the ONE source of truth — `TrackFXSlot` derives its advertised
  `InternalParamDef` list from it (the `InternalFilter` precedent) and
  `timbre-lib/build_device_map.py` parses it straight out of the header.

### 1.2 The palette (no DSP involved)

- **12 factory bass PATCHES** seeded into `HDAW/patches/_factory/` (a new `ChainLibrary::Roster`
  enum replaced the old `bool seedFactory`, so the patch library is seeded with PATCH content
  while the chain roster stays chain-only): 6 `reese_bass` (`Reese Classic`, `Neuro Sync Stab`,
  `Psy Wobble`, `Dub Sub Reese`, `Rolling Mid Reese`, `Fold Gnarl`), 4 `growl_bass`
  (`Growl Rolling Sub`, `Growl Hard Acid`, `Growl Digital Grit`, `Growl Vocal`), 2 `sub_synth`
  (`Sub Pure`, `Sub Acid 303`). Listed by `list_patches` with `source:"factory"`, loadable by
  `load_patch`, never deletable.
- **3 factory bass FX CHAINS** (`Bass Filter Sweep`, `Bass Mid Growl`, `Bass Dub Throw`),
  expressing the guide's filter-discipline canon and a dub-throw for a bass stem. The roster
  is now 11 (`Bass Glue` still there).
- A new `reese-neuro` branch in `docs/paths/psydub.json` (`evidence: untested`) so the psydub
  path walker can pick it.

### 1.3 Four defects fixed (these were disabling movement on sounds you already had)

1. **`psy_fm`: `velocity` and `modWheel` matrix sources were permanently 0** — no writer
   existed anywhere in the tree. Live-verified: before, a velocity-127 note through
   `velocity→op6Feedback` reported `sourceValue:0, rawContribution:0`; now `1` / `0.5`.
   This silently disabled the `acidLead` preset's `modWheel→op6Feedback` route and the
   bank patches `hard-clip-bass` and `acid-squelch-bass`.
2. **`psy_fm`: `barClock` aliased the ratio-sweep LFO** (`sourceIndexFor` had
   `default: return 0`), and `onBarBoundary` had no caller. Both fixed: BarClock is pool
   source 4, driven from the playhead each block, idempotent per bar, with a rewind branch
   that restores the base sweep rate. Live-verified: `barClock:0.5` while playing from bar 2.
   This makes the `riser` preset's "bar clock speeds the sweep" actually happen.
3. **`psyarp` never received the project tempo** — `bpm_` was stuck at 120, so the arp step
   grid and the beat-based delay were wrong at every other BPM. `TrackFXSlot::setTempo` now
   forwards it every block.
4. **`growl_bass`'s `Formant` is not a filter** and its `Ratio Jitter` is non-deterministic
   (`juce::Random::getSystemRandom()`). Recorded, **not** fixed — `Formant` is a gain tilt
   (`output += input·(1/(1+freq/2000))`) and fixing it changes existing renders, which is
   exactly the kind of change that needs sign-off first. Same for adding a saw/LFO/glide to
   `growl_bass` (the user chose the new-engine route instead).

---

## 2. Evidence (measured this session)

Live engine `/tmp/HDAW_headless_mcp`, version 0.39.4, the binary built in this session:

| Check | Result |
|---|---|
| `add_track_with_fx {fxType:"reese_bass"}` | accepted; `list_fx_params` returns 40 params matching `paramDefs()`, pids 100–139 |
| `verify_part` (8-note bass line) | `audible=1 nonClipping=1 soloRms=0.0813 soloPeak=0.1995` |
| `load_patch _factory/Psy_Wobble.json` + `verify_part` | `ok`; readback shows the patch's values; `soloRms 0.1370` |
| `param_verity {paramName:"LFO Cutoff Amt"}` | `anyAudible:true`, both steps audible, `restored:true` |
| two full exports, `LFO Cutoff Amt` 0.0 vs 0.9 → `mix_diff` | `rmsDb 0.115`; band deltas sub +147.9 / bass +977.3 / body +100.2 / high −30.7 (the wobble moves the low bands, leaves the highs alone) |
| `list_patches` | 12 factory bass rows + 20 user `psy_fm` rows |
| `psy_fm_mod_matrix_debug`, `velocity→op6Feedback`, vel 127 | `sourceValue:1 rawContribution:0.5 scaledContribution:0.5` |
| same, CC1 points in the clip, during playback | `sourceValues.modWheel = 1` |
| same, playing from bar 2 | `sourceValues.barClock = 0.5` |
| `add_fx {fxType:"reese_bass"}` on the MCP and RPC surfaces | both accept; a bogus type is refused with the same text on both (twins in `add_fx_parity_test.cpp`) |

Suites: `hdaw_tests_engine` focused 72/72 + 106/107 (1 pre-existing skip); full run
1406 passed / 14 failed — **all 14 the documented environmental set** (Windows
`E:\samples`/`E:\midi` paths, PE/Windows-only scanner tests, mtime-dependent library
backup tests). `hdaw_tests_mcp` 426/428, `hdaw_tests_frontend` focused 108/108.
`timbre-lib/build_device_map.py` → `reese_bass total=40 trap=0 (unclassified=0)`; corpus
266 → 306 internal params (877 → 917 across 22 engines); `--check` clean. Graph refreshed; `graphify explain
ReeseBassEngine` resolves.

### Open (not bass)

`McpServer.ExportAudioWithClapPluginDoesNotHang` fails on this box: it loads the first cached
CLAP instrument (Surge XT) as an **isolated** slot and asserts a non-silent export. Reproduced
independently of the test — a live probe of that path reports 775 published params yet
`audition_plugin rms=0 peak=0 audible=0`, with the engine log showing the plugin's
`save()`/`activate()` on the wrong thread. Nothing in this pass touches the isolation
transport, the proxy slot, the CLAP instance or the export manager, so this is a pre-existing
condition of the tree / this Linux box. It deserves its own investigation.

Also still unaddressed from the audit (deliberately): `growl_bass`'s inert `Formant`, its
non-deterministic `Ratio Jitter`, and its missing saw/LFO/glide — all render-changing, so
they need sign-off before they can be touched.

---

## 3. Files

| Group | Path |
|---|---|
| new engine | `src/engine/ReeseBassEngine.{h,cpp}` |
| wiring | `src/engine/TrackFXSlot.h` (include, `ActiveType::ReeseBass` appended, derived def table, type→ActiveType, prepare push, process dispatch, `setInternalParam`, `internalFxTypeNames` 16→17, deferred-reset, `setTempo`, accessor, member), `src/engine/AudioEngineCommands_Fx.cpp` (`isPreservedInstrumentFxType`), `src/engine/AudioEngineCommands_Composition.cpp` |
| W1 fixes | `src/engine/PsyFmModMatrix.{h,cpp}`, `src/engine/PsyFmEngine.{h,cpp}`, `src/engine/PsyArpEngine.h`, `src/engine/Track.cpp` (transport bar), `src/engine/TrackFXSlot.h` (`setTransportBar`, `applySweepRate`), `src/common/PsyFmModMatrixView.cpp` |
| surfaces | `src/mcp/McpTools_FxSlot.cpp`, `McpTools_Track.cpp`, `McpTools_FxChain.cpp`, `McpTools_CompositionInstrument.cpp` (+ descriptions + enums) |
| palette | `src/engine/ChainLibrary.{h,cpp}` (`Roster` enum, `factoryPatchDefs`, 3 new chains), `src/common/PatchPreset.h` (comment), `docs/paths/psydub.json` |
| device map | `timbre-lib/build_device_map.py`, `timbre-lib/device_map/{intents.json,index.json,reese_bass.params.json}` |
| tests | `tests/unit/engine/reese_bass_test.cpp` (new; also slot integration, live-processor rebuild, preservation, tempo), `psyfm_test.cpp`, `psyarp_engine_test.cpp`, `chain_library_test.cpp`, `patch_preset_test.cpp`, `fx_chain_preset_test.cpp`, `tests/integration/mcp/{device_params_test.cpp,patch_preset_parity_test.cpp}`, `tests/unit/frontend/add_fx_parity_test.cpp`, both `CMakeLists.txt` |
| docs | `docs/plans/2026-10-06-bass-variation.md`, `README.md`, `docs/core-synths-agentic-guide.md`, `docs/psytrance-va-and-production.md`, `docs/psytrance-composition-guide.md`, `docs/composition-toolkit.md`, `docs/handoffs/2026-10-07-bass-variation.md`, `.agents/skills/psy-song-session/{reference.md,roles/sound-selector.md}` **and** their `docs/skills/` mirror (byte-identical; `check_skills_mirror` green) |

## 4. Next steps a future session should consider

- **Ear check.** All evidence here is numerical/spectral — nobody has judged how a reese
  sounds. `load_patch _factory/Reese_Classic.json` + `audition_plugin` is the 2-call entry.
- **Promote the psydub `reese-neuro` branch** from `untested` to `measured` by walking it in
  a real song and appending the verdict to `docs/paths/ledger.json`.
- **A `growl_bass` capability pass** (saw shape, LFO, glide, HP + second filter stage,
  velocity→drive, deterministic jitter) — append-only params, defaults bit-identical, so it
  is the same discipline as this release; it needs the user's sign-off because it touches DSP.
- **The `acid_step` slice 2** (accent → poly-aftertouch → the voice) would pair naturally
  with `sub_synth`'s 303 recipe, which now ships as the `Sub Acid 303` factory patch.
