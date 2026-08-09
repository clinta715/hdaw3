# Fix: Offline render of CLAP instruments produces silence

## Goal
Make `export_audio` (offline render) produce non-silent audio for CLAP/VST3 instrument plugins, so the 64-bar techno project renders to a real WAV.

## Root Cause (confirmed via `%TEMP%\hdaw_debug.log` + code trace)
- The live playback path always uses **plugin isolation** (`PluginManager::createPluginInstance`, `isolated=true` default) → spawns a child proxy process → never calls JUCE's `AudioPluginFormatManager::createPluginInstance`.
- The offline render (`ExportManager::renderThreadFunc`) **disables isolation** (`IsolationToggleGuard` sets `isolationEnabled=false`) → forces the in-process path → `AudioPluginFormatManager::createPluginInstance`.
- `findFormatForDescription` matches a format only if:
  `format->getName() == desc.pluginFormatName && format->fileMightContainThisPluginType(desc.fileOrIdentifier)`.
- `Track::rebuildFXChain` builds `desc.fileOrIdentifier = pluginID` (e.g. `"CLAP-Vital-aaca468a-0"`), `desc.pluginFormatName = "CLAP"`.
- `CLAPPluginFormat::fileMightContainThisPluginType` returns `fileOrIdentifier.endsWith(".clap")`. The ID string does **not** end with `.clap` → no format matches → `"No compatible plug-in format exists for this plug-in"` → plugin fails to load → silent render.
- Evidence: `FXRebuild createPluginInstance result: NULL error=No compatible plug-in format exists for this plug-in`, `isolated=false sr=48000 getSr=0`.

## Fix (localized to non-isolated branch)
In `PluginManager::createPluginInstance` (`src/engine/PluginManager.cpp`), before the in-process `formatManager.createPluginInstance` call (the `#else`/fallthrough path, ~line 552/561), resolve `desc.fileOrIdentifier` from an identifier string to the real plugin file path:

- Build a local copy `resolvedDesc = desc`.
- If `resolvedDesc.fileOrIdentifier` does not end with `.clap`/`.vst3`, scan `knownPluginList.getTypes()` for a descriptor whose `createIdentifierString() == resolvedDesc.fileOrIdentifier`; on match, set `resolvedDesc.fileOrIdentifier = kd.fileOrIdentifier` and (if empty) `resolvedDesc.name = kd.name`.
- Call `formatManager.createPluginInstance(resolvedDesc, ...)`.

Do NOT modify the isolated/proxy branch (lines 502-543) — it already works.

## Second fix: message-thread deadlock during in-process creation
The identifier fix causes `AudioPluginFormatManager::createPluginInstance` to now match the CLAP format and invoke `CLAPPluginFormat::createInstanceFromDescription`. JUCE's default impl (juce_AudioPluginFormat.cpp:71-74) posts an `AsyncCreateMessage` to the JUCE message thread when called off that thread, then blocks on `finishedSignal.wait()`. The offline render calls it from the render worker thread; the MCP export tool (`McpExportTool.cpp:129`) blocks the message thread on `doneFuture.get()` → the AsyncCreateMessage is never serviced → deadlock (repro: rebuildFXChain logs, flat CPU, no WAV).

Fix: override `CLAPPluginFormat::createInstanceFromDescription` (new virtual in `src/engine/CLAPPluginFormat.{h,cpp}`) to call the protected `createPluginInstance` synchronously on the calling thread (CLAP creation is inherently synchronous; `requiresUnblockedMessageThreadDuringCreation` already returns false). This removes the message-thread dependency for both the MCP and frontend (`QEventLoop`) export paths. Do not touch the proxy/isolation path.

## Success Gates
- [ ] Gate 1: Re-running the minimal repro (one Vital track + one MIDI clip + 4 sustained notes, `export_audio`) produces a WAV with peak > 0.01 (non-silent) AND `%TEMP%\hdaw_debug.log` shows `createPluginInstance result: ok` for the off line render (isolated=false), with NO deadlock (render completes < 60s).
- [ ] Gate 2: A new gtest covers the fix. Prefer a unit test on the identifier→path resolution (deterministic, no plugin scan). If an integration test rendering a real installed plugin is added, it must skip gracefully when the plugin is absent (mirror `plugin-isolation.spec.ts`). The test must FAIL on the old behavior and PASS on the new.
- [ ] Gate 3: `cmake --build build --config Debug --target HDAW_headless hdaw_tests` succeeds.
- [ ] Gate 4: `build/Debug/hdaw_tests.exe` runs with no regressions (existing CLAP/proxy/plugin tests still pass).
- [ ] Gate 5: The full 64-bar `test_techno_64bars.hdaw` renders to a non-silent WAV (peak > 0.01).

## Dependency Map
- Upstream caller: `Track::rebuildFXChain` (`src/engine/Track.cpp:162`) — only production caller of `PluginManager::createPluginInstance`.
- Downstream: `AudioPluginFormatManager::createPluginInstance` (JUCE) via `PluginManager::formatManager`.
- Not affected: isolated/proxy branch (`PluginManager.cpp:502-543`), `PluginHost.cpp`, `PluginScannerMain.cpp` (they use real paths already).
- Projections: none (no ValueTree/ReadModel/frontend change).
- No SPSC/audio-thread change (this runs on the render worker thread, not the realtime path).

## Pitfall Gates
- Gate 2 (unimplemented path): the fix is verified by `createPluginInstance result: ok` log + non-silent WAV.
- Gate 4 (stale binaries): always test `build/Debug/...`, never `build/Release`.
- No audio-thread change (Gate 3 N/A). No frontend/CSS (Gates 5/8 N/A). No new IDs (Gate 9 N/A).

## Steps
1. Edit `src/engine/PluginManager.cpp` non-isolated branch to resolve identifier→real path (DONE by subagent 1).
2. Add `CLAPPluginFormat::createInstanceFromDescription` override for synchronous creation (subagent 2).
3. Add a gtest (see Gate 2) validating the resolution and/or a non-silent render.
4. Rebuild `HDAW_headless` + `hdaw_tests`.
5. Run the gtest suite.
6. Re-run the minimal Vital repro + the full 64-bar render; verify non-silence via PCM peak analysis, and confirm no deadlock.
7. Report: files changed, tests run + output, gates passed.

## Verification command
`node` repro script driving `HDAW_headless --mcp-stdio` (add_track_with_fx + add_midi_clip + add_note + export_audio), then PCM peak analysis of the output WAV.