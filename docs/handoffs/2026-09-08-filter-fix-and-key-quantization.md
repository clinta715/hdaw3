# Handoff: 2026-09-08 — Filter Fix, Key Quantization, Arrangement Fix

## Session Summary

Investigated persistent "filter not working" bug, discovered and fixed the root
cause (JUCE DSP class silent no-op + TPT SVF math error), quantized 473
off-key notes, and fixed kick placement at beat 128 instead of 0.

## Changes Made

### src/engine/TrackFXSlot.h — ManualSVF filter replacement
- **Replaced** `std::unique_ptr<juce::dsp::StateVariableTPTFilter<float>> filter`
  with `ManualSVF filter` struct (TPT state-variable filter, per-sample math)
- **Fixed** creation path: `filter.sampleRate = spec.sampleRate; filter.updateCoefficients(); filter.reset();`
- **Fixed** param application: `applyFilterParamsFromValues()` now sets
  `filter.cutoff/type/resonance/sampleRate` + `updateCoefficients()`
- **Fixed** process path: direct per-sample `filter.processSample(ch, sample)` loop
- **Fixed** reset path: `filter.reset()` (struct member, not pointer)
- **Fixed** setInternalParam Filter case: removed `if (!filter) return;` (struct, not pointer)

### Key files touched
- `src/engine/TrackFXSlot.h` — ManualSVF struct, all filter references updated

## Bugs Fixed

1. **Filter FX completely non-functional** (since introduction)
   - Root cause: `juce::dsp::StateVariableTPTFilter::process(ProcessContext)`
     silently passes through in `ProcessContextReplacing` mode
   - Secondary: initial ManualSVF had wrong `v2` formula
   - Fix: ManualSVF with correct TPT SVF math (`v2 = ic2 + g * v1`)
   - Verified: bypass A/B test shows -24dB bass, +30dB body difference

2. **473 off-key notes across 5 tracks** (Lead 22, Arp 60, Pad 368, Stab 8, Riser 15)
   - Root cause: Markov generators don't enforce project scale
   - Fix: post-hoc quantization to A minor (nearest scale degree)
   - TODO: add key-filter gate in generation pipeline

3. **Kick clip at beat 128** (53 seconds of silence)
   - Root cause: corpus arranger placed kick too late
   - Fix: moved kick clip to beat 0 via `set_clip`
   - TODO: add hard constraint in arranger: kick must start within first 4 beats

## Open Issues / TODO

- **Markov key filter:** generate phrases with scale constraint, not post-hoc quantize
- **Arranger constraints:** enforce kick/bass presence from beat 0
- **Master bus processing:** per-track EQ can't fix 87% sub dominance;
  need master EQ or multiband compression for global tonal balance
- **Sidechain compression:** duck bass to kick for tighter low end
- **Master limiter:** RMS is -20.6 dBFS; limiter could bring to -10 to -14 dBFS

## Project State

- `compositions/corpus_track_77_psy_v3.hdaw` — saved with all fixes
- `compositions/FINAL_v7_kickfix.wav` — latest render (kick from beat 0)
- Levels: peak -5.1 dBFS, RMS -20.6 dBFS
- 16 tracks, 33 clips, 145 BPM

## Lesson for AGENTS.md

Consider adding to pitfalls-juce.md:
> `juce::dsp::StateVariableTPTFilter::process(ProcessContext)` silently passes
> through in `ProcessContextReplacing` mode. Use `processSample()` per-sample
> in a loop, or A/B test the block-level process with a known signal. The
> inherited `process()` from the `Processor` base does not apply the filter
> coefficients for in-place processing.
