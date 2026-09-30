# Psy VA technique notes — the *why* behind the corpus (full-on / twilight / forest / dark)

Research synthesis for the patch-corpus agents. Two kinds of statement appear below and are
labelled as such:

- **[external]** — a claim from a web source, cited with a link. Confidence is graded in §4.
- **[local]** — a fact measured on this box, taken from
  [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md),
  [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md),
  [`docs/va-suite-status-log.md`](docs/va-suite-status-log.md),
  [`docs/psytrance-composition-guide.md`](docs/psytrance-composition-guide.md), or the
  emulators' own parameter vocabularies at `D:\pdf\gearmulator-git\source\`.

**Device parameter names below are quoted from those vocabularies, never invented:**
`osTIrusJucePlugin/parameterDescriptions_TI.json` (TI), `osirusJucePlugin/parameterDescriptions_C.json` (C),
`mqJucePlugin/parameterDescriptions_mq.json` (microQ), `xtJucePlugin/parameterDescriptions_xt.json` (XT).
Defaults quoted next to a name come from the same files and are the most useful thing in this
document: **the factory default is the null hypothesis a corpus distribution has to beat.**

Where a web source disagrees with a local measurement, the local measurement wins and the
disagreement is flagged (§3, §5).

## 0. Where the genre sits (so the recipes have a target)

Tempo and character, with the disagreements left visible:

| Branch | Tempo | Character | Source |
|---|---|---|---|
| Full-on | 144–150 | bright leads, minimal breakdown + rising filter sweep, bassline lands hard | [TIMBR](https://timbr.music/blog/psytrance-subgenres) |
| Full-on | 140–148 | "energetic, melodic, peak-time" | [Plugg Supply](https://plugg-supply.net/articles/psytrance-production) |
| Twilight | 144–156 (≈150) | "a kind of Dark Full-On… more aggressive bassline" | [Vibes DJ](https://vibesdj.io/dj-tools/what-bpm-is-twilight-psytrance) |
| Progressive | 138–144 | rolling bass very present, long arcs | [TIMBR](https://timbr.music/blog/psytrance-subgenres) |
| Progressive | 130–138 | slower, more atmospheric | [Plugg Supply](https://plugg-supply.net/articles/psytrance-production) |
| Dark | 148–160 | "detuned basslines", industrial texture | [TIMBR](https://timbr.music/blog/psytrance-subgenres) |
| Dark | 145–155 | aggressive, industrial | [Plugg Supply](https://plugg-supply.net/articles/psytrance-production) |
| Forest | 144–148 | "kick is often softer in the mix, subordinate to texture"; organic, less prominent bassline | [TIMBR](https://timbr.music/blog/psytrance-subgenres), [r/psytrance](https://www.reddit.com/r/psytrance/comments/9wipob/forest_psy_vs_dark_psy/) |

The two genre guides disagree by 4–8 BPM in every branch — treat tempo as a soft gate, not a
test. What matters for the corpus is the **texture claim**: dark psy pushes the *bassline forward*
and mechanical; forest pushes the *textures forward* and the bass back.

---

## 1. Per-role technique

### 1.1 The rolling bass

**[external] Construction.** The canonical line is 16th notes, one low root with rhythmic
variation, and the roll comes from envelope + *between-note* filter movement, not from the
waveform: "The problem is rarely the waveform — it is the envelope behaviour and the filter
movement between notes, which is what gives the bass its bounce and keeps it from smearing into
the kick." ([G-Sonique](https://www.g-sonique.com/psytrance-vst-plugins))

**[external] Two layers, different jobs.** "Sub is the loud one — think of it as the anchor. Top
layer sits noticeably behind in level… Everything below ~200 Hz stays mono. Any stereo width goes
on the top layer, high-passed above 200 Hz." ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline))

**[external] Amp envelope.** "Zero out the release entirely. The gaps between notes are part of the
sound. Amp envelope: attack at zero, decay in the 80–120 ms range, sustain at zero."
([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)) Myloops gives a shorter figure
for the top layer: "50–70 ms decay works" ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline)).

**[external] Filter envelope is the signature.** "Then the move that separates a psy bass from every
other kind: a fast filter envelope on the top layer's cutoff. Short attack, ~40 ms decay, sustain 0,
modulation depth enough that each note briefly opens the filter by roughly an octave and slams it
shut again. That per-note 'chirp' is the sound. Too much depth and you're making acid; too little
and the bass feels dead." ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline))
Monosounds frames the same move smaller: "route a filter envelope to cutoff: small amount, fast
decay, roughly matching the amp envelope" ([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)).

**E-Clip's published Serum recipe** (a working psytrance producer, not a content farm): Amp Env
"Attack to 0, Decay 1/16th note, Sustain −3db, Release 15ms"; filter "18db or 24db"; Env2 →
cutoff with "Attack to 0, decay to 1/16 note… Sustain 60%, Release 10ms"; Osc A phase hard-left,
random 0, a hand-drawn one-shot saw, and **Osc A Volume at zero with Velocity → OSC A Volume** so
each note's level comes from velocity
([E-Clip](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip)).

**[external] Cutoff range — the sources disagree.** Myloops: "cutoff parked somewhere between
400 Hz and 1 kHz to start, resonance modest." Monosounds: "drag the cutoff way down, 200–400 Hz or
so… leaving a round, woody thump." Both agree the filter is *not* keytracked-and-open and that
resonance stays low: "A little gives bite; too much and the bass starts honking"
([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)).

**[local] The house rolling bass is the same shape.** HDAW's internal `growl_bass` ships as a
"dedicated offbeat rolling bass" with `Filter Cutoff` default **800 Hz**, `Filter Res` default 4,
`Filter Env Amt` default **0.7** (upper third of 0–1), ADSR at params 12–15, and a built-in
sidechain (params 24–25) — i.e. the local instrument's defaults already encode "cutoff in the
hundreds of Hz, filter env amount high"
([`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5c). The verified
production-stack chain adds EQ → compressor on the bass plus **LFO0: 2 cycles/beat sine → EQ cutoff,
depth 0.28** and **LFO1: 1/beat pump → Volume, depth 0.6, phase 180** (same file, §5).

**[local] The Virus as the genre instrument.** Both Virus emulations host the parameters this sound
needs, with telling defaults: `Cutoff` (= Filter 1 Cutoff) **default 127 (wide open)**,
`Filter1 Resonance` default 0, `Filter1 Env Amt` **default 0**, `Suboscillator Volume`
**default 0**, `Filter1 Keyfollow` default 64 (bipolar centre = no tracking),
`Distortion Intensity` default 0 (`parameterDescriptions_TI.json`). So on a Virus a psy bass is by
definition a patch that has **moved cutoff, filter-env amount and the sub away from their
defaults**. The manual's architecture: the sub is "An extra 'slave' oscillator tuned an octave
below its master oscillator… If oscillator 1 is in Classic mode, the sub-oscillator is a Square or
Triangle wave slaved to oscillator 1", and its `Volume` "crossfades between HyperSaw and HyperSub"
in HyperSaw mode; `Shape` is "Square, Triangle… Not available if oscillator 1 is in HyperSaw or
WaveTable mode" ([Virus TI2 manual](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html) p.35).
Saturation is a **pre-filter** stage: it acts on "all oscillators and input signals (but not Noise
or Ring Modulator) immediately before entering the filters", and "**the value 0 is unity gain i.e.
already maximum volume — positive values control Saturation intensity only**" (same manual, p.34).
`Punch Intensity` "Enhances the percussive effect of short Attack times in the amplifier envelope.
At higher values, Punch becomes a noticeable 'snap' at the start of each note" (p.34).

### 1.2 Acid stab / pluck

**[external] The routing that matters.** "Most people crank the filter and stop there, but the
pluck character comes from routing a mod envelope to the cutoff, not the amp envelope… Keep the
mod envelope fast so the filter snaps open at the start of each note and closes quickly. That snap
is the pluck… Vary how far the cutoff opens across notes… Accented notes open the filter further
and jump out, which gives the pattern its bounce and rhythm."
([Psytrance Blueprint — Goa 303 acid lead](https://www.psytrance-blueprint.com/tutorials/goa-303-acid-lead/))

**[external] One oscillator, high resonance.** "Acid is one saw wave through a resonant low-pass
filter. No layering needed. Crank the filter resonance so it sings, then drive the cutoff with an
envelope on every note. The per-note filter sweep is the whole acid character."
([Psytrance Blueprint — acid 303](https://www.psytrance-blueprint.com/tutorials/acid-303-psytrance-serum/))

**[external] Glide/detune is the vintage layer, kept small.** "Add a touch of glide so notes slide
into each other, and a small amount of detune to thicken the tone… Do not overdo it. A little glide
and detune reads as vintage, too much reads as broken" (Goa 303 page, above).

**[local] Where the microQ fits.** Its filter is the reputation, and the manual is explicit about
the parameter that creates it: `Resonance` "Controls the emphasis of the frequencies around the
cutoff point… At higher values of 80…113 the sound gets the typical filter character with a strong
boost around the cutoff frequency. When the setting is raised to values above 113, the filter
starts to self-oscillate, generating a pure sine wave. This feature can be used to create
analog-style effects and percussion-like electronic toms, kicks, zaps etc."
([microQ manual](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) p.74).
Verified host-side names for exactly this move (all `isPublic: true`):
`F1Type`, `F1Cutoff`, `F1Resonance`, `F1Drive`, `F1KeyTrack`, `F1EnvMod`, `F1VelMod`,
`F1ModSource`, `F1CutoffMod` (`parameterDescriptions_mq.json`). `F1EnvMod` is "Env (Filter
Envelope Amount) −64…+63" in the manual, i.e. **bipolar** — negative amounts invert the sweep.
`F1Drive` "Determines the amount of saturation that is added to the signal… Increasing the value
will bring in more and more distortion, suitable for harder lead sounds and effects" (microQ p.74).

**[local] microQ caveat that changes the recipe's shape.** The microQ exposes *filter* params to the
host but **collapses its FX sub-parameters** (they share indexes 146–155 and are resolved by
`Fx2Type`), so no amount of republishing creates a host parameter for them; the device-native route
is a 392-byte single dump applied via `apply_preset`/`apply_matrix_preset`
([`docs/va-suite-status-log.md`](docs/va-suite-status-log.md) §"Vavra + Xenia matrix presets").
Practical consequence: build the acid *character* inside the patch, and use host params
(`F1Cutoff`, `F1Resonance`, `F1EnvMod`) only for the movement you want to automate.

**[verdict on the "separate mod env" claim]** Both acid sources and the Virus architecture agree:
keep amp envelope and filter envelope independent (`Filter1 Env Amt` is a separate, invertible
parameter with `Filter1 Env Polarity`; `Filter Envelope` can also be used as a matrix source —
Virus TI2 manual pp.38–43, 65).

### 1.3 Lead

**[external] A psy lead is a stack, not a patch.** "A big psytrance lead is almost never one patch.
It is 4 to 5 layers, each thin on its own… main lead with the harmonic content, a lower octave for
body, a movement layer with a phaser or filter LFO, an acid layer for tension, and optionally a
quiet deep low layer… Check the stack on a correlometer so no layer cancels the mono signal."
([Psytrance Blueprint — lead sound design](https://www.psytrance-blueprint.com/tutorials/psytrance-lead-sound-design-serum/))

**[external] Automate the filter; don't LFO it.** "Acid character comes from a filter sweeping
steadily upward by automation, not an oscillating LFO… Remove the LFO from the filter and instead
automate the cutoff with a rising diagonal line so it opens steadily through the section."
(Same page.) This is the load-bearing claim of §1.5 — see the confidence note in §4.

**[external] Glide and note length.** "Use portamento for legato lines: shorter notes in the low
octave, longer in the high octave for natural flow… Keep low-octave notes short and high-octave
notes longer… think call and response." (Same page.) A second tutorial sets portamento by ear:
"pre-glide to full… in the Portamento section put the time up to about two thirds"
([Dance MIDI Samples](https://www.dancemidisamples.com/making-a-psytrance-lead/)).

**[external] Delay placement.** "Put delay on the cleanest high layer, not the noisy one. You hear
the width and movement better." (Blueprint lead page.)

**[local] Device names for these moves.** Virus: `Portamento` ("Determines how slowly the pitch of
notes glides from one to the next. The actual effect of portamento depends on the KeyMode") and the
UNISON section — "Voices • Off, Twin, 3 to 16… Detune • 0 to 127… Pan Spread • 0 to 127" (Virus TI2
manual pp.34, 68). Host names/defaults: `Unison Mode` default 0 (off), `Unison Detune` default 48,
`Unison Pan Spread` default **127 (already full width)**, `Filter1 Keyfollow` default 64
(`parameterDescriptions_TI.json`). microQ: `GlideEnable`, `GlideMode` (Portamento / Fingered
Portamento / Glissando / Fingered Gliss), `GlideRate`, plus `VoiceMode` and
`UnisonoCount`/`UnisonoDetune` (`parameterDescriptions_mq.json`; manual p.65 — note microQ spells
it "Unisono"). XT: `GlideEnabled`, `GlideType`, `GlideMode`, `GlideTime`
(`parameterDescriptions_xt.json`).

### 1.4 Pads / atmospheres / textures

**[external] Slow attack is the definition of the role, and the manual gives a recipe.** The
MicroWave II/XT cookbook pad patch: set both waves to saw, small positive/negative osc detune,
"Set Cutoff to approx. 080-090", chorus on, "Amplifier-Envelope: Attack = 050 Decay = 000 Sustain
= 127 Release = 050", "Activate Glide and set the Time-parameter to 018", and "We recommend you
use the Hipass or the Bandpass filters" — because "Thanks to its harmonics, the saw-tooth wave is
well suited for pad sounds, the filtering determines the sounds brilliance. Detune and Chorus
provide a floating and full characteristic. The Amp-Envelope controls the volume change over time
and is responsible for the slow attack and release."
([MicroWave II/XT manual](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html) p.15)

**[external] Why a wavetable pad is not a subtractive pad.** The XT manual's own explanation of the
wave envelope: "The Wave-envelope 'scans' the Wavetable with the selected amount, thereby creating a
filter-like timbre change. **What is remarkable, is that so far no filter has been used.**" (Same
manual, p.14.) The wavetable itself is "64 single waves lined up next to each other… played
statically or you can sweep through them, to create an interesting timbre change" (same manual,
quoted on the [manual index page](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html)).
That is the structural difference: in a subtractive pad one static waveform is sculpted by the
filter; in a wavetable pad **the oscillator's own waveform is a modulation destination**, so the
timbre can move in directions a lowpass cannot reach (formants, metallic partials, sync-like
sweeps) — and it keeps moving after the filter is fully open.

**[local] The XT's role-specific names.** `W1EnvAmount` / `W1EnvVelAmount` / `W2EnvAmount`
(bipolar, `isPublic: true`), plus `W1StartW` / `W1StartP` ("Startwave"/"Start phase"),
`W1Keytrack`, `W1Limit`, `MixW1`/`MixW2`/`MixNoise`/`MixRingMod`, and the filter group
`F1Cutoff`, `F1Resonance`, `F1Type`, `F1KeyTrack`, `F1EnvAmount`, `F1EnvVelAmount`, `F1Extra`
(`parameterDescriptions_xt.json`). `MixRingMod` default 96 — i.e. **the ring modulator is not at
zero out of the box**, which matters when the corpus tests "bell/metallic vs clean"
([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §4 lists `MixRingMod` as the XT's
metallic-FX lever).

**[local] Xenia's host-param surface is not a reliable pad-automation route.** `set_fx_param` on
Xenia is **not resolvable** — the same-input spread (0.0056) exceeded the measured effect (0.0023),
so no audible effect is claimed; the device-native route is the 265-byte Waldorf SyEx dump via
`apply_preset`, which *is* verified to change the render and to survive rebuild/export
([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §7 row 2;
[`docs/va-suite-status-log.md`](docs/va-suite-status-log.md) §"Vavra + Xenia matrix presets").

**[external] Forest/dark texture practice.** Forest is where "the kick is often softer in the mix,
subordinate to texture rather than driving it", with a palette of organic sounds
([TIMBR](https://timbr.music/blog/psytrance-subgenres)); dark psy is "more mechanical/digital
sounding with prominent kicks and basslines", forest "may have a less prominent bassline, and the
textures tend to sound more organic giving an eerie feeling"
([r/psytrance](https://www.reddit.com/r/psytrance/comments/9wipob/forest_psy_vs_dark_psy/)).
Confidence: medium — these are genre-description sources, not measurements.

### 1.5 Movement / automation canon, and "movement" vs "modulation"

**[external] Long-form movement is arrangement-scale.** The rising diagonal cutoff across a whole
section (§1.3), and full-on's "minimal breakdown with a rising filter sweep"
([TIMBR](https://timbr.music/blog/psytrance-subgenres)), are section-scale gestures measured in
bars, not cycles.

**[external] Short-cycle movement is the roll itself.** The "per-note 'chirp'" of the bass filter
envelope (Myloops, §1.1) is per-note; the local bass chain's 2-cycles-per-beat EQ-cutoff LFO is
per-beat ([local], §5 of
[`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md)).

**Working distinction for this repo** (it is a synthesis of the sources above, not a quote):

| | **Movement** | **Modulation** |
|---|---|---|
| Timescale | 8–64 bars; section-scale | per-note to per-beat |
| Carrier | automation lane / macro / one-shot env | LFO, note-triggered envelope |
| Job | changes where the *section* sits harmonically and dynamically; survives being soloed | makes the *groove* read; usually inaudible when soloed |
| Signature moves | rising cutoff diagonal, filter-env-depth ramp, LFO rate ramp, FX mix swell | bass "chirp", per-beat pump, gate lift, pad PWM |
| Failure if swapped | LFO in place of a ramp = a section that never arrives | an automation ramp in place of the chirp = a dead-sounding roll |

**[local] The house rules that already encode this.** "**Ramp, never step** — a single parameter
jump clicks; automation lanes and movement plans interpolate, one-shot writes do not." and
"**Automate amounts, not destinations**… ramping the *amount* into a fixed destination is a
sweep." Also: "**No pitch modulation on bass or leads**" and "**Per-voice LFO pumping is not bus
pumping**" ([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §8).

---

## 2. Hypotheses for the corpus agents

Each is phrased so a distribution over the psy-labelled patches (or over project automation) can
**confirm or refute** it. "Default" means the value in the emulator vocabulary file, which is the
null hypothesis. Roles are those already in the sidecar metadata.

### Bass (Virus TI / Virus C — `OsTIrus` / `Osirus`)

1. **Cutoff is far below default.** Default `Cutoff` = 127 (fully open). Psy-labelled bass patches
   should sit in the **lower third** of 0–127, with the median well below the mid-point.
   *Refuted if* the psy bass subset's cutoff distribution is not distinguishable from the
   factory-default cluster at 127.
2. **Filter env amount is engaged and high.** Default `Filter1 Env Amt` = 0. Psy bass patches
   should show non-zero amounts concentrated in the **upper third**. *Refuted if* they cluster at
   or near 0. (External target: "an octave" of opening — [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline);
   local corroboration: `growl_bass` `Filter Env Amt` default 0.7 of 0–1.)
3. **The sub oscillator is engaged.** Default `Suboscillator Volume` = 0. A majority of psy bass
   patches should have it **> 0**. *Refuted if* the psy subset is indistinguishable from the
   default-zero cluster.
4. **Sub shape is Square, not Triangle,** among bass patches that have a sub. *Refuted if* the
   ratio is at or below 50/50 or favours Triangle.
5. **Unison is off on bass.** Default `Unison Mode` = 0. Bass patches should keep it off while lead
   patches do not — the *contrast* is the testable claim. *Refuted if* unison usage on bass
   matches lead usage.
6. **Punch is at or above the default.** Default `Punch Intensity` = 64. Bass patches should sit
   **≥ 64** (attack snap), not below. *Refuted if* the median is below 64.
7. **No filter keytracking on bass.** `Filter1 Keyfollow` default 64 = bipolar centre = no
   tracking. Bass patches should cluster at/near 64. *Refuted if* psy bass patches systematically
   deviate toward the positive (tracking) side. This hypothesis is **contested** — the microQ
   manual recommends the opposite for bass in general (§5), so a refutation here is informative,
   not a mistake.
8. **Amp envelope: short release, zero sustain.** `Amp Env Release` at/near minimum and
   `Amp Env Sustain` = 0 for bass. *Refuted if* psy bass patches show long releases.
9. **Filter env decay is short relative to amp decay** — the chirp must finish inside the note.
   *Refuted if* filter decay ≥ amp decay in the median patch.
10. **Osc Volume stays ≤ 0 (unity or attenuated) on bass.** In the TI vocabulary the saturation
    amount *is* `Osc Volume`/`Osc Volume / Saturation` (−64…+63, 0 = unity gain, positive = added
    saturation). Psy bass patches should keep it **≤ 0** more often than > 0. *Refuted if* the psy
    bass subset skews positive. (External support: E-Clip now builds basslines with no distortion
    because "distortion plugins mess up phase of the bassline" —
    [E-Clip](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip).)
11. **Patch distortion is off on bass.** `Distortion Intensity` default 0, `Patch Distortion/Mix`
    minimal among bass patches.
12. **Filter mode is plain lowpass.** `Filter1 Mode` = LP dominates psy bass; `Analog 1..4 Pole`
    is rare (the manual notes the Analog filter costs "up to 50%" of polyphony).
13. **Oscillator mode is Classic on bass** — required for the square/triangle sub, and the
    `Shape` parameter is "not available if oscillator 1 is in HyperSaw or WaveTable mode", so a
    patch with a sub *must* be Classic. *Test as a joint distribution:* sub-engaged ∧
    Classic-mode should be near-perfectly correlated. A sub engaged on a HyperSaw patch would
    refute this (and would itself be a finding).
14. **Filter 2 is not doing melodic work on bass:** `Filter Balance` skewed to filter 1, or
    Filter 2 cutoff high.

### Acid stab / pluck

15. **microQ acid patches live in the "strong boost" resonance band.** `F1Resonance` in the
    **80–113** band (manual: "typical filter character with a strong boost"), with a tail above
    113 (self-oscillation). *Refuted if* acid-labelled patches cluster in the low 0–80 "brilliance"
    band described for ordinary sounds.
16. **Acid patches use a large `|F1EnvMod|`** (upper third of −64…+63), and a non-zero share use
    **negative** values (inverted sweep) — the manual calls the parameter bipolar.
17. **The acid pluck rides a separate envelope, not just `F1EnvMod`.** Acid-labelled microQ patches
    should show a **ModMatrix slot** with an envelope routed to the filter (source `Env*` →
    `F1CutoffMod` or equivalent), distinct from the amp envelope. *Refuted if* Matrix usage on
    acid patches is indistinguishable from non-acid patches.
18. **`F1KeyTrack` is high on acid.** Manual: "+100% … If you want to play the filter in a tempered
    scale, e.g. for a solo sound with self-oscillation, set the value to +100%", while "on most bass
    sounds lower settings in the range +50…+75% are optimal". Acid *leads* should sit near/above
    the bass range. *Refuted if* acid and bass keytrack distributions coincide.
19. **Glide is on and mode is legato-ish.** `GlideEnable` = on with `GlideMode` = Fingered
    Portamento/Glissando (the legato-only modes) for acid lines, with `GlideRate` mid-range.
20. **`F1Drive` is elevated on acid/stab** (manual: more distortion is "suitable for harder lead
    sounds and effects"), unlike the bass patches of hypothesis 10.
21. **`VoiceMode` = mono on acid** and `UnisonoCount` low. *Caveat:* in
    `parameterDescriptions_mq.json`, `GlideMode` carries `isPublic: true` while
    `GlideEnable`/`GlideRate`/`UnisonoCount`/`UnisonoDetune`/`VoiceMode` do **not** — check
    `list_device_params` before assuming a glide/unison move is host-automatable on Vavra.

### Lead

22. **Unison is the bass/lead discriminator.** Lead patches should show `Unison Mode` ≠ Off and
    `Unison Detune` > 0 significantly more often than bass patches; and **`Unison Pan Spread`
    stays at its default 127** even when unison is off (it also applies to Parallel/Split sounds),
    so pan spread is *not* evidence of unison — `Unison Mode` is.
23. **Lead patches use HyperSaw/WaveTable oscillator modes** more than bass patches (which
    hypothesis 13 expects to be Classic). Joint test across both roles.
24. **Glide is engaged on leads and not on basses** (`Portamento` on Virus;
    `GlideEnabled`/`GlideTime` on XT; `GlideEnable` on microQ).
25. **Project-level (not patch-level): lead sections carry a monotonic cutoff ramp.** Over any
    ≥8-bar section, the lead's cutoff automation should be monotonically non-decreasing
    (a "rising diagonal"), whereas a decorative LFO would show oscillation at 1/4–4 cycles/bar.
    *Refuted if* lead cutoff automation is dominated by short-cycle oscillation.

### Pad / texture (Microwave XT — `Xenia`)

26. **Pad labels carry slow amp attacks.** XT pad-labelled patches have significantly longer
    `AmpEnv Attack` (and/or `Release`) than bass/pluck-labelled patches. Cookbook target: Attack
    050, Decay 000, Sustain 127, Release 050.
27. **Pads move the WAVE, not only the filter.** Pad patches should show `W1EnvAmount`/`W2EnvAmount`
    non-zero and a `W1StartW`/`W1StartP` that is not the patch's static default position — i.e.
    the oscillator itself is modulated. *Refuted if* pad patches modulate only `F1Cutoff`.
28. **Pads do not ring-modulate.** `MixRingMod` should be near 0 for pad-labelled XT patches,
    and high for bell/metallic/FX-labelled ones (default is 96, so the pad subset must move it
    *down*).
29. **Pads favour non-lowpass.** Hipass/Bandpass filter types should be over-represented on pads
    relative to bass patches (the cookbook's own pad advice).
30. **Textures in forest/dark psy are a separate layer from the bass.** In forest projects expect
    the bass fader/velocity lower relative to texture tracks than in dark projects (the genre
    claim in §1.4). *This is the weakest hypothesis here* — it is a claim about genre, not about
    patches, and the sources are descriptive.

### Movement / automation (project-level)

31. **Bass cutoff modulation is beat-synchronous.** Bass tracks should carry an LFO on filter
    cutoff whose rate is an integer division/multiple of the beat (the local verified default is
    **2 cycles/beat**, depth 0.28), not a free-running fraction like 0.12 Hz.
32. **Bass is ducked, not sidechained-to-taste.** The verified local bass chain uses a **1/beat
    Volume pump, depth 0.6, phase 180** — i.e. the pump is an *amplitude* move at the beat rate,
    and per the local rule "per-voice LFO pumping is not bus pumping".
33. **Cutoff ramps are section-scale.** Section-boundary movement uses ≥8-bar ramps (Macro sweep
    recipe in [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5:
    points every 32 beats, 0.1→0.7).
34. **Movement is durable, modulation may not be.** Device-internal modulation lives in the patch
    dump (durable); host-side SysEx pokes and live CC do not survive reload
    ([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §8 "Keep the transition in the
    project file").

### Traps (testable as absences)

35. **No psy bass patch should have a long amp release with non-zero sustain.** The joint
    condition (release in the top third ∧ sustain > 0) should be rare in bass-labelled patches;
    its presence in a "psy bass" distribution falsifies the role label, not the hypothesis.
36. **Bass roots sit in a narrow low register.** Note-level corpora (MIDI/`query_notes`): psy bass
    roots should cluster in a ~1.5-octave window in the bass register rather than being spread
    over the full range.
37. **Bass onsets avoid the kick tail.** In projects, either the kick's amp release is short enough
    that its ~150–250 ms pitch-swept tail is gone before the next 16th, **or** the first bass note
    is nudged late (the external figure is 5–15 ms). Expect to see one of the two, not neither.
    ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline).)

---

## 3. Traps — what kills these sounds, and why

1. **Long amp release on the rolling bass.** The gaps *are* the sound: "Zero out the release
   entirely" ([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)). Why it sounds
   wrong: the note tail runs under the kick and under the next note, so the perceived rhythm
   becomes a continuous tone with an amplitude ripple — the "roll" turns into "mush", and low-end
   energy accumulates across the bar instead of resetting per note.
2. **Filter env depth too large on a bass.** "Too much depth and you're making acid"
   ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline)). Why: the bass's
   job is pitch and weight; a sweep wide enough to be heard as a sweep adds mid-band movement that
   competes with the lead and the hats, and the note stops reading as *one* low event.
3. **Over-saturation / phase damage on the bass.** E-Clip: distortion plugins "mess up phase of the
   bassline", so he now makes "clean basslines without any deformations"
   ([E-Clip](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip)). Why: a
   low-frequency asymmetric clipper shifts the waveform's phase and adds harmonics in the kick's
   band; the sum either thins (partial cancellation) or turns to grumble, and the loss is at the
   fundamental, where you cannot EQ it back.
4. **Sub oscillator fighting the kick.** Two fundamentals in the same band beat: "If your first
   bass note fires while that tail is still ringing, the two sines beat against each other, phase
   cancels in random places, and the whole low end turns to soup", and the kick tail "needs to
   resolve to" the bass's pitch ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline);
   [Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)). One source zones it as
   "muddy frequency collisions between 40–120 Hz" ([Atomic Rose](https://atomic-rose.com/blog/how-to-balance-kick-and-bass-in-psytrance-without-mud)).
   KVR's full-on framing: "having both kick and bass at the same time will be too much, so you use
   a compressor to 'fade' between kick and bass" ([KVR](https://www.kvraudio.com/forum/viewtopic.php?t=457378)).
   Why: identical/near-identical frequencies sum with a slowly rotating phase offset — a slow
   amplitude beating that no static EQ fixes, and that changes from bar to bar.
5. **Wrong octave.** Too high and the track loses its weight and the line competes with the lead;
   too low and it becomes inaudible rumble on most systems while eating all the headroom. Concrete
   local instance of the same failure: importing a Virus patch whose filter was closed landed
   `sub_synth` at `Cutoff = 20 Hz` and the slot rendered near-silent (0.0029 RMS) while reporting a
   successful import; opening it to 300 Hz took the same part to 0.0446 — a 15× change from one
   parameter ([`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5c).
6. **Unison phasing / mono collapse on a lead.** "Check the stack on a correlometer so no layer
   cancels the mono signal" ([Blueprint lead page](https://www.psytrance-blueprint.com/tutorials/psytrance-lead-sound-design-serum/)).
   Why: detuned voices at the same nominal pitch sum with time-varying phase; in stereo it sounds
   wide, but summed to mono the partials cancel and the lead drops out — audible only on the club
   system, never in the studio.
7. **The filter envelope doing the amp envelope's job (acid).** "Why does my acid lead sound flat
   and not plucky? You are probably relying on the amp envelope. The pluck comes from a separate
   mod envelope routed to the filter cutoff" ([Goa 303 page](https://www.psytrance-blueprint.com/tutorials/goa-303-acid-lead/)).
   Why: an amp-only shape changes loudness; the acid character is a *timbre* transient. Shaping
   level produces a duller note, not a snappier one.
8. **Too much resonance.** "A little gives bite; too much and the bass starts honking"
   ([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/)). Why: a resonant peak at a
   fixed cutoff adds a pitched ring at the cutoff frequency; on a bass whose root moves, that ring
   becomes a second, inharmonic "note" competing with the pitch you wrote.
9. **Automating a destination instead of an amount.** Local rule: "Rewriting a matrix
   *destination* per block is a step change with no musical shape; ramping the *amount* into a
   fixed destination is a sweep" ([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §8).
   Why: destinations are discrete — every change is a click plus a discontinuous timbre jump, so
   the "sweep" arrives as a staircase.
10. **Pitch modulation on bass or leads.** Local guide rule §4D ("no discord"): pitch modulation
    belongs on risers/downlifters with small amounts, not on the bass or lead
    ([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §4, §8). Why: the bass and lead carry
    the harmonic identity; a pitch LFO makes every note a detuned interval against the scale the
    rest of the track is in.
11. **Engine-specific trap — the HDAW saturator's `Bits` is inert unless type is Bitcrush.**
    [local] The enum is `SoftTanh=0, SoftAtan=1, Hard=2, Bitcrush=3`, and `Bits` is read **only**
    in the `Bitcrush` branch (`src/engine/SaturatorEngine.h`); with `Type 0` it does nothing, and
    the saturator's **stock defaults are Drive 12 dB / Mix 1.0** — so an untuned saturator is
    already heavy. Why it matters: attributing grit to `Bits` misdiagnoses the cause (real
    mechanism: drive into a `tanh` soft-clip), and the default drive means "adding a saturator"
    is never a neutral act.
12. **Engine-specific trap — master gain is not drive.** [local] Master gain is applied *after*
    the master FX chain (`MasterBusProcessor::processBlock`), so "automate the master gain for a
    build" behaves as an output trim rather than as drive into the limiter. Any external advice
    that assumes gain-staging *into* a master limiter does not transfer to this engine as-is.
13. **Engine-specific trap — Xenia host-param writes are not provably audible.** [local] The
    Xenia `set_fx_param` effect is not resolvable against same-input spread, so a "cutoff sweep"
    on Xenia cannot be trusted from a parameter write alone; use the Waldorf SyEx dump route and a
    spectral A/B ([`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §7 row 2).
14. **Engine-specific trap — isolated children are non-deterministic.** [local] Emulated synths
    with free-running oscillator phase differ ~±2% RMS between renders, so gates must tolerate
    that and A/B by spectral properties, not sample equality
    ([`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §4D "Isolated
    children render non-deterministically"). Why it matters for the corpus work: a difference
    smaller than the ±2% floor is not evidence of an audible patch difference.

---

## 4. Source table

Confidence: **high** = primary (manufacturer manual / emulator vocabulary / local measurement);
**medium** = established production reference or working producer; **low** = forum or
content-farm, used only to show that a practice circulates.

| # | Claim | Source(s) | Confidence | Conflicts |
|---|---|---|---|---|
| 1 | Virus sub oscillator = octave-down slave, Square/Triangle, cross-faded by `Volume`; unavailable in HyperSaw/WaveTable mode | [Virus TI2 manual p.35](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html); `parameterDescriptions_TI.json` | high | — |
| 2 | Virus saturation is a pre-filter stage; **0 = unity gain**, only positive values add saturation | [Virus TI2 manual p.34](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html) | high | — |
| 3 | Virus filter: `Cutoff`/`Resonance`/`Env Amt` 0–127, `Env Polarity` inverts, `Key Follow` −64…+63, `Analog 1..4 Pole` halves polyphony | [Virus TI2 manual pp.38–39](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html) | high | — |
| 4 | Virus `Punch Intensity` = attack snap, default 64 | [Virus TI2 manual p.34](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html); vocab | high | — |
| 5 | Virus unison = stacked, detuned, spread voices: `Unison Mode`/`Detune`/`Pan Spread` | [Virus TI2 manual p.68](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html) | high | — |
| 6 | Virus patch defaults: `Cutoff` 127, `Filter1 Env Amt` 0, `Suboscillator Volume` 0, `Filter1 Resonance` 0, `Filter1 Keyfollow` 64, `Unison Detune` 48, `Unison Pan Spread` 127 | `parameterDescriptions_TI.json` | high | — |
| 7 | microQ filter character: 0–80 brilliance, 80–113 "typical filter character", >113 self-oscillates; `Drive` adds saturation; `F1EnvMod` bipolar; `Keytrack` reference note E3 | [microQ manual pp.74–75](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) | high | — |
| 8 | microQ keytracking advice: "+50…+75% are optimal" on most **bass** sounds; +100% for tempered/self-osc solo | [microQ manual p.75](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) | high | **Contests hypothesis 7** (see §5) |
| 9 | microQ glide: `GlideEnable`, modes Portamento / Fingered Portamento / Glissando / Fingered Gliss, `GlideRate` | [microQ manual p.65](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) | high | — |
| 10 | microQ 16-slot mod matrix, Fast (M1F–M8F) + Standard (M1S–M8S), amount −64…+63 | [microQ manual p.120](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) | high | — |
| 11 | microQ unison exists as `UnisonoCount`/`UnisonoDetune`; filter params `isPublic`, glide/unison params not | `parameterDescriptions_mq.json` | high | — |
| 12 | XT wave envelope "scans" the wavetable for a filter-like change **without a filter**; wavetable = 64 waves swept | [XT manual pp.14 + index](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html) | high | — |
| 13 | XT pad recipe: Cutoff 080–090, AmpEnv A050/D000/S127/R050, glide 018, Hipass/Bandpass, detune+chorus | [XT manual p.15](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html) | high | — |
| 14 | XT bass guidance: saw (sharp) or square (nasal), one oscillator, amp env "short and sharp" | [XT manual p.13](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html) | high | — |
| 15 | XT names: `W1/W2EnvAmount`, `W1StartW/P`, `MixRingMod` (default 96), `F1EnvAmount`, `GlideEnabled/Type/Mode/Time` | `parameterDescriptions_xt.json` | high | — |
| 16 | Rolling bass: 16th-note line; character is envelope + between-note filter movement | [G-Sonique](https://www.g-sonique.com/psytrance-vst-plugins) | low | — |
| 17 | Bass = sub anchor + quieter detuned top layer; below 200 Hz mono | [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline) | medium | Reddit: a single-osc psy bass needs no separate sub (§5) |
| 18 | Zero amp release on the roll; amp decay 80–120 ms, sustain 0 | [Monosounds](https://monosounds.studio/psytrance-bass-serum-2/) | medium | Myloops gives 50–70 ms |
| 19 | Fast filter env on the bass top layer, ~40 ms decay, opens ≈an octave — "the move that separates a psy bass" | [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline) | medium | Monosounds: only a "small amount" |
| 20 | Bass cutoff 400 Hz–1 kHz | [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline) | medium | Monosounds: 200–400 Hz |
| 21 | Bass resonance low; too much "honking" | [Monosounds](https://monosounds.studio/psytrance-bass-serum-2/) | medium | — |
| 22 | Kick tail 150–250 ms must not overlap the bass; fix by shorter kick release and/or 5–15 ms bass nudge | [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline) | medium | — |
| 23 | Bass and kick tail must share pitch; 2–3 dB ducking, fast release | [Monosounds](https://monosounds.studio/psytrance-bass-serum-2/) | medium | KVR frames full-on as compressor "fade" between them |
| 24 | E-Clip's clean bassline: amp env 0/1-16/−3 dB/15 ms; Env2→cutoff 0/1-16/60%/10 ms; Osc A volume 0 + velocity; no distortion | [E-Clip](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip) | medium-high (named producer) | — |
| 25 | Acid = one saw, resonant LP, per-note env→cutoff, accents open the filter further | [Blueprint acid 303](https://www.psytrance-blueprint.com/tutorials/acid-303-psytrance-serum/) | medium | — |
| 26 | Acid pluck comes from a **separate mod env → cutoff**, not the amp env | [Blueprint Goa 303](https://www.psytrance-blueprint.com/tutorials/goa-303-acid-lead/) | medium | — |
| 27 | Lead = 4–5 layers; mono/correlometer check; portamento for legato, short low / long high notes | [Blueprint lead design](https://www.psytrance-blueprint.com/tutorials/psytrance-lead-sound-design-serum/) | medium | — |
| 28 | Lead/acid filter character from **automation ramps**, not an oscillating LFO; delay on the cleanest high layer | [Blueprint lead design](https://www.psytrance-blueprint.com/tutorials/psytrance-lead-sound-design-serum/) | medium | No source found contradicting it; but it is one school's teaching, and the local house bass chain *does* use an LFO on cutoff (§5) — the distinction is timescale |
| 29 | Lead portamento ≈ two-thirds, pre-glide full, band-pass ~1 kHz Q≈5% | [Dance MIDI Samples](https://www.dancemidisamples.com/making-a-psytrance-lead/) | low-medium | — |
| 30 | Subgenre tempos (full-on / progressive / dark / forest / twilight) | [TIMBR](https://timbr.music/blog/psytrance-subgenres), [Plugg Supply](https://plugg-supply.net/articles/psytrance-production), [Vibes DJ](https://vibesdj.io/dj-tools/what-bpm-is-twilight-psytrance) | low-medium | Guides disagree 4–8 BPM per branch |
| 31 | Forest = texture-forward, softer kick; dark = bassline-forward, mechanical | [TIMBR](https://timbr.music/blog/psytrance-subgenres), [r/psytrance](https://www.reddit.com/r/psytrance/comments/9wipob/forest_psy_vs_dark_psy/) | low | — |
| 32 | The Virus is a psy staple (soundset ecosystem: Mechanimal's 100-patch Virus TI psy bank; Heka Virus C/B psy set) and Access calls itself "the de-facto standard in the Virtual Analog synthesis world" | [Dance MIDI Samples](https://www.dancemidisamples.com/category/synth-patches/access-virus-soundsets-synth-patches/access-virus-psytrance-sound-banks/), [Gearspace](https://gearspace.com/threads/heka-psytrance-soundset-for-access-virus-c-b-series-osirus.1460449/), [virus.info](https://www.virus.info) | low (marketing/preset-product claims) | — |
| 33 | HDAW internal `growl_bass` = dedicated offbeat rolling bass; Cutoff 800 default, Filter Env Amt 0.7, Res 4, sidechain built in | [local] [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5c | high | — |
| 34 | Verified bass production chain: EQ→comp + LFO 2 cycles/beat → EQ cutoff (depth 0.28) + 1/beat Volume pump (0.6, phase 180) | [local] [`docs/psytrance-va-and-production.md`](docs/psytrance-va-and-production.md) §5 | high | — |
| 35 | Movement rules: ramp never step; automate amounts not destinations; no pitch mod on bass/leads; per-voice LFO ≠ bus pump | [local] [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §8 | high | — |
| 36 | Emulator param counts: TI 6939 / C 3086 / microQ 7557 / XT 2151 host params vs much smaller device vocabularies (TI 600 slots/655 names, C 305/306, microQ 262/374, XT 136/136) | [local] host counts: **documented** in [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §1 / "Params or nothing" and [`docs/va-suite-status-log.md`](docs/va-suite-status-log.md) (2026-09-19/20), corroborated by the plugin host's `ext-probe` log; only **TI 6939 was live-confirmed this session** (`list_fx_params` on an instantiated OsTIrus slot). Device-vocabulary counts were measured today via `virus_fx_pages.py::load_vocab` | high (docs) / high (today's vocabulary counts) | — |
| 37 | microQ FX sub-params collapse (shared indexes) → only type-level FX params automatable | [local] [`docs/va-suite-status-log.md`](docs/va-suite-status-log.md) §"Vavra + Xenia matrix presets" | high | — |
| 38 | Xenia host param writes not resolvable; Waldorf SyEx dump route is the verified one | [local] [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §7 row 2 | high | — |
| 39 | HDAW saturator: `Bits` inert unless `Bitcrush`; defaults Drive 12 dB / Mix 1.0 | [local] `src/engine/SaturatorEngine.h` (reported by the parent agent, measured this session) | high | — |
| 40 | Master gain is applied after the master FX chain | [local] `MasterBusProcessor::processBlock` (reported by the parent agent) | high | — |

### Distinct sources actually consulted

**24 distinct external sources** (11 fetched and read in full or in substantive part; 13 seen only
as search-result snippets and used only for the specific quoted text), plus **4 local docs** and
**4 device vocabulary files**. The 11 read-in-substance: the three Waldorf/Access manual mirrors
(counted as one source each), Psytrance Blueprint (three pages), Myloops, Monosounds, E-Clip,
Atomic Rose, Plugg Supply, TIMBR, Dance MIDI Samples.

**Top 5 sources for this work**
1. [Access Virus TI2 manual (manualshelf mirror)](https://www.manualshelf.com/manual/access/virus-ti2-keyboard/user-manual-english.html) — the only primary account of the sub/saturation/filter-env architecture that the bass hypotheses rest on.
2. [Waldorf microQ manual (manualshelf mirror)](https://www.manualshelf.com/manual/waldorf/4741/user-manual-english.html) — resonance bands, keytrack guidance, drive, glide, mod matrix.
3. [Waldorf MicroWave II/XT manual (manualshelf mirror)](https://www.manualshelf.com/manual/waldorf/microwave-xt/user-manual-english.html) — wave envelope, wavetable sweep, and the pad/bass cookbook recipes.
4. [Monosounds — Psytrance Bass in Serum 2](https://monosounds.studio/psytrance-bass-serum-2/) — the most concrete envelope/cutoff/kick-lock numbers found anywhere.
5. [Myloops — rolling bassline that locks with the kick](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline) — the two-layer model, the filter-env "chirp", and the kick-tail mechanism.

Honourable mention: [E-Clip's clean bassline post](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip) — the only *named psytrance producer* source with a full recipe, and the origin of the anti-distortion trap.

**Strongest / weakest parts of the evidence base**

- *Strongest:* device architecture. Every Virus/microQ/XT parameter named in this file is now
  checked against the emulators' own vocabularies, including **defaults** — which converts a vague
  "should sound like psy" into a real statistical test per hypothesis.
- *Strongest:* bass envelope/cutoff numbers, independently supported by two producer tutorials plus
  the locally shipped `growl_bass` defaults.
- *Weakest:* anything subgenre-specific (twilight/forest/dark). The tempo tables disagree and the
  character claims are descriptive prose, not measurements; hypotheses 30–31 are the softest.
- *Weakest:* the wavetable-family → role mapping. I could not obtain the named wavetable list
  (see §5), so §1.4 explains *why* wavetables differ from subtractive pads but cannot say "use
  table N for a forest pad".
- *Weakest:* the "Virus is the psy bass synth" claim, which rests on soundset marketing and
  Access's own self-description.

---

## 5. What I could not establish (and the flags)

**Could not establish:**

1. **The Microwave II/XT wavetable names/numbers.** I confirmed a 65-table ROM set with
   descriptions existing, "wavetables from the PPG Wave 2.2 synthesizer, except for the 'Upper
   Waves'", and that "Wavetables 28-52 and 65 are algorithmically generated" — from a search-result
   description of the *Waldorf Microwave Wavetable Reference Guide*
   ([snippet](https://manuals.plus/m/92fa4d2dcba7afdcb80fdd0b341d3f7869e534670e81b51c69790161ffa86234));
   the document itself returned an empty body to `web_fetch`, and the PDF and JS-rendered mirrors
   were not fetchable. **Therefore: no wavetable-family → role claims in this file.** The corpus
   agents can settle this from the Xenia sidecars (`W1StartW`, `W1EnvAmount`) far better than I can
   from the web.
2. **Whether psy rolling basses keytrack the filter.** No source addresses it, and the one primary
   source that does is arguing the other way (microQ manual: +50…+75% keytrack "optimal" for most
   bass sounds). Hypothesis 7 is therefore written as contested.
3. **Filter-envelope amount in device units.** Sources give "about an octave" of opening or "small
   amount"; nobody publishes a device-unit number. The corpus distribution has to be the reference,
   which is exactly why hypotheses 2 and 9 are phrased as relative positions.
4. **Any authoritative "the Virus is *the* psy bass synth" claim.** Only preset vendors and Access's
   own marketing language. I did not find a producer interview or a genre reference making the
   claim directly.
5. **Twilight/forest/dark patching specifics.** Only character prose (bass forward vs texture
   forward). No parameter-level evidence for "forest pads are darker" beyond the wavetable
   architecture argument in §1.4.
6. **Which cutoffs to trust (400 Hz–1 kHz vs 200–400 Hz)** — the two most concrete sources
   genuinely disagree, and the local `growl_bass` default (800 Hz) sits at the top of the higher
   range while still inside it. Hypothesis 1 therefore tests *position relative to default* rather
   than an absolute Hz target.

**Flags where sources disagree (agree-to-disagree, recorded rather than resolved):**

- **Sub layer: needed or not.** Myloops builds a two-layer bass (loud sub anchor + quieter detuned
  saw top) — [Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline). A Reddit
  thread argues the opposite for the usual single-oscillator psy bassline: "you don't get the low
  end mud of detuned stereo basslines often found elsewhere, so a separate sub layer isn't needed"
  — [r/psytranceproduction](https://www.reddit.com/r/psytranceproduction/comments/1by8dxi/is_a_sub_layer_really_necessary_what_are_your/).
  Both can be true (a synthesized sub *oscillator* inside one patch is not a second *layer* in the
  mix), which is why hypothesis 3 asks about `Suboscillator Volume` on the patch, not about track
  count.
- **Amp decay on the bass:** 80–120 ms ([Monosounds](https://monosounds.studio/psytrance-bass-serum-2/))
  vs 50–70 ms ([Myloops](https://www.myloops.net/how-to-make-a-psytrance-rolling-bassline)) vs
  "1/16th note" at ~145 BPM ≈ 103 ms ([E-Clip](https://www.eclipmusic.com/post/clean-psytrance-bassline-by-e-clip)).
  E-Clip's figure is the bridge between them; the spread is real but the order of magnitude is not
  in dispute.
- **Filter env amount on the bass:** "you're making acid" above ~an octave of opening (Myloops) vs
  "small amount" (Monosounds). Unresolved; hypothesis 2 tests position in range, not depth.
- **Subgenre tempo tables:** 4–8 BPM disagreement in every branch (§0).
- **LFO vs automation on the filter.** [Blueprint](https://www.psytrance-blueprint.com/tutorials/psytrance-lead-sound-design-serum/)
  is emphatic that acid character comes from an automated rising cutoff and *not* an LFO; the local
  verified bass chain *does* put an LFO on the bass EQ cutoff (2 cycles/beat). Not a real
  contradiction once the timescale is separated (per-beat modulation vs section-scale movement) —
  which is precisely the distinction §1.5 formalises.

**Flags on local-vs-external tension (do not silently pick a side):**

- **Virus host parameter writes.** [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) §1 says
  "set_fx_param reaches the cache but **the OS ignores host writes**" for the Virus, while the same
  document's §7 row 2 lists "the Virus param path round-trips to offline renders (F-A phase 2)".
  These two local statements are in tension. The corpus agents should not assume either: verify per
  build with `list_device_params` + a render A/B, and treat **CC0+PC (`load_virus_preset`) and
  `send_fx_midi` CC as the durable route** for Virus character (per §1's recommendation).
- **microQ keytrack guidance vs psy practice.** The microQ manual's own bass advice
  (+50…+75% keytrack, "to keep the sound smooth at higher notes") is written for *general* bass
  sounds, and a psy rolling bass stays within one or two notes, where keytracking barely moves the
  cutoff. I have kept the manual claim and the psy expectation as separate, testable statements
  rather than merging them.

**Not researched at all (out of scope, named so nobody assumes coverage):** kick synthesis, hats,
arrangement/structure beyond movement, mixing/mastering numbers, and the Nord Lead 2x / JP-8080
roles in these genres.
