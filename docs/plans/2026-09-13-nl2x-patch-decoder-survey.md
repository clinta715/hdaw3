# NL2X Patch Decoder + Sidecar Sweep (NodalRed2x / Nord Lead 2x)

## Goal
Standalone Python decoder in `timbre-lib/` for the Clavia Nord Lead 2x SysEx
containers found in `D:\pdf\NL2x Banks` (6898 files: 5273 .syx, 82 .mid,
26 .fxb + subfolders), mirroring the shipped Virus pipeline
(`virus_patch.py` → `<patch>.virus.json` sidecars + `virus_survey.json`).
Produces per-patch `<patch>.nl2x.json` sidecars and a library survey report.

## Evidence (verified against gearmulator 2.2.9 sources + real bank files)
- Wire format from `n2xmiditypes.h`: header F0 33 <dev> 04 <msgType> <msgSpec>
  (6B), footer F7; single dump = 66 nibble-encoded params (132 data bytes) →
  139B dump; multi = 4×66 + 90 params (708 data) → 715B; "+name" variants add
  10 bytes (149/725). Param i lives at byte 6+2i, value = low(d[i])|d[i+1]<<4.
- Named params (SingleParam enum): 0-24 + 50-65 named; 25-49 unnamed in the
  enum → reported as `param<NN>` with raw values (never dropped).
- Full single-param enum decoded: Gain 17, Portamento 16, Lfo1Rate 21,
  Lfo1Waveform 56, Lfo1Dest 57, Lfo1Level 22, Lfo2Rate 23, Lfo2Dest 65,
  ArpRange 24, ModEnvA/D 18/19, ModEnvDest 61, ModEnvLevel 20, O1Waveform 50,
  O2Waveform 51, O2Pitch 0, O2PitchFine 1, FmDepth 7, O2Keytrack 54, PW 6,
  Sync 52, Mix 2, AmpEnvA/D/S/R 12-15, FilterEnvA/D/S/R 8-11, FilterType 53,
  Cutoff 3, Resonance 4, FilterEnvAmount 5, FilterVelocity 63,
  FilterKeytrack 55, Distortion 52 (shares byte with Sync: bit4 = distortion).
- Real-file verification: 139B .syx (BoBSwanS) = msgType 0/msgSpec 0 edit-buffer
  single; ProgBank0.mid wraps SMF sysex events (payload w/o leading F0, varlen
  counted) = 99 singles (msgType 1, msgSpec 8..107) + 10 multi dumps (msgType
  1, msgSpec 99..108, 1062B payload → 1063 with F0); .fxb = VST chunk (skip,
  documented); name-in-dump = Aura "+10B" variants only → bank files carry
  program index via msgSpec; names otherwise come from the filename.

## Deliverables
1. `timbre-lib/nl2x_patch.py` — format detection (nl2x syx / nl2x stdmidi /
   nl2x multi-bank), nibble decoder, param naming, description from param
   pseudo-measurements, sidecar writer (`hdaw.nl2x.patch.v1`), survey mode,
   --dump/--sidecars/--role CLI mirroring `virus_patch.py`.
2. `timbre-lib/test_nl2x_patch.py` — pytest: synthetic dumps (build from
   nibbles), real-file spot checks skipped when banks absent, sweep/survey
   invariants, stable-JSON + no-abs-path-leak checks.
3. Sweep `D:\pdf\NL2x Banks` → `timbre-lib/nl2x_survey.json` + sidecars.
4. `timbre-lib/README.md` entry.

## Success Gates
- [x] G1: 15/15 passed (incl. real-library spot checks).
- [x] G2: full timbre-lib suite 91 passed (virus 34 + sidecars 19 + nl2x 15 + rest).
- [x] G3: 6841/6846 files parsed (99.9%); 29436 dumps = 26630 singles + 2806 multis;
      ≥80 .mid banks parsed, patch names non-empty (filename-derived),
      per-format totals reported; survey JSON written to timbre-lib/nl2x_survey.json.
- [x] G4: 6841 sidecars written (0 failed); `<patch>.nl2x.json` next to each patch file;
      schema/keys match the virus sidecar contract (schema/name/engine/format/
      mappedParams/unmapped/description); byte-stable re-run.
- [x] G5: git shows only timbre-lib + docs additions.

## Effort/risk
~0.5d. Risk LOW — pure Python tooling; existing 53-test sidecar suite is the
regression guard.
