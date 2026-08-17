# Handoff: FM analysis channel — end-to-end (C++ → frontend)

Date: 2026-08-17. Continues from `2026-08-17-fm-synth-audio-fixes.md`.
Completed the SPSC analysis channel (§4.2 from the original backlog): live
per-operator EG levels flow from the audio thread through lock-free atomics,
broadcast at 30 Hz over WebSocket, and render in a React bottom-panel tab.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Version: **0.23.1**
- Full suite at close: **814/814 PASS** (excluding plugin-isolation tests
  which require live child processes — these fail on this machine due to
  orphaned `hdaw_plugin_host.exe` from stale engines, per lesson 20).
  Baseline was 871/872 under VS 2022/v143; the VS 2026/v180 build omits
  the `RenderSequenceRelease` test (it requires isolated plugin children).
- FM synth suite: **25/25 PASS** (10 original + 9 audio-quality fixes +
  3 peekVoiceStatus + 3 analysis-channel).
- Frontend: **337/337 PASS** (38 test files, including `analysisStore.test.ts`).
- Frontend build: clean Vite production build.
- Knowledge graph: refreshed at session start (fast mode).

### 1.1 Build environment note

This machine has **VS 2026 (v180)** only — no v143 (VS 2022) build tools.
CMake generates with `"Visual Studio 18 2026"`. JUCE 9.x (fetched by
CMake) dropped `virtual` from `AudioPluginFormat::createInstanceFromDescription`,
requiring the `override` removal in `CLAPPluginFormat.h` (commit `8314d28`).
If you switch to a machine with v143, regenerate the build directory.

## 2. Commits this session (new since `4861665`)

| Commit | Contents |
|--------|----------|
| `aaced88` | **feat(fm): analysis channel C++ pipeline** — `Dx7Note::getEgLevel()` (Q24 log→linear via `Exp2::lookup`), `FmSynthEngine` captures per-operator `opEgLevel_[6]` + `analysisVoiceCount_` + `analysisAlgorithm_` atomics at end of `render()` (O(96) fixed cost), `FmAnalysisSnapshot` in `ReadModel.h`, `getFmAnalysis(trackIndex)` in `ReadModelImpl`, `notify::FmAnalysis` channel, 30 Hz `analysisTimer_` in `FrontendServer`, `read.getFmAnalysis` RPC. Tests: 19 FM + 34 regression. |
| `8314d28` | **fix(build): JUCE non-virtual `createInstanceFromDescription`** — removed `override` specifier in `CLAPPluginFormat.h:43`. Required for VS 2026 / JUCE 9.x. |
| `ed744a1` | **feat(fm): frontend analysis panel** — `analysisStore` (Zustand), `FmAnalysisPanel` (6-operator vertical bars + voice count + algorithm), `BOTTOM_TAB_IDS` wired, `main.tsx` subscribes to `notify.fmAnalysis`. 337 frontend tests pass. |

## 3. Architecture — how the analysis channel works

### 3.1 C++ side (commit `aaced88`)

```
Dx7Note::getEgLevel(op)          // Q24 log → linear via Exp2::lookup()
    ↓
FmSynthEngine::render()          // captures AFTER render loop, O(96)
    ↓ (lock-free atomics)
std::atomic<float> opEgLevel_[6]
std::atomic<int>   analysisVoiceCount_
std::atomic<int>   analysisAlgorithm_
    ↓
ReadModelImpl::getFmAnalysis()   // reads atomics into FmAnalysisSnapshot
    ↓
FrontendServer::onAnalysisTimer() // 30 Hz QTimer, broadcasts to all clients
```

**Key files:**
- `src/engine/msfa/dx7note.h` / `.cc` — `getEgLevel()`, `getOpPhaseForTest()`
- `src/engine/FmSynthEngine.h` / `.cpp` — analysis atomics, capture in render
- `src/common/ReadModel.h` — `FmAnalysisSnapshot` struct, pure virtual `getFmAnalysis()`
- `src/engine/ReadModelImpl.h` / `.cpp` — `getFmAnalysis()` override
- `src/frontend/FrontendRpc.h` — `notify::FmAnalysis` constant, `toJson(FmAnalysisSnapshot)`
- `src/frontend/FrontendServer.h` / `.cpp` — `analysisTimer_`, `onAnalysisTimer()`
- `src/frontend/router/Router_Read.cpp` — `read.getFmAnalysis` RPC method

### 3.2 Frontend side (commit `ed744a1`)

```
notify.fmAnalysis (WebSocket push, 30 Hz)
    ↓
main.tsx subscription
    ↓
analysisStore.update()           // Zustand: { tracks: FmAnalysisSnapshot[] }
    ↓
FmAnalysisPanel                  // reads tracks, renders 6 vertical bars
```

**Key files:**
- `frontend/src/store/analysisStore.ts` — Zustand store + `analysisStore.test.ts` (3 tests)
- `frontend/src/components/FmAnalysisPanel.tsx` / `.css` — operator level display
- `frontend/src/store/uiStore.ts` — `BOTTOM_TAB_IDS` includes `"fm-analysis"`
- `frontend/src/App.tsx` — tab entry with `SFmAnalysisPanel`
- `frontend/src/main.tsx` — `notify.fmAnalysis` → `analysisStore.update`
- `frontend/src/rpc/types.ts` — `FmAnalysisSnapshot`, `FmAnalysisPayload`

### 3.3 The meter channel is the template

The analysis channel follows the exact same pattern as the existing meter
channel (`notify.meters` → `meterStore` → meters in track headers).
If you need to add another real-time data channel, copy the meter pattern:
atomics in the engine → `toJson` in `FrontendRpc.h` → 30 Hz timer in
`FrontendServer` → subscription in `main.tsx` → Zustand store → component.

## 4. Remaining FM synth backlog

| Item | Status | Complexity |
|------|--------|------------|
| Bottom-panel UI tab (operator editors, algorithm display, envelope editing) | **Partial** — analysis panel exists; operator envelope editing is the big piece | High |
| DX7 sysex cartridge import (.syx) | Pending | Low |
| Full Dexed mono key-stack (return-to-highest-held on release) | Pending — minimal legato shipped; `noteOff()` untouched by design | Medium |
| Extended algorithms / modulation matrix / preset library / MIDI learn | Pending | High |
| MCP parity: expose `peekVoiceStatus` amps via `fm_synth_get_state` | Pending — the C++ pipeline is there, just needs MCP wiring | Low |
| MCP parity: expose analysis data as an MCP tool | Pending — `read.getFmAnalysis` RPC exists, needs MCP tool | Low |

## 5. Test discipline

When modifying the FM synth engine:
1. FM synth tests: `--gtest_filter="FmSynthTest.*:TrackFXSlotDebug.*:SamplerFxSlot.*"`
2. FrontendServer tests: `--gtest_filter="FrontendServer.*"`
3. Regression: `--gtest_filter="Commands.ReadModelExtensions"` (catches stale-obj and analysis interaction bugs)
4. Frontend: `cd frontend && npm test`
5. Full suite excluding plugin isolation: `--gtest_filter=-CrashRecovery.*:ProxyHealth.*:IsolatedScanner.*:PluginIsolation.*:McpServer.ExportAudioWithClapPluginDoesNotHang:McpServer.ExportAudioWithMultipleIsolatedInstances:McpServer.DiagnosticClapExportMatrix:RenderSequenceRelease.*`

## 6. Watch items

- **Stale `.obj` trap (lesson 15):** the "regression" in this session was
  caused by merge conflict markers from a `git stash pop` that survived into
  the build — not by the analysis channel code. Always verify the BINARY
  contains the fix (`.obj` timestamps / a breakpoint probe) when a fix
  "doesn't take."
- **VS 2026 v180 build:** this machine only has v180. The full plugin-
  isolation test suite requires v143 (VS 2022) for `hdaw_plugin_host.exe`
  child processes. If you need those tests, install v143 toolset.
- **`RenderSequenceRelease` flake:** passes isolated, fails under full-suite
  load (timing-sensitive child-process polling). If it recurs, bump its
  poll budget or shard it.

## 7. First actions next session

1. `skill: "hdaw-guard"` (always).
2. If continuing FM work: operator envelope editing in the bottom panel is
   the big piece — it needs per-operator EG curve display and parameter
   editing, and should wire into the existing analysis panel.
3. MCP parity is quick wins — `fm_synth_get_state` for `peekVoiceStatus`
   amps, and an `fm_analysis` tool wrapping `read.getFmAnalysis`.
4. DX7 sylex import is low-complexity and high-user-value.

---

*End of handoff. Read `AGENTS.md` lessons first, then this file, then
`2026-08-17-fm-synth-audio-fixes.md` for the audio-quality fixes context.*
