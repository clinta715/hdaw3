# Virus (OsTIrus / Osirus) host-parameter map — verified 2026-09-30

**Why this file exists.** The psy-bass/acid/lead hypotheses in
[`psy-va-technique-notes.md`](psy-va-technique-notes.md) §2 name parameters after the **emulator
vocabulary and the hardware manual** (`Filter1 Env Amt`, `Punch Intensity`, `Amp Env Release`).
Those names are *not* all the names the **host** exposes, and a `set_fx_param` write only resolves
against the host list. This file is the verified translation, so a recipe can be written as calls
that actually land.

**Source of truth:** a live `list_fx_params {trackId, slotIndex:0}` read on an instantiated
`OsTIrus.clap` slot — **6939 params**, of which `Ch 1` has 444 and `Ch 2…16` have 433 each.
Measured, not inferred (see the count table below).

## Rule 1 — every addressable name is PART-scoped

The host name is `Ch <n> <control>`. A bare `Filter 1 Cutoff` **does not resolve**; use
`Ch 1 Filter 1 Cutoff`. 16 parts are exposed (one per Virus channel), so a recipe is really a
*per-part* recipe: state which part it targets (default: `Ch 1`).

## Rule 2 — the host name often differs from the manual/vocabulary name

These are the mismatches that would silently break a recipe. Each was measured against the live list.

| Concept (manual / vocabulary name) | **Actual host name** | Notes |
|---|---|---|
| `Cutoff` / `Filter1 Cutoff` | `Ch 1 Filter 1 Cutoff` | ✅ 6 cutoff-ish names; `Filter 2 Cutoff` also exists |
| `Filter1 Resonance` | `Ch 1 Filter 1 Resonance` | ✅ |
| **`Filter1 Env Amt`** | **`Ch 1 Filter 1 Envelope Amount`** | ⚠️ *not* "Env Amt". Polarity is separate: `Ch 1 Filter 1 Envelope Polarity` |
| `Suboscillator Volume` | `Ch 1 Sub Oscillator Volume` | ⚠️ two words: **"Sub Oscillator"** |
| `Sub Shape` | `Ch 1 Sub Oscillator Waveform Shape` | ⚠️ |
| **`Punch Intensity`** | **`Ch 1 Oscillator Punch Intensity`** | ⚠️ prefixed "Oscillator" |
| `Filter1 Keyfollow` | `Ch 1 Filter 1 Keyfollow` | ✅ (also per-oscillator: `Ch 1 Oscillator 1 Keyfollow`) |
| **`Amp Env Attack/Decay/Sustain/Release`** | **`Ch 1 Amplifier Envelope/Attack`** · `/Decay` · `/Sustain` · `/Release` · `/Sustain Slope` | ⚠️ **"Amplifier"**, not "Amp" — a `\bAmp\b` search finds nothing and falsely suggests it is unexposed |
| `Filter Env Attack/Decay/Sustain/Release` | `Ch 1 Filter Envelope Attack` · `Ch 1 Filter Envelope/Decay` · `/Sustain` · `/Release` · `/Sustain Slope` | ⚠️ note the **inconsistent separator**: *Attack* is space-separated, the rest use `/` |
| `Osc Volume` / `Osc Volume / Saturation` | **`Ch 1 Oscillator Section Volume`** (candidate) | ⚠️ **unconfirmed** — the only saturation-*type* name is `Ch 1 Voice Saturation Type`; no name contains "Saturation" as an amount. Treat the amount mapping as OPEN and verify before relying on it |
| `Distortion Intensity` | `Ch 1 Distortion Intensity` | ✅ also `Ch 1 Distortion Type`, `Ch 1 Distortion Treble Booster` |
| `Filter1 Mode` | `Ch 1 Filter 1 Mode` | ✅ |
| `Filter Balance` | `Ch 1 Filter Balance` | ✅ |
| `Unison Mode/Detune/Pan Spread` | `Ch 1 Unison Mode` · `Ch 1 Unison Detune` · **`Ch 1 Unison Panorama Spread`** | ⚠️ "Panorama", not "Pan" |
| `Osc1 Model / Wave Select / Shape / Pulsewidth / Density / Wavetable` | `Ch 1 Oscillator 1 Model` · `… Wave Select` · `… Waveform Shape` · `… Pulsewidth` · `… Density` · `… Wavetable` / `… Wavetable Index` | ✅ per-oscillator |

## Blocks a psy recipe needs (all confirmed present, `Ch 1` scope, 444 params)

- **Oscillators 1–3**: `Model`, `Wave Select`, `Waveform Shape`, `Pulsewidth`, `Density`,
  `Detune In Semitones`, `Fine Detune`, `Local Detune`, `Keyfollow`, `Sync`, `Wavetable`,
  `Wavetable Index`, `Interpolation`, `Formant Shift`, `Formant Spread`, `FM Amount` (osc 2).
- **Filters**: `Filter 1/2 Cutoff`, `… Resonance`, `… Envelope Amount`, `… Envelope Polarity`,
  `Filter 1 Mode`, `Filter Balance`, `Filter Bank Frequency`.
- **Envelopes**: `Amplifier Envelope/{Attack,Decay,Sustain,Release,Sustain Slope}`,
  `Filter Envelope{ Attack,/Decay,/Sustain,/Release,/Sustain Slope}`,
  `Filter Envelope > Pitch / > FM / > X-Sync`, plus **free `Envelope 3/…` and `Envelope 4/…`** —
  the latter are the natural carriers for section-scale **movement**.
- **LFOs**: `LFO 1/2/3 Rate`, `User Destination`, `User Destination Amount`, `Keyfollow`,
  `Envelope Mode`, and routing like `LFO 1 > Filter Resonance 1+2`, `LFO 2 > Cutoff 1`.
- **Mod matrix (addressable by name — cite these in movement recipes)**:
  `Ch 1 Mod Matrix Slot #/Source`, `… /Dest #`, `… /Amount #` (288 Dest + 256 Amount + 96 Source names).
- **Sequencer**: 512 × each of `Ch # Step # Length`, `… Velocity`, `… Bitfield` — a 512-slot step
  engine, the candidate route for trance-gate / psy-arp patterns rather than an LFO.
- **Voicing/FX**: `Unison *`, `Voice Saturation Type`, `Distortion *`, `Chorus *`, `Vocoder *`,
  `Input Follower *`, `Patch/Part/Channel Volume`, `Velocity > Volume`.

## What this changes for the hypotheses

- **Hypothesis 8 (amp env: short release, zero sustain) IS actionable** — the name is
  `Ch 1 Amplifier Envelope/Release` / `…/Sustain`. *(This file previously looked like evidence it was
  unexposed; that was a search artefact, corrected here.)*
- **Hypothesis 10 (saturation amount ≤ 0)** is **BLOCKED pending name confirmation**: the vocabulary's
  `Osc Volume` has no literal host match. `Ch 1 Oscillator Section Volume` is the only plausible
  candidate; if that is something else, the hypothesis cannot be tested through host writes and must
  go through ROM-program selection instead.
- **Punch (hypothesis 6) and keytrack (hypothesis 7) are actionable** under their real names.

## Reproduce

```powershell
$env:HDAW_MCP_URL = 'http://127.0.0.1:18766/mcp'   # or 18765, with a device open
python scripts/hdaw_mcp_http.py call list_fx_params '{"trackId":1,"slotIndex":0}' --timeout 300 --full
```
Then normalise digits to `#` to reveal block structure, and scope by the `Ch <n> ` prefix.
