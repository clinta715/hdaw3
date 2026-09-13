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
