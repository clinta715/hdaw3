# Xenia (Microwave XT) matrix presets — roles & morph pairs

Regenerated 2026-09-18 over the CORRECTED harvest (the .mid sidecar off-by-2
labeling fix: param N lives at mid key N+2 — xenia-offset-map.md).  All named
values now byte-match the raw dumps at 7+index (410,550 values, 0 mismatches
across all 32 sidecar/bank pairs), which re-ranked every cluster: the corpus's
true distribution is dominated by dense 14-16-slot modulation matrices (the
pre-fix "plain texture" mid-derived clusters were shift artifacts — three of
the five pre-fix morph parents were mid-derived).  40 unique matrix configs
over 2,554 distinct patch configs (was 3,617 — identical configs no longer
split by the shift).  Distances = mean abs diff over the full normalized
signature; 'C' = continuous (morphable) carrier, 'D' = discrete (routing)
carrier.

## Families (assigned roles)

**MOD-MATRIX (40 presets).**  Every corrected config carries a 14-16 slot
matrix + wave env; sub-flavors by ring-mod mix, chorus, fx mode, mod-router
count and filter-env depth (preset names carry the fragments).  The dense
matrix IS the XT bank style — the 2026-09-16 sheet's RING/WAVE/ring-lite
split rested on shifted .mid values and does not survive the fix.

## Mutate-morph candidates (re-picked on corrected data, same criteria:
## smoothest + most-interpretable, preferring named continuous diffs)

Pure-continuous brightness/swell morphs (zero discrete hops):
- [4]<->[25]  d=0.0076 — F1Cutoff (76->67), F1Resonance (23->80), Slot1Amount
  (64->80): resonance-led brightness swell inside the ring-mod-mix texture.
- [13]<->[36] d=0.0040 — the minimal two-carrier morph: F1Cutoff (55->41) +
  Slot1Amount (93->64).  Darken + deepen slot 1.

One documented discrete hop:
- [20]->[33] d=0.0038 — ChorusEnabled (0->1) + F1EnvAmount (72->112): chorus
  switch + bigger filter env.

Slot-depth reshape (one routing re-aim):
- [10]->[34] d=0.0063 — five slot amounts move + Slot4Source re-aim (15->0):
  same voice, re-balanced matrix depth.

Big swing (still zero hops):
- [0]->[4]  d=0.0208 — six continuous carriers: MixRingMod (0->39) fades the
  ring mod IN while F1Cutoff (127->76), F1Resonance (0->23), F1EnvAmount
  (64->76), W1EnvAmount (127->64), W2EnvAmount (64->101) reshape the tone —
  clean wave-morph pad -> ring-mod-mix mover.

## Method + residue
- Roles derive from the param signature; morphs rank by mean signature
  distance with continuous/discrete carrier classification (morph_presets.py;
  each pair's parent-A must byte-verify against the bank corpus for
  xenia_morphs_injectable.json).
- All five pairs keep EffectType (and every other discrete key except the two
  documented hops above) fixed for smooth morphs.
- Per-pair parent provenance lives in xenia_morphs_injectable.json
  (baseResolution.provenance): "verified" = .usb parent (name resolution
  direct), "mid-corrected" = .mid parent (corrected post-fix labeling).
  Every step's SysEx targets the single edit buffer (bank byte 0x20,
  live-verified 2026-09-16).
- 'waldorf-microwave-factory-sysex.syx' is not decodable by microwave_patch.py
  (syx container unsupported; content duplicates Factory.usb).
