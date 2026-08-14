# Plan: `load_plugin_preset_file` MCP Tool

## Goal

Add an MCP tool that loads a `.fxp` preset file into a plugin FX slot via `setStateInformation`, enabling deterministic preset injection for any hosted plugin (including Serum 2 drum presets).

## Success Gates (all must pass)

- [ ] Tool registers and appears in `tools/list` response
- [ ] Loading a known `.fxp` produces different `pluginState` md5 on the target slot (verify via MCP `save_project` + XML inspection)
- [ ] All 5 drum tracks in `polyrhythm_drums_serum.hdaw3` end up with DIFFERENT Serum states after loading 5 different presets
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=McpTools*` passes
- [ ] `cmake --build build --config Debug` succeeds
- [ ] Solo drum renders produce distinct drum-like sounds (not identical tonal blips)

## Dependency Map

- **Blast radius:** ADDS one new tool registration inside `registerFxTools` (McpTools_Audio.cpp). No changes to existing functions.
- **Upstream:** `registerFxTools` called by `registerAudioDomain` (line 711). `McpServer::registerTool` adds to tool map.
- **Downstream:** Calls `setStateInformation` on live plugin instance (same path as project load + A/B swap). Persists to `IDs::pluginState` on ValueTree slot (same path as `Track::prepareToPlay` state capture at Track.cpp:120).
- **God nodes in scope:** None modified. `McpTools_Audio.cpp` is a leaf registration file.
- **Community boundaries crossed:** None — all within MCP → engine boundary.
- **Projections affected:** ValueTree `pluginState` property → ReadModel delta on save.
- **SPSC paths touched:** None — handler runs on MCP/router thread, NOT audio thread. For isolated plugins, `setStateInformation` marshals to child via pipe (existing infrastructure).
- **Path integrity:** MCP handler → `getPluginInstance()->setStateInformation()` → (isolated: pipe → child's `runLifecycleOnMessageThread`) → plugin loads state. Same path as `swapFxSnapshot` (Router_Audio.cpp:194).

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** Trace FULL path: MCP handler → `getPluginInstance()->setStateInformation()` → plugin loads state. Verify by loading a preset and checking `pluginState` md5 changes.
- **Gate 3 (audio-thread safety):** Handler runs on MCP thread. `setStateInformation` for isolated plugins marshals via pipe. For inproc, runs on router thread. Neither is the audio thread. Safe.
- **Gate 9 (ID validation):** Validate `trackId`/`slotIndex` bounds (same pattern as existing `list_plugin_presets`).

## Steps

### 1. Implement `load_plugin_preset_file` in `McpTools_Audio.cpp`

**Location:** Inside `registerFxTools`, after `set_fx_param` (line 325), before the closing `}`.

**Tool signature:**
```
name: "load_plugin_preset_file"
description: "Load an .fxp preset file into a plugin FX slot via setStateInformation."
parameters: trackId (int, required), slotIndex (int, required), filePath (string, required)
```

**Handler logic:**
1. Validate `trackId`/`slotIndex` using `e->getReadModel().getFxSlots(ti)` (same as `list_plugin_presets`)
2. Get live plugin: `e->getMainProcessor()->getTrack(ti)->getFXChain()[si]->getPluginInstance()`
3. Read file at `filePath` using `juce::File::loadFileAsData`
4. Parse FXP header:
   - Validate `CcnK` magic at offset 0 and `FPCh` at offset 8
   - Try Serum-2 layout: `chunkSize@56 == filesize - 60` → data at offset 60
   - Fallback standard: `chunkSize@52 == filesize - 56` → data at offset 56
   - Validate chunkSize > 0 and data within bounds
5. Call `plugin->setStateInformation(chunkData, chunkSize)`
6. Persist: `slotTree.setProperty(IDs::pluginState, juce::MemoryBlock(chunkData, chunkSize).toBase64Encoding(), &um)`
7. Return `"ok"` or error message

**Includes needed:** `<juce_core/juce_core.h>` (for `juce::File`, `juce::MemoryBlock`, `juce::MemoryInputStream`) — check if already included via existing headers.

### 2. Add gtest coverage

**File:** `tests/unit/mcp/tool_registry_test.cpp` (extend existing) or new `tests/unit/mcp/fx_preset_file_test.cpp`

**Tests:**
- Tool registers with correct name and schema
- Missing `filePath` returns error
- Invalid file path returns error
- Non-FXP file returns error (bad magic)

### 3. Inject 5 Serum presets

Use the MCP tool to load the 5 Test Press Phonk presets into the drum tracks of `polyrhythm_drums_serum.hdaw3`:
- Track 3 (Kick): `TSP_SP_Drum_damage_kick.fxp`
- Track 4 (Snare): `TSP_SP_Drum_808_snare.fxp`
- Track 5 (Closed Hat): `TSP_SP_Drum_short_909_hi_hat.fxp`
- Track 6 (Open Hat): `TSP_SP_Drum_hi_hat_open.fxp`
- Track 7 (Clap): `TSP_SP_Drum_707_clap.fxp`

Save as `polyrhythm_drums_serum_presets.hdaw3`.

### 4. Render drum solos

Run `build_and_render_drum_solos.py` to produce 5 solo WAV files. Verify distinct timbres.

## Files to Modify

| File | Change |
|------|--------|
| `src/mcp/McpTools_Audio.cpp` | Add `load_plugin_preset_file` tool in `registerFxTools` |
| `tests/unit/mcp/tool_registry_test.cpp` | Add test for new tool registration + param validation |

## Verification Commands

```powershell
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=McpTools*
```
