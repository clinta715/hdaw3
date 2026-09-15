
## Goal
Fix psytrance v6 handoff bugs with permanent code hardening: (A) psy_fm enum, (B) clipId JSON response, (C) export serialize guard.

## Success Gates
- [ ] Gate 1: `add_track_with_fx {fxType:"psy_fm"}` succeeds (no enum error) and creates track+fx
- [ ] Gate 2: `add_midi_clip` and `add_audio_clip` return JSON {"clipId":N} parseable as JSON; `add_notes {clipId:parsedId}` adds notes (added=N)
- [ ] Gate 3: concurrent `export_audio` second call is rejected with clear "in progress — use cancel_export" error, not silently killing first; `ExportManager::startExport` uses atomic CAS, no TOCTOU
- [ ] Gate 4: `cmake --build build --config Debug` succeeds, `build/Debug/hdaw_tests.exe` passes (no regression, especially Mcp/Clip/Track/Export suites)
- [ ] Gate 5: No raw "clipId=N" text remains in clip-creating MCP tools; xid helper still works (backward compat test)

## Dependency Map
- Blast radius: `src/mcp/McpTools_Track.cpp` (add_track_with_fx enum), `src/mcp/McpTools_Clip.cpp` (add_midi_clip/add_audio_clip return shape), `src/engine/ExportManager.{h,cpp}` + `src/mcp/McpExportTool.cpp` + `src/frontend/router/Router_Export.cpp` (export guard)
- Upstream: MCP clients (psytrance scripts, frontend RPC), McpServer dispatch, AudioEngine::getProjectModel()
- Downstream: clipId consumers (add_notes, add_cc_point, fx slot addressing, export), ExportManager renderThread lifecycle, FrontendServer notify
- God nodes: ExportManager (bridges MCP+frontend+audio), ProjectModel (track/clip allocators)
- Community boundaries: MCP surface <-> command layer <-> ValueTree <-> audio graph projection
- Projections affected: Audio graph (clip creation triggers rebuildRoutingGraph), ReadModel (snapshot), no SPSC timing mutation
- SPSC paths: none for these three (clip add and export do full rebuild, not SPSC bridge)
- Path integrity: graph path RPC->ValueTree->listener->processor exists; verified via read of McpTools_Clip addChild + RoutingManager rebuild

## Pitfall Gates Triggered
- Gate 2 (Unimplemented Code Path): psy_fm via add_track_with_fx was unimplemented enum -> FIX by adding to enum and routing to addFxSlot; verify end-to-end add_track_with_fx -> fxChain ValueTree -> TrackFXSlot
- Gate 9 (ID Namespace): clipId JSON change must still use allocateClipID via ProjectModel::createMidiClipEmpty/createAudioClip — no cross-allocator
- Gate 4 (Stale Binary): after C++ edits must verify built binary timestamp/size before testing; never test Release/HDAW.exe
- Gate 15 (Stale Flags): export CAS guard prevents TOCTOU where two callers both see active==false and both start

## Steps
1. Fix A (McpTools_Track.cpp): add "psy_fm" to add_track_with_fx enum QJsonArray (align with McpTools_FxSlot.cpp add_fx enum which already includes it). Verify no other enum sites miss it.
2. Fix B (McpTools_Clip.cpp): change add_midi_clip and add_audio_clip return from QString("clipId=%1").arg(cid) to JSON `{"clipId":cid}` via QJsonDocument compact. Also update add_audio_clip same, plus generateIntoClip helper in McpTools_CompositionGenerate.cpp if needed consistency check. Keep xid helper backward-compat (parses both).
3. Fix C (ExportManager.h/cpp): replace `if(active.load()) return false; active=true;` with `bool expected=false; if(!active.compare_exchange_strong(expected,false)) return false;` (CAS). Keep existing isExporting() checks in McpExportTool and Router_Export (already return clear error). Ensure cancelAndJoin still clears via ActiveGuard.
4. Build + test: cmake --build build --config Debug ; run build/Debug/hdaw_tests.exe --gtest_filter=Mcp*+*Clip*+*Export*+*Track*; also quick MCP smoke via test_main if available.
5. Update docs/psytrance-composition-guide.md § quirks if needed (note JSON clipId).
