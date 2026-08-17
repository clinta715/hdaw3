# Handoff: WASAPI/COM init fix — choppy audio, only DirectSound devices selectable

Date: 2026-08-16. User report: "audio is choppy and stutters, only DirectSound
device(s) seem selectable (no windows audio devices)".

**Status: diagnosis complete, fix implemented, live-verified on Debug binaries,
full gtest suite green (862/862). Committed as part of v0.23.1.** The packaged
Electron app still ships the pre-fix engine (`build/RelWithDebInfo/` is stale —
rebuild that config + repackage; see AGENTS.md "stale-frontend trap").

---

## 1. Root-cause chain (every link verified)

1. **No COM init on the main thread.** JUCE 8's WASAPI device type never calls
   `CoInitialize` (zero hits in `juce_WASAPI_windows.cpp`; the
   `ComSmartPtr::CoCreateInstance` jasserts `CO_E_NOTINITIALIZED` in
   `juce_ComSmartPtr_windows.h:133`). Since the `QApplication`→`QCoreApplication`
   refactor (dd76505) Qt no longer `OleInitialize`s the main thread, and HDAW
   never initialized COM itself.
2. **Empty WASAPI scan, cached forever.** The first `CoCreateInstance` fails
   with `CO_E_NOTINITIALIZED`; `scanForDevices()` sets `hasScanned = true` and
   caches the empty `devices` list for the process lifetime — the WASAPI types
   stay in `getDeviceTypes()` but enumerate zero endpoints.
3. **DirectSound fallback + stale-name restore.** `initialiseWithDefaultDevices`
   silently falls through to DirectSound (needs no COM): "Primary Sound
   Driver"/"Primary Sound Capture Driver", 2560-sample buffer, **~58 ms**
   emulated latency, jittery callbacks → audible choppy/stutter. The
   saved-device restore then made it permanent: it captured
   `getAudioDeviceSetup()` under DirectSound, switched the type to "Windows
   Audio (Low Latency Mode)", applied the DirectSound-era names → "No such
   device: Primary Sound Driver" → `initDefaultDevice()` → DirectSound again.

## 2. What was implemented (committed at v0.23.1)

| Fix | Files | What it does |
|-----|-------|-------------|
| A (COM init) | `src/common/ScopedComInit.h` (new), `src/main.cpp`, `src/main_headless.cpp`, `tests/test_main.cpp` | RAII `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` as the first statement of every entry point; `RPC_E_CHANGED_MODE` tolerated (COM already up = someone else owns it) |
| B (restore order) | `src/engine/AudioEngine.cpp` (`initialize`, saved-device restore) | Switch driver type FIRST, re-fetch setup after, apply a saved device name only when present in the new type's device list (empty = JUCE default) |
| Docs | `AGENTS.md` (lesson 22), `docs/pitfalls-juce.md` (new section) | Pitfall documented with the JUCE-source evidence |

## 3. Verification (all on this machine, Debug)

- Before: `audio.getOutputDevices` → `[]` under WASAPI; setup on DirectSound
  (2560 buffer, 58 ms). After: WASAPI enumerates
  `["Speakers (Focusrite USB Audio)", "H27D9 (NVIDIA High Definition Audio)"]`.
- Runtime open via RPC: `audio.setOutputDevice("Speakers (Focusrite USB
  Audio)")` under "Windows Audio" → **10 ms latency / 441-sample buffer**.
- Startup log now shows `saved audio device restored: driver=Windows Audio
  (Low Latency Mode)` (was "saved device restore failed: No such device:
  Primary Sound Driver").
- `hdaw_tests.exe`: **862/862 pass** (no engine processes running → no
  pipe/shm slot collisions, per lesson 20).

## 4. Notes / follow-ups

- **RDP caveat:** in an RDP session the WASAPI endpoint set is session-scoped
  (render-only "Remote Audio"); DirectSound's legacy enumeration sees more.
  That is correct behavior, not a regression.
- **Registry during diagnosis:** the QSettings audio keys were mutated by the
  diagnostic probes (`audio/driverType` + `audio/outputDevice`); both restored
  to their pre-probe state afterwards. Check `HKCU:\Software\HDAW\HDAW\audio`
  before trusting the app to remember a device.
- **Packaged app:** engine ships from `build/RelWithDebInfo/` via
  `electron-builder.yml` `extraResources`. Rebuild RelWithDebInfo + repackage
  before the packaged app gets the fix.
- **Standing:** any new Windows entry point touching `AudioDeviceManager` (or
  any JUCE COM path) must construct `ScopedComInit` first.
