# Handoff — SliceDetector rebuild: band-aware onsets, per-piece metadata, re-cut surface

**Date:** 2026-10-01
**Branch:** `main` (ahead of origin; not pushed)
**Commits:** `20c2d0c` (feature + tests + device map + docs counts), `2a2c9d1` + `7c43eaa` (docs)

## 1. What was wrong

`SliceDetector::transient` was a single broadband envelope follower
(`env = 0.999·env + 0.001·|x[i]|`, threshold `(1−sensitivity)·globalPeak·0.5`,
`minGap = len/64`, envelope reset to 0 after each hit, trigger on
`|x[i]| > |x[i−1]|`). Ported line-for-line and measured against real library
loops (ground truth = smoothed-envelope peaks > 25 % of max, 90 ms refractory):

| file | GT hits | shipped | recall |
|---|---|---|---|
| `madoxy loop (full)140bpm.wav` | 51 | **0** | 0 % |
| `madoxy loop (hats only) 140bpm.wav` | 26 | **0** | 0 % |
| `madoxy glitch loop 135bpm.wav` | 43 | 20 | 23 % |
| `ANTINOMY_04_Closed_Hihat_Pluck.wav` | 1 | **0** | 0 % |

Its own unit test PINNED the cause: an onset at frame 500 is detected near
frame 1003 (the 0.001 envelope coefficient needs sustained energy) and only
exercised a sustained 440 Hz tone.

## 2. What replaced it

A **4-band onset engine** (20–150 / 150–800 / 800–2.5k / 2.5k–16k Hz via cascaded
one-poles), each band scored by its own adaptive threshold (histogram percentile
of positive flux + mean+k·stddev floor, scaled by `sensitivity`), with
local-ratio and band-participation gates and a cross-band merge. Returns
`{frame, bandMask, strength}` per onset so simultaneous pieces (kick+hat) are
tagged `LOW|HIGH` and a hat 10 ms later gets its own slice.

Also added: a **drift-aligned grid** (`driftAlignedGrid`) that least-squares
fits tempo+phase to the detected onsets (one outlier pass; falls back to the
nominal grid under 4 onsets or a degenerate fit), so a wobbling source is
sliced where it actually plays. Reachable as `sliceMode: "aligned"` — the
third entry in the shared `sliceMode` vocabulary (`common/SamplerSliceModes.h`).

And a **re-cut surface**: `recut_sampler_slices` (MCP) / `sampler.recutSlices`
(RPC) re-analyse ONLY the requested window (so the per-band statistics describe
the piece, not the file) and preserve boundaries outside it;
`slicePointsOverride` pins boundaries the re-cut never moves, written by
`set_sampler_slice_overrides` (MCP) / `sampler.setSliceOverrides` (RPC).

## 3. What was wrong with the previous fix attempt (and how it was caught)

A linear trim was tried first. It was wrong by arithmetic: a full-kit unison
measures a raw 3.558× unity at DEFAULT params while a single kick sits at
~0.51 — no linear trim can serve both (bounding the unison needs ~0.27, dropping
one kick to ~0.2). The waveform export clamped the peak to 1.0000, hiding the
true 3.558×, and 127 of 272 onsets had normalized position > 0.5833 (beyond the
capped window) — both caught by the test asserting the raw float peak.

## 4. Measured recall (corrected ground truth, FULL-FILE window)

| sensitivity | full loop (GT 78) | hats only (GT 49) | glitch (GT 43) | one hat |
|---|---|---|---|---|
| 0.25 | 62 %r / 72 %p / 67n | 69 %r / 100 %p / 34n | 56 %r / 86 %p / 28n | 100 % / 1n |
| **0.50** | **90 %r / 69 %p / 102n** | **96 %r / 96 %p / 49n** | **53 %r / 74 %p / 31n** | 100 % / 1n |
| 0.75 | 91 %r / 41 %p / 172n | 100 %r / 58 %p / 85n | 84 %r / 69 %p / 52n | 100 % / 1n |

Versus the shipped detector on the same material: 0 / 0 / 20 / 0 interior
onsets. `aligned` mode is reachable end-to-end. The 53 % on the glitch loop at
the default sensitivity is the one soft spot (0.75 recovers it to 84 %); the
dense loop's count band caps `sens` at ~0.55.

## 5. Provenance

Two constants were taken from an unlicensed third-party source
(`D:\pdf\LoS.9x9` has NO LICENSE — all rights reserved) during planning. Both
were replaced (metal bank → a derived `2500·1.25^k` geometric series; rim click
860 → 780 Hz) and the four "clean-room" comments were corrected to
"independently implemented; no third-party source vendored". The implementing
agents never opened the third-party repo. `report_issue` retracted the earlier
`engine_info`/`whoami` identity misdiagnosis (the fields are correct).

## 6. Open items

- Full sharded suite + the 6 sampler-recut behaviour tests are BLOCKED by the
  sandbox ACL reset (workspace temp tree + real %TEMP% both denied from the
  restricted token). 23 sibling tests pass including all 8 detector tests.
- No human critical listening on an 11-voice GM demo.
- The glitch loop at sensitivity 0.5 scores 53 % recall — 0.75 recovers it to
  84 %; a selective gate (per-band refractory or strength floor scaling
  independently of the global knob) would fix it without re-opening the other
  files.
