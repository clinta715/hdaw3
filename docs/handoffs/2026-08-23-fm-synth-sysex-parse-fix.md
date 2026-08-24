# Handoff: FM Synth SysEx Parsing Fix

**Date:** 2026-08-23
**Status:** Ready for pickup
**Priority:** Medium

## Context

During a composition session, we attempted to load DX7 .syx preset files from `D:\pdf\Dexed Presets\80's Librairy\` into HDAW's internal FM synth via the MCP tool `fm_synth_import_sysex`. All attempts failed with:

```
failed to parse SysEx data (bad checksum or size)
```

## What Was Attempted

### Files Tried
All files are **4.0 KB (4104 bytes)** — these are DX7 **32-voice cartridge dumps**, not single voice dumps.

| File | Track | Result |
|------|-------|--------|
| `D:\pdf\Dexed Presets\80's Librairy\bassics.syx` | 7 (Bass) | failed |
| `D:\pdf\Dexed Presets\80's Librairy\synths.syx` | 8 (Lead) | failed |
| `D:\pdf\Dexed Presets\80's Librairy\strings.syx` | 9 (Pad) | failed |

Also tried with `voiceIndex=0` parameter — same failure.

### What Should Work
- **Single voice dumps:** 163 bytes (156 bytes voice data + SysEx header/Footer)
- **Cartridge dumps:** 4104 bytes (32 voices × 128 bytes + 8-byte SysEx envelope)
- The parser should handle both formats and use `voiceIndex` to select from cartridges

## The Fix Needed

The `fm_synth_import_sysex` handler (likely in `src/engine/FMSynthFX.h` or similar) needs to:

1. **Accept cartridge dumps (4104 bytes)** — detect size and parse as 32-voice bank
2. **Use `voiceIndex` parameter** to select which of the 32 voices to load (0–31)
3. **Handle the DX7 SysEx envelope:** `F0 43 00 09 20 <128 bytes voice data> F7` for single voices, `F0 43 00 09 20 <4096 bytes bank data> F7` for cartridges
4. **Fix checksum validation** if that's the failure point — DX7 SysEx uses a simple XOR checksum of the data bytes

## DX7 SysEx Format Reference

### Single Voice (163 bytes)
```
F0 43 00 09 20 [128 bytes voice data] [1 byte checksum] F7
```

### Cartridge Dump (4104 bytes)
```
F0 43 00 09 20 [32 × 128 bytes = 4096 bytes] [1 byte checksum] F7
```

The `voiceIndex` selects offset `voiceIndex × 128` within the 4096-byte data block.

## How to Verify

1. Load any .syx from `D:\pdf\Dexed Presets\80's Librairy\` via `fm_synth_import_sysex`
2. Call `fm_synth_get_state` — should show algorithm/params matching the loaded voice
3. Add a MIDI clip with notes on that track — should produce sound

## Files to Investigate

- `src/engine/FMSynthFX.h` / `.cpp` — FM synth implementation
- MCP handler for `fm_synth_import_sysex` — likely in `src/mcp/` or `src/engine/`
- Any existing SysEx parsing code
