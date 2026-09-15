# VES (Vintage Emulator Studio) preset loading — probe findings

Date: 2026-09-15. Status: PARKED (user decision — no further HDAW integration work). Question: can HDAW load presets/patches into the VES VST3
(MAME-embedded vintage machines)? VES 0.9.289 sources at
`D:\pdf\VintageEmulatorStudio-main`; VST3 installed at
`C:\Program Files\Common Files\VST3\Vintage Emulator Studio.vst3`.

## What VES is
A JUCE plugin that boots selected MAME 0.289 machine drivers (TX81Z, CZ-101,
ESQ-1, Akai samplers, TR-707, ...) inside the plugin process, with in-memory
audio/MIDI bridges and video-canvas UI.

## Preset/patch mechanics (from source)
- Plugin state (`getStateInformation`) = XML ValueTree: selectedMachineDriver,
  editor size, ROM dir, artwork dir, floppy/CD-ROM/hard-disk media paths.
  **No patch data.** Round-trips cleanly (well-behaved, unlike Serum2).
- Program API is a stub (1 program, no-op). `apply_preset`/programs do nothing.
- Patch RAM (MAME NVRAM) is **transient**: per-instance temp dir
  (`%TEMP%\VintageEmulatorStudio\instance-<ptr>\nvram`) is deleted at every
  engine boot and re-seeded only for `requiresSeededNvram` profiles. Patch
  edits do NOT persist and are invisible to the host.
- Therefore "loading presets" means: (a) mount prepared disk images
  (samplers; media paths persist in state), or (b) MIDI injection (PC/sysex)
  via `send_fx_midi`, or (c) UI automation of the emulated canvas.
- VES exposes **zero host parameters**; everything is editor-driven.

## Environment configured this session
- `%APPDATA%\VintageEmulatorStudio\settings\rom-directory.txt` →
  `...\VintageEmulatorStudio\ROM Firmware` (flat `<driver>.zip`/`.7z`,
  46 files matching VES driver names). Verified consumed: instance runtime
  dirs with `nvram/<driver>` were created.

## Probe results (tests/unit/engine/ves_boot_probe_test.cpp, HDAW_REAL_PLUGIN_TESTS=1)
1. **Isolated child: plugin LOADS and runs.** State round-trips (618 B);
   MAME runtime dirs created; the child burns ~1 core continuously for the
   whole drive (MAME emulation is executing).
2. **In-process hosting FAILS**: JUCE "Unable to load VST-3 plug-in file"
   (`HDAW_NO_PLUGIN_ISOLATION=1`). VES is isolated-only in HDAW.
3. **Audio bridge: SILENT.** 60 s of realtime-paced blocks (441/44100),
   noteOn channel 1 every second, transport playing (playhead snapshot
   delivered via SHM), bestPeak=0 every second. Also silent through
   `auditionPlugin` (which re-instantiates the plugin per render — a fresh
   MAME boot inside a short window can never finish booting).
4. Probe left as a diagnostic SKIP (green), verdict printed.

## Open question / next diagnostic
Whether the machine actually boots (screen alive) inside the headless child:
open the VES editor in the isolated child (`toggle_plugin_editor` — Serum
precedent) and watch the TX81Z screen. If the screen is alive and notes play,
the silent bridge is an HDAW↔VES integration question (audio bridge/MIDI);
if the screen is dead, VES itself is not completing boot in the headless child.

## Integration implications if audio flows
- Machine selection: hand-craft the state XML
  (`VintageEmulatorStudioEmbeddedState`, `selectedMachineDriver` prop) and
  apply via `setStateInformation` — restarts the engine on change.
- Patch loading: `send_fx_midi` PC/sysex for synths; disk images for samplers.
- Offline renders: audition/export re-instantiate the plugin per render —
  every render pays a full MAME boot; `windowSeconds` must exceed boot latency
  or exports render silence (needs boot-wait strategy before relying on it).
