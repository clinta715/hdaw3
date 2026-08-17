# DX7 Cartridge Voice Picker — Implementation Plan

> Addresses handoff `docs/handoffs/2026-08-17-dx7-drop-handoff.md` remaining item #1.

## Goal
After dropping a 32-voice `.syx` cartridge onto an FM synth slot, let the user
choose which voice loads — via a voice-picker dropdown on the slot that lists
the cartridge's voices by name and re-imports the selected `voiceIndex`.

## Background
- The MCP tool `fm_synth_import_sysex` and the frontend RPC
  `audio.fm_synthImportSysex` already accept `voiceIndex`. The FXChain drop
  handler always imports voice 0 because the UI has no way to choose.
- `read.snapshot` FX slots do NOT expose cartridge voices, so the picker must
  get its data from the import RPC response (voices are parsed server-side for
  free — no new parse path needed).

## Success Gates (all must pass with evidence)
- [x] Gate A: `cmake --build build --config Debug` succeeds. — BUILD_EXIT=0; `hdaw_tests.exe` + `HDAW.exe` freshly linked 8/17/2026 3:47 PM.
- [x] Gate B: `build\Debug\hdaw_tests.exe --gtest_filter="FrontendServer.*"` — all pass (11/11), including new `FrontendServer.FmSynthImportSysexCartridgeVoices` asserting `totalVoices==32`, `voiceIndex==5`, `voices[]` names/algorithms, live-processor seam. Re-run by orchestrator: `[  PASSED  ] 11 tests`.
- [x] Gate C: `build\Debug\hdaw_tests.exe --gtest_filter="Dx7SysexImport.*"` — all pass (parser untouched). Re-run with `FmSynthTest.*`: `[  PASSED  ] 34 tests`.
- [x] Gate D: `cd frontend && npm run build` succeeds. — `tsc -p tsconfig.node.json && vite build` → `✓ 163 modules transformed`.
- [x] Gate E: `cd frontend && npm test` — `38 test files passed, 337 tests passed` (vitest run).
- [x] Gate F: `npx playwright test e2e/fm-synth-sysex.spec.ts` — **5/5 passed (12.4s)**, including the new "cartridge drop shows a voice picker and picking a voice re-imports it" (drop cartridge → `.fx-voice-btn` → named list → click LEADX → import call with `voiceIndex===4`).
- [x] Gate G: Gate 5 audit — `selectCartridgeVoice` deps `[selectedTrackIndex, cartridgeInfo, refresh]` (reads `cartridgeInfo.get` pre-await); `handleDrop` writes only via functional `setCartridgeInfo` and reads locals post-await. Reset lives in a dedicated `[selectedTrackIndex]` effect (NOT the `[.., refreshKey]` fetch effect, which would wipe the info on every `refresh()`).
- [x] Gate H: Gate 8 audit — new CSS mirrors `.fx-preset-*` conventions (`var(--token, fallback)` only, no bare hex).

## Dependency Map
- **Blast radius (verified via codebase-memory):** `parseCartridgeSysex` has exactly two callers: `src/mcp/McpTools_Audio.cpp` and `src/frontend/router/Router_Audio.cpp`. Both import handlers are the only places that shape the import response. The only UI consumer of the import response is `FXChain.tsx`.
- **Upstream:** `audio.fm_synthImportSysex` is called from `frontend/src/components/FXChain.tsx` (drop handler) and the E2E spec. MCP tool `fm_synth_import_sysex` is called by external MCP clients.
- **Downstream:** `FmSynthEngine::loadPatch` (message thread — verified, no audio-thread change). Parser returns trimmed names already.
- **God nodes:** none (both handlers are leaves; loadPatch unchanged).
- **Community boundaries crossed:** none new — all within the audio-RPC/frontend-FX area.
- **Projections affected:** ReadModel snapshot — NO change (import does not write ValueTree; out of scope, flagged as handoff item #4).
- **SPSC paths touched:** none (import runs on message thread; no audio-thread code changed).
- **MCP parity:** the MCP import tool is extended with the same `voices[]` response so external clients can enumerate cartridge voices (parity with the new UI picker).

## Contract — import response (cartridge only), Router + MCP must match
```json
{
  "ok": true,
  "voiceName": "<trimmed name of loaded voice>",
  "algorithm": 5,
  "totalVoices": 32,
  "voiceIndex": 7,
  "voices": [ { "index": 0, "name": "<trimmed>", "algorithm": 5 }, ... ]
}
```
Single-voice files: response unchanged (no `totalVoices`/`voiceIndex`/`voices`).
The frontend shows the picker only when `Array.isArray(resp.voices) && resp.totalVoices > 1`.
`voiceIndex` is the resolved index actually loaded (backend source of truth).

## Pitfall Gates Triggered
- **Gate 2 (unimplemented path):** the picker's select action re-runs the proven
  import RPC with `voiceIndex` — same code path as the existing drop, already
  covered by `FrontendServer.FmSynthImportSysexRpc` + E2E. New E2E asserts the
  exact `voiceIndex` param. No gap.
- **Gate 5 (stale closures):** `cartridgeInfo` state must be in the deps of
  `selectCartridgeVoice`; `handleDrop` uses functional `setCartridgeInfo` and
  reads only locals after `await`. Add `setCartridgeInfo(new Map())` when
  `selectedTrackIndex` changes (mirrors the existing slot-fetch effect).
- **Gate 8 (CSS tokens):** new classes mirror `.fx-preset-*` exactly — `var()`
  tokens with the file's existing fallback style. No new raw-hex-first rules.
- **Gate 4 (stale binary):** C++ changes → full Debug rebuild before E2E; E2E
  runs against the live engine, so the binary must be fresh.

## Anti-Pattern Scan
- No N separate RPC calls — one import call per selection.
- No `syncSnapshot` — local component state only.
- No full-tree walks — `Map.get(slotIndex)`.
- No `rebuildRoutingGraph` — import path does not rebuild.
- No raw hex in new CSS.

## Steps
1. **C++ (subagent A):** Router_Audio.cpp + McpTools_Audio.cpp — hoist the
   resolved `vi` out of the cartridge branch; build `voices[]` from the parsed
   vector (index/name/algorithm, names already trimmed); add `voiceIndex` +
   `voices` to the response when the file is a cartridge. Add a gtest
   (frontend_server_test.cpp) that writes a 4104-byte cartridge, imports it,
   asserts `totalVoices==32`, `voices.size()==32`, a known name/algorithm, and
   `voiceIndex` honors the request. Build + run `FrontendServer.*`.
2. **Frontend (subagent B):** FXChain.tsx — add `CartridgeInfo`/`CartridgeVoice`
   types + `cartridgeInfo` + `voiceMenuSlot` state; capture the import response
   in `handleDrop`; clear on track change; add `selectCartridgeVoice`; render a
   Voice button + dropdown (name || "Voice N", algorithm tag, current highlight)
   on `fm_synth` slots that have cartridge info. Add `.fx-voice-*` CSS mirroring
   `.fx-preset-*`. Run `npm run build` + `npm test`.
3. **E2E (subagent C, after A+B):** fm-synth-sysex.spec.ts — add a
   `writeCartridgeSysex` helper (32 voices, distinct names) + a test:
   setupFmSynthSlot → spy RPC → drop cartridge → picker lists names → click a
   named voice → assert import call carries that `voiceIndex`. Run the spec.

## Completion Contract
- All gates A–H pass with evidence.
- Diff scanned for anti-patterns; dependency map confirmed (no silent breakage).
- MCP parity preserved (import tool returns voices).
- Knowledge graph refreshed after structural change (new RPC response shape is
  data-only — `index_repository` fast after merge).