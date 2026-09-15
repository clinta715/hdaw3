# JV-880 (VirtualJV) — preset probe findings — PARKED

Date: 2026-09-15. Status: PARKED (user decision — no further HDAW
integration work). Sources: `D:\pdf\jv880_juce-main` (VirtualJV, Nuked-SC55
core); VST3 installed at `C:\Program Files\Common Files\VST3\jv880.vst3`
with ROMs alongside.

## Setup performed
- The plugin expects ROMs in `%APPDATA%\JV880\` with exact filenames — they
  were in the VST3 folder where the loader cannot find them. Copied
  `jv880_{nvram,rom1,rom2,waverom1,waverom2}.bin`, `rd500_patches.bin`,
  `rd500_expansion.bin` to `%APPDATA%\JV880\`. Result: first-load descramble
  completed, `Cache/` populated. ROM loading is FIXED.

## What works (verified)
- Loads in the isolated child; state round-trips; MAME-style MCU emulation
  executes (`processBlock` steps it synchronously — child burns 1 core).
- **Real program system**: 390 programs (65 internal + 65 A + 65 B +
  expansions) with real names ("A.Piano 1", "Mellow Piano", ...);
  `setCurrentProgram` genuinely writes the patch into NVRAM and posts a
  program change to the MCU. `getNumPrograms/getProgramName` are host-real.
- State struct carries current patch + expansion — a real preset blob.
- HDAW proxy path proven good by control run: TyrellN6 through the identical
  direct-drive probe renders audible (peak 0.209) in ~10 s.

## The blocker
- Audio bridge delivers exact zero through 240 s realtime-paced drive with
  noteOns + transport playing + explicit `setCurrentProgram(0)`, while the
  MCU burns exactly 1 core. The machine executes but the PCM never voices.
- Diagnostic printfs (the core prints "Not enough samples!" / "click:" on
  resampler trouble) never fire — verified via the new child-stdout capture
  (`HDAW_PROXY_CHILD_STDOUT`, env-gated redirect in `ProxyProcessManager::
  spawnPluginHost`).
- No editor timer exists; the MCU is driven entirely from processBlock, so
  "editor needed to boot" is ruled out for the headless case.
- Likely inside VirtualJV: the machine boots into a state needing front-panel
  interaction or the headless boot stalls before the PCM arms. Not an HDAW
  plumbing bug (Tyrell control passed).

## If anyone revisits
- One interactive check remains: open the JV-880 editor in the isolated
  child (toggle_plugin_editor), watch the LCD, play a key. Sound with the
  editor open → "editor-opens-it" workaround path. Silent with a live LCD →
  core bug inside VirtualJV.
- The host-side integration is otherwise READY: program list + program
  switching + state persistence all flow through the existing pipelines
  (`send_fx_midi` PC/CC, `apply_preset`, `load_plugin_preset_file`).