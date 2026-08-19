# Plan — `composition.verifyPart` composer self-verification (handoff item #2)

Date: 2026-08-19
Status: COMPLETE (2026-08-19) — all gates passed; full suite 935 tests, 932 passed /
3 env-guarded skips / 0 failures. bandsPresent true on the fm_synth Standard gate part.

## Goal

Add `ProjectCommands::verifyPart(trackIndex, windowSeconds)` — renders the
track's window SOLO and in the FULL MIX (both via the shared
`renderTrackWindow`, extended — not forked), and reports
`soloRms/soloPeak/mixRms/mixPeak/nonClipping/audible/bandsPresent`. Exposed as
RPC `composition.verifyPart` and MCP `verify_part` (parity rule).

## Success Gates (all must pass)

- [ ] G1: `renderTrackWindow` extended in place with a `soloMuteOthers` flag
      (default `true` — existing callers unchanged) and `windowStart` surfaced
      in `RenderWindowResult`. With the flag false: tree copy clears all solos,
      keeps live mutes, mutes nothing.
- [ ] G2: `verifyPart` engine command implemented; validates trackIndex +
      windowSeconds (Gate 9); two sequential renders; band analysis via
      `juce::dsp::FFT` on the solo WAV (offline, command thread); temp files
      deleted on every path.
- [ ] G3: RPC `composition.verifyPart {trackIndex, windowSeconds?}` returns the
      full result JSON (Router_Composition.cpp).
- [ ] G4: MCP `verify_part` tool registered in `registerCompositionTools`,
      calling the SAME engine command.
- [ ] G5: gtest `tests/unit/engine/verify_part_test.cpp` (internal fm_synth
      ONLY — no real plugins): a gain-staged `addInstrumentPart` part passes
      verifyPart (`ok && audible && nonClipping`), `bandsPresent` asserted,
      plus error cases (bad trackIndex, empty track). Added to
      `tests/CMakeLists.txt`.
- [ ] G6: RPC round-trip test `FrontendServer.VerifyPartRpc` and MCP test
      `McpServer.VerifyPartTool`.
- [ ] G7: `cmake --build build --config Debug` succeeds; binary freshness
      verified; targeted filters pass; FULL `hdaw_tests.exe` green
      (baseline 929 + new tests).
- [ ] G8: Per the VST3 finding: NO per-program audio assertions anywhere.

## Dependency Map (verified)

- `renderTrackWindow` (AudioEngineCommands_Composition.cpp:162) callers:
  `autoGainToTarget` (:506, :535), `auditionPlugin` (:723). New param is
  defaulted → zero behavior change for existing callers.
- `measureWav` (:38) extended with optional `BandPresence*` out-param
  (nullptr default) — one file read, FFT over the already-loaded buffer.
- `juce::dsp::FFT` already used in engine (`TransientDetector.cpp:40`,
  `FileLibraryManager.cpp:733`) — module linked, pattern established.
- Export path: `ExportManager::startExport(treeCopy, ...)` — tree-copy render
  never mutates the live graph (Gate 12 safe, proven by audition/gain-stage).
- Projections: none (read-only verification; no ValueTree writes, no undo).
- Delta/fullSync: N/A (no mutation). SPSC: none. God nodes: none touched.

## Pitfall Gates Triggered

- Gate 2 (silent no-op): full chain command→RPC→MCP wired + round-trip tests.
- Gate 3: FFT/measure run on the command thread over a finished render file —
  offline, same pattern as `measureWav`. Nothing on the audio thread.
- Gate 9: bounds-check trackIndex/windowSeconds at the command boundary;
  guard FFT band math against zero total energy.
- Gate 4/15: rebuild + timestamp check before tests.
- Gates 1/6/10: no processor state written → no restore path needed.
- Gates 11/12: no new entry point; tree-copy renders only (no live graph
  mutation, no new pump concerns).
- Anti-pattern: new test .cpp MUST be added to `tests/CMakeLists.txt`
  (audition_test.cpp is at line 91 of that file).

## Design details

### Result struct (src/common/ProjectCommands.h, after AuditionResult)
```cpp
struct VerifyPartResult {
    bool ok = false;
    float soloRms = 0.0f, soloPeak = 0.0f;
    float mixRms = 0.0f, mixPeak = 0.0f;
    bool nonClipping = false;   // mixPeak < 1.0
    bool audible = false;       // soloPeak > 1e-4 (~ -80 dBFS, auditionPlugin's bar)
    bool bandsPresent = false;  // bandLow && bandMid && bandHigh
    bool bandLow = false, bandMid = false, bandHigh = false;
    double windowStart = 0.0;   // seconds (info)
    double durationSeconds = 0.0;
    std::string error;
};
virtual VerifyPartResult verifyPart(int trackIndex, double windowSeconds = 4.0) = 0;
```

### renderTrackWindow extension
Signature → `renderTrackWindow(engine, trackIndex, windowSeconds, fader, applyFader, bool soloMuteOthers = true)`.
In the tree-copy loop: always clear `isSoloed`; when `soloMuteOthers` mute the
other tracks (existing behavior); when false leave mutes as-is. Set
`result.windowStart` before export.

### Band presence (file-local, composition cpp)
```cpp
struct BandPresence { bool low = false, mid = false, high = false; };
```
Extend `measureWav(..., BandPresence* outBands = nullptr)`: after the existing
rms/peak pass, if outBands != nullptr run `juce::dsp::FFT` order 12 (4096) with
a Hann window over up to 8 evenly-spaced frames of channel 0 (mix down to mono
first if multi-channel — average channels), accumulate band energies
low 20–250 Hz / mid 250–4000 Hz / high 4000–20000 Hz (render is 48 kHz).
Band present = bandEnergy > 0.005 * totalEnergy AND totalEnergy > 1e-12.
Include `<juce_dsp/juce_dsp.h>`.

### verifyPart flow (AudioEngineCommands_Composition.cpp)
1. Validate (Gate 9). 2. solo = renderTrackWindow(..., 1.0f, false, true).
3. mix = renderTrackWindow(..., 1.0f, false, false). 4. measureWav with
outBands on the solo file (or fold bands into the solo render's measure call).
5. Fill result; audible/nonClipping/bandsPresent as defined; delete both temp
files on every path (error paths included).

### RPC (Router_Composition.cpp, after auditionPlugin case)
`m == "verifyPart"`: requireInt trackIndex; optDouble windowSeconds 4.0.
Response JSON: ok, soloRms, soloPeak, mixRms, mixPeak, nonClipping, audible,
bandsPresent, bandLow, bandMid, bandHigh, windowStart, durationSeconds, error?.

### MCP (McpTools_Project.cpp registerCompositionTools, after audition_plugin)
`verify_part`: schema trackIndex (integer, min 0, required), windowSeconds
(number, min 0.1, optional). Text output
`ok=.. soloRms=.. soloPeak=.. mixRms=.. mixPeak=.. nonClipping=.. audible=.. bandsPresent=..`.
Errors in-band (`McpToolResult::text(err, true)`) like audition_plugin.

### Tests
- `verify_part_test.cpp`:
  - `VerifyPart.ComposedPartPasses` — addInstrumentPart (trackName "Verify",
    style "Standard", seed fixed, targetRms 0.15, windowSeconds 4.0) →
    verifyPart(res.trackIndex, 4.0): EXPECT ok, audible, nonClipping;
    EXPECT bandsPresent (if it fails, REPORT the measured band fractions —
    do not silently loosen); soloPeak > 1e-4; mixPeak < 1.0.
  - `VerifyPart.TwoPartsMixAtLeastSolo` — compose two parts (different
    styles/seeds); verifyPart on one: mixRms >= soloRms (energy adds).
  - `VerifyPart.InvalidTrack` — trackIndex 99 → error, ok=false.
  - `VerifyPart.EmptyTrack` — default project track 0 (no clips) →
    error "track has no clips" (lesson 9: default tracks are empty).
- `frontend_server_test.cpp` `FrontendServer.VerifyPartRpc` — pattern of
  AuditionPluginRpc (line 550): compose via `composition.addInstrumentPart`,
  then `composition.verifyPart`, assert JSON fields.
- `mcp_server_test.cpp` `McpServer.VerifyPartTool` — pattern of
  McpServer.AuditionPluginTool (line 744): add_instrument_part then
  verify_part; assert output contains `ok=true`/`nonClipping=true`.
- `tests/CMakeLists.txt`: add `unit/engine/verify_part_test.cpp` next to
  audition_test.cpp (line 91).

## Out of scope (noted from handoff wishlist)

- Plugin on/off RMS delta (needs FX-bypass render — separate item if wanted).
- Global-scale gain staging fallback (handoff item #1, next).
- Frontend UI consumption (composer tooling; parity via RPC+MCP matches the
  auditionPlugin precedent).
