# Plan — BUG-7: NodalRed2x preset loading (Nord Lead 2x banks via injected SysEx)

## Evidence
- NodalRed2x renders audibly since the multi-port fix (matrix peaks 0.197-0.243) but only its boot state — `n2xdevice.cpp:92`: the emulated NL2x boots with **no valid presets** (program changes are dropped as garbage) because the NL2x keeps voices in volatile RAM.
- The user's bank library: `D:\pdf\NL2x Banks\` — Clavia SysEx dumps (`F0 33`, 24-25KB banks) and SMF-wrapped dumps (`.mid`, e.g. NL2x Factory ProgBank0-3.mid).
- The firmware handles MIDI SysEx natively ("In SysEx" flag, SysEx ring buffer — `doc/n2x_firmware_sysex_analysis.md` in the gearmulator repo) with single-patch ($5DF4) and multi-patch ($642E) bank storage. Bank dump over MIDI → RAM → PC selects voices.
- HDAW's injection chain ships (Phase 4b): send_fx_midi sysEx → SHM ring (128KB sysex buffer) → child → CLAP_EVENT_MIDI_SYSEX → the plugin's MIDI input.

## Fix
New MCP tool `load_nord_bank {trackId, slotIndex, filePath}`:
1. Read .syx (raw Clavia dump; split concatenated F0-runs) or .mid (SMF; extract sysex events via juce::MidiFile).
2. Validate: starts with F0 33 (Clavia), total ≤ 32768B (SHM margin).
3. Queue each dump via sendFxMidi (SysEx kind) on the slot.
4. Optional `program` param sends a trailing PC for voice selection.

## Gates
- Live-engine: NodalRed2x track + ProgBank0.mid → render differs from the pre-load default and is audible.
- SendFxMidiValidation stays green (new tool registered).
- Full suite: no regression.

## Effort/risk
0.5d. Risk LOW — file parsing + existing injection path; no audio-thread changes.

---

## Resolution (2026-09-13)

Shipped as `load_nord_bank {trackId, slotIndex, filePath, program?}`
(`src/mcp/McpTools_FxSlot.cpp`) with the parser/validator shared in
`src/mcp/PresetFileParser.h`:

- `splitNordSyx` splits concatenated F0..F7 runs (pre-F0 bytes skipped;
  unterminated dump rejects the file).
- `validateNordDump` enforces the Clavia header (F0 33 <dev> 04), F7
  termination and the 32768B SHM cap — EVERY dump validated before anything
  is queued (no partial bank loads).
- Optional `program` (0-127) appends a trailing programChange for voice
  selection (plan step 4); out-of-range = error.
- Format facts baked in from the real-library survey
  (`docs/plans/2026-09-13-nl2x-patch-decoder-survey.md`): SMF F0-event
  lengths INCLUDE the trailing F7 (JUCE rejoins F0..F7); real bank multis
  are 1063 B (8x66 params), not the 715 B the n2x header constant implies.

### Gates — evidence
- G1 (unit): PresetFileParser.* 12/12 (Nord splitter/validation/SMF-rejoin
  roundtrip + DX7/Serum regression), FxMidiInjection.* unit tests green.
- G2 (MCP): McpServer.SendFxMidiValidation extended with the load_nord_bank
  error paths (missing file, unsupported type, bad header, non-plugin slot,
  unknown track, program range) — McpServer.* 37/37 green.
- G2 live-engine: `FxMidiInjection.NordBankLoadChangesNodalRed2xRender`
  (HDAW_REAL_PLUGIN_TESTS=1 + NodalRed2x.clap + real ProgBank0.mid):
  parse -> validate 110 dumps -> queue via SHM -> PC 3 -> capture pluginState
  -> offline audition re-render differs from the boot-state render. 4/4
  stable passes after fixing the capture race (the ~800ms capture timer can
  fire while the SHM ring still drains 110 dumps; a trailing CC with
  captureToTree re-arms the capture post-consumption — flake fix).
- Full engine suite: unchanged contract; no production code outside the MCP
  layer (command layer reuses the existing virtual sendFxMidi face).

### Effort/risk
Realized ~0.3d (tool existed in skeleton form; hardened validation + program
param + tests). Risk LOW: parsing only; no audio-thread changes.
