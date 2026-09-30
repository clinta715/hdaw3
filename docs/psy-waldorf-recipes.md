# Psytrance recipes for the emulated Waldorf synths — microQ (Vavra.clap) & Microwave XT (Xenia.clap)

> ## ⚠ CORRECTION (measured live 2026-09-30) — read before trusting any "not host-writable" row
>
> **The `isPublic` flag in `parameterDescriptions_{mq,xt}.json` does NOT reflect the live host
> surface.** Verified by opening a device and instantiating a real slot on each engine, then
> reading `list_fx_params` and actually writing:
>
> | engine | live params | params this document called "not host-writable" that ARE exposed and accept `set_fx_param` |
> |---|---|---|
> | Xenia | **2151** | `Ch 1 Wave` · `Ch 1 W1StartW` · `Ch 1 MixW1` · `Ch 1 MixW2` (plus `F1Resonance`, `F1EnvAmount`, `F1Cutoff`, `Aliasing`, `Accuracy`, `Clipping`, `AmpVolume`, `EffectType`) |
> | Vavra | **7557** | `Ch 1 O1Shape` · `O2Shape` · `O3Shape` · `Ch 1 GlideRate` · `GlideEnable` · `UnisonoDetune` · `Lfo1Speed` · **`Ch 1 ArpPattern` · `ArpClock` · `ArpMode`** — each returned `ok overrides=N` |
>
> **Consequences.** (1) Oscillator shape, glide, unison and **the 16-step microQ arp are all
> host-writable**, so the "needs a dump" route is a *choice*, not a requirement — which weakens the
> §8 "arp → split" verdict, because the microQ arp is now automatable from HDAW too. (2) The dump
> route (§10) remains necessary only for what the host genuinely does not expose — e.g. microQ
> `ArpTempo` and `GlideEnabled` (note: Xenia spells it `GlideEnabled`, microQ `GlideEnable`).
>
> **The arp claim is now proven by AUDIO, not just by name presence** (isolated solo render of one
> held note on a scratch Vavra slot, `verify_part {soloOnly:true}`, arm default patch):
> `ArpMode` 0→3 took `soloRms` **0.001598 → 0.002688 (+68%)**, and `ArpClock` 9→4 took it to
> **0.003684** — i.e. the arp is not merely exposed, it *functions* and is *rhythmically
> controllable* through `set_fx_param`. All three arp params round-tripped their normalized value
> exactly.
>
> **Scale trap.** These params carry **no `min/max/default` metadata and are normalized 0..1**, while
> every table below is in **raw 0..127 byte units**. Writing a raw byte silently clamps —
> measured: writing `100` reads back `1.0`. Convert with `value = byte / 127` (the XT apply examples
> below already do this correctly). Confirmed on Xenia; the same `÷127` convention is what the
> microQ examples assume, so **verify the scale with a one-line read-back before trusting a write on
> an engine you have not tested**.
>
> Rows below that still say "not host-writable" should be read as *"the vocabulary says so, but this
> is unverified for that specific name"*.

**Status: corpus-derived, evidence-graded.** Every parameter name below is taken from the
device's own vocabulary file (never invented); every count is from an analysis run for this
document; every "verified" claim is a citation to `docs/va-suite-status-log.md` /
`docs/hardware-va-suite.md`, not a re-derivation. Where the evidence is thin it says so.

Scope: read-only corpus research. No engine was started, stopped, rebuilt or mutated; no
project was touched. The only files created are this document and deletable `.tmp_waldorf/`
scratch (analysis scripts + `corpus_report.json`).

---

## 0. How to read this document

Five roles, each with **(1)** parameter values in the device's own vocabulary, **(2)** corpus
counts + example patches, **(3)** wavetable selection (XT), **(4)** movement presets/morphs,
**(5)** the exact MCP calls, **(6)** a falsifiable measurement prediction.

Three vocabularies are in play and they are **not** interchangeable — conflating them is the
single easiest way to write a recipe that silently does nothing:

| Space | What it is | Where it lives |
|---|---|---|
| **Device byte** | one 7-bit byte in the 392-byte microQ / 265-byte XT single dump | `F0 3E 10 …` / `F0 3E 0E …` SysEx |
| **Vocabulary name** | `F1Cutoff`, `W1EnvAmount`, `Slot3Destination` — the parameter's identity | `parameterDescriptions_{mq,xt}.json` |
| **Host parameter name** | what `list_fx_params` / `set_fx_param` accept, prefixed **`Ch <n> `** per part | live plugin instance only |

`dump_byte = 7 + linear_vocabulary_index` holds for both devices (verified — §1).
`set_fx_param` takes a **normalised 0..1** value; the device byte is `round(normalised × 127)`
for a full-range parameter. `list_fx_params` reports `hasRange`/`minVal`/`maxVal`/`defaultVal`/
`plainValue` plus `minText`/`maxText`/`defaultText` in real units, and `hasRange:false` means
**blind normalised 0..1** — read the range before writing (lesson 23 discipline; range
mismatches are how you get a clamped write that looks applied but is not).

**Bipolar parameters read `64` as zero.** On the microQ, every parameter whose `toText` is
`signed` (`F1EnvMod`, `F1CutoffMod`, `F1VelMod`, `O1Detune`, `PitchModAmount`, …) displays as
−64..+63 with byte 64 = 0. On the XT the same holds for `F1EnvAmount`, `W1EnvAmount`, `Detune`,
etc. The corpus median for many of these is **exactly 64** — that is "no modulation", not a
mid setting. Getting this backwards inverts every recipe.

---

## 1. The vocabulary, and how it was verified

Both files exist on this box, outside this repo:

- microQ → `D:\pdf\gearmulator-git\source\mqJucePlugin\parameterDescriptions_mq.json`
- Microwave XT → `D:\pdf\gearmulator-git\source\xtJucePlugin\parameterDescriptions_xt.json`

(Referenced by `docs/hardware-va-suite.md` §1 as `gearmulator-2.2.9/source/{mqJucePlugin,xtJucePlugin}/`.
Only the `gearmulator-git` copy exists on this machine — `D:\pdf\gearmulator-2.2.9` is absent.
The cited files are byte-identical across the two trees per `xenia-offset-map.md` §9 "Tree identity".)

**They are JSON5-ish, not JSON.** `json.load` fails (`Expecting value` — they contain `//`
comments and trailing commas). Strip `//[^\n]*` and `,(\s*[\]\}])` first; then they parse:
microQ 823 entries, XT 451 entries, plus a `valuelists` section (48 / 50 keys) that is the
**authority for enum value names** — filter types, wavetables, mod sources/destinations.

**The mapping rule was re-verified against the shipped offset maps.** Extracting
`{name: linear_index}` from first-occurrence-per-index page-0 entries and checking
`offset_map[byte] == name_at(byte − 7)`:

| engine | names extracted | offset-map entries | mismatches |
|---|---|---|---|
| microQ | 311 | 86 (`vavra_offset_map.json`) | **0** |
| XT | 220 | 85 (`xenia_offset_map.json`) | **1 — documented alias** |

The single XT mismatch is offset 88: the vocabulary's first name for index 81 is
`EffectParamA`, the offset map ships `DelayTime` because "the specific name wins". Both names
exist in the vocabulary for that index; nothing is wrong. **Every other name cited below is
consistent with the shipped, corpus-verified offset maps.**

Enum vocabularies used in the recipes (verbatim from the `valuelists` sections):

- microQ `filterType`: `0 Off, 1 24 dB LP, 2 12 dB LP, 3 24 dB BP, 4 12 dB BP, 5 24 dB HP, 6 12 dB HP, 7 24 dB Notch, 8 12 dB Notch, 9 Comb+, 10 Comb-`
- microQ `oscWaves` (O1/O2): `0 Off, 1 Pulse, 2 Saw, 3 Triangle, 4 Sine, 5 Alt 1, 6 Alt 2`; `osc3Waves` (O3): `0 Off … 4 Sine`
- microQ `fx1Type`: `0 Bypass, 1 Chorus, 2 Flanger, 3 Phaser, 4 Overdrive, 5 Five FX, 6 Vocoder`; `fx2Type` adds `7 Delay, 8 Reverb, 9/10 5.1 Delay`
- XT `filter1Type`: `0 24 dB LP, 1 12 dB LP, 2 24 dB BP, 3 12 dB BP, 4 12 dB HP, 5 Sin(x) > 12 dB LP, 6 12 dB LP > Shaper, 7 Dual 12 dB LP/BP, 8 12 dB LP FM, 9 12 dB LP S&H, 10 24 dB Notch, 11 12 dB Notch, 12 12 dB Band Stop`
- XT `filter2Type`: `0 6 dB LP, 1 6 dB HP`
- XT `effectType`: `0 Off, 1 Chorus, 2 Flanger 1, 3 Flanger 2, 4 AutoWah LP, 5 AutoWah BP, 6 Overdrive, 7 Amp Mod, 32 Delay, 33 Pan Delay, 34 Mod Delay` (8–31 undefined)
- XT `waveType` (the wavetable select — `Wave`, index 25): 0–63 named + 64–127 `User 1..32`. Key entries: `0 Resonant, 3 Square-Sweep, 7 Mellow Saw, 13 Clipper, 16 Formant 1, 17 Polated, 24 Reso Harms, 25 2 Echoes, 26 Formant 2, 27 Formant Vocal, 28 Micro Sync, 29 Micro PWM, 30 Glassy, 31 Square HP, 32–34 Saw Sync 1-3, 35–37 Pul Sync 1-3, 41 PWM Pulse, 42 PWM Saw, 43 Fuzz Wave, 46 Fuzz Sync, 51 19/twenty, 52–55 Wavetrip 1-4, 56 Male Voice, 57 Low Piano, 58 Reso Sweep, 59 Xmas Bell, 60 FM Piano, 61 Fat Organ, 62 Vibes, 63 Chorus 2, 64 True PWM`
- XT `waveStartWave` (`W1StartW`/`W2StartW`, 0..63): `0..59` = wave position **inside** the selected wavetable, then `60 Triangle, 61 Square, 62 Sawtooth`

**Host-writability is a subset.** 96 microQ params and 66 XT params carry `isPublic:true` in
these files (matching "96 curated … × 16 parts" / "66 curated" in `hardware-va-suite.md` §1),
which is what the wrapper turns into the documented **7557 Vavra / 2151 Xenia** live host
parameters, renamed **`Ch <n> <Name>`** per part (`jucePluginLib/controller.cpp:50` returns
`"Ch " + String(part + 1)`). *Caveat:* `isPublic:true` is necessary but not sufficient — the
microQ FX sub-parameters (`Fx1ChorusSpeed`, `Fx1PhaserCenter`, …) are marked public in the JSON
yet are **bit-aliases** that the wrapper collapses onto a single host parameter per index
(`vavra-offset-map.md` §"FX-block aliasing"; `hardware-va-suite.md` §3). Confirm any name you
intend to write with `list_fx_params` on your own slot; do not assume a bare name resolves.

Crucially, these are **not public** and therefore cannot be set by name at all:

| engine | not host-writable → needs a dump |
|---|---|
| microQ | `O1Shape`, `O2Shape`, `O3Shape`, `O1PulseWidth`, `Lfo1Shape`, `Lfo1Speed`, `GlideEnable`, `GlideRate`, `UnisonoDetune`, `UnisonoMode`, `NoiseModeF1/F2`, `O1/O2/O3Level` |
| XT | **`Wave`** (the wavetable!), **`W1StartW`**, **`W2StartW`**, **`MixW1`**, **`MixW2`**, `ArpTempo`, `Accuracy` |

So: **you cannot choose an XT wavetable with `set_fx_param`. You cannot choose a microQ
oscillator shape with `set_fx_param`.** Both need the SysEx dump route (§10).

---

## 2. Corpus inventory — measured

### microQ / Vavra (`D:\pdf\rhythm-lab.com_waldorf_micro_q`)

- **528** `.syx` + **528** `.vavra.json` sidecars. All 528 parse as 392-byte single dumps
  (`F0 3E 10 00 10`, name@370, category@386); the library survey reports `files 528 / parsed 528 / failed 0`.
- **Every one of the 528 dumps carries `byte4=0x10` (SingleDump), `byte5=0x30`, `byte6=0x00`.**
  `0x30` = `mqLib::MidiBufferNum::SingleEditBufferMultiMode` (`mqLib/mqmiditypes.h:39-40`) —
  see §9, this is the framing pitfall.
- Categories (the dump carries a real category field — a stronger role signal than a name guess):
  `Arp 147, Pad 86, Lead 81, Bass 79, Atmo 36, FX 26, Keys 24, Poly 19, Misc 18, Perc 4` + 8 singletons.
- 9 patches are name-`acid`: `Acid bender CJ`, `Acid bliss CJ`, `Acid arp CJ`, `AnotherAcidarpCJ`,
  `Acid bass`, `Acid Allarm CJ`, `Acid Allarm 2 CJ`, `Acid Whines CJ`, `Acid whines 2 CJ`
  (microQ has **no** `Acid` category — this is a name filter, n=9, and that is thin).

### Microwave XT / Xenia (`D:\pdf\microwave`)

- I decoded **5870 patches myself** with `timbre-lib/xenia_dump.py::load_bank` over every
  `.mid/.MID/.syx/.usb/.µsb` under the corpus root: **1535** `.usb`/`.µsb` bank records +
  **4335** SMF single dumps, across 34 files. This is **more** than the 1791 in
  `timbre-lib/microwave_survey.json` (that survey only scanned the 7 top-level containers) and
  more than the 5358 recorded by the 34 shipped `.xenia.json` sidecars — the strict parser finds
  patches the raw scanner skips, exactly as `xenia-offset-map.md` §"Empirical validation" predicts.
- Role split, via `microwave_patch.py::_role_for` (name keywords; the `.µsb` records carry no
  category field, so this is weaker than the microQ's): `other 4428, bass 407, pad 332,
  lead 241, pluck 238, fx 119, keys 105`.
- **33** patches are name-`acid`, span **7 distinct wavetables**.
- The XT corpus is roughly **11×** the microQ corpus, but its role labels are name-guessed and
  two thirds land in `other`.

### Two pieces of sidecar metadata you must NOT use as evidence

I checked, and both are traps:

1. **`roleCheck.verdict` is `"fail"` for all 528 microQ sidecars.** It is not a role — it is the
   output of some pass/fail check (the role lives in `category`). My first aggregation pass took
   it at face value and produced `byRole {'fail': 528}`.
2. **The sidecar `dsp` block does not discriminate patches.** Across all 528 sidecars:
   `rms` 0.002136–0.002715 (1.27× total spread), `peak` 0.0277–0.0289, `zcr` takes only 4
   distinct values, `f0_hz = 0` and `tonal_fraction = 0` for **all 528**, `attack_s ≈ 0.0214`
   and `decay_s ≈ 0.9975` for all of them. That is one probe render analysed 528 times, not 528
   patch renders. It is useless as a per-patch predictor and I have not used it anywhere below.

---

## 3. Recipe 1 — Acid stab

**Engine recommendation: microQ (Vavra).** See §8.

### Device parameters (microQ vocabulary; `Ch <n> ` host prefix when written live)

| Parameter | Byte range | Corpus (n=9, name~acid) median (p10–p90) | Recommended | Notes |
|---|---|---|---|---|
| `F1Type` | 0..10 | 3 = 24 dB BP (p10 1 = 24 dB LP, p90 5 = 24 dB HP) | **1 `24 dB LP`** or **3 `24 dB BP`** | corpus: 24dB LP×3, 12dB BP×2, 24dB BP×2, 12dB LP×1, Comb+×1. The 24 dB slope is what makes the sweep audible before the resonance peak; Comb+ (9) is the "alien squelch" outlier. |
| `F1Cutoff` | 0..127 | 59 (32–70) | **48..64**, start low | 0..127; 64 ≈ mid. For a stab you want the *sweep* to traverse the band, not a high static cutoff. |
| `F1Resonance` | 0..127 | 52 (37–109) | **96..112** | the corpus is conservative (median 52) because it is a general-purpose 1999 library; acid needs the p90 and beyond. Keep ≤ 115 or the self-oscillation fights the bass. |
| `F1EnvMod` | 0..127 **bipolar, 64 = 0** | 70 → **+6** (40..94 = −24..+30) | **112..120** (= +48..+56) | this is the single most important parameter for "acid" and the corpus barely uses it. Marked extrapolation. |
| `F1KeyTrack` | 0..127 (0% = 64, 1:1 ≈ 96) | 66 (58–78) | **64..72** | near-zero keytrack keeps every stab equally bright. |
| `F1Drive` | 0..127 | 0 (0–9) | 0, or 8..16 with `24 dB LP` | corpus drive is essentially off; add sparingly. |
| `FilterEnvAttack` | 0..127 | 0 (0–95) | **0** | instant attack; the corpus agrees at the median. |
| `FilterEnvDecay` | 0..127 | 78 (72–86) | **72..86** | this is the "acidity length". |
| `FilterEnvSustain` | 0..127 | **0 (0–0, all 9)** | **0** | unanimous in the corpus. |
| `FilterEnvRelease` | 0..127 | 73 (31–93) | 64..80 | |
| `AmpEnvAttack` | 0..127 | 9 (0–59) | **0..8** | |
| `AmpEnvDecay` | 0..127 | 80 (0–86) | 72..90 | |
| `AmpEnvSustain` | 0..127 | 91 (0–127) | 96..127 if the note sustains, 32..48 for a stab | |
| `AmpEnvRelease` | 0..127 | 9 (0–84) | 8..24 | short tail; the corpus's 84 p90 is pad-ish. |
| `O1Shape` | 0..6 | 2 = Saw ×4, 1 = Pulse ×3 | **1 `Pulse`** or **2 `Saw`** | **not host-writable** — needs a dump (§10). |
| `O2Shape` | 0..6 | 1 Pulse ×3, 2 Saw ×3 | Pulse, detuned | |
| `O3Shape` | 0..4 | 4 Sine ×3, 1 Pulse ×3, 0 Off ×3 | Off or Sine | |
| `GlideEnable` / `GlideRate` | 0..1 / 0..127 | 1 / 28 | on, 24..40 | acid lines want legato slides. `GlideRate` is **not host-writable**. |
| `UnisonoDetune` | 0..127 bipolar | 1 (0–4) | 0 | keep the stab mono/thin. **Not host-writable.** |
| `FX2Type` | 0..10 | 7 = Delay **×8 of 9** | 7 `Delay` | the acid stab's own delay; `FX2Mix` median 12 (0–29). |
| `FX1Type` | 0..6 | 1 Chorus ×4, 0 Bypass ×3 | Bypass or Overdrive (4) | |

### XT (Xenia) alternative for the same role

Corpus n=33, and it agrees on structure but not on machinery:

| Parameter | Byte range | XT acid median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..12 | **1 = 12 dB LP** (17 of 33); `5 Sin(x)>12dB LP`×3, `6 12dB LP>Shaper`×3 | **1 `12 dB LP`** or **6 `12 dB LP > Shaper`** — the XT's 12 dB + Shaper is its own acid voice and the microQ has no equivalent |
| `F1Cutoff` | 0..127 | 54 (25–64) | 40..56 |
| `F1Resonance` | 0..127 | **100 (17–102)** | 96..110 — the XT corpus is far more committed than the microQ's |
| `F1EnvAmount` | 0..127 **bipolar, 64 = 0** | 77 → +13 (74..94) | 96..112 |
| `F1EnvDecay` | 0..127 | **110** | 96..112 (`F1EnvSustain` 0) |
| `AmpEnvAttack/Decay/Sustain/Release` | 0..127 | 3 / 50 / 110 / 10 | 2 / 48 / 96 / 12 |
| `W1StartW` | 0..63 | **63 (the last wave slot)** | **63** — the corpus drives every acid patch to the end of the wavetable. Host-writable as `Ch 1 W1StartW` — normalized, so write `63/127 = 0.496` (measured 2026-09-30). |
| `Wave` | 0..63 / 64..127 | **0 `Resonant` ×17 (51.5%)**, 17 `Polated`×4, 16 `Formant 1`×3, 35 `Pul Sync 1`×3 | **0 `Resonant`** |
| `AmpVolume` | 0..127 | 127 (all) | 127 |
| `Aliasing` (0..5) / `Accuracy` (0..1) / `Clipping` (0..1) | | 0 / 0 / 1 | 0 / 0 / 1 |
| `GlideTime` | 0..127 | 16 | 16..32 |
| `Detune` | | 30 | 24..32 |

### Corpus evidence — example patches

**microQ (Vavra), name~acid, n=9:**

| Patch | File (relative to `D:\pdf\rhythm-lab.com_waldorf_micro_q`) |
|---|---|
| `Acid bass` | `Bass\Acid bass        Bass.syx` |
| `Acid bender   CJ` | `Arp\Acid bender   CJ Arp.syx` |
| `Acid bliss    CJ` | `Arp\Acid bliss    CJ Arp.syx` |
| `Acid arp      CJ` | `Arp\work\Acid arp      CJ Arp.syx` |
| `Acid Allarm   CJ` | `FX\Acid Allarm   CJ FX.syx` |
| `Acid whines 2 CJ` | `FX\Acid whines 2 CJ FX.syx` |

**XT (Xenia), name~acid, n=33 (sourced from `D:\pdf\microwave`):**

| Patch | File |
|---|---|
| `18dB Acid    WMF` | `2000.µsb` (also `Factory.µsb`, `BFell_XTsoundset_everything.syx`) |
| `12dB Acid    WMF` | `Factory.µsb` |
| `Dork Acid !    S` | `Factory.µsb` (also `factory_1997\fact9712.mid`) |
| `It's Acid !    T` | `Factory.µsb` |
| `Mad Old Acidhead` | `paul_nagle\pnmw0998_x\PUN0998.SYX` |
| `Acid Flash    AV` | `xt_usersoundset3\usersoundset3.mid` |

### Wavetable selection (XT)

The acid subset uses **`0 Resonant`** in 17 of 33 patches (51.5%) — twice the rate of the whole
corpus (14.3%). This is a "hard resonance" wavetable, which is why the XT acid patches can run
`F1Resonance` at a median of 100 with `F1EnvDecay` 110: the resonance character is partly in the
*wavetable*, not only the filter. Secondary choices in the same subset: `17 Polated` (4),
`16 Formant 1` (3), `35 Pul Sync 1` (3), `27 Formant Vocal` (2), `59 Xmas Bell` (2).

### Movement

No `vavra.json` preset is role-tagged `acid` — all 40 microQ presets are role `fx`. Use one of
these two instead:

- **microQ, discrete filter/osc movement**: `list_matrix_presets {engine:"vavra"}` →
  id `3a77cc0171961084`, name `FX1 ring mod+chorus+flanger movement + FX2 chorus+flanger+phaser movement + O1 PWM + O2 PWM + O3 PWM`,
  `appliesVia:"waldorf_dump"`, 363 stamped bytes, base `Technodoodah  CJ Arp.syx`.
  It is an oscillator/FX movement config, so it layers onto an acid patch (PWM on all three
  oscillators + chorus/flanger) rather than replacing the filter envelope.
- **microQ, continuous morph**: `vavra_morphs.json` pair **`25:32`** (distance 0.00308,
  4 steps, `namedDiffRatio` 0.5) — parents
  `e5ec677eae463ee6` → `573e17395f809fd6`, both the "ring mod+chorus+flanger + O1/O2/O3 PWM"
  family. Step 1's `interp` shows `F1Cutoff [29, 60]`, `F1EnvMod [94, 82]`, `FX1Mix [0, 28]` —
  i.e. it opens the filter and brings in the FX mix. **Provenance caveat:**
  `baseResolution.how = "prefix"`, `byteMatches = 1` — the vavra morph parents are resolved by
  **name prefix with a single matching byte**, which is far weaker than the XT's 84/84. Treat
  these steps as *unverified* (`appliesVia: "patch_or_sysex_unverified"`).
- **XT, the documented pair for this character**: `xenia_morphs_injectable.json` pair
  **`4:25`**, distance 0.0076, 4 steps, **zero discrete hops**: `F1Cutoff 76→67`,
  `F1Resonance 23→80`, `Slot1Amount 64→80` — "resonance-led brightness swell". Parents
  `57e3dac33b0dd64f` / `f004b9b0baf5502e` ("16-slot matrix + ring mod mix + wave env"),
  base `Simpl FM` (`tibetarr.mid`), `how:"verified"`, `byteMatches:84`.
  Rationale: this is the cleanest resonance increase in the whole sheet, and resonance is exactly
  the acid axis (corpus `F1Resonance` median 100). For a *darkening* acid stab use pair `13:36`
  (distance 0.0040: `F1Cutoff 55→41`, `Slot1Amount 93→64`).

### Apply via MCP

```
# microQ acid — the whole patch (osc shapes need this, they are not host params)
apply_preset {trackId, slotIndex,
              filePath: "D:\\pdf\\rhythm-lab.com_waldorf_micro_q\\Bass\\Acid bass        Bass.syx"}
get_fx_capture_status {trackId, slotIndex}          # poll until status "ok"

# then the host-writable part, per part (names are "Ch <n> <Name>")
list_fx_params {trackId, slotIndex}                 # find e.g. "Ch 1 F1Resonance"
set_fx_param {trackId, slotIndex, paramName: "Ch 1 F1Resonance", value: 0.87}   # 111/127
set_fx_param {trackId, slotIndex, paramName: "Ch 1 F1EnvMod",    value: 0.89}   # 113/127 = +49
```

```
# XT acid — wavetable + start-wave need a dump
apply_preset {trackId, slotIndex,
              filePath: "D:\\pdf\\microwave\\Factory.µsb"}   # bank image → NOT a single dump; see §9
# better: a single-patch .syx of a 265-byte F0 3E 0E dump framed to bank 0x20,
# or an edit-buffer dump via send_fx_midi (§10), or:
apply_matrix_preset {engine:"xenia", id:"57e3dac33b0dd64f", trackId, slotIndex}
```

### Falsifiable prediction — acid stab

Render the part alone over one bar with `verify_part {trackIndex, startBeat, endBeat}`:

- `audible = true`, `soloPeak` in **−28 … −14 dBFS** (the corpus microQ renders sit at peak
  ≈ 0.028 = −31 dBFS with no gain staging; a produced stab with 1.0 gain should be well above
  the −80 dBFS audibility floor).
- `bandsPresent` must contain **both `mid` and `high`** — a stab whose `high` band is absent is
  a bass patch, not an acid stab.
- A/B against a control render of the *same* patch with `F1EnvMod` set to its neutral byte
  (normalised 0.504 = 64 = +0): the difference must exceed the measured variance floor. The
  project's own verified separations are `rms 0.0094 → 0.0157` (Vavra edit-buffer dump,
  Δ0.0057) and Vavra host-param writes `0.0026 / 0.0077` for `AmpVolume 0 / 1` — so **gate at
  ΔsoloRms ≥ 0.002** on Vavra, and note that the same-input spread measured for Xenia was
  **0.0056**, which exceeded a 0.0023 separation — on Xenia gate on a **band ratio**, not on rms.
- `mix_report {filePath, sections:[{name:"stab", start, end}]}`: the stab window's
  **`high` band energy (> 6000 Hz)** must rise monotonically with `F1Resonance` across three
  renders (52 / 96 / 112 → normalised 0.41 / 0.76 / 0.88). If high-band energy is flat, the
  resonance write did not reach the OS — re-check `list_fx_params` for `overridden`.

---

## 4. Recipe 2 — Rolling bass

**Engine recommendation: microQ (Vavra) for the note; XT (Xenia) for the wavetable variant.** See §8.

"Rolling" is a *performance* pattern (16th/ triplet offbeat rolling bassline), not a parameter
value. What the corpus can tell you is the **tonal envelope** that survives that pattern without
smearing: short amp release, zero filter sustain, and a resonance/cutoff pair that keeps every
note identically bright.

### Device parameters (microQ, n=79 category `Bass`)

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..10 | **2 = 12 dB LP** (24dB LP×35, 12dB LP×22, 24dB BP×11, 12dB BP×8) | **2 `12 dB LP`** for roll clarity, **1 `24 dB LP`** if you want a fatter sub |
| `F1Cutoff` | 0..127 | **50 (30.6–72.6)** | **44..58** — lower than lead/pad, deliberately |
| `F1Resonance` | 0..127 | 41 (16–64) | 40..64; higher only if the wavetable is clean |
| `F1EnvMod` | **bipolar 64=0** | 70 → +6 (64..94 = 0..+30) | 88..104 (= +24..+40) — the roll needs the env to re-open the filter on every 16th |
| `F1CutoffMod` | bipolar 64=0 | **64 (64–104)** | 64 (off) or a short positive ramp; the corpus's p90 = +40 |
| `FilterEnvAttack` | 0..127 | **0 (0–42)** | 0 |
| `FilterEnvDecay` | 0..127 | **69 (39–84)** | **56..72** — must be shorter than the 16th at your BPM |
| `FilterEnvSustain` | 0..127 | **0 (0–5.6)** | **0** |
| `FilterEnvRelease` | 0..127 | 56 (9–90) | 24..48 |
| `AmpEnvAttack` | 0..127 | **0 (0–8)** | **0** |
| `AmpEnvDecay` | 0..127 | 44 (0–86) | 40..64 |
| `AmpEnvSustain` | 0..127 | **123 (0–127)** | **127** (a roll sustains) |
| `AmpEnvRelease` | 0..127 | 37 (0–72) | **16..32** — the corpus median smears; tighten |
| `AmpVelocity` | 0..127 bipolar | 64 (64–127) | 64 (no velocity → every note equal) |
| `O1Shape` | 0..6 | **1 Pulse ×36**, 2 Saw ×25, 4 Sine ×12 | **1 `Pulse`** — 46% of the corpus |
| `O2Shape` | 0..6 | 1 Pulse ×32, 2 Saw ×26 | Pulse, slight detune |
| `F2Type` | 0..10 | 2 = 12 dB LP ×37, 0 Off ×17, 1 24 dB LP ×13 | `12 dB LP` if you want a parallel band, else Off |
| `FilterRouting` | 0..1 | 1 = Serial (median; p10 0) | Serial |
| `FX1Type` | 0..6 | **0 Bypass ×57/79 (72%)** | **Bypass** — no chorus on a rolling bass |
| `FX2Type` | 0..10 | 0 Bypass ×52, **7 Delay ×20**, 8 Reverb ×5 | Bypass; `FX2Mix` median 0 — keep the low end dry |
| `GlideEnable` / `GlideRate` | 0..1 / 0..127 | 0 / 0 | off (a roll is not legato) |
| `UnisonoDetune` | 0..127 bipolar | **0 (0–9.2)** | 0..4 |

The pattern the corpus is telling you: **`F1Type` 12 dB LP, cutoff ≈ 50, resonance ≈ 41,
`F1EnvMod` ≈ +6, `FilterEnvSustain` 0, `AmpEnvSustain` 123, no FX.** That is a dry, punchy,
filter-enveloped bass with no modulation on top — exactly what a rolling bassline needs,
and the 72% FX1-bypass rate is the corpus agreeing.

### XT (Xenia), n=407 role `bass`

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..12 | **0 = 24 dB LP** ×205 (half); 1 12 dB LP ×68; **5 `Sin(x) > 12 dB LP` ×58**; 6 `12 dB LP > Shaper` ×25 | **0 `24 dB LP`**, or **6 `12 dB LP > Shaper`** for bite |
| `F1Cutoff` | 0..127 | **45 (0–74)** | 40..52 |
| `F1Resonance` | 0..127 | **71 (0–103)** | 64..80 |
| `F1EnvAmount` | bipolar 64=0 | 83 → +19 (64–105) | 96..112 (= +32..+48) |
| `F1KeyTrack` | 0..127 | 71 | 64..80 |
| `F1Extra` | 0..127 | 0 (p90 29) | 0 — `F1Extra` is the "extra" of types 5/6/7/8/9; leave at 0 unless you use a shaper/FM type |
| `F1EnvDecay` / `F1EnvSustain` | 0..127 | 32 / 0 | 32..48 / 0 |
| `AmpEnvAttack/Decay/Sustain/Release` | 0..127 | 2 / 40 / 87 / **10** | 2 / 40 / 100 / 12 |
| `W1StartW` | 0..63 | 42 | 40..56 |
| `W2StartW` | 0..63 | 41 | — |
| `MixW1` / `MixW2` | 0..127 | **123 / 112** | both loud — the XT bass is a two-wavetable blend |
| `MixRingMod` / `MixNoise` | 0..127 | 0 / 0 | 0 / 0 |
| `EffectType` | 0..34 | **0 Off ×337/407 (83%)** | Off |
| `Detune` | 0..127 | 17 | 16..32 |
| `Aliasing` / `Accuracy` / `Clipping` | 0..5 / 0..1 / 0..1 | 0 / 0 / 0..1 | `Aliasing 0`, `Clipping 1` — `Clipping` on is the XT bass "glue" and 50%+ of the corpus has it on |
| `ArpMode` | 0..3 | 0 Off ×361, On ×25, Hold ×21 | Off |

**Corpus matrix-occupancy is the interesting XT bass number:** median **6 of 16** modulation
slots used (`slotsUsed` mean 5.82, p10 2, p90 10) for bass, vs 7 for pad and 7.25 corpus-wide.
XT basses are the *least* matrix-dense role — the XT bass recipe is a filter+two-wavetable
recipe, not a modulation recipe.

### Corpus evidence — example patches

**microQ (Vavra), category `Bass`, n=79:**

| Patch | File |
|---|---|
| `bass runner   CJ` | `Arp\work\bass runner   CJ Bass.syx` |
| `Ajax 2        CJ` | `Bass\Ajax 2        CJ Bass.syx` |
| `Ana Bass      CJ` | `Bass\Ana Bass      CJ Bass.syx` |
| `Biffa Bass    CJ` | `Bass\Biffa Bass    CJ Bass.syx` |
| `Blotter       CJ` | `Bass\Blotter       CJ Bass.syx` |
| `Blubber       CJ` | `Bass\Blubber       CJ Bass.syx` |

**XT (Xenia), role `bass`, n=407:**

| Patch | File |
|---|---|
| `Red DanceBass JH` | `2000.µsb` |
| `18dB Acid    WMF` | `2000.µsb` |
| `FatBass      WMF` | `2000.µsb` |
| `First Bass    MK` | `2000.µsb` |
| `Hollow Bass  WMF` | `2000.µsb` |
| `Hopped Bass  WMF` | `2000.µsb` |

### Wavetable selection (XT bass)

Distribution over 407 bass patches (`Wave` = index 25):

| Wave | Name | Share |
|---|---|---|
| 0 | `Resonant` | **20.6%** |
| 64 | `True PWM` | 10.3% |
| 35 | `Pul Sync 1` | 9.6% |
| 31 | `Square HP` | 4.7% |
| 41 | `PWM Pulse` | 4.7% |
| 29 | `Micro PWM` | 4.2% |
| 42 | `PWM Saw` | 3.4% |
| 32 | `Saw Sync 1` | 3.4% |

**Pick `0 Resonant` for a rolling bass, `64 True PWM` for a hollow/"hoover" bass,
`35 Pul Sync 1` for a hard-sync bass.** The whole XT corpus (5870) prefers `True PWM` 16.9% and
`Resonant` 14.3%, so bass is a visible *shift toward* `Resonant` and `Pul Sync 1`.

### Movement

- **microQ**: the cleanest bass-relevant chain is `vavra_morphs.json` pair **`23:38`**
  (distance 0.0246) or **`23:39`** (0.0418) — both resolve their parents from
  `Ana Bass      CJ Bass.syx` (see the `baseResolution.file` field), i.e. they are the only two
  chains whose base patch is *actually a bass*. Memory: `prefix` resolution, 1 byte matched —
  unverified. Pair `38:39` (0.0434) resolves from `Squelch Bass  CJ Bass.syx`.
- **microQ discrete**: `list_matrix_presets {engine:"vavra"}` — the 40 presets are all role `fx`,
  so there is no bass-role matrix preset in the corpus. Layer one of the chorus/PWM configs
  (e.g. `b0ffcef23556cb5b`) only if you want movement, and bypass FX1 otherwise (72% of corpus
  basses do).
- **XT**: all 40 `xenia.json` presets are role `mod-matrix` (not bass-tagged either), but the
  *dense matrix* is the XT bank style (`xenia_roles_morphs.md`: "Every corrected config carries
  a 14-16 slot matrix + wave env"). For bass the corpus says **use fewer of them** (median 6/16),
  so if you apply `apply_matrix_preset {engine:"xenia", id:"f4eda6a665e07bce"}`
  ("16-slot matrix + wave env") you are applying a *pad-typical* density to a bass — expect to
  zero slots 7–16 afterwards (`Slot7Source` … `Slot16Destination`, all host-writable? **no** —
  check `list_fx_params`; the `Slot*` params are index 192–239 and are not in the 66-param public
  set, so they need a dump).

### Apply via MCP

```
# microQ rolling bass — patch first (osc shape), then host params
apply_preset {trackId, slotIndex, filePath: "…\\Bass\\Blotter       CJ Bass.syx"}
get_fx_capture_status {trackId, slotIndex}
list_fx_params {trackId, slotIndex}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Type",      value: 0.157}  # 20/127 → 2 (12 dB LP)
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",    value: 0.394}  # 50/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance", value: 0.323}  # 41/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1EnvMod",    value: 0.551}  # 70/127 = +6
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FilterEnvSustain", value: 0.0}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 AmpEnvAttack",     value: 0.0}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 AmpEnvSustain",    value: 1.0}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FX1Type",     value: 0.0}    # bypass
clear_fx_param_overrides {trackId, slotIndex}   # only if you want to drop the ledger
```

```
# XT rolling bass
apply_matrix_preset {engine:"xenia", id:"f4eda6a665e07bce", trackId, slotIndex, captureToTree:true}
get_fx_capture_status {trackId, slotIndex}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",    value: 0.354}  # 45/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance", value: 0.559}  # 71/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1EnvAmount", value: 0.654}  # 83/127 = +19
set_fx_param {trackId, slotIndex, paramName:"Ch 1 EffectType",  value: 0.0}    # Off
# Wave / W1StartW / MixW1 / MixW2 ARE host-writable — CORRECTED 2026-09-30, measured live:
#   Ch 1 Wave, Ch 1 W1StartW, Ch 1 MixW1, Ch 1 MixW2 all exist in list_fx_params (2151 params)
#   and set_fx_param accepts them ("ok overrides=N"). They are NORMALIZED 0..1 with NO
#   min/max/default metadata, so convert the byte value: value = byte / 127.
#   TRAP: passing a raw byte (e.g. 100) silently clamps to 1.0 — the wrong value, no error.
set_fx_param {trackId, slotIndex, paramName:"Ch 1 Wave",     value: 0.0}          # 0 = Resonant
set_fx_param {trackId, slotIndex, paramName:"Ch 1 W1StartW", value: 0.496}        # 63/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 MixW1",    value: 0.969}        # 123/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 MixW2",    value: 0.882}        # 112/127
# (The dump route in §10 still works and is required for anything the host does not expose.)
```

### Falsifiable prediction — rolling bass

- `verify_part {trackIndex, startBeat, endBeat, soloOnly:true}`:
  `soloRms` **> 0.01** and `mixPeak < 1.0` (`nonClipping = true`) once placed in a mix.
- `mix_report {filePath, bpm}` on the full mix must report **`kickProminence` ≥ 0.5**, where
  `kickProminence = E(35-110) / (E(35-110) + E(120-320))`. This is the *quantitative statement
  of "the bass is not eating the kick"*: if a rolling bass sits too high (`F1Cutoff` > 70) or
  uses `24 dB LP` with high resonance, energy migrates into 120–320 Hz and `kickProminence`
  falls. Grid: three renders at `F1Cutoff` = 44 / 58 / 72 (normalised 0.35 / 0.46 / 0.57) —
  `kickProminence` must be **monotonically decreasing**.
- `mix_report` …`bandEnergy.bass` (90–300 Hz) must be the **largest** of the four bands in the
  bass-only `verify_part` render. If `sub` (40–110) dominates instead, the patch is a sub, not a
  rolling bass.
- `pumpDepth` (see §10) must NOT increase when you enable an FX slot on the bass track — a dry
  bass (corpus: 72% `FX1Type = Bypass`) should leave `pumpDepth` unchanged within the measured
  same-input spread.

---

## 5. Recipe 3 — Lead

**Engine recommendation: microQ (Vavra) for a melodic/acid lead; XT (Xenia) for a
sync/PWM "hoover" lead.** Both are strong here — see §8.

### Device parameters (microQ, n=81 category `Lead`)

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..10 | **3 = 24 dB BP** (12dB BP×18, 24dB LP×16, **Comb+×15**, 12dB LP×14, 24dB BP×12, Comb−×4) | **3 `24 dB BP`** or **1 `24 dB LP`**; `9 Comb+` is the corpus's third choice (18.5%) and is a genuine microQ lead trick with no XT equivalent |
| `F1Cutoff` | 0..127 | 62 (35–96) | 56..72 |
| `F1Resonance` | 0..127 | 38 (5–62) | 48..72 |
| `F1EnvMod` | bipolar 64=0 | 73 → +9 (64–93) | 96..112 |
| `F1VelMod` | bipolar 64=0 | **64 = 0 (64–70)** | 64 (0) or +8 — leads are not velocity-shaped in this corpus |
| `F1Drive` | 0..127 | 0 (**p90 30**) | 0..32 — the corpus does drive leads, unlike basses and pads |
| `FilterRouting` | 0..1 | **1 Serial (median)** | Serial — leads are the one role where serial filters dominate |
| `O1Shape` | 0..6 | **2 Saw ×31**, 1 Pulse ×25, **5 Alt 1 ×13**, 4 Sine ×8, 6 Alt 2 ×3 | **2 `Saw`**, or **5 `Alt 1`** for the corpus's "digital" lead |
| `O2Shape` | 0..6 | 2 Saw ×35, 1 Pulse ×18, 5 Alt 1 ×8 | Saw, detuned ±(4..12) from O1 |
| `O3Shape` | 0..4 | 1 Pulse ×26, 2 Saw ×18, 0 Off ×17 | Saw or Pulse |
| `AmpEnvAttack` | 0..127 | **0 (0–29)** | 0..8 |
| `AmpEnvSustain` | 0..127 | **124 (0–127)** | 112..127 |
| `AmpEnvRelease` | 0..127 | 38 (0–77) | 32..56 |
| `FX1Type` | 0..6 | **1 Chorus ×37**, 0 Bypass ×32, 4 Overdrive ×7, 2 Flanger ×2 | **1 `Chorus`** — 46% of leads |
| `FX1Mix` | 0..127 | 33 (0–127) | 24..48 |
| `FX2Type` | 0..10 | **7 Delay ×46**, 0 Bypass ×25, 8 Reverb ×5 | **7 `Delay`** — 57% of leads; `FX2Mix` median 0 (p90 26) so the corpus keeps send low |
| `Lfo1Shape` / `Lfo1Speed` | 0..5 / 0..127 | 0 Sine ×63 / 35 (20–53) | Sine, 32..48 — the vocabulary has `Lfo1Speed` **not** host-writable, so bake it in the dump |
| `GlideEnable` / `GlideRate` | 0..1 / 0..127 | 0 / 20 (0–51) | enable + 16..32 for psy portamento leads |
| `UnisonoDetune` | 0..127 bipolar | 1 (0–13) | 0..12 — leads use more unison than basses (bass p90 = 9.2) |

### XT (Xenia), n=241 role `lead`

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..12 | **0 = 24 dB LP ×125** (52%); **8 `12 dB LP FM` ×16**; 12 Band Stop ×13; 3 12 dB BP ×12 | **0 `24 dB LP`**, or **8 `12 dB LP FM`** for the XT's FM lead |
| `F1Cutoff` | 0..127 | 62 (11–124) | 56..72 |
| `F1Resonance` | 0..127 | 58 (0–101) | 56..80 |
| `F1EnvAmount` | bipolar 64=0 | 70 → +6 (64–101) | 88..104 |
| `F1Extra` | 0..127 | 0 (**p90 74**) | 0, or 72..96 with type 8 (`12 dB LP FM`) |
| `F1EnvDecay` / `F1EnvSustain` | 0..127 | 44 / 40 | 44..64 / 40..64 |
| `AmpEnvAttack/Decay/Sustain/Release` | 0..127 | 3 / 50 / 97 / 16 | 2 / 48 / 112 / 20 |
| `W1StartW` | 0..63 | 37 | 32..48 |
| `W2StartW` | 0..63 | **61** | 56..63 — the XT lead uses a late second-wave position |
| `MixW1` / `MixW2` | 0..127 | 98 / 83 | both loud |
| `EffectType` | 0..34 | 0 Off ×143, **33 `Pan Delay` ×42**, 32 `Delay` ×23, 1 `Chorus` ×14 | 33 or 32 |
| `ChorusEnabled` | 0..1 | 0 (p90 1) | 0..1 |
| `EffectParamB` | 0..127 | 28 (0–113) | — |
| `ArpMode` | 0..3 | 0 Off ×229, On ×10, Hold ×2 | Off (see §7 for the arp role) |
| `Detune` | 0..127 | 10 | 8..24 |
| `GlideTime` | 0..127 | 32 (0–32) | 32..48 |

### Corpus evidence — example patches

**microQ (Vavra), category `Lead`, n=81:**

| Patch | File |
|---|---|
| `Air head      CJ` | `Lead\Air head      CJ Lead.syx` |
| `Bring it on   CJ` | `Lead\Bring it on   CJ Lead.syx` |
| `Buzzbee       CJ` | `Lead\Buzzbee       CJ Lead.syx` |
| `Caustic       CJ` | `Lead\Caustic       CJ Lead.syx` |
| `Eden          CJ` | `Lead\Eden          CJ Lead.syx` |
| `Halloween     CJ` | `Atmo\Halloween     CJ Lead.syx` |

**XT (Xenia), role `lead`, n=241:**

| Patch | File |
|---|---|
| `Syncron        T` | `2000.µsb` (also `factory_2000\fact2000-08.mid`) |
| `NotchSyncArp WMF` | `2000.µsb` |
| `Quintage Lead MK` | `2000.µsb` |
| `SawPWM Str   WMF` | `2000.µsb` |
| `Fat Sync Hit WMF` | `2000.µsb` |
| `Phat Sync     MK` | `2000.µsb` |

### Wavetable selection (XT lead)

| Wave | Name | Share of 241 leads | Corpus-wide share |
|---|---|---|---|
| 0 | `Resonant` | **21.6%** | 14.3% |
| 64 | `True PWM` | **19.9%** | 16.9% |
| 42 | `PWM Saw` | 5.8% | 4.1% |
| 3 | `Square-Sweep` | 4.6% | — |
| 35 | `Pul Sync 1` | 4.6% | 2.4% |
| 29 | `Micro PWM` | 4.1% | 5.4% |
| 30 | `Glassy` | 2.9% | — |
| 37 | `Pul Sync 3` | 2.9% | — |

**The XT lead is a PWM/sync lead**: `True PWM` (64) + `PWM Saw` (42) + `Micro PWM` (29) +
`Pul Sync 1/3` (35/37) together are **~37%** of leads. That is a real signal and it is the shape
the XT does better than the microQ — the microQ's `Alt 1`/`Alt 2` oscillator shapes are the
closest analogue but are **not host-writable** and are not wavetables. For a psy hoover/stab
lead use `64 True PWM` with `W1StartW` sweeping via `W1EnvAmount`.

### Movement

- **microQ lead/filter movement**: `list_matrix_presets {engine:"vavra"}`, id
  `16f6b843be79d6c9` — `FX1 ring mod+chorus+overdrive movement + FX2 ring mod+chorus+delay movement + O1 PWM + O2 PWM + O3 PWM`
  (`appliesVia:"waldorf_dump"`, 363 bytes). Delay+chorus+ring-mod movement built on the three
  oscillators' PWM — the microQ's answer to a PWM lead.
- **microQ continuous morph**: `vavra_morphs.json` pair **`8:36`** (distance 0.0202,
  `namedDiffRatio` 0.737 — the highest named-diff ratio, i.e. the morph that moves the most
  *named* parameters rather than raw bytes). Parents `a600e074f3ce6442` and its partner.
- **XT**: `xenia_morphs_injectable.json` pair **`20:33`** (distance 0.0038), the one documented
  discrete hop: `ChorusEnabled 0→1` + `F1EnvAmount 72→112`. Base `DX Thang 2    PN`
  (`mw2_pn1.mid`), `how:"verified"`, `byteMatches:84`. This is the "chorus switch + bigger filter
  env" move — exactly the lead gesture in a build.
- **XT, slot-depth reshape**: pair `10:34` (distance 0.0063) — five slot amounts move and
  `Slot4Source` re-aims (15→0) — for re-balancing an already-moving lead matrix.
- **XT, big swing**: pair `0:4` (distance 0.0208, zero discrete hops) — `MixRingMod 0→39`
  fades ring mod in while `F1Cutoff 127→76`, `F1Resonance 0→23`, `F1EnvAmount 64→76`,
  `W1EnvAmount 127→64`, `W2EnvAmount 64→101`. Described in `xenia_roles_morphs.md` as
  "clean wave-morph pad → ring-mod-mix mover" — use it as the lead's activation move.

### Apply via MCP

```
# microQ lead: patch (osc shapes + LFO speed are not host params), then host params
apply_preset {trackId, slotIndex, filePath: "…\\Lead\\Caustic       CJ Lead.syx"}
get_fx_capture_status {trackId, slotIndex}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Type",      value: 0.236}  # 30/127 → 3 (24 dB BP)
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",    value: 0.488}  # 62/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance", value: 0.543}  # 69/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FX1Type",     value: 0.167}  # 1 → Chorus
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FX2Type",     value: 0.875}  # 111/127 ≈ 7 → Delay
# discrete FX/pitch movement baked in the patch:
apply_matrix_preset {engine:"vavra", id:"16f6b843be79d6c9", trackId, slotIndex}
```

```
# XT lead with the documented chorus+filt-env morph
apply_matrix_preset {engine:"xenia", id:"9f58100d4cd8d3c2", trackId, slotIndex}   # pair 20:33 parent-A
# ... or step through the pair: ids come from the step's preset.id (list_matrix_presets)
```

### Falsifiable prediction — lead

- `verify_part {trackIndex, startBeat, endBeat}`: `bandsPresent` must include **`mid` and
  `high`**; `soloPeak` must stay below 0 dBFS so `nonClipping = true` once summed.
- `mix_report` on a full-mix render with a lead-only window: the **`body` band (300–2000 Hz)**
  energy must exceed the same window's value with the lead muted. This is the falsifiable core:
  a `24 dB BP` lead at `F1Cutoff` 62 puts its energy in 300–2000; a `Comb+` lead (corpus ×15/81)
  produces a *pitch-dependent comb* and the body band will move with the note — render two
  different notes and assert the body energy differs between them for `Comb+`, but not for
  `24 dB BP`.
- For the XT `True PWM` lead: sweep `W1EnvAmount` 0 → 64 → 127 (normalised 0 / 0.504 / 1.0)
  across three renders; the **`high` band (> 6000 Hz)** must increase monotonically. `W1EnvAmount`
  *is* host-writable on Xenia, so this is an end-to-end test of the persistence ledger, not just
  of the sound. If the high band is flat, the write did not reach the render.
- `render_and_verify` gives the same verdict in one call if you want the mix-level gates too.

---

## 6. Recipe 4 — Pad / atmos

**Engine recommendation: microQ (Vavra).** See §8 — it has a chorus on 89% of its pads and a
`Comb+` filter on 25% of them, and both are free.

### Device parameters (microQ, n=122 = category `Pad` 86 + `Atmo` 36)

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..10 | **3 = 24 dB BP** (12dB LP×30, 12dB BP×24, 24dB LP×23, **Comb+×23**, Comb−×8) | **2 `12 dB LP`** or **9 `Comb+`** — `Comb+` is 23/122 = 19% of pads (25% including `Comb−`) and is the classic microQ pad texture; the XT has no comb filter |
| `F2Type` | 0..10 | **2 = 12 dB LP ×52** | `12 dB LP`, `FilterRouting` Parallel (median 0) — pads are the one role where parallel filtering beats serial |
| `F1Cutoff` | 0..127 | 65 (41–80) | 60..72 |
| `F1Resonance` | 0..127 | 31 (7–65) | 24..40 — **lower than every other role** |
| `F1EnvMod` | bipolar 64=0 | **64 = 0 (64–94)** | 64..80 (0..+16) — pads barely use filter env in this corpus |
| `F1VelMod` | bipolar 64=0 | **64 (64–64)** | 64 (0) |
| `FilterEnvAttack` | 0..127 | 0 (**p90 82**) | 0..96 — the wide p90 is the "slow swell" half of the corpus |
| `FilterEnvDecay` | 0..127 | **88.5 (38–99)** | 80..104 |
| `FilterEnvSustain` | 0..127 | 0 (0–0) | 0 |
| `FilterEnvRelease` | 0..127 | **84 (34–95)** | 72..96 — this is the pad's tail |
| `AmpEnvAttack` | 0..127 | **51.5 (0–82)** | 48..72 |
| `AmpEnvDecay` | 0..127 | 68 (0–119) | 64..96 |
| `AmpEnvSustain` | 0..127 | **124.5 (0–127)** | 127 |
| `AmpEnvRelease` | 0..127 | **81 (71–87)** | 72..88 — the tightest-clustered parameter in the whole microQ pad set |
| `O1Shape` | 0..6 | **2 Saw ×37, 5 Alt 1 ×35**, 1 Pulse ×21, 4 Sine ×13 | **Saw**, or **5 `Alt 1`** — the corpus wants *two different* oscillator shapes |
| `O2Shape` | 0..6 | 2 Saw ×36, 1 Pulse ×29, 5 Alt 1 ×20 | a different shape from O1 |
| `O3Shape` | 0..4 | 1 Pulse ×32, 4 Sine ×30, 2 Saw ×29 | |
| `FX1Type` | 0..6 | **1 Chorus ×109 of 122 (89%)** | **1 `Chorus`** — unanimous |
| `FX1Mix` | 0..127 | **57 (25–127)** | 48..80 |
| `FX2Type` | 0..10 | 0 Bypass ×70, **7 Delay ×36**, 8 Reverb ×7 | Delay or Reverb, `FX2Mix` median 0 (p90 36) |
| `Lfo1Shape` | 0..5 | **0 Sine ×105** | Sine |
| `Lfo1Speed` | 0..127 | 34 (18–51) | 24..40 (**not host-writable**) |
| `UnisonoDetune` | 0..127 bipolar | 0.5 (**p90 15**) | 8..16 — pads use the most unison of any role |
| `GlideEnable` | 0..1 | 0 | 0 |

**Read that as: slow attack (52), long release (81), wide-open sustain (124), low resonance (31),
essentially no filter modulation (64 = 0), chorus always on, two different oscillator shapes,
and unison detune.** The pad is a *width and time* recipe, not a movement recipe.

### XT (Xenia), n=332 role `pad`

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `F1Type` | 0..12 | **0 = 24 dB LP ×167** (50%); 1 12 dB LP ×53; **7 `Dual 12 dB LP/BP` ×24**; **8 `12 dB LP FM` ×17**; **10 `24 dB Notch` ×15**; 11 `12 dB Notch` ×17 | **0 `24 dB LP`**; `7 Dual 12 dB LP/BP` and `10/11 Notch` are the pad-specific types |
| `F1Cutoff` | 0..127 | **79 (30–127)** | 72..96 — the XT pad is *brighter* than every other role |
| `F1Resonance` | 0..127 | 51 (0–104) | 40..64 |
| `F1EnvAmount` | bipolar 64=0 | **64 = 0 (64–95)** | 64..80 — same "no filter env" as the microQ |
| `F1EnvAttack/Decay/Sustain/Release` | 0..127 | 10 / 50 / 61.5 / 54 | 16..48 / 48..72 / 48..80 / 48..80 |
| `AmpEnvAttack` | 0..127 | **35 (0–70)** | 32..64 |
| `AmpEnvSustain` | 0..127 | **127 (45–127)** | 127 |
| `AmpEnvRelease` | 0..127 | 45 (31–57) | 40..56 |
| `W1EnvAmount` | bipolar 64=0 | **64 = 0 (58–123)** | 64 (static) for a pad; **96..127** for a moving wave-env pad |
| `W2EnvAmount` | bipolar 64=0 | 64 (48–123) | as above |
| `MixW1` / `MixW2` | 0..127 | **80 / 80** | equal — the pad is a two-wavetable *blend*, unlike bass (123/112) or lead (98/83) |
| `MixRingMod` | 0..127 | 0 (**p90 75.9**) | 0..48, or up to 96 for bell/atmos |
| `ChorusEnabled` | 0..1 | **1** | 1 — same conclusion as the microQ |
| `EffectType` | 0..34 | 0 Off ×209, 33 Pan Delay ×41, 1 Chorus ×26, 32 Delay ×15 | 1 / 32 / 33 |
| `F2Cutoff` | 0..127 | **127 (35–127)** | fully open |
| `F2Type` | 0..1 | 0 `6 dB LP` ×268 | `6 dB LP` |
| `Detune` | 0..127 | 11 | 8..24 |
| `ArpMode` | 0..3 | 0 Off ×328 | Off |
| `slotsUsed` | 0..16 | **7 (p10 3, p90 12)** | use the matrix — pads are the most matrix-dense XT role after arp |

### Corpus evidence — example patches

**microQ (Vavra), `Pad` + `Atmo`, n=122:**

| Patch | File |
|---|---|
| `BasaltColumns CJ` | `Atmo\BasaltColumns CJ Atmo.syx` |
| `Eye of newt   CJ` | `Atmo\Eye of newt   CJ Atmo.syx` |
| `Ghost ship    CJ` | `Atmo\Ghost ship    CJ Atmo.syx` |
| `Grease monkey CJ` | `Atmo\Grease monkey CJ Atmo.syx` |
| `Feedback      CJ` | `Atmo\Feedback      CJ Atmo.syx` |
| `Flooky        CJ` | `Atmo\Flooky        CJ Atmo.syx` |

**XT (Xenia), role `pad`, n=332:**

| Patch | File |
|---|---|
| `Wave-Premiere  T` | `2000.µsb` |
| `Plain Choir  WMF` | `2000.µsb` |
| `RingmodBellPadMK` | `2000.µsb` |
| `Shivering Pad MK` | `2000.µsb` |
| `Ping Pad     WMF` | `2000.µsb` |
| `JX10P StringsWMF` | `2000.µsb` |

### Wavetable selection (XT pad)

| Wave | Name | Share of 332 pads | Corpus-wide |
|---|---|---|---|
| 0 | `Resonant` | 13.0% | 14.3% |
| 63 | **`Chorus 2`** | **10.8%** | 2.6% |
| 64 | `True PWM` | 7.5% | 16.9% |
| 31 | **`Square HP`** | **6.3%** | 1.8% |
| 29 | `Micro PWM` | 5.7% | 5.4% |
| 42 | `PWM Saw` | 4.8% | 4.1% |
| 55 | **`Wavetrip 4`** | **3.0%** | — |
| 7 | **`Mellow Saw`** | **3.0%** | — |
| 106 | **`User 11`** | 2.7% | — |
| 27 | `Formant Vocal` | 2.4% | 1.6% |

**The pad-specific wavetables are `63 Chorus 2` (4× its corpus-wide rate), `31 Square HP`,
`55 Wavetrip 4`, `7 Mellow Saw` and the `User` bank (96–127).** That is a meaningful finding:
the XT pads deliberately reach for the *chorused* and *hollow-square* tables rather than the
`True PWM` the bass/lead roles prefer. `User 1..32` (96–127) appearing in the pad top-10 means
the corpus pads also carry user wavetables — for a psy pad, `63 Chorus 2` and `52–55 Wavetrip`
are the reachable, named choices.

### Movement

- **microQ pad/atmos movement**: `list_matrix_presets {engine:"vavra"}` —
  id `80b858f7baf75627`, name `FX1 vocoder movement + FX2 chorus+delay+flanger movement + O1 PWM + O2 PWM + O3 PWM`
  (`appliesVia:"waldorf_dump"`). A vocoder-based pad mover; also id `e7939d142c8d1e87`
  (`FX1 chorus+overdrive+vocoder movement + …`).
- **microQ pad/atmos continuous morph**: `vavra_morphs.json` pair **`23:38`** (distance 0.0246)
  / `23:39` (0.0418) — but their bases are bass patches; the pad-relevant alternative is the
  `0:4`-equivalent family on the XT below. On the microQ the honest statement is: **there is no
  pad-base morph chain in `vavra_morphs.json`** — all five resolve from `Bass`-category or
  unnamed bases, with `how:"prefix"` / `byteMatches:1`. Do not claim a pad morph on Vavra.
- **XT pad morph — the best-documented chain in the whole sheet**: pair **`0:4`**, distance
  **0.0208**, **zero discrete hops**, six continuous carriers:
  `MixRingMod 0→39` (fades ring mod in), `F1Cutoff 127→76`, `F1Resonance 0→23`,
  `F1EnvAmount 64→76`, `W1EnvAmount 127→64`, `W2EnvAmount 64→101`. Base `Tablescan11 HH`
  (`upawbnk.mid`), `how:"verified"`, `byteMatches:84`. `xenia_roles_morphs.md` calls it
  "clean wave-morph pad → ring-mod-mix mover" — built for exactly this role.
- Also **`13:36`** (distance 0.0040, "the minimal two-carrier morph": `F1Cutoff 55→41`,
  `Slot1Amount 93→64`) for a darkening pad, and **`10:34`** (0.0063) for matrix re-balance.

### Apply via MCP

```
# microQ pad/atmos
apply_preset {trackId, slotIndex, filePath: "…\\Atmo\\Ghost ship    CJ Atmo.syx"}
get_fx_capture_status {trackId, slotIndex}
list_fx_params {trackId, slotIndex}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",        value: 0.512}  # 65/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance",     value: 0.244}  # 31/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 AmpEnvAttack",    value: 0.409}  # 52/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 AmpEnvRelease",   value: 0.638}  # 81/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 AmpEnvSustain",   value: 1.0}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FilterEnvRelease",value: 0.661}  # 84/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FX1Type",         value: 0.167}  # Chorus
set_fx_param {trackId, slotIndex, paramName:"Ch 1 FX1Mix",          value: 0.449}  # 57/127
# filter type + oscillator shapes need the dump:
apply_matrix_preset {engine:"vavra", id:"80b858f7baf75627", trackId, slotIndex}
```

```
# XT pad with wave-env movement
apply_matrix_preset {engine:"xenia", id:"f4eda6a665e07bce", trackId, slotIndex}   # "16-slot matrix + wave env"
get_fx_capture_status {trackId, slotIndex}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 W1EnvAmount", value: 0.756}     # 96/127 = +32
set_fx_param {trackId, slotIndex, paramName:"Ch 1 W2EnvAmount", value: 0.756}
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",    value: 0.622}     # 79/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance", value: 0.402}     # 51/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 ChorusEnabled", value: 1.0}
```

### Falsifiable prediction — pad / atmos

- `verify_part {trackIndex, startBeat, endBeat, windowSeconds: 8}`:
  the **`attack` of the solo envelope** must be observable — render 2 s and 8 s windows and
  `soloRms` must be **higher in the 8 s window**, because `AmpEnvAttack` ≈ 52 puts the
  envelope's rise inside the first seconds. A pad whose 2 s and 8 s `soloRms` are equal has no
  attack (or the write did not land).
- `mix_report {filePath, bpm, sections}`: `pumpDepth` over the pad's section must be **lower**
  than the bass section's. `pumpDepth = (max−min)/mean of per-beat RMS, averaged over sections
  with ≥ 8 beats` — a sustained pad has near-constant per-beat RMS, so a high `pumpDepth` in a
  pad-only window falsifies the sustain settings (`AmpEnvSustain` should be 127).
- **The chorus claim**: A/B a pad render with `FX1Type` Chorus (1) vs Bypass (0) on the microQ.
  The `high` band (> 6000 Hz) must **rise** with chorus on, and the difference must exceed the
  render variance floor. This is directly testable because `FX1Type`/`FX1Mix` *are* host-writable
  while the chorus *character* is not (§1) — so if the high band moves, the type-level write
  reached the OS and you have isolated the parameter you can automate.
- **The XT wavetable claim**: from the pad recipe, `63 Chorus 2` should produce a higher
  `high`-band energy than `0 Resonant` at the same `F1Cutoff`/`F1Resonance`, because
  `Chorus 2` is a chorused table. Render both (dump route required — `Wave` is not a host
  parameter) and compare `mix_report`'s `bandEnergy.high`. If the two are indistinguishable, the
  wavetable did not change and the dump was mis-framed (§9).

---

## 7. Recipe 5 — Wavetable texture (the XT's specialty) + both onboard arps

> ### ⚠ MEASURED CONSTRAINT (2026-09-30): the XT does not sustain a HELD note
>
> A recipe applied exactly as written in this section, then played as **one held triad for 7.5 beats**,
> rendered **silence** (`soloRms 1.2e-06`, `audible=0`). The **same patch on the same track** played as
> **16th notes** rendered `soloRms 0.102418`, `audible=1`.
>
> This is **not** a parameter problem and **not** the amp envelope: the probe set
> `Ch 1 AmpEnvSustain` to 0.87 (a sustaining value), and held-note silence persisted while repeated
> notes sounded. Isolating the wave parameters one at a time against a known-sounding baseline
> (`Wave`, `W1StartW`, `MixW1/2`, `WaveEnvTime1/Level1`, `F1EnvAmount`) **never** produced silence —
> so no single wave parameter is responsible.
>
> **Consequence for this recipe: a wavetable texture on the XT must be RETRIGGERED, not held.** Do not
> write sustained-chord pad parts for `Xenia.clap` on the assumption that a long note sustains; either
> retrigger (16ths / a rate-quantised pattern) or treat the XT as a textural *layer* on top of a
> sustaining instrument rather than as the pad body. The exact mechanism (envelope one-shot behaviour
> vs an emulation limitation) was **not** pinned down — recorded as an open question, not explained
> away.
>
> This does not affect the **acid** recipe verified live earlier on the same engine
> (`soloRms 0.0285`, `audible=1`) — that part was 16th notes.

**Engine recommendation: XT (Xenia), without qualification.** This is the one role where the XT
is not merely competitive but has no microQ analogue: the microQ has no wavetables (its
oscillators are Pulse/Saw/Triangle/Sine/Alt 1/Alt 2), so "wavetable texture" is XT-only by
construction. The microQ's nearest relatives are `O1Shape = 5 Alt 1` / `6 Alt 2`, which are
not host-writable and not wavetables.

### What the corpus says the role *is*

I defined the subset **objectively, not by name**: `W1EnvAmount ≥ 32 AND MixW1 > 0` — i.e.
patches where the wave envelope actually moves the wave position and wave 1 is audible.
That is **n = 5260 of 5870 (90%)** — which is itself the headline finding: *the XT's default
architecture is a wavetable texture.* The XT is not a synth that can do wavetable texture;
it is a synth that is a wavetable texture by default, which is why it makes a poor
"plain subtractive" and an excellent "moving texture".

### Device parameters (XT vocabulary)

| Parameter | Byte range | n=5260 subset median (p10–p90) | Recommended |
|---|---|---|---|
| `Wave` | 0..63 / 64..127 (User 1..32) | 36 (0–64) | see the table below; **host-writable** as `Ch 1 Wave` (normalized ÷127 — measured 2026-09-30) |
| `W1StartW` | 0..63 | **30 (0–63)** | 0..16 to start at the bright/attack end, 48..63 to start dark and *sweep up* through the envelope; **host-writable** as `Ch 1 W1StartW` (normalized ÷127 — measured 2026-09-30) |
| `W1EnvAmount` | bipolar 64=0 | **64 = 0 (64–114)** | **96..127** for a real texture; the corpus's own p90 is 114 |
| `W1EnvVelAmount` | bipolar 64=0 | 64 (64–64) | 64 (0) |
| `W2StartW` | 0..63 | **31 (0–63)** | offset from `W1StartW` by 8..24 for a two-table beating texture |
| `W2EnvAmount` | bipolar 64=0 | 64 (64–79) | 64..96 |
| `W1Keytrack` | 0..127 | (index 30) | 0/64 for a fixed timbre across the keyboard — keytracking a wavetable makes the top octave thin |
| `MixW1` | 0..127 | **100 (55–127)** | 100..127 |
| `MixW2` | 0..127 | 80 (0–127) | 64..100 |
| `MixRingMod` | 0..127 | **0 (p90 117!)** | 0 for a clean texture, **64..117** for a metallic/inharmonic texture — note the p90 of 117: a large minority of the corpus runs ring mod *hard* |
| `MixNoise` | 0..127 | 0 (p90 3) | 0 — wavetable texture is not a noise recipe |
| `F1Cutoff` | 0..127 | 64 (19–117) | 64..96 — **leave the filter open and let the wave env do the movement**, otherwise you are stacking two sweeps on one gesture |
| `F1Resonance` | 0..127 | 70 (0–105) | 56..80 |
| `F1EnvAmount` | bipolar 64=0 | 76 → +12 (64–105) | 64 (0) if the wave env is doing the work |
| `F1Type` | 0..12 | 1 = 12 dB LP (median) | **0 `24 dB LP`** or **7 `Dual 12 dB LP/BP`** |
| `F1Extra` | 0..127 | 0 (p90 82) | 0, or 72..96 with a shaper/FM type |
| `EffectType` | 0..34 | 0 Off (p90 33) | 32/33 Delay/Pan Delay, or 1 Chorus |
| `MixRingMod` + `Aliasing` | 0..5 | `Aliasing` 0 (0–2) | `Aliasing` is a *deliberate* texture control here: the corpus shows 0–5 and `xenia-offset-map.md` records values >5 as tolerated junk. **Set `Aliasing` 0 for a clean texture, 2..5 for digital grit** — it is host-writable |
| `WaveEnvTime1..8` / `WaveEnvLevel1..8` | 0..127 | (indices 125–140) | **the actual texture shape** — an 8-segment wave envelope. The corpus carries a full 14–16-slot matrix *plus* this wave envelope on every harvested config (`xenia_roles_morphs.md`). **Host-writable — CORRECTED 2026-09-30:** `Ch 1 WaveEnvTime1` and `Ch 1 WaveEnvLevel1` are both in the live 2151-param list and round-trip exactly (measured). The earlier "not in the 66-param public set → needs a dump" was another instance of the vocabulary's `isPublic` flag not matching the live host surface |

### Wavetable selection (the answer to "which wavetables does the corpus favour")

Across the wavetable-texture subset (n=5260 distinct=101):

| Rank | Wave | Name | Share |
|---|---|---|---|
| 1 | 64 | **`True PWM`** | **16.9%** |
| 2 | 0 | **`Resonant`** | **14.3%** |
| 3 | 29 | `Micro PWM` | 5.4% |
| 4 | 42 | `PWM Saw` | 4.1% |
| 5 | 63 | `Chorus 2` | 2.6% |
| 6 | 41 | `PWM Pulse` | 2.5% |
| 7 | 35 | `Pul Sync 1` | 2.4% |
| 8 | 32 | `Saw Sync 1` | 1.9% |
| 9 | 31 | `Square HP` | 1.8% |
| 10 | 27 | `Formant Vocal` | 1.6% |
| 11 | 16 | `Formant 1` | 1.5% |
| 12 | 61 | `Fat Organ` | 1.4% |

**But when you filter to patches where the wave envelope is *actually driving* the table
(`W1EnvAmount ≥ 96`, n=664), the ranking changes and this is the more useful table for a
"moving texture":**

| Rank | Wave | Name | Share of high-env subset |
|---|---|---|---|
| 1 | 63 | **`Chorus 2`** | **4.8%** |
| 1= | 0 | **`Resonant`** | **4.8%** |
| 3 | 41 | `PWM Pulse` | 4.5% |
| 4 | 59 | **`Xmas Bell`** | **4.2%** |
| 5 | 35 | `Pul Sync 1` | 3.9% |
| 6 | 51 | **`19/twenty`** | **3.3%** |
| 7 | 42 | `PWM Saw` | 3.0% |
| 7= | 27 | `Formant Vocal` | 3.0% |
| 9 | 52 | **`Wavetrip 1`** | **2.9%** |
| 10 | 32 | `Saw Sync 1` | 2.4% |
| 11 | 64 | `True PWM` | 2.1% |
| 11= | 53 | **`Wavetrip 2`** | **2.1%** |

That distinction matters: `True PWM` is the single most common table overall, but it **drops**
in the high-envelope subset while `Chorus 2`, `Xmas Bell`, `19/twenty` and the `Wavetrip`
series **rise**. `Wavetrip 1–4` (52–55) and `19/twenty` (51) are the tables the corpus reaches
for when it wants the wave envelope to travel — which is precisely the psy "evolving texture"
gesture.

### The XT's own arp (n=474, `ArpMode ≠ 0`)

The corpus splits **exactly** 237 `On` / 237 `Hold`, 0 `Sound`; across all 5870 the split is
`Off 5396 / Hold 237 / On 237`.

| Parameter | Byte range | median (p10–p90) | Recommended |
|---|---|---|---|
| `ArpMode` | 0..3 (`Off/On/Hold/Sound`) | On/Hold (by construction) | **1 `On`** for a fixed run, **2 `Hold`** to latch a held-chord texture |
| `ArpClock` | 0..15 → `1/1, 1/2 D, 1/2 T, 1/2, 1/4 D, 1/4 T, 1/4, 1/8 D, 1/8 T, 1/8, 1/16 D, 1/16 T, 1/16, 1/32 D, 1/32 T, 1/32` | **12 = `1/16` (p10 9 = `1/8`)**, corpus-wide `1/16` 3425 / `1/1` 1774 | **10..12 (`1/16 D`..`1/16`)** for psy 16th texture; `11 = 1/16 T` for triplets |
| `ArpRange` | 1..10 | **2 (1–4)** | 2..4 — a 2-octave span; >4 turns into a "trance gate" smear |
| `ArpDirection` | 0..3 (`Up/Down/Alt/Random`) | **0 `Up`** | 0 or 2 `Alt` |
| `ArpNoteOrder` | 0..3 (`By Note/By Note Rev/As Played/Reversed`) | **0 `By Note`** | 0, or 2 `As Played` for a played-phrase feel |
| `ArpPattern` | 0..16 (`Off/User/1..15`) | **0 `Off` (p90 9)** | **0 `Off`** — the corpus overwhelmingly does *not* use the XT's built-in patterns; it arpeggiates the chord. Using pattern 1..15 is an unexplored direction, not a corpus-supported one |
| `ArpTempo` | 1..127 | 40 (24–65) | irrelevant if you clock externally; **not host-writable** |
| `ArpVelocity` | 0..1 | — | 0..1 |
| `ArpReset` | 0..1 | — | 1 for a deterministic downbeat |
| `ArpUserPatternLength` / `ArpUserPattern1..16` | 0..15 / 0..1 | — | only with `ArpPattern = 1 `User`` |

**The arp's own timbre**: with the arp on, the corpus's median `F1Type` is **4 = `12 dB HP`**
(vs `24 dB LP` for every non-arp role), `F1Resonance` 79, `MixRingMod` 20.5 and `EffectType`
median 32 (`Delay`). That is a specific and non-obvious recipe: **arp patches are high-passed,
more resonant and more ring-modulated than the same synth's leads** — they are meant to sit
*above* the bass, not compete with it. `F1Type`, `MixRingMod`, `EffectType` and `ArpClock`/
`ArpRange`/`ArpDirection`/`ArpNoteOrder`/`ArpPattern` are all in the 66-param public set, so
this whole recipe is host-writable except the wavetable.

`W1EnvAmount` in the arp subset: 64 (p10 63, p90 111) — arps also use the wave envelope.

### The microQ's own arp — the largest microQ category, and it is per-sound

I initially assumed the microQ arp was a host/global feature. **It is not** — the vocabulary puts
45 arp parameters at **page 0**, linear indices **311–323** (globals) and **327–358** (per-step),
i.e. dump bytes **318–365**, all inside the 392-byte single dump's parameter block (7..369):

| Index | Parameter(s) | Value list |
|---|---|---|
| 311 | `ArpMode` | `Off, On, One Shot, Hold` |
| 312 | `ArpPattern` | `Off, User, 1..15` |
| 313 | `ArpMaxNotes` | 0..15 |
| 314 | `ArpClock` | 131-entry 192nd-note grid (`1/64 … 1/1 T`), **`9 = 1/16`** |
| 315 | `ArpLength` | short staccato lengths (`1/128 T … Legato`), **`6 = 7/192`** |
| 316 | `ArpOctaveRange` | 0..6 |
| 317 | `ArpDirection` | `Up, Down, Alt Up, Alt Down` |
| 318 | `ArpSortOrder` | `As Played, Reversed, Note Low-High, Note High-Low, Vel Low-High, Vel High-Low` |
| 319 | `ArpVeloMode` | `Each Note, First Note, Last Note` |
| 320 | `ArpTFactor` | 0..127 |
| 321 | `ArpSameNoteOverlap` | 0..1 |
| 322 | `ArpPatternReset` | 0..1 |
| 323 | `ArpPatternLength` | 0..15 |
| 327+n (n=0..15) | `Arp{n:02d}Step` / `Arp{n:02d}Glide` / `Arp{n:02d}Accent` | Step: `*, ␠, -, <, >, <>, Chord, ?` · Accent: `Off, -3..+3` |
| 343+n (n=0..15) | `Arp{n:02d}Length` / `Arp{n:02d}Timing` | Length: `Legato, -3..+3, As Note Length` · Timing: `Random, <<<, <<, <, \|, >, >>, >>>` |

**Measured corpus behaviour, category `Arp` (n=147 of 528 — the biggest microQ category):**

| Parameter | Median | Distribution |
|---|---|---|
| `ArpMode` | **3 `Hold`** | Hold 142, On 5 (across all 528: Off 375, Hold 144, On 7, One Shot 2) |
| `ArpPattern` | **1 `User`** | User 143, Off 4 — the microQ corpus **uses** its 16-step user pattern |
| `ArpClock` | **9 `1/16`** | 1/16 ×138 of 147 (×493 of all 528) |
| `ArpLength` | **6 = `7/192`** | p10 1 (`1/64 T`), p90 11 (`11/192`) — short, staccato |
| `ArpMaxNotes` | **15** | p10 9 — it plays the whole chord |
| `ArpOctaveRange` | 0 | p90 1 |
| `ArpDirection` | 1 `Down` | Up 66, Alt Up 43, Down 19, Alt Down 19 |
| `ArpSortOrder` | 0 `As Played` | 0 ×106, 2 `Note Low-High` ×40 |
| `ArpVeloMode` | 1 `First Note` | 1 ×72, 0 `Each Note` ×54, 2 `Last Note` ×21 |
| `ArpPatternLength` | **15** | p10 15 — the full 16 steps |
| `ArpPatternReset` | 1 | |

So the microQ arp recipe, from the corpus: **`ArpMode = Hold`, `ArpPattern = User`,
`ArpClock = 1/16`, `ArpLength` short (`7/192`..`11/192`), `ArpMaxNotes = 15`,
`ArpPatternLength = 15`, `ArpDirection = Up` or `Alt Up`, `ArpPatternReset = 1`** — a latched
16-step 16th-note staccato arp over the whole held chord.

**Two caveats, both real:**

1. **None of the 45 arp parameters is `isPublic`.** The microQ arp is reachable *only* through
   the 392-byte dump — and therefore must be re-framed to `0x20/0x00` (§9) or routed through
   `apply_preset` to be audible. There is no `set_fx_param` route.
2. **The per-step field → byte mapping is unresolved.** Five fields per step (Step/Glide/Accent/
   Length/Timing) are expressed over only **two** linear indices per step (327+n and 343+n),
   as name aliases on the same index — the same alias pattern as the FX block, but the FX block's
   aliasing is explained by the selected FX type and this one has no such explanation in the
   sources I read. `dump_byte = 7 + index` gives the right *address*; which of the three
   (or two) names a given byte carries at a given moment I could **not** determine without the
   plugin's parameter-send path. **Write these bytes and verify audibly; do not trust the
   name→field pairing.**

### The microQ arp — apply, and prediction

```jsonc
// The arp lives in the patch. Two audible routes, both verified in shape:
apply_preset {trackId, slotIndex,
              filePath: "D:\\pdf\\rhythm-lab.com_waldorf_micro_q\\Arp\\Arizona       CJ Arp.syx"}
//   → retargets to 0x20/0x00 + fixes the checksum (PresetApply.h:547-565)
// or re-frame a matrix-sheet dump yourself (§9) and send_fx_midi it.
list_fx_params {trackId, slotIndex}   // confirm: NO "Ch n Arp*" entry will appear — expected
```

Prediction: `verify_part` on the arp part must show `bandsPresent` **without** a dominant `sub`
band, and `mix_report`'s `pumpDepth` on the arp window must exceed the pad window's (16 pulses
per bar at `ArpClock = 1/16` vs a sustained pad). Re-render with `ArpClock` byte 314 set to
`21 = 1/8` in a re-framed dump: `pumpDepth` must **halve**. If `pumpDepth` is unchanged, the arp
byte never reached the OS — check the framing before concluding the parameter is inert.

### Corpus evidence — example patches

**XT wavetable texture (high-`W1EnvAmount` subset ∩ corpus names):**

| Patch | File |
|---|---|
| `CS-80      T&WMF` | `2000.µsb` |
| `Wave-Premiere  T` | `2000.µsb` |
| `Geegaagook   WMF` | `2000.µsb` |
| `Drooo-like     S` | `2000.µsb` |
| `Syncron        T` | `2000.µsb` |
| `NotchSyncArp WMF` | `2000.µsb` |

**XT arp (`ArpMode ≠ 0`):**

| Patch | File |
|---|---|
| `Geegaagook   WMF` | `2000.µsb` |
| `NotchSyncArp WMF` | `2000.µsb` |
| `Flutter FM   WMF` | `2000.µsb` |
| `ARP Trig1*    DK` | `2000.µsb` |
| `Talking Arp    T` | `2000.µsb` |
| `Red DanceBass JH` | `2000.µsb` |

### Movement

The XT's movement story **is** the matrix, and the harvest says so explicitly:
`xenia_roles_morphs.md` — *"Every corrected config carries a 14-16 slot matrix + wave env … the
dense matrix IS the XT bank style."* All 40 `xenia.json` presets are role `mod-matrix`; the
whole sheet is one family with sub-flavours ("ring mod mix", "chorus on", "fx mode N",
"2 mod-router(s)", "delayed LFO1", "big filter env"). Pick by sub-flavour from the preset names:

| id | Name (sub-flavour) | Use |
|---|---|---|
| `f4eda6a665e07bce` | `16-slot matrix + wave env` | the base texture config |
| `57e3dac33b0dd64f` | `16-slot matrix + ring mod mix + wave env` | metallic texture (parent of morph `4:25`) |
| `10858f84d6afc608` | `16-slot matrix + chorus on + big filter env + wave env` | the pad/atmos mover |
| `9cb94669c8f3fb90` | `16-slot matrix + 2 mod-router(s) + chorus on + big filter env + delayed LFO1 + wave env` | the most movement |
| `09095c9b1b41ca29` | `15-slot matrix + 3 mod-router(s) + chorus on + big filter env + delayed LFO1 + wave env` | maximum router count |
| `bf0b4b5d177ba695` | `16-slot matrix + loud ring mod + big filter env + wave env` | aggressive/dark |

Morph chains for the texture role (all `how:"verified"`, `byteMatches:84`):
`0:4` (0.0208, zero hops — the big clean→ring-mod swing), `4:25` (0.0076, resonance-led
brightness swell), `13:36` (0.0040, minimal two-carrier darkening), `10:34` (0.0063, one
routing re-aim), `20:33` (0.0038, chorus switch + bigger filter env).

`xenia_roles_morphs.md` also records the honest caveat: these five pairs keep `EffectType` and
every other discrete key fixed for smooth morphs, and every step's SysEx targets the single edit
buffer at bank `0x20` — "live-verified 2026-09-16".

### Apply via MCP

```
# 1) A harvested moving-texture config (correctly framed already: bank byte 0x20)
list_matrix_presets {engine:"xenia"}
apply_matrix_preset {engine:"xenia", id:"57e3dac33b0dd64f", trackId, slotIndex, captureToTree:true}
get_fx_capture_status {trackId, slotIndex}          # poll to status "ok"

# 2) The host-writable half of the texture
set_fx_param {trackId, slotIndex, paramName:"Ch 1 W1EnvAmount", value: 0.898}   # 114/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 W2EnvAmount", value: 0.756}   # 96/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 MixRingMod",  value: 0.0}     # or 0.74 for metallic
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff",    value: 0.622}   # 79/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Resonance", value: 0.551}   # 70/127
set_fx_param {trackId, slotIndex, paramName:"Ch 1 Aliasing",    value: 0.0}     # 0 clean / 2..5 grit

# 3) The wavetable itself — NOT a host parameter. Send a 265-byte dump:
send_fx_midi {trackId, slotIndex, captureToTree: true, messages: [
  { kind: "sysEx", bytes: [240,62,14,0,16, 32,0, /* 256 param bytes */ 247] } ]}
```

Build that dump with the repo's own tooling rather than by hand:

```python
# bank byte 0x20 = single edit buffer, program 0  → the audible target
import sys; sys.path.insert(0, "timbre-lib")
import xenia_dump as xd
base = xd.load_bank(r"D:\pdf\microwave\2000.µsb")[0]["params"]   # any parent patch
dump = xd.build_single_dump(base, {"Wave": 63, "W1StartW": 12, "W1EnvAmount": 114,
                                   "MixW1": 110, "MixW2": 80},
                            name="PSY WT", bank=0x20, program=0x00)
open(".tmp_waldorf/psy_wt.syx", "wb").write(dump)   # 265 bytes, checksum computed
```

then either `send_fx_midi` with `bytes: list(dump)`, or write the file and use the front door:

```
apply_preset {trackId, slotIndex, filePath: "…\\.tmp_waldorf\\psy_wt.syx"}
```

(`xenia_dump.build_single_dump` frames bank/program and recomputes the checksum itself —
`compute_checksum` = `sum(dump[7:263]) & 0x7F`, `Xenia-offset-map.md` §7.)

### Falsifiable prediction — wavetable texture + arp

**Texture:**
- Because `W1EnvAmount` is host-writable on Xenia, this is a clean end-to-end test. Render three
  versions at `W1EnvAmount` = 0 / 64 / 127 (normalised 0 / 0.504 / 1.0) on a **sustained** note
  (≥ 2 s), and run `mix_report` on each:
  - **`bandEnergy.high` (> 6000 Hz) must be non-monotonic in the right way**: a wavetable sweep
    moves energy *through* the band, so the correct signature is that the **`body`
    (300–2000 Hz) and `high` band energies change by more than the render noise floor**, and
    that they change *relative to each other*, not merely that both rise. A flat `high` band
    falsifies the write reaching the OS.
  - The measured Xenia render variance to beat: **same-input spread 0.0056** (recorded in
    `docs/plans/2026-09-21-plugin-param-persistence.md`), which exceeded a 0.0023 separation.
    So gate on a **band ratio**, e.g. `high/body` differing by ≥ 20% between `W1EnvAmount`
    0 and 127 — not on rms.
- **The wavetable claim**: with `Wave` = `0 Resonant` vs `63 Chorus 2` at identical
  `F1Cutoff`/`F1Resonance`/`W1EnvAmount`, `mix_report`'s `bandEnergy.high` must differ. If it
  does not, the dump was mis-framed or `Wave` never changed.

**Arp:**
- `verify_part` on the arp part: `bandsPresent` must include **`mid` and `high` but may lack
  `sub`** — that is the falsifiable content of the `F1Type = 12 dB HP` finding. If an arp patch
  shows strong sub-band energy, the high-pass was not applied.
- `mix_report {filePath, bpm: <your bpm>, sections:[…]}`: `pumpDepth` on the arp section must be
  **≥ the pumpDepth of the lead section** — a 1/16 arp at `ArpClock 12` with `AmpEnvSustain`
  low produces 16 pulses per bar, which `pumpDepth` (max−min)/mean of per-beat RMS measures
  directly. Set `ArpClock` to `9` (1/8) and re-render: `pumpDepth` must **decrease**.
- `kickProminence` must not drop when the arp is unmuted — the high-pass is what keeps the arp
  out of the kick's `E(35-110)` region.

---

## 8. microQ vs XT — the verdict, per role

The track currently uses neither synth, so this is the decision table. "Corpus role n" is my
measured count; the verdict weighs corpus support, host-writability, and the device's own
architecture as evidenced above.

| Role | microQ / Vavra | XT / Xenia | **Winner** | Why |
|---|---|---|---|---|
| **Acid stab** | n=9 name~acid (thin). `F1Type` 24 dB LP/BP, `F1Resonance` median 52, 8/9 use `FX2Type = Delay` | n=33 name~acid. `F1Resonance` median **100**, `F1EnvDecay` 110, 51.5% on `Resonant` | **XT** (narrow) | The XT corpus is 3.7× the acid sample and is *committed* (resonance 100 vs 52). The XT's `12 dB LP > Shaper` and `Resonant` wavetable do the acid voice natively. **microQ wins on one axis**: `Comb+`/`Comb−` filters (no XT equivalent) and its `FX2 Delay` is onboard per patch. If you want a *squelchy 24 dB acid*, the microQ is more predictable; if you want a *resonant, shaper-driven acid*, the XT is better supported. |
| **Rolling bass** | n=79. Coherent recipe: `12 dB LP`, cutoff 50, res 41, `FilterEnvSustain` 0, `AmpEnvSustain` 123, **72% FX1 bypass**, `unisono` 0 | n=407 (5×). `24 dB LP` half the time, cutoff **45**, res **71**, `effectType` **Off 83%**, `Matrix` median 6/16 slots | **microQ** (narrow) | Higher `F1Resonance` (71 vs 41) on a *bass* is a liability for a rolling line — it puts the resonant peak where the kick lives. And the XT bass's two-loud-wavetable blend (`MixW1` 123 / `MixW2` 112) means two wavetables of phase-beating low end on every 16th. The microQ's single-shape, low-resonance, serial-filter bass is the safer roll. **XT wins if** you specifically want a wavetable bass (a `Pul Sync 1` or `Micro PWM` roll is unmistakably XT and the microQ cannot make it). |
| **Lead** | n=81. `24 dB BP` + **`Comb+` 15/81 (18.5%)**, chorus 46%, delay 57%, `F1VelMod` 0, serial routing | n=241 (3×). `24 dB LP` 52%, **`12 dB LP FM` 16**, PWM/sync wavetables **~37%**, `Pan Delay` 42 | **draw** — pick by target | The microQ's `Comb+` lead and its `Alt 1`/`Alt 2` shapes are the more *organic* lead; the XT's `True PWM` / `PWM Saw` / `Pul Sync` tables are the more *produced* lead (hoover, sync, 90s trance). Neither dominates: the microQ has the comb filter and the XT has the sync/PWM tables. Use **microQ for melodic/acid leads, XT for hoover/sync leads.** |
| **Pad / atmos** | n=122. **89% chorus**, `24 dB BP`/`12 dB LP`, `Comb+` **19%**, `AmpEnvAttack` 52, `AmpEnvRelease` 81 (very tight, p10–p90 71–87), `unisono` p90 15 | n=332. `24 dB LP` 50%, cutoff 79, `ChorusEnabled` 1, `MixW1/W2` **80/80**, slots 7/16, `W1EnvAmount` p90 123 | **microQ** (clear) | Two independent reasons. (a) **Architecture**: the microQ has a real **comb filter** (`Comb+`/`Comb−`) that 25% of its pads use and the XT simply does not have — and comb filtering is the classic psy pad/atmos texture. (b) **Corpus consensus**: 89% chorus with a *tight* release cluster (71–87) is a much stronger, more reproducible recipe than the XT pad's diffuse `W1EnvAmount` 0–123 spread. The XT pad wins only if you need a *wavetable-morphing* pad (§7) — which is a different sound, not the same role. |
| **Wavetable texture** | **not possible** — microQ oscillators are Pulse/Saw/Triangle/Sine/Alt 1/Alt 2, no wavetables | n=5260 of 5870 (90%) are wave-envelope-moving; 101 distinct tables in use | **XT, uncontested** | By construction. The XT is the only one of the two with wavetables, an 8-segment wave envelope (`WaveEnvTime/Level1..8`), two independent wave carriers with independent start-wave/phase/env, and named `User 1..32` slots (96–127) for your own tables. The microQ's nearest analogue is `Alt 1`/`Alt 2`, which are static shapes and not host-writable. |
| **Arp** | `Arp` is the **largest microQ category (147 of 528)** and the arp *is* per-sound: 45 parameters at linear indices 311–323 + 327–358, **all page 0, all inside the single dump** (bytes 318–365 ≤ 369). A 16-step user pattern with per-step Step/Glide/Accent/Length/Timing. **But none of it is `isPublic`** → not host-writable at all; the whole arp must ride in the 392-byte dump. Corpus: `ArpPattern = 1 `User`` in **143/147**, `ArpMode = 3 `Hold`` in **142/147**, `ArpClock = 9 `1/16`` in **138/147** (and `1/16` in 493 of all 528) | n=474 with `ArpMode ≠ 0`; per-sound, and `ArpClock`/`ArpRange`/`ArpDirection`/`ArpNoteOrder`/`ArpPattern`/`ArpVelocity`/`ArpReset` are **all host-writable**, so the arp is HDAW-automatable | **split — XT for automatable arps, microQ for programmed step patterns** | The XT arp is the only one of the two you can *automate from HDAW* (step `ArpClock` 9→12 or `ArpRange` 1→4 from an automation lane / track LFO), and its arp patches have a distinct, corpus-supported timbre (`F1Type` `12 dB HP`, res 79, `MixRingMod` 20.5, delay). The microQ arp is the more *expressive* one — 16 steps × step-type (`*`, `-`, `<`, `>`, `<>`, `Chord`, `?`) × glide/length × accent × timing, with `ArpPattern = User` in 97% of its arp patches — but every one of those 45 parameters is dump-only, which means (per §9) they must be re-framed to `0x20` or routed through `apply_preset` to be heard. Note the microQ corpus's near-unanimous `ArpClock = 1/16` + `ArpLength = 7/192` (short staccato) + `ArpMaxNotes = 15`: it is a 16th-note staccato arp machine. |

**Summary for the track:** build **rolling bass + pad/atmos on Vavra**, **wavetable texture on
Xenia**, and split the remaining three: **lead** and **acid stab** by target sound (microQ for
comb/organic, XT for PWM/sync/resonant), and **arp** by how you want to drive it — **Xenia if
you want to automate the arp parameters, Vavra if you want a programmed step pattern** (with the
§9 framing caveat). That uses each engine where its own architecture — not merely its corpus
count — gives it an advantage.

---

## 9. The framing pitfall — measured this session, and where the tooling diverges

This is the highest-value operational finding in the document, and it contradicts a natural
assumption about the shipped matrix sheets.

`docs/va-suite-status-log.md:58` records the root cause of the microQ's original "NOT APPLYING"
bug: *real bank dumps carry `0x30` (multi-edit) or `0x40+` (bank) buffer bytes the single-mode
OS never plays; retargeting to `0x20` makes the OS load the edit buffer.* The fix lives in
**exactly one place**: `src/common/PresetApply.h:547-565` — the `apply_preset` Waldorf-SysEx route
rewrites `d[5] = 0x20` (`MidiBufferNum::SingleEditBufferSingleMode`), `d[6] = 0x00`, and
recomputes the Waldorf checksum.

**Measured, this session:**

| Source | n | byte5 (bank/buffer) | byte6 (program) |
|---|---|---|---|
| microQ corpus `.syx` (all 528 files) | 528 | **`0x30` — 528/528** | `0x00` |
| `matrix_presets/vavra.json` stamped `sysex` | 40 presets | **`0x30` — 40/40** | `0x00` |
| `matrix_presets/vavra_morphs.json` step `sysex` | 20 steps | **`0x30` — 20/20** | `0x00` |
| `matrix_presets/xenia.json` stamped `sysex` | 40 presets | **`0x20` — 40/40** ✅ | `0x00` |
| `matrix_presets/xenia_morphs_injectable.json` step `sysex` | 20 steps | **`0x20` — 20/20** ✅ | `0x00` |

And — **before the 2026-09-30 fix described in the box above** — `apply_matrix_preset` injected the
sheet's `sysex` array **verbatim** through `send_fx_midi` (the analysis below is preserved as the
evidence that found the defect; read it in the past tense):

- sheet presets → `src/common/MatrixPresetService.cpp:620-645` (`dev.sysex.push_back(...)` →
  `sendFxMidi(dp)`),
- morph steps → `src/common/MatrixPresetService.cpp:736-753` (same, `{queued, captureDeferred}`).

> ### ✅ FIXED 2026-09-30 — `apply_matrix_preset {engine:"vavra"}` now retargets (Phase 5)
>
> The defect below was real and is **now fixed**. The retarget exists in exactly **one** place —
> `src/common/WaldorfEditBuffer.h` (`retargetWaldorfDumpForSingleEditBuffer`) — and **three** routes
> call it: the `apply_preset` file loader (which always did) **plus both `apply_matrix_preset` sheet
> dump routes**, the preset `device_dump` and the **morph-step** `sysex` route (the second one was
> easy to miss, and it is the route all 20 `vavra_morphs.json` steps take).
>
> Verified: build rc 0; **6/6 new tests pass**, including `MicroQEditBufferRetargetIsOneImplementation`
> (asserts the file-loader and matrix routes emit *identical bytes* — the test that would have caught
> the original divergence) and `MicroQRetargetLeavesXeniaDumpsByteIdentical`. Measured bytes for a
> 392-byte microQ dump: `… 01 `**`30 40`**` 07 … 05 `**`2B`**` F7` → `… 01 `**`20 00`**` 07 … 05 `**`5B`**` F7`
> — only offsets **5, 6 and 390** change (checksum delta `0x30`), asserted at all three real call
> sites against a recording fake plugin, with an in-test **negative control** proving a route that
> skipped the helper would fail the assertion.
>
> So in the consequences list below, **item 3 is resolved** and **item 2 still stands**: a *direct*
> `send_fx_midi` with a raw `0x30` corpus dump or sheet array is still unretargeted, because
> `ProjectCommands::sendFxMidi` deliberately carries no Waldorf knowledge — the retarget belongs to
> the callers. If you hand-inject a dump yourself, stamp `d[5]=0x20`, `d[6]=0x00` and recompute
> `sum(d[4 .. size-3]) & 0x7F` first.
>
> Known residual (deliberately left, for a decision): the matrix dump route validates only the
> `F0`/`F7` framing and size, **not** the Waldorf `3E`/machine header, so a mislabeled 392-byte
> non-Waldorf dump inside a `vavra` sheet would also be rewritten. Only `engine:"vavra"` can reach
> it (every other engine id maps to mw2 ⇒ no-op), so the blast radius is that one sheet. Adding a
> `d[1]==0x3E && d[2]==machine` check to the helper would close it with **zero** change for valid
> dumps.

There is **no Waldorf retarget inside `ProjectCommands::sendFxMidi` itself** (by design — the
retarget lives in the callers, one shared implementation in `WaldorfEditBuffer.h`).

**Consequences:**

1. `apply_preset {filePath: "<microQ .syx>"}` → retargeted to `0x20/0x00`, checksum fixed,
   **verified audible** (`va-suite-status-log.md:58`: live injected rms 0.00945 → 0.01512,
   Δ0.0057; rebuilt-from-tree replay Δ0.0054). ✅
2. `send_fx_midi` with a raw corpus dump or a `vavra.json`/`vavra_morphs.json` `sysex` array →
   **injected at `0x30`** — the multi-mode edit buffer, i.e. the exact framing the project
   already documented as the "the current sound does not change" case. ⚠️
3. `apply_matrix_preset {engine:"vavra"}` → ~~same as (2)~~ **FIXED 2026-09-30**: it now retargets
   each sheet dump to `0x20/0x00` with a recomputed checksum before `sendFxMidi` (see the FIXED note
   above). The shipped Vavra sheet is still framed `0x30` on disk; the engine no longer relies on it.
4. `apply_matrix_preset {engine:"xenia"}` → the sheet is stamped `0x20/0x00`
   (`xenia_matrix_sysex.py:66-73` explicitly re-frames; the code even carries the measured
   comment "render delta 0.00017" for the un-reframed case). ✅

This is **not** a contradiction of any doc: the docs say the *loader* retargets, and it does — on
the `apply_preset` route. The matrix sheet was simply never given the same treatment for Vavra.
Note the asymmetry in the tooling itself: `xenia_matrix_sysex.py` re-frames to `0x20`; the
mirror-image `vavra_matrix_sysex.py` does not (its `vd.build_dump` preserves the parent's
bytes and only the `apply_preset` path fixes them afterwards).

**Two ways to work around it, both read-only:**

**(a) Route the underlying `.syx` through `apply_preset`** (recommended — it is the verified path):

```
apply_preset {trackId, slotIndex, filePath: "…\\Bass\\Acid bass        Bass.syx"}
```

**(b) Re-frame the dump yourself before `send_fx_midi`** — the 392-byte microQ single dump:
`byte 5 = 0x20`, `byte 6 = 0x00`, `byte 390 = (sum(dump[4:390]) & 0x7F)`:

```python
import sys; sys.path.insert(0, "timbre-lib")
import vavra_dump as vd
raw = open(r"D:\pdf\rhythm-lab.com_waldorf_micro_q\Bass\Acid bass        Bass.syx", "rb").read()
d = bytearray(raw)                       # 392 bytes
d[5]  = 0x20                             # SingleEditBufferSingleMode  (mqLib/mqmidityes.h:39)
d[6]  = 0x00                             # EditBufferCurrentSingle
d[390] = sum(d[4:390]) & 0x7F            # Waldorf checksum, low 7 bits of [4 .. size-2)
open(".tmp_waldorf/psy_acid_reframed.syx", "wb").write(bytes(d))
```

For a `vavra.json` preset the same operation applies to `preset["sysex"]` (a list of 392 ints):

```python
import json
sheet = json.load(open("timbre-lib/matrix_presets/vavra.json", encoding="utf-8"))
pr = next(p for p in sheet["presets"] if p["id"] == "3a77cc0171961084")
s = list(pr["sysex"]); s[5] = 0x20; s[6] = 0x00
s[390] = sum(s[4:390]) & 0x7F
# → send_fx_midi {messages:[{kind:"sysex", bytes: s}]}
```

The XT's 265-byte dump needs **no** fix-up if it came from `xenia.json` /
`xenia_morphs_injectable.json` (already `0x20`). It needs it if you built it from a *bank
image*: `xenia_dump.build_single_dump(..., bank=0x20, program=0x00)` does it for you, and
`xenia_matrix_sysex.py` shows the manual form (`dump[IDX_BANK] = 0x20`, `dump[IDX_PROGRAM] = 0`,
`dump[IDX_CHECKSUM] = compute_checksum(dump)`).

**One more XT-specific framing fact:** `apply_preset` on a **bank image** (`.µsb`) is not a
single dump — a `.µsb` is 256 × 256-byte records. `microwave_survey.json` records
`containers: {"sysex-single-dump": 255, "usb-record": 1536}`. Use `apply_matrix_preset`, a
per-patch `.syx`, or a synthesized edit-buffer dump for the XT; do not point `apply_preset` at a
bank image and expect the sounding patch to change.

---

## 10. Applying anything via MCP — exact calls and constraints

Every call below is a real registered tool (verified by reading the registrations:
`src/mcp/McpTools_Matrix.cpp`, `McpTools_FxSlot.cpp`, `McpTools_AudioRead.cpp`,
`McpTools_CompositionInstrument.cpp`). `trackId`/`trackID` are interchangeable aliases
(`stableRefRuleText`).

### Front doors

```jsonc
// 1. Whole patch / bank / dump file. Dispatches by slot fxType+pluginId AND file header.
//    Vavra + F0 3E 10 .syx  → live MIDI SysEx import + plugin-state capture
//                             (and the microQ 0x20/0x00 retarget + checksum fix)
//    Xenia + F0 3E 0E .syx  → same route
apply_preset {trackId, slotIndex, filePath, captureToTree:true}
// → then ALWAYS poll, because the capture completes after the call returns:
get_fx_capture_status {trackId, slotIndex}     // status: pending | ok | failed:<reason>

// 2. Harvested matrix preset or morph step. The mechanical front door.
list_matrix_presets {engine}                   // engine: vavra | xenia | virus | je8086 | nodalred2x
//   → {presets:[{id,name,role,appliesVia,evidence}], morphs:[{pair,distance,steps,apply}]}
apply_matrix_preset {engine, id, trackId, slotIndex, captureToTree:true}
//   → parameter-level ids: {applied,skipped,unmapped} + deferred capture info
//   → SysEx-carrying ids:   {queued, captureDeferred:true}   (route through send_fx_midi)
//   ✅ Vavra: the shipped sheet's sysex is framed 0x30, but as of 2026-09-30 the engine retargets
//      every sheet dump to 0x20/0x00 before sendFxMidi (WaldorfEditBuffer.h) — see §9.

// 3. Raw injection (this is where you control the framing yourself).
send_fx_midi {trackId, slotIndex, captureToTree:true, messages:[
  {kind:"sysEx",         bytes:[240,62,16,0,16,32,0, /*…*/,247]},
  {kind:"programChange", program:0},
  {kind:"controlChange", controller:0, value:0},
  {kind:"noteOn",        pitch:60, velocity:100}]}
//   → "queued=N capturedToTree=1"; then get_fx_capture_status
```

### Host parameters (durable, reaches renders)

```jsonc
list_fx_params {trackId, slotIndex}
//   plugin params report hasRange/minVal/maxVal/defaultVal/plainValue/stepped
//   + minText/maxText/defaultText in real units. hasRange:false ⇒ blind normalised 0..1.
//   Names are per part: "Ch 1 F1Cutoff", "Ch 2 AmpVolume", …  (jucePluginLib/controller.cpp:50)
//   An EMPTY list right after add_fx/load means the isolated child is still booting (~12 s) — retry.
//   Params you wrote are flagged "overridden".

set_fx_param {trackId, slotIndex, paramName:"Ch 1 F1Cutoff", value:0.394}   // 0..1 NORMALISED
//   or paramIndex:N. paramName wins when both are given; case-insensitive.
//   write is live AND persisted into the slot's appliedParamOverrides ledger, which
//   ExportManager replays into every fresh export child → it reaches export_audio /
//   audition_plugin / verify_part and survives save/load.

clear_fx_param_overrides {trackId, slotIndex}   // drop the ledger
```

**Durability, per engine (cited, not re-derived):**

| Engine | Evidence | Measured |
|---|---|---|
| Vavra | `FxMidiInjection.VavraHostParamPersistedWriteAffectsExport` | base **0.0174** rms; `Ch N AmpVolume` → 0 = **0.0026**, → 1 = **0.0077**, monotonic |
| Xenia | `FxMidiInjection.XeniaHostParamsChangeRender` | `F1Cutoff` 1.0 → 0.1 via the OS, same-child render Δ > 1e-5 |
| both | `hardware-va-suite.md` §1/§3 | 7557 (Vavra) / 2151 (Xenia) live host params; the old "LIVE writes do not move the render" reading was a harness artifact — every audit render is a tree copy into a fresh child, so a live-only write was never an input (lesson 27) |

### Read-back / verification

```jsonc
// part-level: is it audible, does it clip, which bands are present
verify_part {trackIndex, startBeat, endBeat, windowSeconds, soloOnly}
//   → ok=… soloRms=… soloPeak=… mixRms=… mixPeak=… nonClipping=… audible=… bandsPresent=…
//   audible = solo peak > −80 dBFS; nonClipping = mix peak < 1.0;
//   soloOnly:true halves cost but sets mixMeasured=false — never read the zeroed mix fields.

// mix-level: band energies, pump, kick prominence, section stats, drop/build gate
mix_report {filePath, bpm, sections:[{name,start,end}], fromPlan, targets, dropBuildRatio}
//   → overall peak/RMS; 4-band energy (sub 40-110, bass 90-300, body 300-2000, high >6000);
//     per-section RMS/peak/boundaryPeak/bands; pumpDepth = (max-min)/mean of per-beat RMS
//     (sections with >= 8 beats); kickProminence = E(35-110)/(E(35-110)+E(120-320)) in 0..1;
//     with fromPlan: loudnessGates = each drop's RMS vs the build before it (>= dropBuildRatio,
//     default 0.9); with targets: targetChecks + targetsOk.
//   Section times are SECONDS, windows are [start,end).
//   Optional wait:false → {jobId, state:"running", pollWith:"poll_job"}

// one call, both
render_and_verify {…}    // render + a mix_verdict-identical verdict
```

**Variance floors — set your thresholds from these, not from intuition:**

- Vavra edit-buffer dump changes the render by **Δ0.0057 rms** (0.0094 → 0.0157); the
  rebuilt-from-tree replay by **Δ0.0054**.
- Xenia edit-buffer: boot→dumpA **Δ0.0058**; dumpA→dumpB **Δ0.0057**; fresh child rebuilt from
  the tree **Δ0.0067** vs factory; offline export differs **Δ0.0013**.
- Vavra host-param write separation: **0.0026 / 0.0077** (≈ 0.005 separation).
- **Xenia same-input spread: 0.0056** — larger than a 0.0023 separation, which is why
  `2026-09-21-plugin-param-persistence.md` explicitly refused to assert a threshold there.
  **Do not gate Xenia on rms; gate on a band ratio.**
- Two consecutive no-write Vavra renders agreed to **4e-07** — so a *clean* control is cheap.

### The complete flow (what I would actually run)

```jsonc
// (1) put a Vavra on a track and load a real patch — the only route with a verified gate
add_fx {trackId, pluginId:"Vavra.clap", index:0}          // BARE name resolves against the scan DB
apply_preset {trackId, slotIndex:0, filePath:"…\\Bass\\Acid bass        Bass.syx"}
get_fx_capture_status {trackId, slotIndex:0}              // → status "ok"

// (2) shape it with host params (per part!)
list_fx_params {trackId, slotIndex:0}
set_fx_param {trackId, slotIndex:0, paramName:"Ch 1 F1Resonance", value:0.87}
set_fx_param {trackId, slotIndex:0, paramName:"Ch 1 F1EnvMod",    value:0.89}

// (3) a device-native movement config on top (⚠️ Vavra: re-frame it — §9)
apply_matrix_preset {engine:"xenia", id:"0:4", trackId:0, slotIndex:1}   // XT side: fine as shipped

// (4) prove it
verify_part {trackIndex:0, startBeat:64, endBeat:80}
render_and_verify {…}
mix_report {filePath:"<render.wav>", bpm:145, fromPlan:true}
```

---

## 11. What the corpus could not answer

Honest limits, in descending order of importance to the decisions above:

1. **The corpora are not psytrance libraries.** The microQ pack is Chris Jones's 1999-era
   general-purpose sound set; the XT banks are Waldorf factory + user banks. There is **no genre
   label anywhere in either corpus**. So the corpora establish (a) *which* parameters actually
   separate roles, (b) the central tendency to anchor from, and (c) named example patches — they
   do **not** establish psytrance-appropriate extremes. Every "recommended" value outside the
   corpus's p10–p90 is explicitly marked as extrapolation (most visibly `F1EnvMod` on the acid
   stab: corpus median +6, recommended +48..+56). **Treat the corpus as a prior, not a target.**

2. **The XT's role labels are name-guessed.** `microwave_patch.py::_role_for` matches substrings
   in the patch name (`bass|sub|acid`, `lead|solo|saw|sync|hoover`, `pad|string|atmo|choir|warm|wash`,
   `pluck|stab|short|bell|ping|arp`, `fx|noise|sweep|riser|formant|scary|alien`,
   `piano|organ|clav|e-piano|rhodes`), first match wins, **4428 of 5870 (75%) fall through to
   `other`**. The `.µsb` records carry no category field at all, and there is no category
   analogue to the microQ's `category@386` — so the XT distributions are *weaker evidence than
   the microQ's*, despite being 11× larger. The n=33 acid subset in particular could easily be
   33 patches that all happen to be Waldorf's own takes on "acid" in 1998.

3. **Nothing here was rendered.** No render-through-plugin confirmation exists for any specific
   recipe in this document. The `vavra.json` and `xenia.json` sheets both carry
   `"unverified": true`; and `xenia-offset-map.md` says of the morph chains explicitly:
   *"'unverified' stays true: the dumps are byte-verified, but no render-through-the-plugin
   confirmation exists yet."* What **is** verified is the *route* (Vavra edit-buffer retarget,
   §9; Xenia bank-0x20 dumps; both engines' host-param persistence) — so a recipe is
   "route-verified, value-unverified".

4. **The Vavra morph provenance is weak and asymmetric.** `vavra_morphs.json`'s
   `baseResolution` shows `how: "prefix"`, `byteMatches: 1`, `nameMatches: 1` for all five
   pairs, and its step presets carry `appliesVia: "patch_or_sysex_unverified"`. Compare
   `xenia_morphs_injectable.json`: `how: "verified"`, `byteMatches: 84` for all five pairs.
   **Parent resolution for the microQ morphs rests on a name prefix and one matching byte** —
   do not treat a Vavra morph step as a byte-verified patch. (And, per §9, its framing is
   `0x30`, so as shipped the sheet itself did not change the sounding patch — now moot, because the engine retargets every sheet dump as of 2026-09-30 (§9).)

5. **Three of the five Vavra morph pairs are bass-based, and none is pad-based.**
   `baseResolution.file` is `Prowler 2 CJ Bass.syx`, `DeathStepDnB2 CJ Bass.syx`,
   `Ana Bass CJ Bass.syx` (×2), `Squelch Bass  CJ Bass.syx`. There is **no pad/atmos, lead or
   acid morph chain on the microQ side**; the pad recipe above points at XT chains instead.

6. **The XT's wavetable *content* is not in the corpus.** `Wave` 0..63 names the factory tables
   and 96..127 are `User 1..32`, but the banks here contain **no `WaveDump` wavetable payloads
   that I decoded** — `microwave_survey.json` records `WaveDump: 250` and `WaveCtlDump: 32`
   messages *inside* `boele_gerkes.mid`, and `WaveDump`/`MultiDump` are "counted but not indexed
   as patches". So I can tell you *which* tables the corpus selects, but **not what any of them
   sound like, and not whether the `User 1..32` references point at tables that exist on your
   install.** If a recipe selects `User 11`, verify it audibly.

7. **The microQ acid sample is 9 patches.** Every acid-stab number above rests on n=9, of which
   3 are `FX`-category and 4 are `Arp`-category — i.e. only one (`Acid bass`) is a *bass* patch.
   The acid recipe is the least supported of the five.

8. **`*_offset_map.json` covers only 86/85 of the 311/220 vocabulary names.** I extended the
   mapping to the full page-0 index space using the documented `dump_byte = 7 + index` rule and
   verified it against every shipped offset-map entry (0 mismatches for microQ, 1 documented
   alias for XT). That is a consistency proof, **not** an independent verification of the
   un-mapped names — parameters I cite outside the offset maps (`AmpEnvDecay`, `FilterEnvSustain`,
   `WaveEnvTime1..8`, `ArpClock`, …) are verified by vocabulary + rule, not by the corpus
   byte-check.

9. **Both FX-parameter vocabularies are aliased and I did not disambiguate them.** Bytes
   137–150 (FX1) and 153–166 (FX2) on the microQ, and the aliased XT `EffectParamA/B/C`
   bytes, hold one byte whose meaning depends on the selected FX type; the corpus contains values
   above the declared maxima at exactly those bytes (microQ up to 127 where the JSON declares
   ≤ 24, XT `EffectType` up to 127 where the JSON declares 0..34, `ChorusEnabled` up to 112 where
   it declares 0..1). **I report the *type* distributions (`FX1Type`/`FX2Type`/`EffectType`) but
   deliberately do not report per-FX-parameter distributions** — they would be averaged across
   incompatible meanings. To use the FX sub-parameters, read `FX1Type`/`FX2Type` first, then
   interpret the byte.

10. **I did not open an audio device, instantiate a slot, or call `list_fx_params`.** The
    parent agent reported the `18765` engine as deviceless at the time of writing; `whoami`
    succeeded and reported `trackCount 13`, `version 0.39.2`, `output: ""`, and I left it
    untouched (another agent owns the live composition session on `18766`). So the **host
    parameter counts (7557 / 2151) and the `Ch <n> <Name>` naming are cited from
    `hardware-va-suite.md` §1 and `jucePluginLib/controller.cpp:50`, not measured live.** If you
    want them confirmed, open a device, add a scratch `Vavra.clap`/`Xenia.clap`, and read
    `list_fx_params` — and if you do, **also confirm the exact `Ch <n> ` prefix spelling**,
    because every recipe above depends on it and I could not verify it against a live instance.

11. **The microQ sidecar `dsp` blocks and `roleCheck` are unusable** (§2). If a future session
    wants per-patch measured features for this corpus, they have to be regenerated — the ones
    on disk are 528 copies of one probe render.

12. **The microQ arp's per-step field → byte mapping is unresolved** (§7). The 93 arp names
    collapse onto 45 linear indices, with five per-step fields (Step/Glide/Accent/Length/Timing)
    expressed as aliases over **two** indices per step. I can give you the exact *address*
    (`dump_byte = 7 + index`, bytes 318–365, all inside the single dump) and the exact *value
    lists*, and I verified the global arp parameters' behaviour from 147 corpus patches — but
    **which byte carries Step vs Glide vs Accent at a given moment I could not determine** without
    the plugin's parameter-send path. The FX block has the same alias shape with a known cause
    (the selected FX type); the arp block has no such documented cause in the files I read. Treat
    any per-step arp value in a recipe as needing audible confirmation.

13. **I corrected one of my own errors in the course of this work, and it is worth recording
    because it is the kind of error this task invites.** My first draft of §8 asserted that the
    microQ's arp parameters live on the multi/global pages (`page 100+`). That was an inference
    from the *layout* of the vocabulary (373 entries do carry `page 100+`) applied to a name
    family I had not actually looked up. When I checked the 93 `Arp*` entries, they are **all
    `page 0`**, indices 311–358, inside the single dump. The corrected version is in §7 and §8.
    Similarly, an ad-hoc analysis script of mine indexed the XT `params` dict with **dump byte
    offsets** (69 for `F1Cutoff`) where the dict is keyed by **param index** (62) — a silent +7
    shift that produced a plausible-looking but entirely wrong table (`F1Cutoff` "constant at
    64"). Both are the same failure mode: crossing a coordinate space without checking. The
    committed scripts now resolve every index through the vocabulary
    (`.tmp_waldorf/xt_focus.py` asserts `NAME2IDX["F1Cutoff"] == 62` before computing anything).

---

## Appendix A — measured distribution tables (full)

The complete per-role numeric + categorical aggregates, including every parameter I computed
(not only the ones quoted above), are in **`.tmp_waldorf/corpus_report.json`** (deletable
scratch). Headline numbers reproduced above:

- microQ: roles `bass_rolling` n=79, `bass_acid` n=9, `lead` n=81, `pad_atmo` n=122,
  `arp` n=147, `fx` n=27, `all` n=528.
- XT: roles `bass` n=407, `bass_acid` n=33, `lead` n=241, `pad` n=332, `pluck` n=238,
  `fx` n=119, `wavetable_texture` n=5260 (`W1EnvAmount≥32 ∧ MixW1>0`), `arp` n=474
  (`ArpMode≠0`), `all` n=5870.

## Appendix B — reproduction

Read-only; writes only `.tmp_waldorf/`. All scripts live in `.tmp_waldorf/` and are deletable.

| Script | What it does |
|---|---|
| `.tmp_waldorf/vocab.py` | tolerant JSON5-ish reader for `parameterDescriptions_*.json`; `linear()` = first-occurrence-per-linear-index page-0 entries |
| `.tmp_waldorf/dump_tables.py` | emits `{mq,xt}_params.tsv` (linear index → dump byte → name/min/max/toText/isDiscrete/isPublic) and the `valuelists` dumps |
| `.tmp_waldorf/aggregate.py` | the main pass: cross-checks the full vocabulary against the shipped offset maps, then aggregates 528 microQ sidecars + 5870 XT bank patches per role → `corpus_report.json` |
| `.tmp_waldorf/summary.py` | human-readable per-role tables from `corpus_report.json` |
| `.tmp_waldorf/xt_focus.py` | the XT per-role medians (uses the **vocabulary index**, not the dump byte — an earlier ad-hoc version got this wrong by +7) |
| `.tmp_waldorf/movement.py` | dumps the 40+40 matrix presets and the 5+5 morph pairs (ids, names, roles, `appliesVia`, `sysexOverrides`, `baseResolution`) |
| `.tmp_waldorf/examples.py` | resolves example patch names to corpus file paths |

Verify the vocabulary rule yourself:

```powershell
python .tmp_waldorf/aggregate.py
# vocab mq names=311 xt names=220 | offset-map mismatches mq=0 xt=1
```

The one XT mismatch is the documented `EffectParamA`/`DelayTime` alias at offset 88
(`xenia-offset-map.md`: "the specific name wins").
