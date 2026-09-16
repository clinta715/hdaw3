# Terra Signal — re-imagined with the VA-suite tooling (2026-09-16)

Applied the session's new capabilities to `compositions/terra-signal` (145 BPM, dark,
184 bars, 12 palette tracks, 63 clips) and re-gated it with the audits.

## Deliverables
- `compositions/terra-signal/terra-signal-reimagined.hdaw`
- `compositions/terra-signal/terra-signal-reimagined.wav` (306 s, peak 0.414, rms 0.209)

## Before → after (audits, measured)

| Check | Before | After |
| --- | --- | --- |
| `audit_song_structure` | **ok=false**, `dropsAtLeastBuildLoad=false` (build 9 roles vs peak 8) | **ok=true**, all four gates pass, `dropsThinnerThanBuild: []` |
| `audit_modulation_coverage` | **2/12 covered**, 10 tracks needing attention | **14/14 covered**, attention list empty |
| `loudnessGates` (mix_report from the plan) | not previously green | **ok=true** — peak 1.00x, peak2 1.00x, peak3 1.00x, outro 0.93x vs their builds |
| Section RMS | — | intro 0.102 / build 0.234 / peak 0.233 / breakdown 0.114 / build2 0.232 / peak2 0.233 / peak3 0.233 / outro 0.216 |

## What the new tech contributed

1. **Patch libraries built this session, used end to end.**
   - Track 12 (**Nord Lead 2x / NodalRed2x**): `audition_plugin {keepTrack}` then
     `load_nord_bank` on `NL2x Banks/Discovery Pro Export/Discovery Pro Lead 3.syx` ->
     **110 sysex dumps loaded** (the one loader verified to change renders).
   - Track 13 (**JE8086 / JP-8080**): `load_je8086_preset` on the exploded per-patch
     file `.../exploded/Kulshan Mystical Psytrance/perf015-part1 DEEPSAW.syx` (Kulshan
     is the standout psy bank from the `je8086_survey` role shortlist).
   Both layers are written **only into the three peaks** (beats 160/416/544), which is
   what lifts the drops above the builds and closes the structure gate.
2. **The §8 transitional-effect levers, applied as movement plans.**
   - Lever 2 (automate the *amount*, not the destination): cutoff (param 58) sweep
     0.35 -> 0.85 across the first 24 beats of each peak - **96 points written**.
   - Lever 4 (FX over time): delay level (param 87) 0.05 -> 0.70 over the last 8 beats
     of each peak - **32 points written** (a throw into the next section).
3. **The global modulation rule enforced** with one `add_lfo` per sounding track (12
   tracks) -> coverage 2/12 to 14/14, i.e. the "modulation on everything, subtle"
   standing rule is now mechanically satisfied.


## Fix pass (v5, same day) — the four gaps from the first pass, closed

| Gap | Fix | Result |
| --- | --- | --- |
| Flat dynamics (peaks 1.00x their builds) | **Volume-lane dips across each WHOLE build window** (beats 96-160, 352-416) on the dominant elements: kick to 0.26x, bass to 0.45x, hats to 0.70x, rising back into each drop | **peak 1.322x, peak2 1.301x, peak3 1.296x, outro 1.201x** — all pass |
| Unused headroom (peak 0.414, then overshot to 0.966) | master gain staged twice: 1.6 (too hot) -> 0.85 | **peak 0.782**, rms 0.357 |
| Bass fader silently overridden by its Volume lane | `set_fader_authoritative {trackId:5, authoritative:true}` | `faderOverriddenIds: [0,3,5]` — now *intentional* (those lanes carry the build dips) |
| JE8086 curated patch not renderable | not fixable engine-side: its state does not round-trip the patch (measured) | documented; the Nord layer remains the renderable VA demonstrator |

Section arc after the fix: intro 0.150, **build 0.299, peak 0.395**, breakdown 0.209,
**build2 0.333, peak2 0.433**, peak3 0.431, outro 0.400 — i.e. quiet intro, thin builds,
loud drops, a real breakdown dip, and each build visibly lifting into its drop.
Coverage still 14/14 and the structure gate still green.

### The reusable lesson (worth more than the numbers)

My first two attempts failed for a *measured* reason, not a taste one:

- A dip of `4 dB on 3 of `10 elements moved the ratio only **0.99 -> 1.05** — the mix sum
  is dominated by everything, so a partial dip barely registers.
- A dip that covered only **4 of the 64 beats** in a build window did nothing at all: the
  section RMS averages the whole window, so the dip must span it.
- What worked was stripping the **dominant** element (the kick, `0.72 kickProminence)
  across the **whole** build window: ratio 1.32x.

Rule of thumb for build/drop contrast on these tracks: automate the *kick and bass* (the
loudness carriers), not the risers/FX; span the entire build; and expect the first
estimate to be too shallow by `6 dB.

## Honest gaps (next targets)

- **Flat dynamics**: builds (0.232-0.234) sit at the same level as the peaks (0.233).
  The gate passes at exactly 1.00x, so the *arc* has no room; the Neon Meridian recipe
  (thin the builds a little, lift the peaks) is the obvious next step.
- **Headroom**: overall peak 0.414 - roughly 4 dB of unused headroom; the gain stage
  could be driven hotter.
- **Track 5 (Bass) has an enabled Volume lane**, so its fader is overridden
  (`faderOverriddenIds: [5]`). Call `set_fader_authoritative` before gain staging it.
- **The JE8086 layer renders the plugin's default patch**, not the curated Kulshan
  patch: its `pluginState` does not round-trip the patch (measured), so curated JE8086
  patches remain audition-only. The Nord layer is the renderable VA demonstrator.
- No per-section morph was done: a bank load changes a track's character for *all* its
  clips, so section-specific morphs need a second layer (the §8 lever-5 caveat).
