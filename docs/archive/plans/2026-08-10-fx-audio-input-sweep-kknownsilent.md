# Preset/audio-input sweep for the `kKnownSilent` CLAP plugins on isolated export

**Status:** RESOLVED (2026-08-10). All gates pass. The three `kKnownSilent`
plugins were misdiagnosed: they are effect plugins; the matrix fed them MIDI
with no audio input. With a 440 Hz sine clip fed through them on isolated
export: ShinRonin peak=0.298889, Gneiss peak>0.01 (varies per run, observed
0.07-0.25), Retrospect peak=0.300385. `kKnownSilent` is now empty; all 10
matrix targets are fully asserted. `McpServer.*` suite: 12/12 green.

## Goal

Resolve the `DiagnosticClapExportMatrix` `kKnownSilent` set (ShinRonin, Gneiss,
Retrospect) by feeding effect plugins a real **audio input** (the correct signal
type for an FX) instead of a MIDI phrase, and re-assert them.

## Root cause of the "silence" (verified 2026-08-10)

All three `kKnownSilent` plugins are **effects, not instruments** (verified via
vendor pages):

| Plugin | Vendor | Type | Default behavior with no audio input |
|--------|--------|------|--------------------------------------|
| ShinRonin | Audio Damage | modular filter/delay FX | silence (no input signal) |
| Gneiss | Hvoya Audio | morphing multi-stage filter FX | silence (no input signal) |
| Retrospect | Conceptual Machines | curve-driven time-warp FX | silence (no input signal; "flat curve is passthrough" once fed audio) |

The matrix (`McpServer.DiagnosticClapExportMatrix`,
`tests/integration/mcp/mcp_server_test.cpp:571-822`) runs the SAME per-plugin
pipeline for every plugin: `add_track_with_fx` → `generate_phrase` (MIDI notes)
→ `export_audio`. **FX plugins receive no audio input and do not consume MIDI
notes** → `outPeak=0` with a healthy pipeline (`act=1, proc=1`, `CONTINUE`) was
misdiagnosed as "factory default patch is genuinely silent". There is no preset
to load either: CLAP program APIs are no-ops in this host
(`CLAPPluginInstance::setCurrentProgram` is `{}`; no `clap_plugin_preset_load`
support; zero `.clap-preset` files on disk), so the old "preset-load sweep"
framing was a dead end. The correct lever is audio input.

## Success Gates (all must pass to declare done)

- [ ] Gate 1: `cmake --build build --config Debug` succeeds (test file force-
      touched first — lesson 15: MSBuild skips stale sources; the matrix then
      runs the OLD `kKnownSilent` silently).
- [ ] Gate 2: `build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix`
      runs to completion (~80 s) and every plugin logs a `RESULT` row with peak.
- [ ] Gate 3: ShinRonin / Gneiss / Retrospect each log an **audio-input row**
      (sine WAV clip added on their track) with a measured peak. Each plugin
      that now renders `peak > 0.01` is **removed from `kKnownSilent`** and fully
      asserted by the existing per-plugin `EXPECT_GT`. Any plugin that stays
      silent with input keeps its slot in `kKnownSilent` with an updated
      evidence comment (input-fed, still silent → then a real defect to chase).
- [ ] Gate 4: `McpServer.*` suite stays green (no regression to the other
      export tests) — full `hdaw_tests.exe` run additionally reported if time
      permits (known baseline: 4 `PluginIsolation.*` failures, pre-existing).
- [ ] Gate 5: docs corrected — the matrix comment block states the
      effect-vs-instrument distinction; the stale claim in
      `docs/handoffs/2026-08-09-nodalred2x-multiport.md` (§Key residual
      knowledge, lines 63-64) is marked superseded with the correct story.

## Dependency Map

- Blast radius: **test-only**. Single file: `tests/integration/mcp/mcp_server_test.cpp`.
  No production code, no RPC surface, no engine change.
- Upstream: none (leaf test). Downstream: matrix results → `kKnownSilent` → the
  comment/docs cited above.
- Projections affected: none. SPSC paths touched: none.
- Knowledge graph: the test file is not indexed in `graphify-out` (query
  returned no relevant nodes) — change is isolated to the test suite.
- MCP tools used (all exist, fully wired): `add_track_with_fx`, `add_audio_clip`
  (`src/mcp/McpTools_Project.cpp:325`), `export_audio`, `new_project`,
  `generate_phrase` (instruments only).

## Pitfall Gates Triggered

- Gate 2 (unimplemented path): N/A — `add_audio_clip` → engine audio-clip →
  export path is implemented end-to-end (the E2E `addAudioClip` helper drives
  the same path). Confirmed `McpTools_Project.cpp:325-349`.
- Gate 4 (stale binaries): **apply** — must force-touch
  `mcp_server_test.cpp` (lesson 15) and rebuild `hdaw_tests` before running.
- Gate 1/6 (state restore), Gate 3 (audio thread), Gate 9 (IDs): N/A — no
  processor, audio-thread, or ID code touched.

## Steps

1. Add a helper in `mcp_server_test.cpp` that writes a temp stereo sine WAV
   (440 Hz, ~0.5 amplitude, 8 s @ 44100, 16-bit) via `juce::WavAudioFormat`
   (`AudioFormatManager` is already used in this file, so JUCE audio formats are
   linked). Temp path via `makeTempWavPath`-style helper or
   `File::getSpecialLocation(tempDirectory)`.
2. In the matrix worker loop, classify each selected plugin:
   - **Effect** if `pd.numInputChannels > 0` (CLAP effects scan as 2-in/2-out;
     instruments as 0-in/2-out). Log the classification. Fall back to an
     explicit name list (`ShinRonin`, `Gneiss`, `Retrospect` — the known FX
     targets) if a cached description shows 0 inputs for one of them.
   - Effects: write the sine WAV, `add_audio_clip` on the plugin's track
     (`start=0`, `length=16` beats → 8 s at 120 bpm), skip `generate_phrase`,
     then `export_audio` + peak scan (existing decode block).
   - Instruments: unchanged (current phrase path).
3. Record the measured peaks; remove from `kKnownSilent` every plugin that now
   renders `> 0.01`; update the comment block with the effect-vs-instrument
   evidence. Keep any still-silent plugin with an updated "input-fed, still
   silent" note (and its `list_fx_params` names in the row log if useful).
4. Update `docs/handoffs/2026-08-09-nodalred2x-multiport.md` lines 63-64:
   mark the "factory default patches are genuinely silent" claim superseded by
   this sweep (they are FX; silence was the matrix feeding MIDI with no audio
   input).
5. Build + run the gates above. Report: per-plugin RESULT rows (name / peak /
   note), final `kKnownSilent` contents, gate pass/fail.

## Verification commands

```powershell
taskkill /IM cl.exe /F 2>$null; taskkill /IM MSBuild.exe /F 2>$null
(Get-Item tests\integration\mcp\mcp_server_test.cpp).LastWriteTime = Get-Date
cmake --build build --config Debug --target hdaw_tests          # big timeout (~10 min)
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix   # ~80 s
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.*       # full MCP suite
```

## Anti-patterns to avoid

- Do NOT weaken `EXPECT_GT` for any non-`kKnownSilent` plugin.
- Do NOT hide a still-silent effect by leaving it in `kKnownSilent` without
  updated evidence — the comment must say it was fed audio and stayed silent.
- Do NOT add new production code paths (no host changes, no engine changes) —
  this is a test + docs fix.
