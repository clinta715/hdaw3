# Psytrance Virus Recipes — Role Parameter Recipes for OsTIrus / Osirus

## What this document is

A role-by-role recipe book for programming the **Access Virus** (emulated by the
**OsTIrus** / **Osirus** CLAPs in the HDAW hardware-VA suite) from a measured
corpus, expressed in **host parameter names** that the engine actually accepts.

This section covers **SECTION 1: FRONT MATTER + THE BASS ROLE**. Later sections
(lead, pluck, pad, acid, arp, fx, perc) are appended by other agents.

## The corpus

- **5,296 rows** ingested, **2,818 unique after dedup**.
- Roles are **name-derived designer intent** — i.e. a patch named `Bass …` is
  counted as a bass patch because its *author named it that*, not because anyone
  listened to it or measured it. This is intent, not acoustics.
- **No genre labels anywhere.** The corpus carries no "psytrance" field and no
  genre tag. Therefore this document gives **structure and priors** — which
  parameters move together, and in which direction, in patches humans labelled
  for a role — and **not** measured psytrance targets. Where a recommendation
  leaves the corpus interquartile range, it is labelled *extrapolation* and the
  reasoning is given.

## Method

1. Per-role byte distributions were computed over the deduped corpus
   (`.tmp_virus/analysis.txt`, RAW BYTE DISTRIBUTIONS table); every value in this
   document's tables is a byte in **0..127** as stored in the patch.
2. Distributions are quoted as `median (p25–p75)`, plus an **ALL-patches**
   baseline column for contrast.
3. Every parameter name is taken from
   [`docs/psy-virus-host-param-map.md`](psy-virus-host-param-map.md) or the live
   parameter list — the map is the **name authority**. Several names that appear
   in Access manuals and in the corpus vocabulary are *not* host parameters and
   are marked as such rather than invented.
4. Predictions are stated so a render can falsify them (`mix_report`,
   `verify_part`).

## Live host-surface verification (measured this session)

The parameter surface was verified against a **live** OsTIrus instance, not a
manual:

- A real OsTIrus slot reports **6939 parameters**.
- Host names are **`Ch <n> `-prefixed** (e.g. `Ch 1 Filter 1 Cutoff`), and writes
  to them **persist into offline renders** — measured: cutoff `0.33 → 0.12` with
  the sub oscillator engaged moved the rendered **high band 1579.9 → 57.2** and
  the **bass band 5.9 → 490.9**.
- **Bypassing the slot renders digital silence** — so a silent render from a Virus
  slot is an audibility failure, not a "quiet patch".

---

# SECTION 1 — BASS (Access Virus via OsTIrus / Osirus)

## 1.1 Parameter table

Byte = the 0..127 device value stored in the patch (`analysis.txt` RAW BYTE
DISTRIBUTIONS, deduped corpus). "Corpus bass" = role `bass`, **n = 92**; "ALL" =
all 2818 deduped patches, quoted as `p25/median` exactly as the artifact prints it.
Every host name below is from
[`docs/psy-virus-host-param-map.md`](psy-virus-host-param-map.md) and is
`Ch 1 `-prefixed; the byte row it is derived from is named in *notes*.

| parameter (host name) | byte range | corpus bass med (p25–p75) | ALL baseline (p25/med) | recommended | notes |
|---|---|---|---|---|---|
| `Ch 1 Filter 1 Cutoff` | 0..127 | **31** (14–43) | 32/58 | **31** | the one decisive bass parameter (§1.2 H1); 27 below the ALL median |
| `Ch 1 Filter 1 Envelope Amount` | 0..127 | **78** (42–92) | 0/52 | **78** | §1.2 H2; polarity is separate and is `1` for 100% of bass |
| `Ch 1 Filter 1 Resonance` | 0..127 | **0** (0–22) | 0/6 | **0–22** | threshold, not a character control here; >22 is **extrapolation** (justified only by acid, whose median is 64) |
| `Ch 1 Filter Envelope/Decay` | 0..127 | **41** (30–47) | 35/46 | **41** | §1.2 H9 — the chirp |
| `Ch 1 Filter Envelope/Release` | 0..127 | **127** (53–127) | 54/108 | **53–127** | slow filter return; harmless because the amp release (13) shuts the voice first |
| `Ch 1 Filter Envelope/Sustain` | 0..127 | **0** (0–35) | 0/0 | **0** | env returns to the cutoff floor — the rolling-bass behaviour |
| `Ch 1 Filter Envelope Attack` | 0..127 | **0** (0–0) | 0/0 | **0** | every bass patch in the corpus is at 0; any non-zero is **extrapolation** |
| `Ch 1 Amplifier Envelope/Decay` | 0..127 | **83** (48–127) | 59/127 | **83** | §1.2 H9's comparator |
| `Ch 1 Amplifier Envelope/Sustain` | 0..127 | **110** (0–127) | 85/127 | **110** *or* **0** | **bimodal** (p25 = 0): ~a quarter gated at 0, the upper half sustained. Both are in-corpus; pick deliberately (§1.2 H8) |
| `Ch 1 Amplifier Envelope/Release` | 0..127 | **13** (4–26) | 4/37 | **4–26** | §1.2 H8 — short, and shorter than the corpus norm |
| `Ch 1 Amplifier Envelope/Attack` | 0..127 | **0** (0–7) | 0/10 | **0** | |
| `Ch 1 Filter 1 Keyfollow` | 0..127 | **64** (64–64) | 64/64 | **64** | 64 = bipolar centre = no tracking (§1.2 H7) |
| `Ch 1 Sub Oscillator Volume` | 0..127 | **45** (0–92) | 0/0 | **45–92** | largest role-vs-baseline divergence in the table (§1.2 H3) |
| `Ch 1 Oscillator Punch Intensity` | 0..127 | **52** (0–64) | 0/64 | **52–64** | corpus is *below* the default 64; programming ≥64 is **extrapolation** (§1.2 H6) |
| `Ch 1 Distortion Intensity` | 0..127 | **18** (0–39) | 0/0 | 18 | bass runs *more* than baseline, not less (§1.2 H11); 0 is **extrapolation** away from the corpus |
| `Ch 1 Filter 1 Mode` | discrete 0..7 | byte **0 = 66%**, 7 = 15%, 6 = 9%, 5 = 3%, 1/2/3/4 ≤3% | 0 = 77% | byte **0** | no byte→mode-name legend in the artifacts, so "0 = LP" is asserted nowhere (§1.2 H12) |
| `Ch 1 Filter 2 Cutoff` | 0..127 | **64** (37–64) | 40/64 | **64** | parked at baseline while F1 moves — §1.2 H14 |
| `Ch 1 Filter Balance` | 0..127 | **64** (0–64) | 40/64 | **64** | 64 = centre = no skew to either filter; §1.2 H14 |
| `Ch 1 Filter Routing` | discrete 0..3 | 0 = 53%, 1 = 30%, 2 = 12%, 3 = 4% | 0 = 58%, 1 = 25% | 0 or 1 | |
| `Ch 1 Filter 2 Mode` | discrete 0..3 | 0 = 80%, 2 = 12%, 1 = 8% | 0 = 74%, 1 = 12%, 2 = 9%, 3 = 5% | 0 | |
| `Ch 1 Oscillator 1 Waveform Shape` | 0..127 | **64** (17–64) | 20/64 | **64** | consistency evidence for §1.2 H13, not proof of the model |
| `Ch 1 Noise Volume` | 0..127 | **0** (0–0) | 0/0 | **0** | zero across the whole bass role; noise is a perc/fx parameter (perc p75 = 107) |
| `Ch 1 Oscillator 3 Volume` | 0..127 | **64** (20–64) | 61/64 | 64 | |
| `Ch 1 Filter Envelope/Sustain` time-base pair: `Ch 1 Filter Envelope/Sustain` × `… Sustain Time` | 0..127 | Sustain Time **64** (64–64) | 64/64 | **64** | both envelope `Sustain Time` rows (`Filter` and `Amplifier`) are pinned at exactly 64 in the corpus — nobody moves them |
| `Ch 1 Patch Volume` | 0..127 | **116** (103–127) | 100/103 | **116** | bass patches are printed hotter than the corpus norm |
| `Ch 1 Oscillator 2 Detune In Semitones` | 0..127 | **28** (0–46) | 15/32 | 28 | near-baseline; bass is not a detune role (compare pad p25 = 20) |
| `Ch 1 Saturation Curve` | 0..127 | **0** (0–6) | 0/0 | 0 | the saturation *amount* is unmapped — §1.2 H10 |

**Not in the artifacts** (so no row exists): a sub-shape waveform byte
(`Sub Shape`), any oscillator-model byte (`Osc1 Model`), and a saturation-amount
byte. See §1.2 H4, H10, H13.

---

## 1.2 Hypothesis verdicts — bass (hypotheses 1–14 of `psy-va-technique-notes.md` §2)

Each verdict is judged against **that hypothesis's own *"refuted if"* clause**, and
quotes the numbers that decide it. Values are corpus bytes (0..127).

**H1 — Cutoff far below default. → CONFIRMED.**
Criterion: default 127 = fully open; bass should sit in the **lower third** (bytes
0–42) with the median well below the mid-point. Measured: bass `Cutoff` median
**31** (p25 14, p75 43); ALL median **58**. 31 is inside the lower third, and 31 ≪
the 64 mid-point. The refutation clause ("not distinguishable from the
factory-default cluster at 127") fails hard: p75 is 43, so ≥75% of bass patches
are below a third-open. Bass sits **27 bytes below** the ALL median, so this is
role-specific, not a corpus-wide habit.

**H2 — Filter env amount engaged and high. → CONFIRMED** (direction; magnitude
partly). Criterion: default `Filter1 Env Amt` = 0; refuted if bass "clusters at or
near 0". Measured: bass median **78** (p25 42, p75 92) vs ALL median **52** (ALL
p25 = 0). A median of 78 is 78 bytes away from the default — no clustering at 0.
Corroboration: `Filter1 Env Polarity` is **1 for 100% of bass** (n = 92), i.e. the
env is positively polarised everywhere. Honest caveat: the clause "concentrated in
the **upper third**" (bytes ≥ 85) is met only at p75 = **92**; the *median* 78
lands in the middle-upper band, so "engaged, and above baseline" is confirmed
while "concentrated in the upper third" is true only of the upper quartile.

**H3 — Sub oscillator engaged. → CONFIRMED.** Criterion: default
`Suboscillator Volume` = 0; a **majority** of bass patches should be **> 0**;
refuted if indistinguishable from the default-zero cluster. Measured: bass median
**45** (p25 **0**, p75 92) vs ALL median **0** (p25/med = 0/0). A median of 45
means ≥50% of bass patches sit at ≥45, so a majority have the sub engaged. Caveat:
p25 = 0, so ≥25% have no sub at all — "a majority", not "nearly all". This is the
largest role-vs-baseline divergence in the whole bass table.

**H4 — Sub shape is Square, not Triangle. → INCONCLUSIVE.** The artifacts contain
**no `Sub Shape` row**: the only sub-related parameter in `analysis.txt` is
`Suboscillator Volume` (line 65). Grepping the whole analysis for sub-shape,
waveform-shape-of-sub and `Sub Shape` yields nothing, so the Square-vs-Triangle
ratio the hypothesis needs **cannot be computed from `.tmp_virus/`**. Not
"50/50" and not "favours Triangle" — simply unmeasured.

**H5 — Unison off on bass, and off *unlike* lead. → INCONCLUSIVE.** The parameter
is recorded for almost nobody: `Unison Mode` has **n = 5** for bass and **n = 1**
for lead (ALL n = 323 of 2818) — 5/92 = 5% coverage of the bass role and 1/107 =
1% of lead, because the byte is absent from most stored layouts. The recorded
sample is bass `0: 80%, 7: 20%` (n = 5) and lead `15: 100%` (n = 1). The bass
sample is *consistent with* unison-off, but the hypothesis's actual test is the
**contrast** against lead, and n = 5 vs n = 1 cannot establish a contrast.

**H6 — Punch at or above default. → REFUTED.** Criterion: default
`Punch Intensity` = 64; refuted if the median is **below 64**. Measured: bass
median **52** (p25 0, p75 64); ALL 0/64. 52 < 64, so the claim fails at its own
median. The bass role is *below* the corpus norm (ALL median 64), the opposite of
the hypothesis' direction; only the upper quartile reaches the default.

**H7 — No filter keytracking. → CONFIRMED.** Criterion: default
`Filter1 Keyfollow` = 64 = bipolar centre = no tracking; refuted if bass
"systematically deviates toward the positive (tracking) side". Measured: bass
min 0 / p25 **64** / med **64** / p75 **64** — byte-for-byte identical to ALL
(64/64). The median *and* p75 are exactly the no-tracking centre. Note the
minimum of 0: a minority tracks **negatively**, which is deviation away from the
positive side, not toward it. Per the hypothesis, this refutation *not*
occurring is informative but contested (the microQ manual recommends the
opposite for bass).

**H8 — Amp env: short release *and* zero sustain. → REFUTED** (as a
conjunction; the release half holds). Release clause: bass `Amp Env Release`
median **13** (4–26) vs ALL median **37** (4–37) — near the bottom of 0..127, so
"short/at minimum" is met and is **shorter than the corpus norm**. Sustain clause:
bass `Amp Env Sustain` median **110** (p25 **0**, p75 127) vs ALL 85/127 — the
median patch is *sustained*, not zero. The distribution is bimodal: p25 = 0 means
roughly a quarter of bass patches do sit at sustain 0, but they are the minority
mode. So H8's headline ("zero sustain") is refuted by 110 while its release claim
stands at 13.

**H9 — Filter decay short relative to amp decay. → CONFIRMED.** Criterion:
refuted if filter decay ≥ amp decay in the median patch. Measured: bass
`Filter Env Decay` median **41** (30–47) vs bass `Amp Env Decay` median **83**
(48–127). 41 < 83 by **42 bytes**, so the chirp finishes inside the note. The
quartiles do not overlap either (filter p75 = 47 < amp p25 = 48).

**H10 — Osc Volume ≤ 0 (unity or attenuated). → INCONCLUSIVE / BLOCKED.** Per the
name authority, the vocabulary's `Osc Volume` / `Osc Volume / Saturation` has
**no literal host match**: `Ch 1 Oscillator Section Volume` is the only candidate
and is explicitly **unconfirmed**, and the only saturation-*type* name is
`Ch 1 Voice Saturation Type` — no host name contains "Saturation" as an amount.
The corpus row `Osc Mainvolume` (bass median **64**, 64–101; ALL 64/64) is a
0..127 **level**, not the −64…+63 bipolar saturation amount the hypothesis
describes, so it cannot stand in. Therefore the ≤0 / >0 split is **not in the
artifacts** and this hypothesis cannot be settled by host writes at all — it must
go through ROM-program selection.

**H11 — Patch distortion off on bass. → REFUTED.** Criterion: `Distortion
Intensity` default 0, minimal among bass patches. Measured: bass `Distortion
Intensity` median **18** (0–39) vs ALL median **0** (p25/med 0/0) — bass carries
**more** distortion than the corpus baseline, not less. Supporting row:
`Distortion Curve` bass median **3** (0–12), also non-zero. For contrast, acid
sits at median **75**. Note also that the hypothesis names a `Patch
Distortion/Mix` parameter: **no such row exists in the artifacts** — the only
distortion rows are `Distortion Curve` and `Distortion Intensity`, so that clause
is untestable. The related `Saturation Curve` is near-zero (bass median 0, p25–p75
0–6), i.e. small but not absent.

**H12 — Filter mode is plain lowpass. → INCONCLUSIVE.** The artifacts hold the
byte distribution but **no byte→mode-name legend**. Measured: bass `Filter1 Mode`
is byte **0 = 66%**, 7 = 15%, 6 = 9%, 5 = 3%, 1 = 3%, 2 = 1%, 4 = 1%, 3 = 1%
(n = 92); ALL 0 = 77%; `Filter2 Mode` bass 0 = 80%. A search of the artifact
vocabulary files (`layout_names.txt`, `modenums.json`, `roles.txt`) for
Lowpass/Bandpass/Analog/4-pole names returns nothing. So "0 = LP" cannot be
asserted from these artifacts, and "Analog 1..4 Pole is rare" cannot be
evaluated. What *is* established: one discrete value holds two thirds of the bass
role and the remaining third is spread over 7 values — whether those are the
Analog modes is unknown here.

**H13 — Oscillator mode is Classic on bass (joint with the sub). → INCONCLUSIVE.**
No oscillator-model row exists anywhere in the analysis (no `Osc1 Model` /
`Oscillator 1 Model`), so the required joint distribution
sub-engaged ∧ Classic **cannot be computed**. Consistent-but-not-decisive
evidence: `Osc1 Shape` for bass is 64 with **p25 = 17**, so ≥75% of bass patches
carry a non-zero Shape byte, and a Shape value is only meaningful in Classic mode;
H3 already shows the sub engaged for a majority. That is compatible with the
hypothesis but does not establish the model. The *refuting* case the hypothesis
names — a sub engaged on a HyperSaw patch — is undetectable in these artifacts.

**H14 — Filter 2 is not doing melodic work on bass. → CONFIRMED** on its second
branch; **first branch REFUTED.** Clause A ("`Filter Balance` skewed to filter
1"): bass `Filter Balance` median is **exactly 64** (p25 0, p75 64) — and 64 is
the no-skew centre in this corpus's own convention (ALL p25/med = 40/64) — so the
balance is **not** skewed to filter 1; this clause is refuted. Clause B ("or
Filter 2 cutoff high"): `Cutoff2` bass median **64** (37–64) against `Cutoff` bass
median **31** (14–43) — filter 2 sits **33 bytes** (≈26% of the 0..127 span) above
filter 1, and filter 2's own resonance is 0 (0–19). Filter 2 is therefore parked
at the neutral/baseline position while filter 1 does the movement, which supports
the hypothesis. Caveat: "high" has no absolute Hz scale in the artifacts — the
argument is **relative** (F2 ≫ F1, F2 = baseline, F2 resonance 0), not absolute.

### Verdict summary

| # | Hypothesis (short) | Verdict |
|---|---|---|
| 1 | Cutoff far below default | **CONFIRMED** |
| 2 | Filter env amount engaged/high | **CONFIRMED** (direction; "upper third" only at p75) |
| 3 | Sub oscillator engaged | **CONFIRMED** (majority, not nearly-all) |
| 4 | Sub shape Square not Triangle | INCONCLUSIVE — no sub-shape byte |
| 5 | Unison off on bass vs lead | INCONCLUSIVE — n = 5 bass / n = 1 lead |
| 6 | Punch ≥ 64 | **REFUTED** — median 52 |
| 7 | No filter keytracking | **CONFIRMED** |
| 8 | Short release **and** zero sustain | **REFUTED** — sustain median 110 (release half holds) |
| 9 | Filter decay < amp decay | **CONFIRMED** — 41 < 83 |
| 10 | Osc Volume ≤ 0 | INCONCLUSIVE — no confirmed host name |
| 11 | Distortion off | **REFUTED** — bass median 18 vs ALL 0 |
| 12 | Filter mode is LP | INCONCLUSIVE — no byte→mode legend |
| 13 | Osc mode Classic ∧ sub | INCONCLUSIVE — no oscillator-model byte |
| 14 | Filter 2 not melodic | **CONFIRMED** (branch B; branch A refuted) |

Score: **6 confirmed, 3 refuted, 5 inconclusive.** The three refutations are all
*quantitative* — bass patches are less punchy, more distorted, and more sustained
than the technique notes predicted.

---

## 1.3 Example bass patches

The artifacts **do** carry a per-role patch list, and it is name-derived:
`.tmp_virus/analysis.txt` §`EXAMPLE PATCHES PER ROLE` prints a `--- bass (92)`
block as `name | file | format`. The first six entries:

| # | Patch name | Source file | Format |
|---|---|---|---|
| 1 | `ArtBass Pl` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| 2 | `BA Animal` | `AZS Dream State Vol.2.mid` | tibank |
| 3 | `BA Click` | `AZS Dream State Vol.2.mid` | tibank |
| 4 | `BA Deep Un` | `AZS Dream State Vol.2.mid` | tibank |
| 5 | `BA GDSQR` | `AZS Dream State Vol.2.mid` | tibank |
| 6 | `BA LowDTNE` | `AZS Dream State Vol.2.mid` | tibank |

**The sidecars are not usable as a bass patch list — and this is worth stating
plainly, because it looks like they are.** `.tmp_virus/side_stab/` holds **46
role-scoped sidecar copies**, and every one of them carries
`roleCheck.role: "stab"` — it does **not** name-match the bass role. Worse, the
role label is **folder-scoped, not per-patch**: the identical sidecar path
`Virus TDM format\1979 Gangs.virus.json` appears in `side_bass/` with
`"role": "bass"` and in `side_stab/` with `"role": "stab"`. A sidecar's `role`
therefore records which copy-folder it was placed in, not how the patch was
classified. Two further limits:

- Each sidecar is one `hdaw.virus.patch.v1` document for **one** patch
  (`name`, `program`, `bank`, `mappedParams`, `unmapped`, `roleCheck`), and its
  `mappedParams` are keyed to the **internal `sub_synth`** parameter vocabulary
  (`osc1_wave`, `amp_attack`, `filter_env_amount`, `cutoff`, `drive`, …) — *not*
  to Virus host names. Its `unmapped` list names the Virus-side things it drops
  (`mod_matrix`, `keytrack`, `fx_delay`, `noise_level`, …).
- The `roleCheck` inside is explicitly **not audio**: the sidecar's own note reads
  *"role check computed from mapped sub_synth params as pseudo-measurements (patch
  bytes, not audio analysis)"*. The exemplar is a failure by that check — the
  `AZS Dream State Vol.2.mid` sidecar is `name: "AR 3Body"`, `program: 0`,
  `verdict: "fail"`, with `centroid 9339.4649` outside the bass range `[60, 250]`
  and `mel_low 0.122` below the `0.35` floor.

So: use §`EXAMPLE PATCHES PER ROLE` (name-derived) for patch names, and the
sidecars only for byte-level cross-checks of a single patch.

## 1.4 How to apply via MCP

Agent transport is the **HTTP twin**, never a per-call engine
([`AGENTS.md`](../AGENTS.md) "Agent transport"):

```powershell
python scripts/hdaw_mcp_http.py tools
python scripts/hdaw_mcp_http.py call list_fx_params '{"trackId":1,"slotIndex":0}' --timeout 300
```

`list_fx_params` is the confirmation step: a live `OsTIrus.clap` slot must report
**6939** parameters, with `Ch 1 `-prefixed names.

**Step 1 — select a ROM patch (CC0 + program change).** `load_virus_preset` schema
(`src/mcp/McpTools_FxSlot.cpp:628`): `trackId`/`trackID`, `slotIndex`,
`bank` 0–7 (banks A–H singles), `program` 0–127, `channel` 1–16; required
`slotIndex`, `bank`, `program`. It is described as CC0 bank select + program
change "like the hardware front panel", applied to the **live** plugin instance.

```powershell
python scripts/hdaw_mcp_http.py call load_virus_preset '{"trackId":1,"slotIndex":0,"bank":0,"program":12,"channel":1}'
python scripts/hdaw_mcp_http.py call get_fx_capture_status '{"trackId":1,"slotIndex":0}'
```

Poll `get_fx_capture_status` (`{status, stateBytes, capturedAtMs, hasPluginState}`)
until `status` leaves `pending` — the realtime capture completes *after* the load
call returns, so do not trust an immediate `capturedToTree=0`.

**Step 2 — named parameter writes.** `set_fx_param` takes `trackId`/`trackID`,
`slotIndex`, `paramName` (the name `list_fx_params` returns; case-insensitive,
and it wins over `paramIndex` when both are given) and a **normalized 0..1**
`value`. For plugin slots the write is live **and** persisted as a slot-level
offline-replay override (returned as `ok overrides=N`), so it reaches
`export_audio` / `audition_plugin` / `verify_part` renders and save/load;
`list_fx_params` then marks those names `overridden`, and
`clear_fx_param_overrides` removes them.

Byte → value is `byte / 127`. Applying the §1.1 recommendations:

```powershell
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Filter 1 Cutoff","value":0.244}'
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Filter 1 Envelope Amount","value":0.614}'
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Sub Oscillator Volume","value":0.354}'
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Filter Envelope/Decay","value":0.323}'
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Amplifier Envelope/Release","value":0.102}'
python scripts/hdaw_mcp_http.py call set_fx_param '{"trackId":1,"slotIndex":0,"paramName":"Ch 1 Filter 1 Keyfollow","value":0.504}'
```

(0.244 = 31/127, 0.614 = 78/127, 0.354 = 45/127, 0.323 = 41/127, 0.102 = 13/127,
0.504 = 64/127 — the corpus medians.) **Mind the separators**: `Ch 1 Filter
Envelope Attack` is space-separated while `Ch 1 Filter Envelope/Decay|Sustain|
Release|Sustain Slope` use `/`, and the amp block is **`Amplifier`**, not `Amp`
(a `\bAmp\b` search finds nothing and falsely suggests it is unexposed).

**Step 3 — the routes that do *not* work.** Three traps:

1. **Plugin slots ignore injected SysEx.** A proxy-isolated plugin slot has no MIDI
   input path, so `send_fx_midi` / raw dump injection cannot select a Virus ROM
   program there. ROM selection travels as **CC0 + PC** (`load_virus_preset`, or
   `apply_preset` with `program`).
2. **`apply_preset` sends an Access SysEx header to the internal `sub_synth`.**
   Its dispatch (`src/mcp/McpTools_FxSlot.cpp:877`) reads the **slot's** fxType +
   pluginId and the file header: a **Gearmulator Virus slot + `program`** (optional
   `bank`, **no `filePath`**) is a ROM preset via CC0+PC, but **`Internal sub_synth
   slot` + Virus dump `F0 00 20 33`** becomes `loadVirusPatch` (`voiceIndex` for TI
   banks). So an Access `.syx` aimed at `apply_preset` is a `sub_synth` load, not an
   OsTIrus load — for the plugin slot use the `program` form.
3. **Realtime mutations are not undoable.** SysEx/CC/PC routes are outside the undo
   system; capture via project save.

## 1.5 A falsifiable prediction

`mix_report` (`src/mcp/McpTools_AudioRead.cpp:463`) takes `filePath` (**required**),
plus `bpm`, `fromPlan`, `wait`, `dropBuildRatio`, `targets`, and `sections` (an
array of `{name, start, end}` in **seconds**). Its bands are fixed in Hz —
**sub 40–110, bass 90–300, body 300–2000, high > 6000** — and `bandEnergy` is mean
power per FFT window as **linear amplitude², not dB**, so compare **ratios, never
dB differences**. `kickProminence = E(35–110)/(E(35–110)+E(120–320))` ∈ 0..1;
`pumpDepth` is `(max−min)/mean` of per-beat RMS over sections with ≥8 beats.

The front-matter measurement is the reference: with the sub engaged, cutoff
`0.33 → 0.12` moved the rendered **high band 1579.9 → 57.2** and the **bass band
5.9 → 490.9** — derived: a **27.6×** high-band reduction, an **83.2×** bass-band
increase, and a post-change **bass:high ratio of 8.58:1**.

- **P1 (ordering).** A patch built to §1.1 (cutoff 0.244, sub volume 0.354, F1 env
  amount 0.614, filter decay 0.323 < amp decay 0.654, amp release 0.102) must
  render **bass-band energy > high-band energy** over the bass window, matching the
  measured reference ordering. **Falsified if** `mix_report` returns high-band
  energy above bass-band energy for that window.
- **P2 (`verify_part` pair).** `verify_part` on that track must report
  `audible=true` (solo peak > −80 dBFS), `nonClipping=true` (mix peak < 1.0) and
  `bandsPresent=true`. **Falsified by** `audible=false`. Read it carefully: the
  front matter establishes that a **bypassed** slot renders digital silence, so an
  `audible=false` with the slot engaged is evidence against the **write path**,
  not against this recipe (lessons 25 → prove audibility before A/B-ing).
- **P3 (the cutoff write is load-bearing, and the ordering inverts).** Re-running
  the identical patch with `Ch 1 Filter 1 Cutoff` at **0.890** should raise the
  high band sharply and drop the bass band, inverting P1's ordering. If the bands
  do **not** move, the `set_fx_param` write did not land — a control for P1 rather
  than a claim about the sound. (0.890 is `113/127`, the `cutoff` value stored in
  the `AR 3Body` sidecar.)

```powershell
python scripts/hdaw_mcp_http.py call verify_part '{"trackIndex":0,"startBeat":0,"endBeat":16,"soloOnly":false}'
python scripts/hdaw_mcp_http.py call mix_report  '{"filePath":"<render.wav>","fromPlan":true}'
```

`verify_part` (`src/mcp/McpTools_CompositionInstrument.cpp:291`) takes
`trackIndex` (required, ≥0), `windowSeconds` (≥0.1, default 4.0), `startBeat` /
`endBeat` (**beats**; `endBeat` > `startBeat`; the window is `[startBeat,endBeat)`),
and `soloOnly` (default `false` — it halves cost by skipping the full-mix render,
after which the mix metrics report `mixMeasured=false`, so never read zeroed mix
fields as measured). It returns `soloRms`, `soloPeak`, `mixRms`, `mixPeak`,
`nonClipping`, `audible`, `bandsPresent`.

---

# SECTION 2 — ACID STAB (Access Virus via OsTIrus / Osirus)

## 2.0 How to read this section's numbers

Corpus role `acid`, **n = 26**. Two column conventions, both taken from the
artifacts (`analysis.txt`, deduped corpus):

- **byte range** is the observed `min / p25 / med / p75`. The artifacts carry **no
  maximum column** — a max is never quoted, in either direction.
- **ALL baseline** is the corpus-wide **`p25/med`** pair. The ALL column is
  `p25/med`, not `p25–p75` — do not read the second ALL number as a p75.
- Normalized recommendation = **byte / 127** (the front-matter round-trip rule).
- Anything I recommend outside the corpus p25–p75 is labelled **extrapolation**.

The technique notes state hypotheses 15–21 in `microQ`/`Vavra` vocabulary
(`F1Resonance`, `F1EnvMod`, `F1KeyTrack`, `F1Drive`, `GlideEnable`, `VoiceMode`).
Each verdict below is judged **against its own *"refuted if"* clause** using the
Virus corpus row that carries the same control. Where no such row exists in the
artifacts, the verdict is `INCONCLUSIVE` — never an invented number.

## 2.1 Parameter table (acid, n = 26)

| parameter | byte range (min/p25/med/p75) | corpus median (p25–p75) | ALL (p25/med) | recommended | notes |
|---|---|---|---|---|---|
| `Ch 1 Filter 1 Cutoff` | 11 / 41 / 47 / 50 | 47 (41–50) | 32/58 | 47 → 0.370 | corpus row `Cutoff`. Acid's median sits **below** the corpus median 58 |
| `Ch 1 Filter 1 Resonance` | 0 / 49 / 64 / 79 | 64 (49–79) | 0/6 | 64 → 0.504 | corpus row `Filter1 Resonance`; 10.7× ALL med. >79 is **extrapolation** |
| `Ch 1 Filter 1 Envelope Amount` | 0 / 29 / 37 / 56 | 37 (29–56) | 0/52 | 37 → 0.291 | host name is **not** "Env Amt"; polarity is a separate param |
| `Ch 1 Filter Envelope/Decay` | 15 / 24 / 46 / 59 | 46 (24–59) | 35/46 | 46 → 0.362 | median identical to ALL |
| `Ch 1 Filter Envelope/Release` | 0 / 0 / 127 / 127 | 127 (0–127) | 54/108 | 127 → 1.000 | bimodal; med = device max |
| `Ch 1 Amplifier Envelope/Attack` | 0 / 0 / 0 / 0 | 0 (0–0) | 0/10 | 0 → 0.000 | whole IQR is instant attack |
| `Ch 1 Amplifier Envelope/Decay` | 0 / 36 / 67 / 127 | 67 (36–127) | 59/127 | 67 → 0.528 | |
| `Ch 1 Amplifier Envelope/Sustain` | 0 / 0 / 126 / 127 | 126 (0–127) | 85/127 | 126 → 0.992 | bimodal — p25 = 0 |
| `Ch 1 Amplifier Envelope/Release` | 0 / 0 / 4 / 11 | 4 (0–11) | 4/37 | 4 → 0.031 | shortest release of any melodic role |
| `Ch 1 Oscillator 1 Waveform Shape` | 29 / 64 / 80 / 127 | 80 (64–127) | 20/64 | 80 → 0.630 | corpus row `Osc1 Shape` |
| `Ch 1 Oscillator 2 Waveform Shape` | 0 / 64 / 64 / 127 | 64 (64–127) | 18/64 | 64 → 0.504 | |
| `Ch 1 Oscillator 2 Sync` | 0 / 0 / 0 / 127 | 0 (0–127) | 0/0 | 0 → 0.000 | categorical: **27%** of acid patches sit at 127 vs **7%** ALL — the single most role-distinctive byte here |
| `Ch 1 Oscillator Punch Intensity` | 0 / 64 / 64 / 64 | 64 (64–64) | 0/64 | 64 → 0.504 | corpus row `Punch Intensity`; p25 is already 64 |
| `Ch 1 Sub Oscillator Volume` | 0 / 0 / 0 / 64 | 0 (0–64) | 0/0 | 0 → 0.000 | p75 = 64: half of acid patches engage a sub |
| `Ch 1 LFO 3 Rate` | 29 / 48 / 92 / 94 | 92 (48–94) | 65/94 | 92 → 0.724 | high median; **no cycles/beat decode** in the artifacts |
| `Saturation Curve` | 0 / 0 / 4 / 6 | 4 (0–6) | 0/0 | 4 → 0.031 | **host name OPEN** — the param map names no saturation-*amount* param (`Ch 1 Oscillator Section Volume` is an unconfirmed candidate). Categorical: **73%** of acid patches non-zero vs **32%** ALL |
| `Portamento Time` | 0 / 0 / 18 / 27 | 18 (0–27) | 0/0 | 18 → 0.142 | **no confirmed host name** in the param map. Acid is the only melodic role with a non-zero p75 here (bass p75 = 4, lead 15, pluck 0) |
| `Oscillator Balance` | 0 / 0 / 95 / 127 | 95 (0–127) | 54/64 | 95 → 0.748 | corpus row `Osc Balance`; no host name in the param map's block list |

`Ch 1 Filter 1 Keyfollow` is deliberately **not** in this table — it carries no role
signal at all (see verdict 18).

## 2.2 Hypothesis verdicts 15–21

**15 — REFUTED.** The claim is the microQ "strong boost" resonance band `80–113`.
Virus `Filter1 Resonance` on acid is `min 0 / p25 49 / med 64 / p75 79`: **the entire
interquartile range is below 80**. The *refuted-if* clause ("acid clusters in the low
0–80 band") is exactly what the data shows. The *direction* survives as a secondary
observation — median 64 against a corpus ALL median of **6** — but the band does not.

**16 — REFUTED.** Two refutable halves, both fail. (a) "Upper third of 0…127" means
≥ 85; measured acid `Filter1 Env Amt` is `med 37, p75 56` — **p75 is 29 bytes short of
the lower bound of the claimed band**, and acid (37) is *below* the corpus ALL median
(52). (b) "a non-zero share use negative values": acid
`Filter1 Envelope Polarity` is **1 in 100% of 26 patches** (ALL: 1:98%, 0:2%) — there
are **zero** inverted-sweep acid patches in the corpus. (The artifacts do not say
which polarity byte means "negative"; the absence of any departure from the corpus
polarity is the measured fact.)

**17 — INCONCLUSIVE.** The routing claim (an envelope → filter slot in the mod matrix,
distinct from the amp envelope) cannot be decoded: the artifacts give matrix *source*
and *destination* **bytes with no name table**. What can be measured leans against it:
active matrix slots per acid patch are `0:7%, 1:15%, 2:26%, 3:15%, 4:11%, 5:15%, 6:7%`
— i.e. **7% of acid patches have zero active slots**, versus **0% of bass, 0% of lead
and 0% of pluck** (each role's lowest bucket is `1:`). So the *"refuted if matrix usage
is indistinguishable"* clause is close to firing, but "indistinguishable" is a
routing-level statement the bytes cannot settle. Reason recorded, verdict withheld.

**18 — REFUTED.** *Refuted if* acid and bass keytrack distributions coincide. Virus
`Filter1 Keyfollow`: bass `0/64/64/64`, acid `37/64/64/64`. **p25, median and p75 are
identical (64/64/64)** — they differ only in the minimum (0 vs 37). The distributions
coincide at every quartile, and both sit on the corpus ALL median of 64, so keytrack is
not an acid discriminator in this corpus.

**19 — INCONCLUSIVE.** `Portamento Time` on acid is `0/0/18/27`: **at least half of
acid patches carry zero glide** (median 0), with the upper quartile reaching 18–27 of
127. The claim's second half — a *legato-ish* glide mode — has no counterpart row: the
artifacts contain no `GlideEnable`/`GlideMode` (those are microQ names), and the
`Key Mode` byte distribution (`0:31%, 4:27%, 1:23%, 3:12%, 2:8%`) is not name-decoded.
The claim has no *refuted-if* clause, and neither half can be settled; recorded as
INCONCLUSIVE rather than scored on a guess.

**20 — CONFIRMED (on the corpus's saturation proxy).** The named parameter (`F1Drive`)
is microQ and **is not in the artifacts**, so the verdict rests on the closest measured
control, `Saturation Curve`: acid is non-zero in **73%** of patches (categorical
`0:27%, 4:27%, 7:12%, 3:12%, 6:8%, 10:8%`) against **32%** corpus-wide, and the numeric
row is `med 4 (0–6)` against an ALL median of 0 — with the top acid value `4` at 27%
versus 3% corpus-wide. Elevated drive-type saturation on acid/stab is therefore
supported; the *name substitution* is the caveat, and the map leaves the
saturation-**amount** host name OPEN.

**21 — INCONCLUSIVE.** `VoiceMode` has no row in the artifacts. The nearest control is
`Key Mode`: acid is **69% non-zero** (`0:31%`) against **21%** non-zero corpus-wide
(`0:79%`), which would support a mono/legato reading *if* byte 0 were the poly default —
that mapping is not in the artifacts, so it is not asserted. `UnisonoCount` has no row:
the corpus `Unison Mode` row exists for only **15 of 26** acid patches
(`0:67%, 1:13%, 2:13%, 11:7%`) and gives no unison *count*. INCONCLUSIVE, as the
hypothesis's own caveat predicts (its microQ params are mostly not host-public).

## 2.3 Example patches (from the artifacts' acid list, n = 26)

| name | file | format |
|---|---|---|
| `303Sync MS` | `Access_Virus_TI/wc_olo_garb_virus_ti-a01.syx` | tibank |
| `ACID WARS` | `Virus TDM format/ACID WARS` | tdm |
| `ACID WARS` | `Nyel Myel - Quadrivium Soundset vol1.mid` | bcsingle |
| `Acid Fight` | `Virus TDM format/Acid Fight` | tdm |
| `Aciddance` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| `Acidia NK` | `Best Analog& Vintage Sounds(B,C Version).mid` | bcsingle |

Two of the six come from the *PsyLoad … Psytrance Soundset* and *Quadrivium* banks;
neither is labelled genre-tagged in the corpus (front matter: no genre labels), so read
these as acid-*named* patches, not as certified psy targets.

## 2.4 Apply via MCP

Reuse the **§1.4 contract** verbatim — do not re-derive it here. In short: resolve the
slot with `list_fx_params {"trackId":…,"slotIndex":0}` (a live OsTIrus slot reports
**6939** params; `Ch 1` has 444, `Ch 2…16` have 433 each), then write **named** values
with `set_fx_param`, names **`Ch <n> `-prefixed** exactly as in the tables above
(`Ch 1 Filter 1 Envelope Amount`, not "Env Amt"), values **normalized 0..1 = byte/127**.
Named writes have been measured to persist into offline renders (cutoff 0.33 → 0.12 with
the sub engaged moved the rendered high band 1579.9 → 57.2 and the bass band
5.9 → 490.9), and a **bypassed slot renders digital silence** — so a silent acid render
is an audibility failure, not a quiet patch.

Two names in §2.1 are **not** writable as-is and must not be guessed: `Saturation Curve`
(no confirmed host name) and `Portamento Time` (absent from the map). Confirm both with
`list_fx_params` before a recipe depends on them.

## 2.5 Falsifiable prediction

- **A1 (resonance does the work, not env amount).** A patch written to §2.1
  (`Ch 1 Filter 1 Resonance` 64 → 0.504, `Ch 1 Filter 1 Envelope Amount` 37 → 0.291,
  `Ch 1 Amplifier Envelope/Release` 4, `Ch 1 Oscillator 2 Sync` 0) must render with
  **more energy at the filter's resonant peak than the same patch with resonance at 0**,
  on a `verify_part` window over the acid phrase. **Falsified if** the resonance-0 render
  is *not* below the resonance-64 render in `soloPeak`/`soloRms` — that would mean the
  write did not land (§1.4) rather than that the recipe is wrong.
- **A2 (the diagnosis is testable).** Swap only
  `Ch 1 Filter 1 Envelope Amount` to the corpus ALL median **52** (0.409) and the patch
  should stay audible and move *further* from the acid-band ordering, since acid's own
  median (37) is below the corpus median. **Falsified if** 52 and 37 render identically,
  which would prove the env-amount write is inert on this build (front-matter lesson:
  prove audibility before A/B-ing).

---

# SECTION 3 — LEAD (Access Virus via OsTIrus / Osirus)

Same conventions as §2.0: **byte range** is `min/p25/med/p75` with **no max column** in
the artifacts; **ALL baseline** is the corpus-wide **`p25/med`** pair; recommended
normalized = byte/127; anything outside the corpus p25–p75 is **extrapolation**.
Corpus role `lead`, **n = 107**.

## 3.1 Parameter table (lead, n = 107)

| parameter | byte range (min/p25/med/p75) | corpus median (p25–p75) | ALL (p25/med) | recommended | notes |
|---|---|---|---|---|---|
| `Ch 1 Filter 1 Cutoff` | 0 / 51 / 102 / 127 | 102 (51–127) | 32/58 | 102 → 0.803 | highest cutoff median of any melodic role (acid 47, pluck 51, bass 31) |
| `Ch 1 Filter 1 Resonance` | 0 / 0 / 0 / 26 | 0 (0–26) | 0/6 | 0 → 0.000 | corpus row `Filter1 Resonance`. Leads are **not** the resonant role here — contrast acid's median 64. >26 is **extrapolation** |
| `Ch 1 Filter 1 Envelope Amount` | 0 / 0 / 42 / 66 | 42 (0–66) | 0/52 | 42 → 0.331 | p25 = 0: a quarter of lead patches have no filter env movement |
| `Ch 1 Filter Envelope/Release` | 0 / 98 / 127 / 127 | 127 (98–127) | 54/108 | 127 → 1.000 | long filter tail — the widest-gap role param vs ALL |
| `Ch 1 Amplifier Envelope/Attack` | 0 / 0 / 0 / 12 | 0 (0–12) | 0/10 | 0 → 0.000 | |
| `Ch 1 Amplifier Envelope/Decay` | 21 / 53 / 95 / 127 | 95 (53–127) | 59/127 | 95 → 0.748 | |
| `Ch 1 Amplifier Envelope/Sustain` | 0 / 77 / 110 / 127 | 110 (77–127) | 85/127 | 110 → 0.866 | sustained, unlike the percussive roles |
| `Ch 1 Amplifier Envelope/Release` | 0 / 8 / 18 / 34 | 18 (8–34) | 4/37 | 18 → 0.142 | |
| `Ch 1 Oscillator 1 Waveform Shape` | 0 / 61 / 85 / 127 | 85 (61–127) | 20/64 | 85 → 0.669 | corpus row `Osc1 Shape`; 127 alone is 36% of leads vs 13% of basses |
| `Ch 1 Oscillator 2 Waveform Shape` | 0 / 64 / 64 / 127 | 64 (64–127) | 18/64 | 64 → 0.504 | |
| `Ch 1 Oscillator 2 Detune` | 0 / 0 / 32 / 58 | 32 (0–58) | 15/32 | 32 → 0.252 | corpus row `Osc2 Detune`; the map exposes **two** candidates (`… Detune In Semitones`, `… Fine Detune`) — which one this byte is, is **ambiguous** |
| `Oscillator Balance` | 0 / 36 / 64 / 87 | 64 (36–87) | 54/64 | 64 → 0.504 | corpus row `Osc Balance`; no host name in the map's block list |
| `Ch 1 Filter 1 Keyfollow` | 64 / 64 / 64 / 74 | 64 (64–74) | 64/64 | 64 → 0.504 | identical to ALL at p25/med; only p75 differs (74 vs 64) |
| `Ch 1 LFO 1 Rate` | 0 / 48 / 48 / 96 | 48 (48–96) | 48/48 | 48 → 0.378 | p75 = 96: a quarter of leads run an LFO fast |
| `Ch 1 Oscillator Punch Intensity` | 0 / 0 / 64 / 64 | 64 (0–64) | 0/64 | 64 → 0.504 | corpus row `Punch Intensity` |
| `Portamento Time` | 0 / 0 / 0 / 15 | 0 (0–15) | 0/0 | 0 → 0.000 | **no confirmed host name** in the param map; p75 = 15 is the only support for glide (see verdict 24) |
| `Saturation Curve` | 0 / 0 / 0 / 1 | 0 (0–1) | 0/0 | 0 → 0.000 | **host name OPEN**. Categorical: 69% at 0 vs 68% ALL — leads show **no** drive elevation (contrast acid's 73% non-zero) |

## 3.2 Hypothesis verdicts 22–25

**22 — INCONCLUSIVE.** *Refuted if* — the hypothesis has no refuted-if clause, and it
cannot be scored either way. The corpus stores the `Unison Mode` byte in only **323 of
2818** patches, and the role counts are fatal to the comparison: **lead n = 1** (that one
patch is `15:100%`) versus **bass n = 5** (`0:80%, 7:20%`). A one-patch sample cannot
show "significantly more often". `Unison Detune` and `Unison Panorama Spread` have **no
byte distribution in the artifacts at all** (the map confirms the host names
`Ch 1 Unison Detune` / `Ch 1 Unison Panorama Spread`), so the hypothesis's own caveat —
that pan spread is not evidence of unison — cannot be tested here either. Recorded as
INCONCLUSIVE with the n-values as the reason.

**23 — INCONCLUSIVE.** HyperSaw/WaveTable vs Classic is an **oscillator *model***
distinction, and the artifacts carry no `Model` row: they carry `Osc1 Shape` /
`Osc1 Wave Select`, which are Classic-mode controls. What is measurable does differ
between the roles — lead `Osc1 Shape` `med 85 (61–127)` with the max byte 127 at **36%**
of leads, versus bass `med 64 (17–64)` with 127 at **13%**; and lead `Osc1 Wave Select`
is byte 0 in **77%** of patches versus **62%** corpus-wide, i.e. leads are *less* likely
than average to move off the default wave selection. That is consistent with a different
oscillator mode, but no artifact maps a byte to "HyperSaw", so the joint claim is not
verifiable — and I will not read a mode off a shape byte.

**24 — REFUTED.** "Glide is engaged on leads and not on basses." Measured
`Portamento Time`: lead `0/0/0/15`, bass `0/0/0/4`. **Both medians are 0 and both p25
values are 0**, so at least half of each role's patches carry no glide at all; the entire
difference lives in p75 (15 vs 4). Leads are ~3.75× more likely to carry *some*
portamento in the upper quartile, but "engaged on leads" as a role-level statement is
contradicted by the lead median of 0. (No refuted-if clause was written; this is judged
on the claim's own words.)

**25 — INCONCLUSIVE (by construction).** The claim is **project-level**: a monotonic
cutoff ramp over a ≥8-bar section, distinguished from a 1/4–4 cycles/bar LFO. The
artifacts are patch-byte distributions — they contain no automation lanes, no section
trajectory, and no cycles/bar decode for `LFO 1/2/3 Rate`. The *refuted-if* clause
(automation dominated by short-cycle oscillation) is therefore **untestable from the
corpus**, which is not evidence against it. §3.5 gives the project-level test that
could settle it; until that runs, the corpus cannot vote.

## 3.3 Example patches (from the artifacts' lead list, n = 107)

| name | file | format |
|---|---|---|
| `BrightLead` | `Access_Virus_TI/wc_olo_garb_virus_ti-c01.syx` | tibank |
| `Dub-Lead P` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| `E-Lead  MS` | `Access_Virus_TI/wc_olo_garb_virus_ti-b01.syx` | tibank |
| `FmLead Pl` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| `JapanLead` | `Ephilions Future Space.mid` | tibank |

(`JapanLead2` and `JapanLead3` are the next entries in the same block and the same
file.) The technique notes' own lead claim is that a psy lead is a **stack of 4–5
layers**, not one patch — the corpus can only supply one layer per row, so treat these
as stack candidates, not complete leads.

## 3.4 Apply via MCP

Reuse the **§1.4 contract** (reference, not re-derivation): `list_fx_params
{"trackId":…,"slotIndex":0}` to confirm the `Ch <n> ` name, then named `set_fx_param`
writes with values normalized **byte/127**. Named writes persist into offline renders;
a bypassed slot renders digital silence. For a *stack*, apply per-part: `Ch 1` has 444
params and `Ch 2…16` have 433 each, so the second layer is a **second part** on the same
slot — write it with a `Ch 2 ` prefix rather than layering plugins.

`Saturation Curve` and `Portamento Time` remain unwritable-as-named (see §2.4); confirm
before depending on them.

## 3.5 Falsifiable prediction

- **L1 (the lead/acid split is real and inverted).** A patch written to §3.1
  (`Ch 1 Filter 1 Cutoff` 102 → 0.803, `Ch 1 Filter 1 Resonance` 0,
  `Ch 1 Amplifier Envelope/Sustain` 110) rendered over the same window as the §2 acid
  patch must show **higher high-band energy and lower bass-band energy** than the acid
  patch. **Falsified if** `mix_report` puts the lead's high band at or below the acid
  patch's, since the two recipes differ by ~50 bytes of cutoff and 64 bytes of resonance.
- **L2 (settles hypothesis 25 — project-level).** Automate `Ch 1 Filter 1 Cutoff`
  from **0.10 → 0.70** with points every **32 beats** across an ≥8-bar lead section
  (the macro-sweep shape the technique notes' §5 describes), render the whole project,
  and read per-beat high-band energy with `verify_window`. H25 is **supported** if the
  per-beat trajectory is monotonically non-decreasing; it is **falsified** if a
  short-cycle (1/4–4 cycles/bar) oscillation dominates the energy envelope. This is the
  only test of H25 that can be run — the corpus cannot supply it (§3.2).

---

# SECTION 4 — PLUCK (Access Virus via OsTIrus / Osirus)

Conventions as in §2.0: **byte range** = `min/p25/med/p75`, **no max column** in the
artifacts; **ALL baseline** = corpus-wide **`p25/med`**; recommended normalized =
byte/127; outside the corpus p25–p75 = **extrapolation**. Corpus role `pluck`,
**n = 131** — the largest of the four melodic roles (lead 107, bass 92, acid 26).

## 4.1 No hypothesis applies to `pluck`

**Stated explicitly, as required.** The technique notes carry **no pluck hypothesis**.
Their hypothesis sets are: 1–14 bass, 15–21 under the heading *"Acid stab / pluck"*, 22–25
lead, 26–30 pad/texture, 31–34 movement. Despite that heading, **every one of 15–21 is
written about *acid-labelled* patches** ("microQ acid patches…", "Acid patches use…",
"`F1KeyTrack` is high on acid", "`F1Drive` is elevated on acid/stab") — none takes
`pluck` as its subject, and hypothesis 17's phrase "the acid pluck" is a description of
the acid sound, not a second role. There is therefore **no pluck-specific hypothesis to
confirm or refute**, and I do not retro-fit the acid verdicts of §2.2 onto this role:
doing so would score claims about a different label set. The recipe below is written
**purely from the corpus distributions** for the `pluck` column.

For a later agent, the three corpus facts that most invite a pluck hypothesis (offered
as observation, not verdict): pluck `Filter1 Env Amt` `med 61` sits **above** the corpus
ALL median **52** and well above acid's **37**; pluck `Filter1 Resonance` `med 12` is
low (acid 64, ALL 6); and pluck `Portamento Time` is `0/0/0/0` — the **only melodic** role
whose p75 is 0 (bass 4, acid 27, lead 15).

## 4.2 Parameter table (pluck, n = 131)

| parameter | byte range (min/p25/med/p75) | corpus median (p25–p75) | ALL (p25/med) | recommended | notes |
|---|---|---|---|---|---|
| `Ch 1 Filter 1 Cutoff` | 0 / 31 / 51 / 68 | 51 (31–68) | 32/58 | 51 → 0.402 | between bass (31) and lead (102) |
| `Ch 1 Filter 1 Resonance` | 0 / 0 / 12 / 36 | 12 (0–36) | 0/6 | 12 → 0.094 | corpus row `Filter1 Resonance`; low — the pluck is not defined by resonance here. >36 is **extrapolation** |
| `Ch 1 Filter 1 Envelope Amount` | 0 / 26 / 61 / 77 | 61 (26–77) | 0/52 | 61 → 0.480 | **above** the ALL median 52 — the strongest single pluck signal in the corpus |
| `Ch 1 Filter Envelope/Decay` | 0 / 31 / 46 / 72 | 46 (31–72) | 35/46 | 46 → 0.362 | |
| `Ch 1 Filter Envelope/Sustain` | 0 / 0 / 0 / 53 | 0 (0–53) | 0/0 | 0 → 0.000 | p75 = 53: three quarters of plucks let the filter env fall away |
| `Ch 1 Filter Envelope/Release` | 0 / 42 / 99 / 127 | 99 (42–127) | 54/108 | 99 → 0.780 | |
| `Ch 1 Amplifier Envelope/Attack` | 0 / 0 / 0 / 13 | 0 (0–13) | 0/10 | 0 → 0.000 | |
| `Ch 1 Amplifier Envelope/Decay` | 0 / 42 / 64 / 98 | 64 (42–98) | 59/127 | 64 → 0.504 | shorter than acid (67), lead (95), bass (83) |
| `Ch 1 Amplifier Envelope/Sustain` | 0 / 0 / 50 / 119 | 50 (0–119) | 85/127 | 50 → 0.394 | **below** the ALL median 85 — the amp env decays rather than holds |
| `Ch 1 Amplifier Envelope/Release` | 0 / 4 / 21 / 50 | 21 (4–50) | 4/37 | 21 → 0.165 | |
| `Ch 1 Oscillator 1 Waveform Shape` | 0 / 0 / 63 / 89 | 63 (0–89) | 20/64 | 63 → 0.496 | corpus row `Osc1 Shape`; p25 = 0 — a quarter of plucks sit at the shape minimum |
| `Ch 1 Oscillator 2 Waveform Shape` | 0 / 0 / 64 / 70 | 64 (0–70) | 18/64 | 64 → 0.504 | |
| `Ch 1 Oscillator 2 Detune` | 0 / 0 / 32 / 48 | 32 (0–48) | 15/32 | 32 → 0.252 | corpus row `Osc2 Detune`; map has two detune candidates — **ambiguous** which this byte is |
| `Ch 1 Oscillator 2 FM Amount` | 0 / 0 / 0 / 53 | 0 (0–53) | 0/0 | 0 → 0.000 | p75 = 53: a quarter of plucks add FM — the largest FM share of any melodic role (acid p75 31, lead 0) |
| `Ch 1 Filter 1 Keyfollow` | 39 / 64 / 64 / 88 | 64 (64–88) | 64/64 | 64 → 0.504 | p75 88 is the highest keyfollow p75 of the melodic roles |
| `Ch 1 Sub Oscillator Volume` | 0 / 0 / 0 / 57 | 0 (0–57) | 0/0 | 0 → 0.000 | |
| `Ch 1 Oscillator Punch Intensity` | 0 / 0 / 64 / 64 | 64 (0–64) | 0/64 | 64 → 0.504 | corpus row `Punch Intensity` |
| `Ch 1 Filter 1 Mode` | see notes | categorical `0:69%, 2:8%, 1:6%, 6:5%, 7:5%, 4:5%` | `0:77%, 2:7%` | 0 → default | corpus row `Filter1 Mode`; the only role where byte 6 appears in the top five |
| `Oscillator Balance` | 0 / 8 / 64 / 86 | 64 (8–86) | 54/64 | 64 → 0.504 | corpus row `Osc Balance`; no host name in the map's block list |
| `Portamento Time` | 0 / 0 / 0 / 0 | 0 (0–0) | 0/0 | 0 → 0.000 | **entire IQR is 0** — no glide in three quarters of plucks; **no confirmed host name** in the param map |
| `Saturation Curve` | 0 / 0 / 0 / 5 | 0 (0–5) | 0/0 | 0 → 0.000 | **host name OPEN**. Categorical: 47% non-zero vs 32% ALL, top value `5` at 7% — a mild, not decisive, drive lean |

## 4.3 Example patches (from the artifacts' pluck list, n = 131)

| name | file | format |
|---|---|---|
| `ARP_Plucky` | `DWSDVSS_5.mid` | tibank |
| `AT-Plucks` | `Ollie - Virus Psytrance Vol1.mid` | tibank |
| `Age Pl NK` | `Access Virus NK - Best Atmospheric Sounds (BC vers) 3.0.MID` | tibank |
| `Amber Pl` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| `Analog Pl` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |
| `Anker Pl` | `PsyLoad Access Virus TI Psytrance Soundset.mid` | tibank |

Note the label convention visible here: pluck patches are usually named with the
`… Pl` / `… Pluck…` suffix, which is what makes this role name-derived (front matter).
Four of the six come from psy-oriented banks, but the corpus carries **no genre labels**,
so these are pluck-*named* patches, not measured psy targets.

## 4.4 Apply via MCP

Reuse the **§1.4 contract** (reference, not re-derivation): `list_fx_params
{"trackId":…,"slotIndex":0}` to confirm each `Ch <n> ` name, then named `set_fx_param`
writes with values normalized **byte/127**. Named writes persist into offline renders;
a bypassed slot renders digital silence, so a silent pluck render is an audibility
failure (prove it before A/B-ing). The pluck recipe needs no part split — it is a
single-part patch (`Ch 1`), unlike the §3 lead stack.

As in §2.4, `Saturation Curve` and `Portamento Time` are **not writable as named** —
the map confirms no saturation-amount name and does not list Portamento — so confirm
with `list_fx_params` before relying on either.

## 4.5 Falsifiable prediction

- **P1 (the pluck is the *filter* envelope, not the amp envelope).** A patch written to
  §4.2 (`Ch 1 Filter 1 Envelope Amount` 61 → 0.480, `Ch 1 Filter Envelope/Decay` 46,
  `Ch 1 Filter Envelope/Sustain` 0, `Ch 1 Amplifier Envelope/Sustain` 50) must show a
  **falling high-band trajectory within each note** — high-band energy concentrated at
  note onset and decaying before the next note — over a `verify_part` window on a pluck
  phrase. **Falsified if** the per-note high band is flat: that would mean the
  envelope-amount write did not land (the §1.4 diagnosis), not that the corpus is wrong.
- **P2 (the corpus prior separates pluck from acid by *resonance*, not cutoff).** The
  pluck recipe (resonance 12, cutoff 51) and the acid recipe (resonance 64, cutoff 47)
  differ by ~52 bytes of resonance and only 4 bytes of cutoff; rendered on the same
  note pattern, the acid patch must show the **narrower, louder spectral peak** (higher
  `soloPeak` at similar `soloRms`, i.e. a higher crest) while the pluck stays closer to
  its unfiltered level. **Falsified if** the two renders are spectrally
  indistinguishable — in that case the corpus's role prior is not surviving the write
  path, and the next step is `param_verity` on `Ch 1 Filter 1 Resonance`, not a new
  recipe.
