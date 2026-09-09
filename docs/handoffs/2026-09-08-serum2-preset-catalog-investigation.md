# HANDOFF: Serum 2 preset semantic catalog — investigation results (2026-09-08)

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, VS generator, JUCE 8, React 19 + TS.
> **MANDATORY:** before ANY code change, load the `hdaw-guard` skill (plan-first, success gates, subagent execution, stale-binary discipline).
> **Predecessor context:** `docs/archive/handoffs/2026-08-12-drums-serum-mcp-tool.md` (the "5 identical Serum states md5 e6977e55" blocker this session's work intersects).

## Mission (user's goal)

The user owns a huge Serum 2 preset library and wants a **semantic preset catalog**: select presets of interest, render each to a probe WAV, run the existing local LLM/analysis pipeline (timbre-lib), store the preset state + rendered-feature description so presets can be recalled later. He also proposed driving Serum's own preset/bank UI via the Windows API.

## Library inventory (confirmed on disk)

- `D:\pdf\Xfer\Serum 2 Presets\Presets` — 34,629 files: **16,741 `.serumpreset`**, **11,630 `.fxp`**, plus 2,919 wav / 2,617 png / 354 mid / 104 serumpack / 93 mp3 / 35 vital / 29 phaseplant etc.
- `D:\pdf\Xfer\Serum Presets\Presets` (Serum 1-era) — 19,395 files: **19,038 `.fxp`**, 139 `.serumpreset`.
- Serum 2 loads both formats (user-confirmed for .fxp).

### File formats (byte-level verified this session)

**`.serumpreset` (native Serum 2, "XferJson" container):**
| Offset | Size | Field |
|---|---|---|
| 0–7 | 8 | magic `XferJson` |
| 8 | 1 | 0x00 terminator |
| 9–12 | 4 | **metadata JSON length, little-endian** (e.g. 312) |
| 13–16 | 4 | reserved, zeros |
| 17 | metaLen | JSON metadata: `fileType="SerumPreset"`, `presetName`, `presetAuthor`, `presetDescription`, `product="Serum2"`, `productVersion`, `tags[]`, `url`, `vendor`, `version:7` |
| 17+metaLen | rest | **binary payload** (starts `0e 2e 00 00 02 00 00 00` + 16-byte hash-like salt + data — not zlib, possibly obfuscated/encrypted) |

**`.fxp`** (both Serum 1 and Serum 2 use the same CcnK/FPCh/XfsX layout):
- `CcnK` @0, chunkSize BE @4 (= filesize), `FPCh` @8, Serum-2 layout: chunkSize BE @56 with data @60 (matches when `60+cs == filesize`); standard fallback: chunkSize @52, data @56.
- **The chunk data is zlib-compressed** (`78 01` header) — inflates to the full raw state (~172 KB float-struct dump for ARP KETAMINE).
- Catalog note: the metadata name also appears at offset 28–55 (null-padded).

## CORE BLOCKER (the big finding): Serum 2 VST3 `setStateInformation` silently rejects every external format we fed

**Bottom line: HDAW cannot programmatically inject a Serum preset into Serum 2 via `load_plugin_preset_file` / `setStateInformation`. This is a pre-existing lie in the 08-12 plan — it was never verified against real Serum.**

Evidence:
- We loaded, via the existing/now-extended `load_plugin_preset_file` (`setStateInformation`), **seven candidate formats** on a live Serum 2 slot:
  1. raw `.fxp` chunk (zlib) — 6,515 B
  2. `.serumpreset` binary payload — 1,455 B
  3. inflated raw state — 172,736 B
  4. `3076.`+base64(raw) envelope — 230,381 B
  5. re-zlib'd raw — 3,470 B
  6. **full verbatim `.serumpreset` container** (probe D)
  7. **full verbatim `.fxp` file** (probe E)
  plus host program change (`load_plugin_preset` programIndex 17).
- **All rejected.** The plugin-host log proves the bytes genuinely reach the child: `SET_STATE chunked applied bytes=N` logged for every attempt. The plugin just doesn't change.
- Captured state (via `save_fx_chain` / `getStateInformation`) stayed **constant across every attempt**: `stateBase64` starts `3076.`, 4,107 chars, md5 `e6977e55e27739b4cd2b45ac833ed6c2` — that's **Serum's init/default serialization** (its own proprietary `3076.` + dot-delimited text blob, ~3 KB decoded).
- Render check: two very different presets (SY Galaxy lead vs EMZEE Bass 1) produced **byte-identical WAVs** (md5 `EFAE90B2CB323EC7872A56204C833651`) → the DSP never leaves the init patch.
- The archived 08-12 handoff recorded the same invariant md5 (`e6977e55`) when the user loaded presets **manually via UI** in the packaged app (5 tracks, identical states). So even UI-loaded Serum presets were not being captured differently back then (pre-`STATE_CHUNK` fixes; may behave differently now — see Open questions).

### Why the parser work is still valid
- `PresetFileParser.h` correctly splits all three containers (unit-tested), but the extracted payload is fed to a plugin that only accepts **its own serialization format**, which we cannot synthesize from the files (�.serumpreset payload is salted/obfuscated, `.fxp` chunk is zlib of the raw state, and neither equals Serum's `3076.`-envelope).
- Serum 2 is **VST3-only** on this machine (47 CLAP plugins exist, none Serum) → no CLAP preset/state-extension escape hatch.
- Host-visible programs are 128 × "Prog N" (no real preset names) and appear to be empty slots until presets are loaded via Serum's own browser.

## What was changed in the repo (all verified build+tests green)

1. **`src/mcp/PresetFileParser.h`** (new, header-only): `parsePresetFile(MemoryBlock)` → data/size/format/presetName/error; handles syx (F0 43), `XferJson` SerumPreset, Serum-2-layout + standard FXP; bounds-checked.
2. **`src/mcp/McpTools_FxPreset.cpp`**: `load_plugin_preset_file` rewired through the parser; tool description now mentions `.SerumPreset`; persists parsed payload to `IDs::pluginState` (same as before).
3. **`tests/unit/mcp/preset_file_parser_test.cpp`** (new): 7 tests — syx/Serum2-FXP/standard-FXP/native SerumPreset success + malformed JSON/fileType/payload errors.
4. **`tests/unit/mcp/tool_registry_test.cpp`**: registry tests incl. `FxPresetFileToolMentionsSerumPreset` and `TogglePluginEditorToolMentionsEditor`.
5. **`src/mcp/McpTools_FxSlot.cpp`**: new MCP tool **`toggle_plugin_editor`** (trackId, slotIndex) → `getAudioGraphCommands().toggleFXEditor` — UI-parity tool (frontend `toggleFXEditor` RPC existed; MCP lacked it).
6. **`.gitattributes`**: added `*.bat text eol=crlf` / `*.cmd text eol=crlf` and renormalized `build-fast.bat`, `mcp-launch.bat`, `frontend/build.bat`, `run_fast_tests.bat`, `scripts/time-sync.cmd` from LF-only to CRLF. **Root cause of the session's "can't run build scripts" failure:** the repo-wide `* text=auto eol=lf` had rewritten Windows batch files to LF → `cmd.exe` misparsed prose lines as commands. This is now fixed and is a real bug-fix worth keeping (lesson: Windows scripts must stay CRLF in this repo).

## UI-automation feasibility (user's original idea — now the ONLY path to load presets)

- **`toggle_plugin_editor` works end-to-end even from the headless MCP engine**: it opened a real Serum window in the isolated child — `hdaw_plugin_host.exe (pid 22160) "Plugin Editor" window_id 789446` (`cua-driver list_windows` confirms). The child runs its own GUI, so a headless engine can still pop real plugin windows.
- **`cua-driver` MCP sees the real interactive desktop** and can list/focus/click/type/drag windows (get_window_state, bring_to_front, press_key, type_text, click, move_cursor). **`windows-mcp` Screenshot/Click DO NOT see any windows** (its server runs in a different desktop/session context — "No windows found") — do not use windows-mcp for desktop targeting; use cua-driver.
- **Serum's editor canvas is custom-drawn (JUCE)**: UIA tree from cua-driver only shows the window chrome (min/max/close buttons) — no search-box element. Automating it means coordinate-based clicking with screenshot ground truth.
- **Vision is not available in this agent model** ("Current model does not support images") — we could not visually locate Serum's search box. Tried Windows WinRT OCR via PowerShell (works in principle — `OcrEngine::TryCreateFromUserProfileLanguages` returns non-null — but the `Await`/AsTask helper kept failing: filtered the non-generic `IAsyncAction` AsTask overload → `MakeGenericMethod` threw "not a GenericMethodDefinition"). Fix that helper (filter `IAsyncOperation` + 1 generic param) and OCR becomes the coordinate source. No tesseract installed; Python 3.14 available but pytesseract not installed.

## Recommended path forward (Option A — semi-automated capture loop)

1. **User selects presets in Serum's own browser** (its keyword/multi-keyword search is the best selector and the only reliable load path).
2. HDAW captures the **live plugin state after the load** (Serum's own `3076.` serialization — the one format it accepts) via existing `save_fx_chain`/`getStateInformation` path; render a standardized probe; run `timbre-lib/analyze_probe.py` (+ LLM); store state + features + description (delete WAVs after analysis, per user).
3. **Recall** = `setStateInformation` with the stored Serum-native blob → should round-trip (this is the natural next verification: capture init state, restore it, compare).
4. Option B (full automation) later: use cua-driver to click Serum's preset-name bar → type keywords → step results, piloting after Option A proves the capture/store loop on a UI-loaded preset.

## Open questions / outstanding verifications
- Does a **UI-loaded** Serum preset (user manually loads via the now-open editor window) capture as a *different* state blob than `e6977e55`? (Test: user loads a preset in the opened window → `save_fx_chain` → compare md5. If it differs, the capture loop is fundamentally sound and only *loading* needs UI.)
- Does the `3076.` state round-trip through `setStateInformation` (capture → restore → compare)?
- Fix the WinRT OCR Await helper (select the generic `IAsyncOperation<T>` AsTask) or pip-install pytesseract for coordinate grounding.

## Scratch/artifacts left on disk (safe to delete)
- `compositions/probe_rawstate.fxp`, `probe_enveloped.fxp`, `probe_zlib.fxp`, `probeD_fullserumpreset.fxp`, `probeE_fullfxp.fxp` (synthetic probe files)
- `compositions/serum_editor_shot.png` (cua-driver screenshot), `compositions/ocr_serum.ps1` (OCR helper, last implementation as-is on disk)
- `compositions/serum2_native_galaxy.wav`, `serum2_native_bass1.wav`, `serum2_prog17.wav` (probe renders)
- `serum_probe_galaxy.hdaw3` (repo root, saved project)
- `%APPDATA%\hdaw\chains\user\*` test chains: SY_Galaxy_Basics_Serum2_native.json, EMZEE_Bass_1_native.json, Probe_*.json, Prog_17_test.json, Serum_Semantic_Probe_-_ARP_KETAMINE.json
- MCP session currently has a live "Serum UI Probe" track (track 3) with a Serum 2 editor window open (pid 22160) — close/cleanup when done with it.

## Session-derived lessons (add to AGENTS.md pitfalls if valuable)
- **Windows .bat/.cmd files in this repo must stay CRLF**; `* text=auto eol=lf` rewrites them into cmd.exe garbage. `.gitattributes` now pins it; renormalize any new Windows scripts.
- **MCP-headless engines can still pop real plugin windows** (child-host GUI) — `toggleFXEditor` is the door for UI automation.
- **cua-driver works on the interactive desktop; windows-mcp does not** (session isolation). Coordinate targets: window_id from `cua-driver list_windows`.
- **Serum 2 VST3 preset injection via setStateInformation is a dead end** — treat "load preset file" as a UI/editor-driven operation, not a state-bytes operation.
