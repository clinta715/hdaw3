# PsyArp Engine Implementation Plan

## Goal
Create a new `PsyArpEngine` audio processor (self-contained osc → filter → delay →
reverb synth with internal arp pattern generation) and wire it into `TrackFXSlot`
as a new internal synth type, following the exact GrowlBassEngine/FmSynthEngine pattern.

## Success Gates
- [ ] G1: `cmake --build build --config Debug` succeeds
- [ ] G2: `build/Debug/hdaw_tests.exe` runs with no regression
- [ ] G3: PsyArpEngine compiles, can be instantiated, prepare() + render() produce non-silent audio
- [ ] G4: All arp/filter/delay params are automatable via existing TrackFXSlot param system
- [ ] G5: "psyarp" type creates correctly via addMidiFxSlot

## Files to Create
1. `src/engine/PsyArpEngine.h` — header
2. `src/engine/PsyArpEngine.cpp` — implementation

## Files to Modify
1. `src/engine/TrackFXSlot.h` — add ActiveType::PsyArp, engine member, param defs, wire prepare/process/reset/applyInternalParamToDsp
2. `CMakeLists.txt` — add `src/engine/PsyArpEngine.cpp` to source list
3. `src/engine/AudioEngineCommands_Fx.cpp` — add "psyarp" case for ValueTree property defaults

## Architecture
- PsyArpEngine follows the GrowlBassEngine pattern: `prepare(sampleRate, maxBlockSize)` + `render(buffer, midi)`
- Lock-free atomic param setters (message thread → audio thread)
- Internal arp pattern generator: maintains held notes from MIDI input, generates tempo-synced arp sequences
- Internal DSP chain: oscillator (saw/square/supersaw) → resonant lowpass filter (slow sweep) → delay (ping-pong) → reverb
- Filter sweep clock is DECOUPLED from note clock (the defining psytrance arp characteristic)
- Processed as part of the audio FX chain in Track::processBlock (after MidiFx chain)

## Pitfall Gates
- Gate 3: No heap allocation in render(). Pre-allocate all buffers in prepare().
- Gate 13: PsyArpEngine created/destroyed under stateLock in rebuildFXChain (matches existing pattern).
- Gate 4: New .cpp added to CMakeLists.txt source list.

## Steps
1. Create PsyArpEngine.h with all param enums, voice struct, arp state, delay/reverb state
2. Create PsyArpEngine.cpp with full DSP implementation
3. Modify TrackFXSlot.h: add ActiveType::PsyArp, unique_ptr<PsyArpEngine>, getParamDefsForType("psyarp"), prepare/process/reset/applyInternalParamToDsp cases
4. Modify CMakeLists.txt: add src/engine/PsyArpEngine.cpp
5. Modify AudioEngineCommands_Fx.cpp: add "psyarp" case with default property values
6. Build and verify
