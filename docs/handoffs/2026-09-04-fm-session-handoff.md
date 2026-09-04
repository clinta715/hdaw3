# Handoff: FM / DX7 patch pipeline completed (persistence, export, sweep, search)

**Date:** 2026-09-04
**Status:** completed / verified
**Related commit:** (this session) — FM/DX7 patch pipeline + library search + MCP stdio shutdown fix

## TL;DR

The FM patch work that was "paused" in the prior handoff is now **complete and verified**. DX7 and Virus patches sweep through the internal `fm_synth` / `sub_synth` engines, render offline, get per-role timbre analysis (optional local Qwen prose), and become **searchable descriptions** via the patch-library + `search_library` flow. Three real engine bugs were found and fixed along the way, and the `engine_restart` shutdown crash was root-caused with cdb and fixed.

## What this session shipped

1. **FM patch persistence** — `fm_synth_import_sysex` / `fm_synth_load_preset` now persist the 156-byte DX7 patch into the slot ValueTree (`fmPatchData`; base64) via a new `ProjectCommands::setFmPatch`, and restore it on rebuild/export/save-load (`TrackFXSlot::loadFmPatchFromTree` + staged consumption in `prepare`). Tree-copy renders (export/gain-stage/audition) now actually hear the imported patch.
2. **Offline render reset fix** — `FmSynthEngine::prepare()` re-seeded the DX7 init patch on every call, and `TrackFXSlot::reset()` (after `Track::releaseResources()`) wiped the loaded patch right before offline renders, so every export was the same init tone. Fix: `initPatchSeeded_` — seed init exactly once per engine; a later `prepare`/`reset` preserves the loaded patch. Also `loadPatch` clears `paramsDirty_` so the imported patch is authoritative over prepare-time default param pushes.
3. **Cartridge VMEM unpack fix** — `unpackVmemVoice` per-op tail was misaligned (output level read from the keyvel field with a 3-bit mask → near-silent cartridge imports; the real output-level byte landed in the osc-mode slot). Rewritten to the authoritative Dexed `sysex-format.txt` layout. Cartridges now render audibly and correctly.
4. **Raw 4096-byte bank support** — the ~22% of `d:\pdf\dexed presets` that are raw VMEM banks (no `F0 43` framing) now import (C++ `parseCartridgeSysex` + MCP routing + `timbre-lib/dx7_patch.py`).
5. **Patch sweep runner** — `timbre-lib/sweep_dx7_patches.py`: connects to the engine (spawns `HDAW_headless.exe --mcp-stdio`), builds a probe track with an `fm_synth` or `sub_synth` slot, renders a role probe via `export_audio`, runs `analyze_probe.py` (optional Qwen prose via `--gguf`), and writes aggregated reports. `--sidecars` writes `<patch>.dx7.json` / `.virus.json` next to each source so `FileLibraryManager` ingests them. Do-not-downgrade guard keeps curated Qwen prose. `--engine fm_synth|sub_synth`, `--bank/--names/--shuffle/--limit/--cart-voices/--reuse-wavs`.
6. **Searchable descriptions** — the sweep's Qwen prose lands in patch sidecar `description`; `add_library type="patch"` + `scan_library` + `search_library` now find patches by what they sound like.
7. **Multi-word library search** — `FileLibraryManager::search` was a whole-string `contains`; now tokenized AND (all query words must appear in the entry text). Strict superset, no single-word regression.
8. **MCP stdio shutdown crash fix** — `engine_restart` → `exit(42)` destroyed the still-running `ReaderThread` (`QThread::~QThread` abort). Root-caused with cdb through 4 failure modes (double-`_close` watson, UCRT fd-table-lock deadlock, Qt fd dup, 16 KB buffer read block). Final fix: the Windows reader polls stdin (`PeekNamedPipe` + exact-avail `ReadFile`) and observes `stopped_`, so `stop()` needs no fd/handle manipulation. Verified: `engine_restart` exits cleanly with code 42.

## Verified

- `FmPatchPersistence.*` 3/3, `Dx7SysexImport.*` 20/20, `FileLibraryTest.*` 37/37 (`FileLibrary*` 44/44), affected engine suites 47 pass / 6 env-gated skip.
- Sweep E2E: DX7 single/cartridge/raw banks and Virus (TI/B-C/extensionless) all render audibly + distinctly per patch; `search_library` finds patches by Qwen prose ("polyphonic" → 4, "dark" → 3, "brass" → 2).
- `engine_restart` under cdb: clean exit 42, no crash/hang.

## How to use

```
py -3.14 timbre-lib/sweep_dx7_patches.py --role bass --names bass --limit 40 --sidecars --gguf timbre-lib/Qwen2.5-3B-Instruct-Q4_K_M.gguf
py -3.14 timbre-lib/sweep_dx7_patches.py --engine sub_synth --role lead --dir "D:\pdf\Virus Presets" --sidecars --gguf ...
```
Then `add_library type="patch"` + `scan_library` + `search_library` (e.g. "gritty dark bass").

## Notes / follow-ups

- **Reconnect needed:** this session's `engine_restart` test crashed the running engine (pre-fix binary) and dropped the opencode `hdaw` MCP server. Reconnect via opencode `/mcp` or session restart; `mcp-launch.bat` then ships the freshly built binary (with all fixes).
- `BRASS-13` voice 0 renders silent in export despite valid patch data — a single-patch engine render anomaly, deferred.
- Virus `virus_patch.py` still writes deterministic role descriptions (not Qwen prose); the sweep upgrades them with `--gguf`.
- Multi-word search live-verification (e.g. `search_library "gritty dark bass"`) is pending engine reconnect.
- The broader uncommitted working tree (sub-synth enhancements, automation-PID, frontend WIP, etc.) is untouched and separate from this commit.