# Synth Probe Analyzer Design

Date: 2026-09-03
Status: Approved design

## Goal

Provide a standalone WAV-in feedback loop that evaluates rendered VST/CLAP
instrument probes against a requested psytrance production role and returns
evidence-backed textual and machine-readable recommendations.

## Scope

The first slice is an external Python analyzer. It does not enumerate plugins,
render audio, mutate HDAW projects, change plugin parameters, or add an MCP
tool. Those integrations are follow-up work after the analyzer contract is
validated.

## Input

The command accepts a WAV path and a required role. Optional metadata identifies
the patch and plugin and an optional GGUF path selects the local language model.
Supported roles are `kick`, `bass`, `hat`, `snare`, `rim`, `clap`, `lead`,
`arp`, `stab`, `pad`, `riser`, and `fx`.

## Processing

1. Validate that the input exists, is readable audio, and contains finite
   samples.
2. Reuse `timbre.py` for deterministic DSP descriptors and envelope/spectral
   measurements.
3. Reuse the existing CLAP caption/tag stage when its optional dependencies
   are available.
4. Apply deterministic role checks from a dedicated `role_targets.py` module.
5. Optionally pass the measured evidence, role, plugin, and patch metadata to
   the local Qwen model through `llm_stage.py`.
6. Assemble one stable JSON report. Human-readable output is optional and is
   derived from the same report data.

Deterministic checks produce the verdict. The LLM explains measured evidence
and may offer creative suggestions, but it must not override a check or invent
unsupported instruments, effects, or parameter values.

## Role Checks

- `kick`: centroid below 120 Hz, dominant low energy, short decay
- `bass`: energy concentrated around 60-250 Hz and controlled decay
- `hat`: strong energy above 6 kHz and short envelope
- `snare`, `rim`, `clap`: transient mid/high energy and short envelope
- `lead`, `arp`, `stab`: tonal or pitched midrange presence
- `pad`: sustained envelope, body/mid energy, and nonzero upper content
- `riser`, `fx`: evolving or increasing spectral energy

Recommendations use four priorities:

- `critical`: silence, clipping, non-finite samples, or severely wrong register
- `high`: substantial miss against the requested role
- `medium`: useful but imperfect timbral characteristic
- `low`: optional creative variation

Thresholds and measurement names are explicit and testable. They are not
embedded in the LLM prompt.

## Report Contract

The report contains the input metadata, requested role, measured descriptors,
deterministic role result, capability warnings, prose description when
available, and structured recommendations. A representative shape is:

```json
{
  "input": {
    "path": "probe.wav",
    "name": "Example Patch",
    "plugin": "Example Synth"
  },
  "role": "bass",
  "measurements": {},
  "roleCheck": {},
  "description": "...",
  "recommendations": [],
  "warnings": []
}
```

The schema remains valid when CLAP or the local LLM is unavailable. In those
cases the report contains DSP and role-check data plus explicit warnings.

## Files

- `timbre-lib/analyze_probe.py`: CLI and report assembly
- `timbre-lib/role_targets.py`: deterministic role thresholds and rules
- `timbre-lib/llm_stage.py`: role-aware evidence prompt inputs
- `timbre-lib/test_analyze_probe.py`: analyzer and schema tests

The existing `lib_analyze.py` library-indexing workflow remains unchanged. The
probe analyzer does not create `.timbre.json` sidecars unless a future explicit
flag is added.

## Verification

- Analyze existing and generated WAV fixtures.
- Test each role against synthetic signals representing pass and fail cases.
- Test silent, clipped, malformed, non-finite, mono, and invalid inputs.
- Test DSP-only fallback without CLAP or GGUF dependencies.
- Verify stable JSON for identical input and arguments.
- Confirm no HDAW, plugin-host, RPC, or audio-engine changes are required.

## Future Extension

After this analyzer is trusted, a separate probe runner can enumerate VST/CLAP
presets, create standardized role MIDI probes, request isolated renders, feed
the WAVs into this analyzer, and present recommendations. Parameter mutation
will remain a constrained, explicit agent action using enumerated plugin ranges;
the LLM will not directly write arbitrary plugin state.
