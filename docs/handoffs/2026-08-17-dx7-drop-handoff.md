# Handoff: HDAW DX7 SysEx Import — Drop Handler + Cartridge Voice Picker Done, Full-Suite Remaining

**Project root:** `D:\pdf\roo projects\hdaw3`
**Current version:** 0.23.1 (in sync in `CMakeLists.txt` + `frontend/package.json`)
**Build:** VS 2026 (v180), CMake generator `"Visual Studio 18 2026"` — `cmake --build build --config Debug -- /p:CL_MPCount=8`
**⚠️ Voice-picker batch since `2e39cd8` IS UNCOMMITTED** — see "Working tree state" below.

---

## What is complete (prior session + this session)

### Prior session (DX7 import core)
- `src/engine/Dx7SysexImport.h` + `.cpp` — parser: `parseSingleVoiceSysex` (163 B), `parseCartridgeSysex` (4104 B, 32 voices), `unpackVmemVoice` (packed VMEM → 156-byte engine VCED layout). Checksum bug fixed (checksum byte included via `dataLen + 1`).
- `tests/unit/engine/dx7_sysex_import_test.cpp` — 15 unit tests.
- `src/mcp/McpTools_Audio.cpp` — MCP tool `fm_synth_import_sysex` (`trackId`, `slotIndex`, `filePath`, optional `voiceIndex` for cartridge dumps; returns `voiceName`/`algorithm`/`totalVoices`).
- `frontend/src/components/FileBrowser.tsx` — `.syx` added to `PRESET_EXTS` (classified as "presets").
- `CMakeLists.txt` / `tests/CMakeLists.txt` — source + test wired.

### This session (cartridge voice picker — handoff item #1)
- **Import RPC + MCP tool** now return `{ ok, voiceName, algorithm, totalVoices,
  voiceIndex, voices: [{index, name, algorithm}] }` for cartridge files (Router
  + MCP symmetric; single-voice responses unchanged).
- **FXChain.tsx** — per-slot `cartridgeInfo` state (from the import response)
  + a **Voice** button / named `.fx-voice-list` dropdown on FM slots; selecting
  a voice re-imports with that `voiceIndex`. Per-track reset via a dedicated
  `[selectedTrackIndex]` effect.
- **Tests** — new `FrontendServer.FmSynthImportSysexCartridgeVoices` gtest
  (asserts 32 voices + names + voiceIndex + live processor) and a new E2E
  journey (drop cartridge → picker → pick named voice → `voiceIndex` param).
  Plan: `docs/plans/2026-08-17-dx7-cartridge-voice-picker.md`.

### Verified this session (all evidence on record)
- C++ build ✓ (HDAW_lib + HDAW.exe + hdaw_tests.exe, 8/17 3:47 PM)
- `FrontendServer.*` 11/11 (incl. new cartridge test) · `Dx7SysexImport.*` +
  `FmSynthTest.*` 34/34
- Frontend unit 337/337 · `npm run build` ✓
- E2E `fm-synth-sysex.spec.ts` 5/5 (incl. cartridge voice picker journey)

### This session (frontend drop path + coverage)
- **`src/frontend/router/Router_Audio.cpp`** — NEW RPC `audio.fm_synthImportSysex` (`trackIndex`, `slotIndex`, `filePath`, optional `voiceIndex`). Frontend-facing twin of the MCP tool (MCP parity preserved). Reads file → parses → `slot->fmSynthEngine()->loadPatch(voice->patchData.data())` → returns `{ok, voiceName, algorithm, totalVoices?}`. Error codes: -32602 bad params/slot-not-fm-synth/file-not-found, -32603 engine not init / parse failure.
- **`frontend/src/components/FXChain.tsx`** — `handleDrop` now accepts external `application/hdaw-file` drops: `.syx` extension AND `slot.fxType === "fm_synth"` → calls `audio.fm_synthImportSysex` + `refresh()`. Non-`.syx` / non-FM slots silently ignored. Internal slot-reorder path preserved (first branch, early return). `handleDragOver` sets `dropEffect` to `"move"` (reorder) vs `"copy"` (file). Deps: `[selectedTrackIndex, dragSlot, slots, refresh]` (Gate 5 OK).
- **`frontend/e2e/fm-synth-sysex.spec.ts`** — NEW, 4 tests: (1) RPC import round-trip; (2) **drag-drop `.syx` onto FM slot triggers import** via real `DataTransfer` + `window.rpc.call` spy; (3) non-`.syx` drop ignored; (4) `.syx` on non-FM slot ignored.
- **`tests/unit/frontend/frontend_server_test.cpp`** — NEW `FrontendServer.FmSynthImportSysexRpc` gtest: WebSocket RPC → addFxSlot → read.getFxSlots → import → asserts live processor (`getTrack(0)->getFXChain()[slotIndex]->fmSynthEngine()` non-null, `getType()=="fm_synth"`).

### Verified this session (all evidence on record)
- C++ build `HDAW_lib` + `HDAW.exe` ✓
- `FrontendServer.*` 10/10 · `Dx7SysexImport.*` + `FmSynthTest.*` 34/34
- Frontend unit 337/337 · frontend build ✓
- E2E: new spec 4/4, file-browser 8/8, fx-chain + bottom-tabs 12/12
- Knowledge graph re-indexed (fast) ✓

---

## Working tree state (DO NOT lose this)

**Voice-picker work since `2e39cd8` is uncommitted** (the earlier DX7/drop batch
is committed as `5339206`..`2e39cd8`). Modified:
`src/frontend/router/Router_Audio.cpp`, `src/mcp/McpTools_Audio.cpp`,
`tests/unit/frontend/frontend_server_test.cpp`, `frontend/src/components/FXChain.tsx`,
`frontend/src/components/FXChain.css`, `frontend/e2e/fm-synth-sysex.spec.ts`
Untracked:
`docs/plans/2026-08-17-dx7-cartridge-voice-picker.md` (+ this handoff updated).

**Before starting new work, decide:** commit this batch (one commit — it's a
single feature) or keep working on top of it.

---

## Remaining work (prioritized)

1. **Cartridge voice selection UI — DONE (2026-08-17)**

   The import RPC (`audio.fm_synthImportSysex`) and MCP tool now return, for
   cartridge files, `{ ok, voiceName, algorithm, totalVoices, voiceIndex, voices:
   [{index, name, algorithm}] }`. FXChain stores the parsed list per slot after a
   cartridge drop and renders a **Voice** button (`.fx-voice-btn`) on FM slots,
   opening `.fx-voice-list` with named entries (`name || "Voice N"`, `Alg N`
   tag, `--current` highlight); clicking a voice re-imports with that
   `voiceIndex`. Per-track state resets via a dedicated `[selectedTrackIndex]`
   effect (NOT the `[.., refreshKey]` fetch effect — `refresh()` would wipe it).
   Gates: FrontendServer 11/11 (new `...CartridgeVoices` gtest incl. live
   processor), Dx7+FmSynth 34/34, npm build ✓, vitest 337/337, E2E spec 5/5.
   Plan: `docs/plans/2026-08-17-dx7-cartridge-voice-picker.md`.

2. **Full engine test suite** — Prior run timed out at 5 min. Use the plan doc's exclusion filter (Task 5 Step 1):
   ```
   build\Debug\hdaw_tests.exe --gtest_filter=-CrashRecovery.*:ProxyHealth.*:IsolatedScanner.*:PluginIsolation.*:McpServer.ExportAudioWithClapPluginDoesNotHang:McpServer.ExportAudioWithMultipleIsolatedInstances:McpServer.DiagnosticClapExportMatrix:RenderSequenceRelease.*
   ```
   (the five known-fail proxy tests need a clean engine tree first — see AGENTS.md lesson 20). Verify no regression from the DX7/drop changes.

3. **Plan-doc check** — `docs/plans/` has ~40 plans. `2026-08-17-dx7-sysex-import.md` is the active one (its checkboxes are now effectively complete except Task 5 verification). Older plans (sampler crossfade, clip disk streaming, guard-and-auth, hise-derived master plan) may have unfinished items — scan before assuming done.

4. **Known observability gap (out of scope, but flagged):** `FmSynthEngine::loadPatch` writes ONLY to the engine (`patchData_`, `paramsDirty_`), NOT to the slot ValueTree. So `read.getInternalFxParams` / `read.snapshot` still show pre-import algorithm/params. Also note `applyPendingParams()` (run on render) OVERWRITES `patchData_[134]` (algorithm) and `[135]` (feedback) from the atomics — a loaded patch's algorithm/feedback could be clobbered on next render unless the atomics are updated. If the import should be reflected/observable (and survive renders), this needs `setAlgorithm`/`setFeedback` calls + ValueTree param writes in the import path. **This is a real correctness smell worth a follow-up.**

---

## Key architecture notes (same context as before)

- **Engine → frontend:** JSON-RPC 2.0 over WebSocket (port 8766); serves React SPA over HTTP (port 8765). `frontend/src/rpc.ts` exposes `window.rpc` (E2E seam).
- **Frontend RPC ≠ MCP server.** `rpc.call("audio.*")` routes via `src/frontend/router/Router_Audio.cpp` → `frontend::dispatch` (FrontendRouter). MCP tools (`src/mcp/`) are for external clients ONLY. A user-facing feature needs BOTH (MCP parity rule).
- **`FmSynthEngine::loadPatch(const uint8_t[156])`** — operators in DX7 VCED order (OP6 at offset 0, OP1 at offset 101), 21 B/op, globals at 126+, `patchData_[155]` = op on/off (default 0x3F).
- **`TrackFXSlot` API:** `getType()` returns `juce::String` (NOT a public `fxType` member); `fmSynthEngine()` returns the engine ptr. `project.addFxSlot` accepts `{trackIndex, fxType}` (also `type`/`position`/`slotIndex` spellings) and calls `rebuildTrackFX(trackIndex)`, so the live chain is immediately ready.
- **FXChain UX constraint (learned this session):** the `.fx-slot` elements ONLY render when (a) the FX Chain bottom tab is active (`page.locator(".bt-tab", { hasText: "FX Chain" })`) AND (b) `selectedTrackIndex` is set (click `.th-row` → `selectClip(null, idx)`). FXChain fetches slots on mount, not on notify. Any E2E that must see slots: select track → add slot via RPC → open tab.
- **Drag contract:** FileBrowser sets `application/hdaw-file` = `JSON.stringify({path, name})`; `effectAllowed: "copy"`. Real HTML5 `DataTransfer` (via `page.evaluate`) is required for Playwright drop simulation — a plain `{getData}` object doesn't survive serialization.
- **Mandatory pre-flight:** invoke `hdaw-guard` skill before ANY code change (AGENTS.md lessons 1-22). Read AGENTS.md fully — pitfalls 9, 10, 15, 16, 20 are the ones that bite in this area.
- **Test discipline:** C++ change → gtest (suite is the contract); UI change → Vitest/Playwright; new RPC → gtest + E2E (this session's pattern is the model).
- **Build:** `cmake --build build --config Debug -- /p:CL_MPCount=8`; tests `build\Debug\hdaw_tests.exe --gtest_filter="Suite.*"`; frontend `cd frontend && npm test`, `npm run build`; E2E `npx playwright test e2e/<spec>.spec.ts --reporter=list`. E2E runs against live engine + Vite dev server (no rebuild needed for src changes; C++ changes DO need rebuild).