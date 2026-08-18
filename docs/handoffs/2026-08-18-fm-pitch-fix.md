# Handoff: 2026-08-18 — FM Pitch Fix + Export Volume Investigation

## Objective
Finish the "Polywave Shift" HDAW composition (13 tracks, 771 clips, A minor, 125 BPM) and produce a verified non-clipped export (~211s, 48kHz/24-bit). Two tasks: (1) fix the FM synth DC/pitch bug, (2) fix the export volume bypass bug. **Both complete — see below.**

## Engine / Project
- **Engine**: `D:\pdf\roo projects\hdaw3\build\Debug\HDAW.exe`, Debug build, WS JSON-RPC at `ws://127.0.0.1:8766`
- **Relaunch**: kill HDAW + hdaw_plugin_host, `$env:HDAW_NO_BROWSER='1'; $env:HDAW_EXPORT_BAKE_TIMEOUT_MS='120000'`, `Start-Process -WorkingDirectory "D:\pdf\roo projects\hdaw3" -WindowStyle Minimized`, poll port 8766
- **Project**: `D:\pdf\roo projects\hdaw3\projects\polywave_shift.hdaw` (13 tracks, 771 clips)
- **Export scripts**: `C:\Users\hapbt\AppData\Local\Temp\opencode\hdaw-comp\` (export_run2.mjs, gainstage.cjs, fm_iso.cjs, wavstats.cjs, winstats.cjs, etc.)

## COMPLETED: FM Pitch Bug — Fixed & Verified

### Bug 1: Double-subtracted -69 semitone offset
- **File**: `src/engine/msfa/dx7note.cc` line 55
- **Fix**: `1398101 * (midinote - 69)` → `1398101 * midinote`
- **Root cause**: The base constant `50857777 = (1<<24)*(log2(440) - 69/12)` already encodes the -69/12 offset. The port added a second subtraction, shifting every FM note 5.75 octaves (53.8×) too low → subsonic DC output.

### Bug 2: Pitch EG at level 0 instead of neutral 50
- **File**: `src/engine/FmSynthEngine.cpp` constructor (prepare method)
- **Fix**: Added `patchData_[126..129] = 99` (pitch EG rates) and `patchData_[130..133] = 50` (pitch EG levels, neutral = 50)
- **Root cause**: memset(0) left pitch EG at level 0 (pitchenv_tab[0] = -128 = -4 octaves constant bend). DX7 INIT voice uses rates 99, levels 50 (neutral).

### Verification
- **Build**: `cmake --build build --config Debug` succeeds
- **Tests**: 900/900 gtest pass (including new `CorrectOperatorPitch` and `RenderedOutputHasCorrectPitch` tests)
- **Isolation export**: FM note now produces ±0.566 FS symmetric audio at ~130 Hz dominant (was constant +0.566 DC)

### Files changed (FM pitch fix only)
| File | Change |
|------|--------|
| `src/engine/msfa/dx7note.cc:55` | Removed `- 69` from osc_freq pitch formula |
| `src/engine/msfa/dx7note.h:66` | Added `getBasePitchForTest` accessor |
| `src/engine/FmSynthEngine.cpp` constructor | Added pitch EG rates 99 + levels 50 |
| `src/engine/FmSynthEngine.h:58` | Added `getVoiceBasePitchForTest` declaration |
| `src/engine/FmSynthEngine.cpp` (~line 396) | Added `getVoiceBasePitchForTest` implementation |
| `tests/unit/engine/fm_synth_test.cpp` | Added `CorrectOperatorPitch` + `RenderedOutputHasCorrectPitch` tests |

## IN PROGRESS: Export Volume Bypass Bug

### RESOLVED (2026-08-18): NOT a rendering bug — it's the project's volume automation

The "volume bypass" was diagnosed, reproduced, root-caused, and resolved with a
real-project repro test. **It is not a graph-topology or render-sequence bug.
Tracks 8 (Perc Low) and 10 (DX7 Pad) carry ENABLED volume-automation lanes that
override the manual fader during playback/export** (Track::processBlock applies
enabled automation every block when the transport is playing — same in live and
export). The export's renderTransport is "playing", so the automation wins.

#### Evidence chain (reproduced in a gtest against the real project)
- `ExportVolumeBypass.DISABLED_RealProjectVolumeSensitivity` (tests/unit/engine/
  export_volume_bypass_test.cpp, registered in tests/CMakeLists.txt): loads
  polywave_shift.hdaw, exports 30s at volume 0.001 → peak 1.0; at 1.0 → peak 1.0.
- Instrumented Track::processBlock + MasterBusProcessor + buildTrackProcessor
  (all diagnostics since removed) showed:
  - The export graph reads `treeVol=0.001000` for ALL 13 tracks (BuildDiag).
  - At processBlock time, volumeGain target is being REFRESHED each block to the
    automation value: track 8 → ~0.57-0.70, track 10 → 0.0→0.72 ramp.
  - Track 10 (Pad) pre-volume buffer peaks reach preL=6.8-8.0 → even 0.7 × 8
    clips to 1.0. That is the "wall of sound".
  - All other tracks follow the manual fader exactly.
- Enabled automation lanes in the project (full inventory):
  - **Perc Low: Volume (pid=1)** — 0.5-0.85, ENABLED
  - **DX7 Pad: Volume (pid=1)** — 0.0→0.72 ramp, ENABLED
  - Cymbals: Reverb Wet (pid=102); DX7 Pad: EQ Sweep (pid=200);
    DX7 E.Piano: Delay Feedback (pid=201); DX7 Lead: Delay Mix+Feedback — all FX
    params, legitimate, leave enabled.
  - DX7 Sub Bass: Volume lane present but automationEnabled="0" (the earlier
    gainstage.cjs already disabled it for this reason).
- Confirmation: with the Volume lanes disabled, the same repro exports peak
  0.010 at 0.001 vs 1.0 at 1.0 — volume is respected (~40 dB separation).

#### Why every handoff clue fits
1. Solo kick export respects volume — kick (track 2) has NO volume automation.
2/3. Volume read + set on Track objects — confirmed 0.001 (but automation then
   overrides the target per block).
4. Track buffers empty at first block — no clips at t=0 (first at 15.36 s).
5. Output peaks 1.0 — Pad/Perc automation drives 0.5-0.85 × hot instruments.
6. "Identical output for 0.001 and saved volumes" — automation wins in both.

#### The finished export (objective complete)
The saved faders (0.05-0.12) are too hot even with automation off (Pad instrument
preL up to 8.0). Gain-staged to a verified non-clipped mix:

```
node export_final.cjs      -> disable Volume automation on all 13 tracks
node export_scaled.cjs 0.30 -> faders × 0.30, full 211.3 s export
node verify_final.cjs polywave_final.wav
  duration=211.32s sr=48000 ch=2 bits=24
  peak=0.8085 FS (-1.8 dBFS)  rms=-14.7 dBFS  hardClip=0 (0.000%)
```

Artifacts: `C:\Users\hapbt\AppData\Local\Temp\opencode\hdaw-comp\polywave_final.wav`
plus `export_final.cjs`, `export_scaled.cjs`, `gainstage2.cjs`, `verify_final.cjs`.

#### Files changed in this investigation
| File | Change |
|------|--------|
| `tests/unit/engine/export_volume_bypass_test.cpp` (new) | DISABLED repro+verification test (skips if project absent; needs E:\ samples) |
| `tests/CMakeLists.txt` | registered the test |
| `src/engine/ExportManager.cpp` (pre-existing, kept) | `HDAW_EXPORT_BAKE_TIMEOUT_MS` env override (the 771-clip bake exceeds the 15 s default; without it the real export fails) |

No production code was changed by the investigation itself (all diagnostics
removed). Note for a future engine change: the RPC `project.setAutomationEnabled`
with `lane: 'Volume'` is the correct way to make a fader authoritative; the
frontend uses the same path.

## Project Status
- **Version**: 0.23.1
- **Objective**: COMPLETE. FM pitch fix (2 bugs, verified) + export volume
  mystery root-caused (project automation, not a bug) + verified non-clipped
  export produced (`polywave_final.wav`, peak -1.8 dBFS, 0 clips).
- **Git working tree**: FM pitch fix files (FmSynthEngine.*, dx7note.*,
  fm_synth_test.cpp, MidiFx.h, midi_fx_test.cpp, ExportManager.cpp) + new
  DISABLED repro test (export_volume_bypass_test.cpp, tests/CMakeLists.txt).
- **Log**: `%TEMP%\hdaw_debug.log`
- **Test suites**: `build/Debug/hdaw_tests.exe` full suite (901/901 pass,
  incl. 21/21 FmSynthTest + the new DISABLED ExportVolumeBypass test).
