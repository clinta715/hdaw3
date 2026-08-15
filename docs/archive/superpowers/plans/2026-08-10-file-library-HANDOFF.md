# Handoff: Complete File Library System Implementation (v2)

## Context for the next session

You are continuing implementation of a **File Library System** for HDAW (a JUCE 8 desktop DAW). The brainstorming, spec, and implementation plan are complete. You are in the middle of **subagent-driven execution** of a 17-task plan. Tasks 1-8 are done and committed. Task 9 was interrupted mid-execution with partial uncommitted work.

## How to continue

This is a **subagent-driven-development** workflow. Read the skill at `C:\Users\hapbt\.opencode\skills\subagent-driven-development\SKILL.md` for the full process. The short version: for each task, dispatch an implementer subagent, then a spec-reviewer subagent, then a code-quality-reviewer subagent, fix any issues, then move to the next task. The **MANDATORY** guard skill `hdaw-guard` must be loaded before any code change.

The complete plan with full code for every step lives at:
`docs/superpowers/plans/2026-08-10-file-library.md`

The approved design spec lives at:
`docs/superpowers/specs/2026-08-10-file-library-design.md`

**Do not re-read the whole plan into your context at once** — it's ~2400 lines. Read it task-by-task as you dispatch each implementer.

## Current state

### Git log (most recent first)
```
420f156 feat(library): add Zustand library store for frontend state
8bc8cd5 feat(library): add RPC handlers for library namespace
12de51b fix(library): validate empty id in set_library_autoscan MCP tool
6125928 feat(library): add MCP tools for library management and search
4431ed4 fix(library): eliminate loadLibraryEntries data race and add lazy-load test
d8ea260 feat(library): implement search with name, type, duration, BPM, key filters
3da245a feat(library): implement audio metadata extraction with key detection
bfbb7e3 fix(library): fix deadlock, callback races, scanning counter, and test flakiness
2cd411e feat(library): implement MIDI metadata extraction and scanning
1d6e5b5 fix(library): address code quality issues - thread safety, test isolation, persistence test
dbd7a14 feat(library): implement FileLibraryManager registry persistence
db5d543 feat(library): add FileLibraryManager header with data structures
```

Branch: `main`, not pushed.

### Task completion status

| Task | Status | Notes |
|------|--------|-------|
| 1: FileLibraryManager header | ✅ DONE | `src/engine/FileLibraryManager.h` |
| 2: Registry persistence | ✅ DONE | `src/engine/FileLibraryManager.cpp` + tests |
| 3: MIDI metadata extraction | ✅ DONE | + deadlock/callback/counter fixes |
| 4: Audio metadata extraction | ✅ DONE | `3da245a` — extractAudioMetadata + scanDirectory generalized to audio |
| 5: Search | ✅ DONE | `d8ea260` + race fix `4431ed4` — lib.type filter, bpm fallback, lazy-load, sort, 4 new tests |
| 6: MCP tools | ✅ DONE | `6125928e` + autoscan validation fix `12de51b` — 7 MCP tools, engine wired |
| 7: RPC handlers | ✅ DONE | `8bc8cd5` — 7 RPC methods (library namespace), `method::Library` constant |
| 8: Zustand library store | ✅ DONE | `420f156` — DI pattern, 17 vitest tests, reportRpcError on failure |
| **9: FileBrowser Library mode** | ⏳ INTERRUPTED | Partial work in working tree (see below). Resume from here. |
| 10: Preferences library settings | ❌ NOT STARTED | |
| 11: Default MIDI directory + examples | ❌ NOT STARTED | |
| 12: Integration tests | ❌ NOT STARTED | |
| 13: Scan progress events to frontend | ❌ NOT STARTED | `notify::ScanProgress` constant already exists in FrontendRpc.h:47 |
| 14: Partitioning for large libraries | ❌ NOT STARTED | |
| 15: Incremental scan | ❌ NOT STARTED | |
| 16: Auto-play preview integration | ❌ NOT STARTED | |
| 17: Error handling hardening | ❌ NOT STARTED | Fold in Task 4 suggestions: sampleRate==0 guard, pi constant, FFT named consts |

### Test status
**C++ engine tests (11 pass):**
- `AddLibraryPersistsToRegistry`
- `RemoveLibraryDeletesFromRegistry`
- `SetAutoScanTogglesFlag`
- `RegistryPersistenceRoundTrip`
- `ExtractMidiMetadata`
- `ExtractAudioMetadata`
- `SearchByName`
- `SearchFiltersByType`
- `SearchSortsByName`
- `SearchPaginates`
- `SearchLazyLoadsFromDiskAcrossInstances`

Run: `build\Debug\hdaw_tests.exe --gtest_filter=FileLibraryTest.*`

**Frontend tests (17 pass):** `cd frontend; npx vitest run src/store/libraryStore.test.ts`

**Full frontend suite:** `cd frontend && npx vitest run` — 34 files, 311 tests pass (no regression).

### Working tree (uncommitted)

**Task 9 partial work (IN PROGRESS — needs completion):**
- `frontend/src/store/browserStore.ts` — ✅ DONE: `"library"` added to `FileKindFilter` type + `loadKindFilter` validation
- `frontend/src/components/FileBrowser.tsx` — ⚠️ PARTIAL: `LibraryView` component is DEFINED (with library list, search, drag, formatDuration/formatSize helpers) but **NOT WIRED INTO THE RENDER TREE**. The main `FileBrowser` component's render still shows favorites+tree unconditionally — it needs a conditional: `{kindFilter === "library" ? <LibraryView /> : (<>{favorites}{tree}</>)}`. Also missing: CSS additions, test additions.
- `frontend/src/components/FileBrowser.css` — ❌ NOT MODIFIED (no CSS for library mode yet)
- `frontend/src/components/FileBrowser.test.tsx` — ❌ NOT MODIFIED (no library-mode tests yet)

**Pre-existing dirty files (unrelated, do NOT stage with library commits):**
- `src/engine/AudioEngine.cpp` — arranger chain WIP
- `src/engine/MainAudioProcessor.cpp` — arranger chain WIP
- `src/engine/TransportManager.h` — arranger chain WIP

## Key decisions & deviations from the plan (IMPORTANT — ALL SESSIONS MUST FOLLOW)

The plan was written before implementation. During Tasks 2-9, implementers discovered plan inaccuracies. These corrections are **established and non-negotiable** for later tasks:

### 1. JUCE 8 API differences (plan uses older signatures)
- `juce::Uuid::newUuid()` → `juce::Uuid()` (no `newUuid` static)
- `DynamicObject::getProperty(key, default)` 2-arg → use 1-arg `getProperty(key)` and check separately
- `var(DynamicObject::Ptr)` → `var(obj.get())` (pass raw pointer)
- MIDI tempo meta: use `getMetaEventData()` + `getMetaEventLength()`, not `getMetaEventParameters()`
- `addNoteOn`/`addNoteOff` on `MidiMessageSequence` → use `addEvent(juce::MidiMessage::noteOn(...))`

### 2. Test isolation
A second constructor `explicit FileLibraryManager(const juce::File& baseDir)` exists for tests. All tests use `HDAW::FileLibraryManager mgr(tempDir);`. This constructor is NOT in the plan's header — it was added during Task 2.

### 3. `scanning` flag changed to counter
`std::atomic<bool> scanning{false}` → `std::atomic<int> scanningCount{0}` (counter). `isScanning()` returns `scanningCount.load() > 0`.

### 4. Async scan wait pattern
Tests must poll `isScanning()` in a loop, NOT use a fixed `Thread::sleep`:
```cpp
for (int i = 0; i < 50 && mgr.isScanning(); ++i)
    juce::Thread::sleep(100);
```

### 5. Task 4 scanDirectory was hardcoded to MIDI
The committed `scanDirectory` needed to be generalized to branch on library type (`"midi"` vs `"audio"`). This was fixed in Task 4. The fix uses a type-lookup under the mutex, then chooses the directory-iterator wildcard and extractor by type.

### 6. Task 5 search() has a 4-phase deadlock-safe pattern
`loadLibraryEntries` takes its own `mutex`. Calling it from `search()`/`getEntry()` while holding `mutex` deadlocks (std::mutex is non-recursive). The fix: Phase 1 collect candidates under lock, Phase 2 lazy-load OUTSIDE lock, Phase 3 filter under lock, Phase 4 sort+paginate outside lock. **Never hold mutex while calling loadLibraryEntries.**

### 7. Task 6 — FileLibraryManager is owned by AudioEngine
The plan's MCP sample (`s.fileLibraryManager()`) is WRONG. The correct architecture: `AudioEngine` owns `FileLibraryManager fileLibraryManager` (value member) + exposes `getFileLibraryManager()`. MCP tools get the manager via `e->getFileLibraryManager()`. RPC dispatch does the same via `engine.getFileLibraryManager()`. `AudioEngine::initialize()` calls `fileLibraryManager.initialize()`.

**AudioEngine.cpp has pre-existing uncommitted arranger WIP.** The library additions to AudioEngine were staged surgically (only the `fileLibraryManager.initialize()` hunk + the AudioEngine.h member/getter). The arranger WIP must NOT be committed with library changes.

### 8. Frontend store pattern: DI, not singleton import
The plan's frontend store sample uses `(await import("../rpc")).rpc` inside actions. **This is NOT the codebase pattern.** Every store uses dependency injection: actions take `rpc: RpcClient` as a parameter, and the component layer passes the singleton in. Follow `arrangerStore.ts` and `libraryStore.ts` as references. This makes stores testable (pass a mock).

### 9. CSS tokens — proven only
The plan's CSS uses `var(--hover-bg)` and `var(--accent-bg)` — **these tokens do NOT exist** in the theme. Use ONLY tokens already proven in `FileBrowser.css`: `--accent`, `--bg-elevated`, `--bg-panel`, `--bg-widget`, `--border`, `--border-light`, `--text-muted`, `--text-primary`, `--text-secondary`, `--error`, `--info`, `--success`. For hover backgrounds use `--bg-elevated`. Gate 8 (CSS tokens) applies to all CSS changes.

### 10. QString↔juce::String conversion idioms
- **MCP layer:** use `jstr(juceString)` (defined in `McpTools.cpp`) for juce→Qt. Use `juce::String(qtString.toUtf8().constData())` for Qt→juce.
- **Frontend router:** use a local `qstr()` helper (`QString::fromUtf8(s.toRawUTF8())`). The frontend predominantly uses `QString::fromStdString()` in older code, but `fromUtf8(toRawUTF8())` is cleaner/preferred.

### 11. `loadLibraryEntries` has double-checked locking
After Task 5's race fix (`4431ed4`), `loadLibraryEntries` uses double-checked locking: fast-path check under lock, file I/O outside lock, re-check + populate under lock. The `loadedLibraries.count(id)` read is always under `mutex` now.

### 12. MCP parity rule
Every user-facing feature must also be an MCP tool. The 7 RPC methods (`library.list/add/remove/scan/search/getEntry/setAutoScan`) mirror the 7 MCP tools exactly. JSON shapes must match field-by-field (see parity audit between `McpTools_Library.cpp` and `Router_Library.cpp`).

### 13. `notify::ScanProgress` constant exists
`FrontendRpc.h:47` already has `inline constexpr const char* ScanProgress = "notify.scanProgress";` — use it for Task 13 event wiring.

### 14. Baseline tsc has ~22 pre-existing errors
`npx tsc --noEmit` reports ~22 errors in unrelated WIP files (MidiThumbnailCanvas, MixerStrip, NoteGrid, PopUpBrowser, TimelineMinimal, TrackHeaders, meterStore.test). These are NOT from the library feature. When verifying tsc for frontend tasks: confirm your files add 0 errors (move-aside test), not that tsc is fully clean.

### 15. Test suite names differ from the plan
The plan references `McpServerTest.*` / `McpFunctionalityTest.*` / `RpcSurfaceTest.*` — these don't exist. The actual gtest suites exercising MCP/RPC are `McpServer` and `FrontendServer`. Use `--gtest_filter=McpServer.*:FrontendServer.*:FileLibraryTest.*` for comprehensive coverage.

## Resume point: Task 9 (FileBrowser Library mode)

**Status:** Partially implemented. The implementer was dispatched but the session was interrupted before it completed.

**What's done (in working tree, NOT committed):**
- `browserStore.ts`: `"library"` added to `FileKindFilter` type + `loadKindFilter` validation — COMPLETE.
- `FileBrowser.tsx`: `LibraryView` component is fully defined (library list with scan/progress, search input with debounce, results table with formatDuration/formatSize, drag support using `{path, name}` payload, DI pattern passing `rpc` to all store actions, slice selectors). BUT: the main `FileBrowser` render has NOT been modified to conditionally show `LibraryView` when `kindFilter === "library"`.

**What's missing:**
1. **Wire LibraryView into FileBrowser render:** In the main `FileBrowser` component, conditionally render `<LibraryView />` when `kindFilter === "library"`, and hide the favorites+tree in that mode. Keep header/search/chips/preview-bar visible in both modes.
2. **CSS:** Append library-mode styles to `FileBrowser.css`. Use ONLY proven tokens (see §9 above). Plan's sample CSS is in the plan file Task 9 lines ~1610-1624.
3. **Tests:** Add library-mode tests to `FileBrowser.test.tsx`. Existing tests (filter chips) must still pass. New tests: Library chip renders, LibraryView shows libraries/search results, Rescan All triggers scan. Follow the existing test pattern (mock `rpc`, set `useBrowserStore`/`useLibraryStore` state).
4. **Build & verify:** `npm run build`, `npx vitest run src/components/FileBrowser.test.tsx`, full vitest suite.
5. **Commit:** 4 files: `browserStore.ts`, `FileBrowser.tsx`, `FileBrowser.css`, `FileBrowser.test.tsx`. Message: `feat(library): add Library mode to FileBrowser with metadata columns`. Do NOT stage the arranger WIP files.

The full Task 9 spec is in the plan file at lines ~1506-1637. The implementer subagent prompt from the previous session is also a good reference for the corrected spec (it incorporates all the corrections above).

## Remaining tasks (10-17)

| Task | Files | Key notes |
|------|-------|-----------|
| 10: Preferences library settings | `PreferencesDialog.tsx` | Add "Libraries" section. Use `useLibraryStore` + `rpc` DI. |
| 11: Default MIDI dir + examples | `FileLibraryManager.cpp` | `initialize()` creates `%APPDATA%/HDAW/MIDI`, registers it, creates example files. |
| 12: Integration tests | `file_library_test.cpp` | Full pipeline test (add+scan+search), scan progress callback test. |
| 13: Scan progress events | `FrontendServer.cpp`, `libraryStore.ts`, `App.tsx` | Wire `setScanProgressCallback` → WebSocket `notify.scanProgress` → `useLibraryStore.updateScanProgress`. `notify::ScanProgress` constant already in `FrontendRpc.h:47`. |
| 14: Partitioning | `FileLibraryManager.cpp` | Save/load in per-first-char partition files when entries > 50k. |
| 15: Incremental scan | `FileLibraryManager.cpp` | Timestamp-based change detection in `scanDirectory`. |
| 16: Auto-play preview | `FileBrowser.tsx` | Wire audio preview via `preview.load`/`preview.play` on library entry click. |
| 17: Error handling hardening | `FileLibraryManager.cpp` | try/catch per file in scanDirectory, sampleRate==0 guard, pi constant, FFT named consts. |

## Architecture summary (for later tasks)

```
AudioEngine
├── FileLibraryManager fileLibraryManager  (value member)
│   ├── libraries: vector<LibraryInfo>     (under mutex)
│   ├── entries: map<id, vector<LibraryEntry>>  (under mutex, lazy-loaded)
│   ├── loadedLibraries: unordered_set<id> (under mutex, double-checked locking)
│   ├── threadPool{2}                     (scanning)
│   └── persistence: %APPDATA%/HDAW/libraries/
│       ├── registry.json
│       └── <id>.json
│
├── MCP: McpTools_Library.cpp → 7 tools via lib->getXxx()
├── RPC: Router_Library.cpp → 7 methods via lib.getXxx()
└── Frontend:
    ├── libraryStore.ts (DI: actions take RpcClient)
    ├── FileBrowser.tsx LibraryView (uses store + rpc singleton)
    ├── PreferencesDialog.tsx (Task 10: add/remove libraries)
    └── App.tsx (Task 13: event wiring)
```

## Quick reference: build & test commands

```powershell
# C++ build (Debug)
cmake --build build --config Debug

# C++ library tests
build\Debug\hdaw_tests.exe --gtest_filter=FileLibraryTest.*

# C++ MCP + frontend-server tests
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.*:FrontendServer.*:FileLibraryTest.*

# Frontend TypeScript check (expect ~22 pre-existing errors in WIP files)
cd frontend; npx tsc --noEmit

# Frontend unit tests
cd frontend; npx vitest run

# Frontend production build
cd frontend; npm run build

# Frontend library store tests only
cd frontend; npx vitest run src/store/libraryStore.test.ts

# Frontend FileBrowser tests only
cd frontend; npx vitest run src/components/FileBrowser.test.tsx
```

## Subagent dispatch template (recap)

For each remaining task, follow this loop:
1. **Implementer** (general subagent): Give it the full task text from the plan + the "Key decisions & deviations" section above. Have it implement, test, commit.
2. **Spec reviewer** (goal-verify subagent): Verify all deliverables from the task spec exist.
3. **Code quality reviewer** (goal-verify subagent): Check thread safety, error handling, test quality, JUCE API correctness.
4. If reviews find issues → dispatch a **fix subagent** with specific issues, then re-review.
5. Mark task complete in TodoWrite, move to next.

Do NOT pause to check in with the user between tasks — execute continuously through Task 17. Only stop on BLOCKED status you can't resolve or genuine ambiguity.

## After all tasks

Run the final code reviewer over the whole implementation, then use the `finishing-a-development-branch` skill to wrap up.
