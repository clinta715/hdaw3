# Plan: remove Dexed live-preset path (proven silent 2026-09-14)

Date: 2026-09-14. Status: approved (user), implementing.

## Goal
Remove the Dexed live-preset pipeline that reports success while changing
nothing audible (fresh probe: peak 0, state byte-identical), keeping the DX7
parse machinery that serves the working internal fm_synth import.

## Success Gates
- [ ] G1: grep-clean — no `load_dexed_cartridge|DexedCartridge|
  runDexedCartridgeFile|isDexedPluginId` in src/tests/frontend/docs(skills).
- [ ] G2: F0 43 + plugin slot resolves None with actionable error
  (still contains "cannot determine preset type"; steers to fm_synth).
- [ ] G3: resolver/apply_preset/fm_synth/parser suites pass (updated
  expectations); dx7_sysex_import + preset_file_parser untouched green.
- [ ] G4: full build green ✓; affected suites 79/79 ✓ (DexedRoute crash
  site gone). Fast/full tiers BLOCKED by the same session-audio outage +
  pre-existing MasterBusFx AV (both proven independent of this change;
  see clap-param-metadata plan). Re-run when audio returns.
- [ ] G5: docs consistent — guide/skill/dispatch-preamble steer DX7 users
  to fm_synth_import_sysex; no references to the removed tool.

## Dependency Map
- Blast radius: PresetRoute.h (enum+resolver+runner), McpTools_FxSlot.cpp
  (tool + dispatch case + 2 description strings), 2 test files, CMakeLists,
  docs. No engine/DSP/graph/proxy changes.
- Upstream: MCP tool registry (tool removal; registry fixture uses 2-tool
  fake — unaffected). Upstream callers of removed symbols: none outside
  the 4 known files (verified by grep).
- Downstream: apply_preset dispatcher (Dexed branch removed; F0 43+plugin
  now None-with-guidance). fm_synth_import_sysex path untouched (separate
  code: verify no shared helper).
- Kept deliberately: DX7/F0-43 detection constants, PresetFileParser Xfer/
  CcnK + DX7 branches + tests (pure, cheap, serve working paths),
  generic send_fx_midi sysEx (Nord depends on it), SerumPresetRoute fake
  test (guards generic dispatcher; no Serum-specific engine code exists).
- Projections/SPSC: none. God nodes: none.

## Pitfall Gates Triggered
- Gate 2 (dangling branches): G1 grep verifies no dead references incl.
  tool descriptions naming the removed tool.
- Test discipline: dead tests deleted (proven-silent behavior must not be
  asserted); resolver expectations updated to the honest error.
- Crash disposition: DexedRouteQueuesSysexInjection removal eliminates the
  observed AV site; single order-dependent AV with no spawn + no touched
  code in path = documented flake-watch, not this change.

## Steps
1. PresetRoute.h: drop enum/branch/runner/helper + guidance error.
2. McpTools_FxSlot.cpp: drop tool + case + description fixes.
3. Tests: delete dexed file, update resolver test, drop DexedRoute test,
   CMakeLists unregister.
4. Docs: guide + skill + preamble steering.
5. Sync dance + build + focused suites + fast tier + full suite.
