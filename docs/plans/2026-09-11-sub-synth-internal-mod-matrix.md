# Plan: SubSynth internal LFO/mod matrix (2026-09-11)

## Scope
Add a minimal Virus-style internal modulation source to `sub_synth` without changing default sound:

- one global per-slot LFO
- destinations: cutoff, pitch, amp, Osc2→Osc1 FM
- append-only params 27..32, all default-neutral
- preserve existing track-level LFO/modulation system unchanged

## Success gates

1. Defaults are bit-identical against an engine with new params explicitly set neutral.
2. New param defs are exposed by `TrackFXSlot` and restored/applied on rebuild.
3. Internal LFO changes rendered output when a nonzero destination amount is set.
4. Focused engine tests pass: `SubtractiveSynthEngine.*` and SubSynth TrackFX rebuild tests.
5. No project/model/RPC schema break; appended params only.

## Risk notes

This touches the synth render path, so verification must include audio behavior tests. It does not touch `processBlock`, routing graph topology, plugin isolation, render/export plumbing, or external FX contracts.
