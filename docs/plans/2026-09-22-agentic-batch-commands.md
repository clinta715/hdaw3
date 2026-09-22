# Plan: batch commands for the agentic workflow (2026-09-22)

## Problem

The agentic songwriting workflow (psy-song-session) orchestrates six-seven
subagent roles through the MCP surface. Each role reads a role file and
executes a multi-step procedure using fine-grained MCP tools. Three pain
points surfaced during the 2026-09-22 sessions:

1. **Tool-call count per procedure** — Sound Selector's palette build is
   15-20 calls (set_tempo, add_track_with_fx ×9, load_fx_chain ×9,
   select_patch ×9, set_internal_fx_param, tone_verity ×9); the Layer
   Agent's four gates are 4+ calls each × 7 layers. At the wrapper's 10 s
   per-call timeout, long sequences risk exit-42 restarts that wipe
   unsaved state.

2. **Cross-tool state** — `apply_preset` → `get_fx_capture_status` →
   `export_audio` is a three-call sequence with a race (the capture is
   deferred ~800 ms; the export must wait). The sweep fixed this with a
   poll, but an agent calling the tools individually must know to do the
   same.

3. **Verification scatter** — the Mix Verifier runs mix_report +
   analyze_tuning + tone_verity per track + audit_modulation_coverage +
   audit_song_structure as 5+ separate calls, then mentally joins the
   results into a pass/fail.

## Proposal: batch commands at the workflow-task level

These are NOT new DSP — they orchestrate existing tools engine-side (same
pattern as `add_instrument_part`, which already encapsulates 6 calls).
Each returns a structured verdict; each is a single MCP tool + RPC route.

### `select_palette` (Sound Selector → engine)

Input: `{brief, roles: [{role, engine, pluginId, chain}], seed}`
For each role: `select_patch` (variety) → `add_track_with_fx` (core synth
or sampler) → `load_fx_chain` → `apply_preset` → `tone_verity` (audibility
+ f0/centroid in role register). Returns the palette track map + per-role
verify results. Replaces Sound Selector's 51-line procedure with one call.

Engine impact: additive — a new `AudioEngineCommands::selectPalette` that
loops over the existing `selectPatch` + `createPluginInstance` +
`loadFxChain` + `renderTrackWindow` internals. No DSP, no graph changes.

### `verify_layer` (Layer Agent → engine)

Input: `{trackIndex, role, window, expectations?}`
Calls `renderTrackWindow` once → analyzes the wav for tone_verity
(envelope/AM/centroid/f0) + `audit_modulation_coverage` for that track +
`param_verity_corpus` if the layer has a plugin slot. Returns a single
`{pass, gates: [{name, pass, measured, expected}]}` verdict.

Engine impact: additive — a new `verifyLayer` that calls the existing
`renderTrackWindow` + `analyzeToneWav` + `collectModulationCoverage`
internals. Same pattern as `verifyPart` (which already solo-renders +
full-mix-renders in one call).

### `verify_mix` (Mix Verifier → engine)

Input: `{filePath, fromPlan, dropBuildRatio}` (existing mix_report args)
Calls `mix_report` + `audit_modulation_coverage` + `audit_song_structure` +
`diagnose_intro_blast` and merges into one verdict with per-gate rows.
These are all already engine-side (MixReportAnalyzer, ModulationCoverage,
SongStructureAudit, BlastReport) — this is pure aggregation.

## What does NOT need to change

- The DSP engine (processBlock, plugin hosting, rendering) is untouched.
- The per-role subagent model stays — the batch commands give each role a
  single entry point but the role files still define WHAT to verify and
  WHY.
- The fine-grained tools (set_fx_param, set_internal_fx_param, etc.) stay
  for interactive use and debugging; the batch commands are the
  production path.

## Priority

1. `verify_mix` — pure aggregation, zero new engine code, highest daily value
2. `verify_layer` — needs `tone_verity` + `audit_modulation_coverage` composition
3. `select_palette` — needs `selectPatch` + `createPluginInstance` + chain
   composition; most new code but biggest workflow-time saving

All three follow the existing `verifyPart` / `add_instrument_part` /
`apply_movement_plan` patterns: additive methods on `PluginCommands` /
`AudioEngineCommands`, MCP tool + RPC route + parity mapping.
