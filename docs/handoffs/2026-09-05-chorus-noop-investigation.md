# Handoff: Internal chorus FX is a no-op (byte-identical output)

**Date:** 2026-09-05
**Status:** open — needs investigation
**Related:** 5-min demo mix session (`timbre-lib/demo_compose.py` track5)

## TL;DR

The internal `chorus` FX slot changes **nothing** in the rendered audio. An A/B
probe rendered a synth phrase through (a) a plain chain and (b) the same chain
plus a chorus slot with aggressive settings; the two WAVs are **byte-identical**
over the whole window. The internal chorus is either never applied in
`processBlock`, or its params are being pushed in a form that leaves the DSP at
its neutral/off state. The reverb/saturator/phaser/delay internal FX were
verified working in the same probe, so this is specific to chorus (not a
general "internal FX don't render" issue).

## Evidence

- `C:\Users\hapbt\AppData\Local\Temp\opencode\fx_ab.py` — the A/B probe script.
  It renders a 4-bar fm_synth phrase on a probe track, once with an extra
  `chorus` slot (`set_internal_fx_param` rate/depth/wet up) and once without,
  then byte-compares the exported WAVs (`filecmp`). Result: **identical**.
- Same script verified `reverb` (size/wet → large level change), `saturator`
  (drive → level change), `phaser` (rate/depth → level change), and `delay`
  (sync/feedback/mix → level change) all DO alter the output.
- The `filter` FX result was inconclusive in the same run (fm_synth default tone
  sits below the tested cutoff); re-test with a cutoff inside the tone's range.

## Likely cause areas (in order)

1. **`TrackFXSlot::processBlock` / chorus DSP application** — `src/engine/TrackFXSlot.{h,cpp}`.
   The chorus slot's `process` path may not actually be wired into the buffer
   loop (e.g. a bypass/mix gate stuck off, or the wet signal never summed back).
   Check the chorus case in the FX dispatch (the slot switch that routes
   `eq`/`compressor`/`reverb`/`chorus`/... to their DSP).
2. **Param mapping to the chorus DSP** — the `ChorusFX`/`juce::dsp::Chorus`
   params (rate/depth/centre/delay/mix) may be fed defaults (rate 0 / mix 0) if
   `param_0..N` → DSP mapping is missing or misindexed, matching the earlier
   "setProperty no-op"/param-clamp class of bugs (lessons 2/23).
3. **`set_internal_fx_param` write path** — less likely (verified working for the
   other FX), but confirm the chorus param indices actually reach the DSP and
   aren't swallowed by a clamp/validation quirk.

## Reproduction (fast)

```
py -3.14 C:\Users\hapbt\AppData\Local\Temp\opencode\fx_ab.py chorus
```

Prints `IDENTICAL` when the bug reproduces. Requires a running engine build
(`build\HDAW_headless.exe --mcp-stdio` — the script spawns its own via
`demo_compose`'s engine helper).

## Notes

- Chorus was removed from the demo's arp chain once confirmed dead (psy_arp's
  own delay/phaser cover the width). The demo (`psy5min_demo.wav` / `v2.wav`,
  both clean: peak 0.78, 0 clipped blocks) is otherwise unaffected.
- The psy-pad chain calls for a light chorus (goa pad template); fixing this
  unlocks the pad width without relying on phaser alone.