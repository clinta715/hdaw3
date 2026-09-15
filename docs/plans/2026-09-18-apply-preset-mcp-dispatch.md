# Plan: apply_preset MCP dispatch tool

Date: 2026-09-18 · Status: implemented (M1)

## Goal
One `apply_preset {trackId, slotIndex, filePath?, program?, bank?, voiceIndex?, channel?, captureToTree?}` MCP tool that dispatches by file type and target plugin, replacing the 5 individual preset-loading tools for agentic use. The 5 individual tools stay registered (backwards compat).

## Success Gates
- [x] apply_preset dispatches all routes — resolver unit tests assert route kind for every dispatch row; loopback tests assert the real loader effect per route (fm_synth -> fmPatchData in tree; sub_synth -> loadVirusPatch result JSON; dexed/virus/nord -> loader-specific "queued ..." sendFxMidi result; .SerumPreset/.fxp -> reaches runLoadPluginPresetFile).
- [x] The individual tools remain registered (ToolRegistry-style test).
- [x] No duplicated loader logic — the 6 executors in src/mcp/PresetRoute.h are the single implementations; the 5 existing handlers delegate to them.
- [ ] Focused tests + fast tier green (hdaw_tests.exe).

## Design
- `src/mcp/PresetRoute.h` (new, header-only):
  - `resolvePresetRoute(fxType, pluginId, bytes, size, extension, hasProgram)` — pure dispatch table:
    file-based: fm_synth slot + F0 43 -> FmSysex; sub_synth slot + F0 00 20 33 -> SubSynthVirus; NodalRed2x slot + F0 33/.mid -> NordBank; Dexed slot + F0 43 -> DexedCartridge; any plugin slot + XferJson/CcnK or .SerumPreset/.fxp/.fxb -> PluginPresetFile.
    file-less: Virus gearmulator slot (OsTIrus/Osirus/Vavra/Xenia/JE8086) + program -> VirusRom.
    else: None + "cannot determine preset type".
  - Executors (extracted VERBATIM from the existing handler bodies — behavior-identical, error strings preserved): runVirusRomPreset, runDexedCartridgeFile, runNordBankFile, runFmImportSysex, runSubSynthImportSysex, runLoadPluginPresetFile.
- `apply_preset` registered in McpTools_FxSlot.cpp (thin dispatcher: slot lookup -> file header -> resolvePresetRoute -> executor).
- Refactored delegates: McpTools_FxSlot.cpp (load_virus_preset, load_dexed_cartridge, load_nord_bank, sub_synth_import_sysex), McpTools_FmSynth.cpp (fm_synth_import_sysex), McpTools_FxPreset.cpp (load_plugin_preset_file).

## Dependency Map
- Blast radius: MCP layer only. No processBlock/DSP/graph/SPSC changes; sendFxMidi/setFmPatch/loadVirusPatch/setStateInformation interfaces unchanged.
- Upstream: McpServer tool registry only. Downstream: existing engine commands (unchanged).
- Pitfall gates: Gate 2 (dispatch target commands pre-exist; tests assert the loader ran), Gate 9 (guarded slot/pointer/index access, copied from existing handlers), Gate 4/15 (verify the freshly built test binary runs the new tests).
- Env-gated real-plugin suites (DexedPresetFile.*, FxMidiInjection.*) keep covering the refactored handlers end-to-end.

## Tests
- tests/unit/mcp/apply_preset_test.cpp (registered in tests/CMakeLists.txt).
