
# Plan: TimbreLib sidecar ingestion into HDAW FileLibraryManager

Status: APPROVED by orchestrator (hdaw-guard pre-flight complete)
Date: 2026-08-24

## Goal
Make HDAW's audio library index ingest TimbreLib sidecars (<file>.timbre.json),
store tags + description per entry, match them in search(), and expose them in
search_library / get_library_entry (MCP) and the Qt router responses — so
`search_library {query:"dark gritty pad"}` works inside HDAW.

## Success Gates (all must pass with evidence)
- [ ] G1: `build-fast.bat test` succeeds (Windows MSVC, RelWithDebInfo); hdaw_tests.exe mtime is newer than the edited sources (Gate 4/15 binary freshness).
- [ ] G2: New unit tests pass (FileLibraryTest.*): sidecar ingestion populates tags/description; search("word") matches via tags; search matches prose text in description; tags/description persist across manager instances (lazy-load round trip); sidecar mtime bump triggers rescan; missing + malformed sidecar tolerated without crash.
- [ ] G3: mcp_coverage_test passes, incl. new assertions that search_library + get_library_entry results carry tags/description when present (MCP parity, Gate 2 full path: RPC -> FileLibraryManager -> JSON response).
- [ ] G4: Full hdaw_tests.exe suite passes (no regressions).

## Dependency Map (verified via codebase-memory graph + grep)
- Blast radius: FileLibraryManager (src/engine) -> McpTools_Library.cpp (MCP tools
  list_libraries/search_library/get_library_entry) and Router_Library.cpp
  (Qt frontend RPC path "search"/"getEntry"). Upstream callers of
  FileLibraryManager::search: Router_Library (verified trace_path). Frontend TS
  libraryStore.ts consumes RPC results — additive JSON fields are backward-compatible.
- Downstream consumers of LibraryEntry JSON: McpTools_Library.cpp serializer,
  Router_Library.cpp serializer, registry persistence (libraries/<id>.json +
  partition files), frontend libraryStore/search UI (additive-safe).
- God nodes in scope: none (FileLibraryManager is leaf-ish; search called from router only).
- Community boundaries crossed: engine <-> MCP server, engine <-> Qt router.
  Interface contract: LibraryEntry struct + JSON field names must stay consistent.
- Projections: none (library data is not a ReadModel/audio-graph projection).
- SPSC paths: none (scan runs on threadpool; search on control thread).
- Delta vs fullSync: n/a (library registry is its own persistence).
- Undo: n/a (library ops are not undoable project edits).
- Path integrity: full path exists and is exercised by existing tests (search called by router directly; MCP coverage test calls search_library).

## Design decisions (fixed, do not deviate)
1. `LibraryEntry` gains: `juce::String tags;` and `juce::String description;`
2. Sidecar file: `<audiofile>.timbre.json` next to the audio file
   (e.g. `beat.wav.timbre.json`), written by TimbreLib `lib_analyze.py --sidecars`.
   JSON schema (subset used): "dsp_words" (string), "prose" (string),
   "captions" (array of [text, score]), "tags" (array of [label, score]).
3. Compose in a new private helper `static void applyTimbreSidecar(LibraryEntry&, const juce::File& audioFile);`
   (header private section; cpp implementation):
   - entry.tags = dsp_words; then append ", " + text of top 3 captions; then
     ", " + label of top 3 tags (only fields present in the JSON).
   - entry.description = prose;
   - Tolerate missing sidecar and malformed JSON (juce::JSON::parse -> var;
     guard with .isObject(); malformed => leave fields empty; NEVER throw out of scan).
4. scanDirectory (audio libs only): call applyTimbreSidecar after
   extractAudioMetadata. Rescan condition addition: also rescan when the
   sidecar exists and its modification time is newer than the entry's
   modifiedTime (analysis ran after the audio was last written). Reuse branch
   (needsRescan==false) must be skipped when sidecarNewer.
5. search(): text match condition ADDS (keep existing name/path/key semantics):
   `&& !entry.tags.toLowerCase().contains(queryLower)
    && !entry.description.toLowerCase().contains(queryLower)`
6. Persistence: serializeEntry adds "tags"/"description"; loadLibraryEntries
   deserializes them with empty-string default (missing fields must not fail —
   existing partition test JSON lacks them). Both single-file and partition
   paths use the same serializer.
7. MCP results (McpTools_Library.cpp): search_library entry object and
   get_library_entry object gain conditional fields
   `{"tags", jstr(e.tags)}` / `{"description", jstr(e.description)}` only when non-empty
   (mirror the existing `if (e.format.isNotEmpty())` style).
8. Qt router (Router_Library.cpp): same conditional fields in "search" and "getEntry" responses.

## Files to modify (exact)
- src/engine/FileLibraryManager.h            (struct fields + private helper decl)
- src/engine/FileLibraryManager.cpp          (applyTimbreSidecar, scanDirectory hook,
                                              search() condition, serializeEntry, loadLibraryEntries)
- src/mcp/McpTools_Library.cpp               (search_library + get_library_entry response objects)
- src/frontend/router/Router_Library.cpp     ("search" + "getEntry" response objects)
- tests/unit/engine/file_library_test.cpp    (new TEST_F cases, see G2)
- tests/integration/mcp/mcp_coverage_test.cpp(extend/add audio-sidecar library assertions, see G3)
Do NOT modify CMakeLists.txt (no new files). Do NOT touch frontend TS in this task.

## Pitfall Gates Triggered + how addressed
- Gate 2 (unimplemented path): every new field/behavior exercised by tests
  (unit + MCP coverage) asserting real values through scan -> search -> JSON.
- Gate 3 (audio thread): no audio-thread code touched (scan threadpool,
  search control thread). JSON parse happens on scan threadpool only.
- Gate 4/15 (stale binaries): verify hdaw_tests.exe mtime after build;
  never trust source-only verification.
- Gate 9 (validation): malformed sidecar JSON must be tolerated (isObject guard,
  empty defaults, no exceptions escaping scan's per-file try/catch).
- Gate 11 (message pump): no new entry points (runs inside existing hdaw_tests).
- Gates 1/6/10/12/13/14/16: not triggered (no processor state, no rebuild path,
  no graph mutation, no DSP writes, no plugin/IPC).

## Anti-pattern scan (none triggered)
No new .cpp (no CMakeLists edit), no RPC loops, no DBG(), no raw hex, no
ValueTree/SPSC changes. Follow existing style (jstr/qstr helpers, conditional
JSON fields).

## Steps (implementation)
1. Header: add fields + helper decl.
2. cpp: implement applyTimbreSidecar; hook into scanDirectory; extend search;
   extend serialize/deserialize.
3. MCP + router response fields.
4. Unit tests (file_library_test.cpp) — mirror existing patterns
   (tempDir, WAV writer, async scan polling loop, setLastModificationTime bump).
5. MCP coverage test additions.
6. Build: `cmd.exe /c "cd /d D:\pdf\roo projects\hdaw3 && build-fast.bat test"`
   (Windows MSVC via WSL interop). Record binary mtime before/after.
7. Run: filter FileLibraryTest.* -> McpCoverageTest.* -> full suite
   (`build\RelWithDebInfo\hdaw_tests.exe`). Capture output.
8. Report: files changed, diff summary, build log tail, test results per gate.

## Evidence to report back
- build output (tail) + hdaw_tests.exe timestamp before/after
- gtest output for FileLibraryTest.* (new + existing), McpCoverageTest.*, full suite
- any gates that failed and why
