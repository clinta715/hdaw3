# Handoff — 2026-10-01: internal `drum_synth` — an 11-voice TR-909-style analog drum kit

**Status:** the new internal instrument `fxType` is **shipped and fully tested on the final
build** (full sharded suite 2195 passed / 0 failed of 2235 intended). **Nothing was committed
at the time of writing** — the main agent commits. This is the first session of the
"independently implemented internal instrument" line; no third-party source is vendored.

---

## 1. What shipped

### 1.1 The engine

`src/engine/DrumSynthEngine.{h,cpp}` (new, global namespace like `FmSynthEngine`) — an
independently implemented, **per-sample, realtime-safe 11-voice TR-909-style analog drum kit**:
Kick, Snare, Clap, Rim, TomLow, TomMid, TomHigh, ClosedHat, OpenHat, Crash, Ride. No samples
are required and no third-party source is vendored. `render()` performs no allocation, no
locks, no strings and no file I/O; params are atomics read once per block into a
`ParamSnapshot`, and every voice's noise source is a `xorshift32`.

Wiring is the standard internal-slot path: `TrackFXSlot` gains `ActiveType::DrumSynth`, the
51-row def table (`getParamDefsForType("drum_synth")`), the `prepare()` restore branch, the
`process()` branch and the `setInternalParam()` routing; `internalFxTypeNames()` now lists
**16** internal types. `isPreservedInstrumentFxType` (`AudioEngineCommands_Fx.cpp`) treats
`drum_synth` as an instrument, so applying a reverb-only `ChainPreset` keeps the drum slot in
place (regression-pinned).

### 1.2 The 51-param contract (pinned order)

Seven globals, then four per instrument at `7 + i*4 + {0..3}`; instruments in
`DrumSynthEngine::Instrument` order (Kick..Ride):

| # | Name | Default | Range | Note |
|---|---|---|---|---|
| 0 | Output Level | 0.8 | 0.0 .. 1.5 | post-ceiling; deliberate overdrive allowed |
| 1 | Kit Tune | 0.0 | −24 .. +24 st | |
| 2 | Decay Scale | 1.0 | 0.1 .. 4.0 | |
| 3 | Accent | 0.5 | 0.0 .. 1.0 | |
| 4 | Voice | 0.0 | 0 .. 10 (int) | which instrument in **Fixed** mode |
| 5 | Note Map | 0.0 | 0 .. 1 | **0 = Fixed (default)**, 1 = GM |
| 6 | Key Track | 1.0 | 0.0 .. 1.0 | pitched voices follow the incoming note |
| `7+i*4+0` | `<Inst> Level` | 0.8 | 0.0 .. 1.5 | i = 0..10 |
| `7+i*4+1` | `<Inst> Tune` | 0.0 | −24 .. +24 st | |
| `7+i*4+2` | `<Inst> Decay` | 0.5 | 0.0 .. 1.0 | |
| `7+i*4+3` | `<Inst> Tone` | 0.5 | 0.0 .. 1.0 | |

### 1.3 Note resolution — Fixed default, GM opt-in, key tracking

- **`Note Map` defaults to Fixed (0): every incoming note triggers `Voice`.** This is a
  deliberate HDAW-specific default, not a generic-drum-machine default. HDAW's own generators
  emit the kick at `diaRoot(keyRoot, 2)` = MIDI **36…47**, one role per track, so a GM-first map
  would misroute **every** key-derived kick onto real GM tom/hat notes. GM is therefore opt-in,
  for the single-track case that actually carries a real GM clip.
- **`Note Map = 1` (GM)** uses the pinned `instrumentForNote()` table (kick 35/36, rim 37,
  snare 38/40, clap 39, toms 41/43/45/47/48/50, hats 42/44/46, crash 49/57, ride 51/59, …);
  the regression test pins the full row list.
- **`Key Track` (default 1.0)** makes the four pitched voices (Kick, TomLow, TomMid, TomHigh)
  follow the incoming note:
  `freq = baseFreq * 2^((KeyTrack*(note−baseNote) + kitTune + instTune)/12)`, clamped to
  20 Hz..20 kHz, with per-voice base `(baseNote 45, baseFreq Hz)`: Kick 55, TomLow 90,
  TomMid 125, TomHigh 170. Base note 45 with those freqs keeps the 36…47 generator range in a
  sane **33–62 Hz** span — i.e. a key-derived kick actually tunes to the key. The seven
  unpitched voices (Snare, Clap, Rim, ClosedHat, OpenHat, Crash, Ride) ignore the incoming note
  for pitch and use only the Tune params.

### 1.4 Choke, declick, determinism

- **ClosedHat chokes OpenHat** (classic 909 behaviour): a closed-hat note steals the open-hat
  voice with a 2 ms release.
- **2 ms declick** via a **second voice slot per instrument**: slot 0 is the sounding voice,
  slot 1 holds the 2 ms linear fade of the voice it retriggered, so a retrigger/choke has no
  discontinuity. Regression-pinned
  (`ClosedHatChokesOpenHatWithoutDiscontinuity`).
- **Deterministic**: every voice's `xorshift32` is seeded from a **pure function of
  (instrument, note)** only — never a counter, pointer, wall-clock or `juce::Random` — so two
  identical renders (and a second `prepare()`+`render()` on the same engine) are **bit-identical**.

### 1.5 The kit soft ceiling, and the arithmetic that forces it

The eleven voices sum into **one** slot. At **default** params a full-kit unison hit measures a
raw voice sum of **3.558× unity (+11 dB)**, while a **single** kick sits at a raw sum of
**~0.79**. No linear trim can serve both: bounding the unison linearly needs ~**0.27**, which
drops one kick to ~**0.2** (unusable). The fix is a **memoryless soft knee applied to the voice
sum BEFORE `Output Level`**:

```
kCeilingKnee = 0.80      // linear at or below this
kCeilingSpan = 0.15      // soft region knee .. knee+span, asymptote 0.95
shaped = knee + span * (1 - exp(-(a - knee)/span))   for |x| > knee
```

Because the knee sits **above** one and two voices, a single default kick (0.788) and any
two-voice hit pass through **bit-identically**; only **3+ simultaneous** voices engage the soft
region, which asymptotes to 0.95. Placing the ceiling **before** `Output Level` keeps that
param's full 0..1.5 range, so a deliberate overdrive can still exceed unity (the user's choice).
Expected peaks at default params, velocity 127: single kick 0.788 → ×0.8 ≈ **0.630**; 4-voice hit
0.948 → shaped 0.894 → ≈ **0.715**; 11-voice unison 3.558 → ≈0.94999 → ×0.8 ≈ **0.760**; all
levels + Output at 1.5 → sum 6.67 → 0.95 → ×1.5 ≈ **1.425** (finite, no NaN/Inf). The proof is a
**property**, not a brittle amplitude: halving `Output Level` halves the single-kick peak exactly.

### 1.6 `add_instrument_part {role:"drums", fxType:"drum_synth"}` auto-selects GM

The `drums` role emits a **multi-voice GM pattern** (kick 36, closed hat 42, clap 39), which only
resolves correctly in GM mode — so `addInstrumentPart` writes `Note Map = 1` (param 5) when the
role is `drums` **and** the type is `drum_synth`, and leaves the Fixed default (0.0) for every
other role/type. Regression-pinned on the **live** processor across a full `rebuildFXChain`
(`InstrumentPart.DrumsRoleDrumSynthSelectsGmNoteMap`), with negatives for `sub_synth` (same param
index, different meaning) and for `role:"bass"`.

## 2. Evidence

All numbers below were **measured this session** on the final build.

| Measurement | Result |
|---|---|
| Full sharded suite (final build) | **2195 passed, 0 failed, 2235/2235 intended** |
| Focused (`DrumSynthEngineTest.*` + `InstrumentPart.*` + `FxChainPreset.*` + `InternalFxParamClamp.*`) | **34 passed**, 1 pre-existing skip |
| 11 GM voices, same sample, default params, **full export** | `clipping: false`, `ceilingHitPct: 0`, WAV peak **0.457** |
| Solo kick spectral analysis | `sub 13932.6 / bass 1697.0 / body 29.8 / high 0.0014`, `kickProminence 0.968` |
| Full-kit unison raw voice sum (engine render, default params) | **3.558× unity (+11 dB)**; single kick ~**0.79** |
| Device map | `drum_synth.params.json` **51 params, 0 trap / 0 unclassified**; `list_device_params {engine:"drum_synth"}` → `matched: 51` |
| README counts | internal fxTypes **15 → 16**, device-map params **191 → 242** |
| MCP surface | `add_fx`, `add_track_with_fx`, `list_fx_params`, `set_fx_param`, `set_internal_fx_param`, `get_internal_fx_param`, `load_fx_chain` descriptions + `add_instrument_part`'s `fxType` enum all list `drum_synth` |
| Graph | refreshed; `graphify explain DrumSynthEngine` resolves |

## 3. Provenance episode (documented as it happened)

While planning, the agent read `D:\pdf\LoS.9x9\Source\Samples.cpp` — a repo with **no LICENSE
file** (its README badge claims MIT, but
`raw.githubusercontent.com/CarlosFranzetti/LoS.9x9/master/LICENSE` → **404**, i.e. all rights
reserved) — and, **before catching it**, pinned **two of its literal constants** into the
implementation brief. Both were replaced afterwards:

- the **metal bank** became a derived geometric series `2500 · 1.25^k` =
  2500 / 3125 / 3906 / 4883 / 6104 / 7630 Hz (six mutually inharmonic square partials), and
- the **rim click** 860 Hz became **780 Hz**.

Four comments claiming "clean-room" were corrected to *"independently implemented; no
third-party source vendored"*. The implementing agent never opened the third-party repo. The
lasting rule: **check for a LICENSE before borrowing a constant** — and never claim more than you
can evidence, which is why every provenance comment in `DrumSynthEngine.{h,cpp}` reads
*"independently implemented; no third-party source vendored"* rather than a clean-room claim.

## 4. Traps encountered

Four traps hit this session became **lessons 44–47** (narratives by the sibling agent in
`docs/lessons-learned.md`). Pointers, with the concrete evidence from this session:

- **Lesson 44 — a clamped WAV export hides the true float peak; size headroom from the
  RENDER, not the file.** Evidence here: the full-kit unison's **3.558×** sum is only visible in
  the engine/render path; the exported WAV of the 11-voice GM demo reported peak **0.457** with
  `ceilingHitPct: 0`, and the headroom decision was therefore taken from the render, not the
  file. The regression asserts a **property** (halving `Output Level` halves the single-kick
  peak) rather than an absolute amplitude.
- **Lesson 45 — an N-voice instrument summed into ONE slot cannot be linearly bounded.**
  Evidence here: the 3.558× vs 0.79 arithmetic in §1.5, resolved with the memoryless soft ceiling
  on the **voice sum**, placed above the 1–2 voice range and **before** the output-level param.
- **Lesson 46 — the MCP engine answering your calls is a COPY in `%TEMP%`, and its image name
  differs, so `taskkill /IM HDAW_headless.exe` misses it.** Hit while verifying this feature; the
  running engine was resolved via `whoami` (`runningBinaryPath` / `runningMtime` vs the built
  exe) instead of by image name.
- **Lesson 47 — a windowed offline render never delivers a note-on that falls BEFORE the window
  start.** Evidence here: the drum part's kick lands at beat 0 (`diaRoot(keyRoot, 2)`), so the
  focused verification had to start its window **at/after** the first hit — `verify_part`
  rejects `startBeat = 0` (the pre-existing O7 wart), and a zero `soloPeak` on an audible part is
  a windowing artifact first.

## 5. Open items

- **No human critical listening of an 11-voice GM demo.** All evidence is numerical/spectral
  (export peak, band energies, `kickProminence`); nobody has judged how the kit *sounds*.
- **Nothing committed at the time of writing** (the main agent commits).

## 6. Files changed

| Group | Path | What |
|---|---|---|
| engine | `src/engine/DrumSynthEngine.h` **(new)** | class, pinned param layout, voice struct, `kitCeiling` contract |
| engine | `src/engine/DrumSynthEngine.cpp` **(new)** | per-sample 11-voice DSP, GM table, choke/declick, deterministic noise, soft ceiling |
| engine | `CMakeLists.txt` | register the two new sources |
| wiring | `src/engine/TrackFXSlot.h` | `ActiveType::DrumSynth`, 51-row def table, restore/process/`setInternalParam`, `internalFxTypeNames` (16) |
| wiring | `src/engine/AudioEngineCommands_Fx.cpp` | `isPreservedInstrumentFxType` accepts `drum_synth` |
| wiring | `src/engine/AudioEngineCommands_Composition.cpp` | `add_instrument_part` drums-role → `Note Map = 1` |
| wiring | `src/mcp/McpTools_FxSlot.cpp` | `add_fx` / `list_fx_params` / `set_fx_param` / `set_internal_fx_param` / `get_internal_fx_param` descriptions + enums |
| wiring | `src/mcp/McpTools_Track.cpp` | `add_track_with_fx` description + enum |
| wiring | `src/mcp/McpTools_FxChain.cpp` | `load_fx_chain` preserved-instrument list |
| wiring | `src/mcp/McpTools_CompositionInstrument.cpp` | `add_instrument_part` `fxType` enum |
| tests | `tests/unit/engine/drum_synth_test.cpp` **(new)** | voice/GM/key-track/choke/determinism/clamp/rebuild/preset/headroom gates |
| tests | `tests/unit/engine/instrument_part_test.cpp` | drums-role GM branch (live processor, negatives) |
| tests | `tests/unit/engine/fx_chain_preset_test.cpp` | 16-type `internalFxTypeNames` pin |
| tests | `tests/CMakeLists.txt` | register `drum_synth_test.cpp` |
| docs | `README.md` | 16 internal fxTypes / 242 params, `drum_synth 51` |
| docs | `docs/composition-toolkit.md` | internal-fxType list |
| docs | `docs/core-synths-agentic-guide.md` | `drum_synth` row |
| docs | `docs/handoffs/2026-10-01-drum-synth-internal-fx.md` **(new)** | this handoff |
| docs | `docs/handoffs/INDEX.md` | new row + `dub-stab-and-internal-device-maps.md` → historical |
| device map | `timbre-lib/build_device_map.py` | `drum_synth` source-table entry |
| device map | `timbre-lib/device_map/drum_synth.params.json` **(new)** | 51 params, 0 trap / 0 unclassified |
| device map | `timbre-lib/device_map/index.json` | `drum_synth` (51) added; param count 242 |
