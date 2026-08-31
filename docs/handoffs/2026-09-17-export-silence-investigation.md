# Handoff: Export Silence Bug (0.6s Cutoff) — RESOLVED

**Date:** 2026-09-17 (investigation), resolved same session
**Status:** ✅ RESOLVED — root cause found, fixed, and verified end-to-end
**Related:** `docs/handoffs/2026-09-02-export-silence-0.6s.md` (original symptom report)
**Supersedes:** the earlier "ClipSourceProcessor bounds check" theory in this file — **disproven**, see §3.

## TL;DR

The 0.6s (frame 28800 @ 48kHz) export cutoff was **not** a clip-bounds or
transport bug. A project carried an out-of-range internal FX parameter
(**reverb `param_0` (Room Size, valid [0,1]) = 900.0** on track `AtmoClip` in
`renders/forest_cathedral.hdaw`). `TrackFXSlot` pushed it **unclamped** into
`juce::dsp::Reverb` (Freeverb). Freeverb maps roomSize → comb feedback
≈ `0.7 + 0.28 × roomSize` ⇒ loop gain ≈ **252** ⇒ the render **diverges
exponentially** (~0.02 RMS → 1098 → 7e13 → `inf` → `NaN` within 0.6s of
audio) ⇒ the WAV writer converts NaN to **zeros**. Hence: correct-length
file, healthy audio until exactly 0.6s, then hard digital silence —
regardless of the rest of the project's content, because the runaway track
poisons the master sum.

Fix: clamp internal FX params to their documented ranges at every entry
point (`TrackFXSlot` load/set/prepare + write-side in
`AudioEngineCommands::setFxSlotParam`). Verified: the same export now
renders **full 10.00s** with bounded RMS (max 1.27, no inf/NaN).

## 1. Evidence chain

1. **Reproduced on the current tree** (`build/HDAW_headless.exe`, Ninja
   RelWithDebInfo): load `renders/forest_cathedral.hdaw` via WS JSON-RPC
   (`project.loadProject`), export 10s (`export.audio`) → WAV cuts at
   exactly frame 28800, peak 1.000 FS.
2. **Per-10-block RMS probe** (the uncommitted `ExportDebug` log in
   `ExportManager::renderThreadFunc`) showed the divergence:
   block 10 (0.107s) rms=0.020 → block 20 (0.213s) rms=1097.9 →
   block 30 rms=6.9e13 → blocks 40–50 rms=inf → block 60+ rms=-nan(ind).
   NaN → zeros in the written file. The "0.6s" is simply where NaNs first
   reach the writer.
3. **Track bisection** (mute-all, unmute one track at a time, export 1.5s):
   only **track 19 `AtmoClip`** diverges (cut @ 0.600s, 1.000 FS); all
   other tracks are silent (no notes yet) or clean.
4. **Causality A/B**: AtmoClip reverb `param_0` 900 → **0.9** = CLEAN
   (peak 0.005 FS); restored to **900** = DIVERGES (cut @ 0.600s,
   1.000 FS). Same track, same input, only the parameter differs.
5. **Code**: `TrackFXSlot::prepare()` / `loadParamsFromTree()` /
   `setInternalParam()` all pushed `param_N` values into DSP **raw**;
   reverb Room Size def is [0,1] but nothing enforced it.
6. This also explains the 2026-09-02 anomaly "failing files peak at 32768
   (full scale) while working files peak ~10596": failing exports were
   **clipping as they diverged** before NaN onset.

Repro tooling kept in `repro_out/` (`repro_driver.py`, `bisect_driver.py`,
`causality_driver.py` — all drive the engine over `ws://127.0.0.1:8766`).

## 2. The fix (uncommitted in working tree, 2026-08-31 session)

| File | Change |
| ---- | ------ |
| `src/engine/TrackFXSlot.h` | New `clampToParamDef(idx, value)` helper (defs-driven `juce::jlimit`); applied in `prepare()` (whole-vector clamp before the DSP switch), `loadParamsFromTree()`, and `setInternalParam()`. |
| `src/engine/AudioEngineCommands_Fx.cpp` | Write-side clamp in `setFxSlotParam` BEFORE `setProperty("param_N")` — future writes (RPC `project.setFxSlotParam`, MCP `set_internal_fx_param`/`set_fx_param` — same command layer) can no longer persist out-of-range values. Plugin/none slots pass through (no defs). |
| `tests/unit/engine/internal_fx_param_clamp_test.cpp` (+ `tests/CMakeLists.txt`) | Regression suite `InternalFxParamClamp`: reverb param 900/-5 via poisoned tree → all-finite bounded render; `setInternalParam` clamp read-back; delay feedback clamp. RED pre-fix, GREEN post-fix. |
| `src/engine/ClipSourceProcessor.h` | Reverted the investigation's uncommitted `>=`→`>` bounds-check experiment (output-neutral, based on the disproven theory). Now identical to HEAD. |

`setAutomationParam` was already safe (denormalizes from defs) — untouched.

**Verification:** new suite 4/4; `*Fx*:*FX*:*Reverb*:*Track*` 139/139;
MCP/commands coverage 109/109; end-to-end repro pre-fix cut @ 0.600s →
post-fix **full 10.00s**, RMS trace healthy (block 20: 0.054 vs 1097.9
pre-fix; max 1.27, finite throughout).

## 3. Why the earlier "ClipSourceProcessor bounds check" theory was wrong

- It claimed failing projects contain clips with `duration ≈ 0.6s`.
  `renders/forest_cathedral.hdaw` contains **zero** such clips (durations
  0.15s–301.6s; 13 MIDI clips + a 19.7s drone start at t=0 and must play far
  past 0.6s).
- A 0.6s clip ending at 0.6s is *correct* behavior, not a bug.
- Its "Option A" (remove the early return) is a **functional no-op**: the
  downstream `audibleRemaining`/`numToRead` clamp clears exactly the same
  samples when `clipLocalSample >= durSamples`.
- The theory never explained the full-scale pre-cutoff peaks, the 0.49s /
  3.46s cutoffs of `test_mid`/`test_long_notes` (different runaway rates
  from different param values), or the all-silent `test_fresh`.
- The 09-17 session never rebuilt the engine (no `ExportDebug` string in
  any binary it ran); the theory was untested code reading.

## 4. How the poison parameter likely got in

`setFxSlotParam` had no range validation; `param_0=900` on a reverb is
plausibly a **units mix-up** — e.g. an intended "900 ms" or "size in
seconds" value (PsyArp's reverb size IS in seconds, def [0.1, 10]) written
into TrackFXSlot's normalized [0,1] Room Size. Whatever wrote it, the
engine now clamps at all entry points, so this class of bad value can no
longer reach recursive DSP.

## 5. Follow-ups / open items

1. **`mcp-launch.bat` points at a stale build path (live lesson-21 trap).**
   It copies `build\Debug\HDAW_headless.exe` (an Aug-30 VS-generator Debug
   relic; the active generator is Ninja → `build\HDAW_headless.exe`
   RelWithDebInfo). Every future MCP session launched via mcp-launch runs
   the stale engine **without this fix** (and with Debug Qt DLLs on PATH
   that don't match a release exe). Repoint `BUILD_DIR`/`SRC` at `build\`
   or rebuild the Debug config deliberately.
2. **Test-suite config drift.** AGENTS.md says run `build/Debug/hdaw_tests.exe`,
   but the Debug dir is a stale relic; the current suite is
   `build/hdaw_tests.exe` (RelWithDebInfo). 4 `RealtimeSafety.*` tests fail
   there purely because `BufferCheck` is `#if JUCE_DEBUG` (compiled out
   under NDEBUG) — they need a Debug-config run. Also known-flaky in any
   config: `McpServer.ExportAudioWithClapPluginDoesNotHang`,
   `McpServer.ExportAudioWithMultipleIsolatedInstances`,
   `McpServer.DiagnosticClapExportMatrix` (external CLAP children),
   `PluginManagerInProcessVst3.InstantiatesRealVst3ByIdentifier` (needs a
   real VST3 registered).
3. **`ExportDebug` per-10-block RMS log** in `ExportManager.cpp` is still
   uncommitted debug instrumentation (non-RT render thread, cheap). Keep
   for future export triage or remove before committing — decide at commit
   time.
4. **Poisoned saves stay poisoned by design**: legacy projects keep their
   raw out-of-range `param_N` in the tree (ReadModel reports it); the DSP
   now receives clamped values; a re-save via `setFxSlotParam` writes
   clamped values. Optionally add a load-time sanitizer later.
5. The failing WAVs in `renders/` are Aug-31 artifacts of the pre-fix
   engine; re-render any that matter after the fix lands.
6. Full-resolution commit of the 45-file in-flight working tree (including
   this fix) is pending; this doc describes the state as left in the tree.
