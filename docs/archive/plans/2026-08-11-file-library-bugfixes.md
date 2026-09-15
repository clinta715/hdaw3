# Plan: Fix File Library System v0.18.0 Bugs

## Goal
Fix all 9 bugs identified in the code review of the recently committed File
Library System (v0.18.0): thread-safety/lifecycle issues, algorithm correctness
issues, and a notification-name collision.

## Success Gates (all must pass to declare done)
- [ ] G1: `cmake --build build --config Debug` succeeds with zero warnings new to this change.
- [ ] G2: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibrary*` passes (existing tests green).
- [ ] G3: New gtest `FileLibraryTest.ScanLibraryDecrementsScanningCountOnException` passes — proves fix #1 (RAII guard).
- [ ] G4: New gtest `FileLibraryTest.RemoveLibraryDuringScanDoesNotResurrect` passes — proves fix #2 (re-check + job cancellation).
- [ ] G5: New gtest `FileLibraryTest.EmptyLibraryMarkedLoaded` passes — proves fix #5 (no re-read on empty).
- [ ] G6: New gtest `FileLibraryTest.ShrinkFromPartitionedRemovesPartitions` passes — proves fix #6 (cleanup on shrink).
- [ ] G7: New gtest `FileLibraryTest.IncrementalScanDetectsSubSecondChange` passes — proves fix #7 (int64 mtime comparison).
- [ ] G8: New gtest `FileLibraryTest.AudioKeyDetectionProducesNonEmptyKey` passes — proves fix #4 (float accumulation; a sine-tone C4 wav yields key "C").
- [ ] G9: `cd frontend && npm test` passes (libraryStore tests green after notification rename).
- [ ] G10: `cd frontend && npm run build` succeeds.
- [ ] G11: grep confirms no remaining `notify.scanProgress` emission for library scans in `src/frontend/FrontendServer.cpp` (fix #9).
- [ ] G12: grep confirms `main.tsx`, `FXChain.tsx`, `PluginManagerDialog.tsx` each subscribe to the correct notification name.

## Dependency Map (from grep + review)
- **Blast radius:** All C++ fixes localized to `src/engine/FileLibraryManager.{h,cpp}`. No public API signature changes.
- **Upstream callers (unchanged signatures, no consumer edits needed):**
  - `AudioEngine::initialize()` → `FileLibraryManager::initialize()`, `scanLibrary()`
  - `FrontendServer` ctor → `setScanProgressCallback`, `setScanCompleteCallback`
  - `Router_Library.cpp` → all public query/mutation methods
  - `McpTools_Library.cpp` → same set
- **Downstream consumers:** `libraryStore.ts` (frontend), gtest suite `file_library_test.cpp`.
- **God nodes in scope:** None — `FileLibraryManager` is a leaf module (no graph hub).
- **Community boundaries crossed:** One — the notification rename crosses engine→frontend event channel (fix #9 only).
- **Projections affected:** None (no ReadModel, no audio graph, no SPSC bridge). Library metadata is offline-scanned, not realtime.
- **SPSC paths touched:** None.
- **Path integrity:** N/A — fixing existing paths, not adding wiring.
- **On-disk format change:** fix #7 adds a `modifiedTime` (int64) field to serialized entries. Old entries without it deserialize as 0 → forces a one-time rescan. Backward compatible.

## Pitfall Gates Triggered
- **Gate 4 (stale binaries):** C++ changes require `cmake --build build --config Debug` and testing against `build/Debug/hdaw_tests.exe` (not Release). Covered by G1/G2.
- **Gate 9 (operator[] default-construction):** Bug #2's root cause includes `entries[id]` default-constructing a spurious entry. Fix avoids `operator[]` in the scan job (uses `find` or skips if library removed).
- **Gates 1, 3, 5, 6, 8:** Not applicable (no audio-thread code, no routing rebuild, no React hooks/CSS, no SPSC state).
- **Anti-pattern scan:** The scan job already uses one batched file walk (not N RPCs). Fix #2 must not introduce a full-tree walk — uses indexed lookup on `libraries`.

## Bug Inventory (fix → location → approach)

### Fix #1 — CRITICAL: `scanningCount` leaked on exception
**File:** `FileLibraryManager.cpp:225-257` (scanLibrary job)
**Approach:** Wrap the job body in a try/catch that always decrements. Cleanest: a small RAII guard (`struct ScanCountGuard { std::atomic<int>& c; ~ScanCountGuard(){ c.fetch_sub(1); } }`) constructed right after `fetch_add`, so the decrement happens regardless of how the job exits (return, throw, exception propagation). The `completeCb(id, false)` should fire on exception.

### Fix #2 — CRITICAL: `removeLibrary` races in-flight scans (data resurrection)
**File:** `FileLibraryManager.cpp:109-125` (removeLibrary), `225-257` (scanLibrary job)
**Approach:**
1. In `removeLibrary`, record the removed `id` in a `std::unordered_set<juce::String> removedIds` (under `mutex`) so the scan job can detect "I'm scanning a removed library."
2. In the scanLibrary job, after `scanDirectory` returns and before saving, re-check under `mutex` that `id` is still in `libraries` AND not in `removedIds`. If removed, skip `saveLibraryEntries`/`saveRegistry`/`completeCb(true)` and bail (the RAII guard from #1 still decrements).
3. Also avoid `entries[id]` operator[] in the job (line 234) — use `entries.find(id)` and skip if absent, so we don't default-construct a spurious entry for a removed library (Gate 9).
4. Note: `ThreadPool` has no per-job cancellation API that's clean here; the re-check approach is the robust pattern (check-point in the job, not pre-emptive cancel).

### Fix #3 — HIGH: Shutdown hang — scan jobs aren't cancellable
**File:** `FileLibraryManager.cpp:404-520` (scanDirectory)
**Approach:** The scan job is a `juce::ThreadPoolJob` (via `threadPool.addJob` with a lambda). Check `shouldExit()` (the lambda receives a `ThreadPoolJob*` reference — use `juce::ThreadPool::getJob` pattern OR convert the lambda to check `threadPool.contains(this)`... actually the clean JUCE pattern: `addJob` with a lambda that takes no args can't easily check shouldExit). **Revised approach:** add a `std::atomic<bool> shutdownRequested{false}` member, set it in `~FileLibraryManager` before `removeAllJobs`, and check it inside the `scanDirectory` file loop (lines 453-503) to break early. This gives prompt cancellation on shutdown.

### Fix #4 — HIGH: Audio key-detection magnitude truncation
**File:** `FileLibraryManager.cpp:648-715` (extractAudioMetadata), `598-646` (detectKey), `522-596` (extractMidiMetadata call site)
**Approach:** Change `detectKey` signature from `const std::vector<int>&` to `const std::vector<double>&`. In `extractAudioMetadata`, accumulate `std::vector<double> pitchClassCounts(12, 0.0)` with `pitchClassCounts[pc] += (double)mag;` (no truncation). In `extractMidiMetadata`, change `noteCounts` to `std::vector<double>` (`noteCounts[pitch] += 1.0`). The scoring math in `detectKey` already uses doubles internally — just propagate doubles from the input.

### Fix #5 — MEDIUM: Empty library never marked loaded → re-reads disk every search
**File:** `FileLibraryManager.cpp:271-335` (loadLibraryEntries)
**Approach:** Move `loadedLibraries.insert(id)` to commit unconditionally when we've read the file(s) (even if `loaded` is empty), BEFORE the `if (loaded.empty()) return;` early-exit. Specifically: after the file-load section, take the lock, re-check `loadedLibraries.count(id)`, and if absent, do `entries[id] = std::move(loaded); loadedLibraries.insert(id);` unconditionally. The empty case still inserts an empty vector.

### Fix #6 — MEDIUM: Partition cleanup missing on shrinkage
**File:** `FileLibraryManager.cpp:365-373` (saveLibraryEntries non-partitioned branch)
**Approach:** Extract the partition-cleanup logic (currently at lines 387-390) into a small lambda or static helper, and call it at the top of BOTH branches. I.e., when writing a single file (non-partitioned), also delete any stale `{id}_part_*.json` files. Concretely: hoist the cleanup so it runs whenever we're about to write, regardless of branch.

### Fix #7 — MEDIUM: Incremental-scan timestamp granularity (sub-second changes undetected)
**File:** `FileLibraryManager.h` (LibraryEntry struct), `FileLibraryManager.cpp` (scanDirectory, loadLibraryEntries deserialize, saveLibraryEntries serialize)
**Approach:**
1. Add `juce::int64 modifiedTime = 0;` to `LibraryEntry` (raw milliseconds from `file.getLastModificationTime()`).
2. In `scanDirectory`, populate `modifiedTime` alongside `modified` (keep the ISO string for display/JSON).
3. Compare on `modifiedTime` instead of `modified` string (line 461): `if (it->second.modifiedTime == currentModifiedTime)`.
4. Serialize/deserialize `modifiedTime` in save/load (default 0 on old entries → one-time rescan).
5. Keep `modified` (ISO8601) for any display consumer (currently none in the frontend, but harmless to keep).

### Fix #8 — LOW: Callback setters write without lock
**File:** `FileLibraryManager.cpp:144-149`
**Approach:** Take `std::lock_guard<std::mutex> lock(mutex);` in both `setScanProgressCallback` and `setScanCompleteCallback`. The readers already take the lock (lines 215-218, 489-493). This makes the locking discipline consistent.

### Fix #9 — LOW: Notification-name collision (library vs plugin scan progress)
**Files:** `src/frontend/FrontendRpc.h`, `src/frontend/FrontendServer.cpp:51-66`, `frontend/src/main.tsx:84-101`
**Approach:**
1. In `FrontendRpc.h`, add to `namespace notify`:
   - `inline constexpr const char* LibraryScanProgress = "notify.libraryScanProgress";`
   - `inline constexpr const char* LibraryScanComplete = "notify.libraryScanComplete";`
2. In `FrontendServer.cpp` library callbacks (lines 58, 65), use the new constants instead of the shared `ScanProgress` and the literal string.
3. In `main.tsx:84`, change the subscription from `"notify.scanProgress"` to `"notify.libraryScanProgress"`, and `:96` from `"notify.libraryScanComplete"` (already correct literal, but switch to matching the constant name for consistency — the literal stays the same string).
4. Plugin scan consumers (`FXChain.tsx:117`, `PluginManagerDialog.tsx:77`) are unchanged — they keep `notify.scanProgress`.

## Steps

### Phase 1 — C++ engine fixes (Task 1, one subagent)
All 8 FileLibraryManager fixes (#1-#8) are in the same file and several interact (e.g., #2's re-check and #3's shutdown flag both touch the scan job; #1's RAII guard wraps the same job). One subagent does them together to avoid edit conflicts.

1. Edit `FileLibraryManager.h`:
   - Add `std::atomic<bool> shutdownRequested{false};` member (fix #3).
   - Add `std::unordered_set<juce::String> removedIds;` member (fix #2).
   - Add `juce::int64 modifiedTime = 0;` to `LibraryEntry` (fix #7).
   - Change `detectKey` signature to `const std::vector<double>&` (fix #4).
2. Edit `FileLibraryManager.cpp`:
   - Add RAII guard for `scanningCount` in `scanLibrary` job (fix #1).
   - Add removed-id re-check + `find` instead of `operator[]` in scan job (fix #2, Gate 9).
   - Set `shutdownRequested` in destructor; check it in `scanDirectory` loop (fix #3).
   - Change `pitchClassCounts`/`noteCounts` to `double`, update `detectKey` body (fix #4).
   - Move `loadedLibraries.insert(id)` before empty-check in `loadLibraryEntries` (fix #5).
   - Hoist partition-cleanup to both branches of `saveLibraryEntries` (fix #6).
   - Populate/compare/serialize `modifiedTime` in scanDirectory/load/save (fix #7).
   - Lock the two callback setters (fix #8).
   - Add `removedIds` maintenance in `removeLibrary`.
3. Edit `tests/unit/engine/file_library_test.cpp`:
   - Add the 6 new gtest cases (G3-G8).

### Phase 2 — Notification rename (Task 2, parallel subagent)
Independent files (`FrontendRpc.h`, `FrontendServer.cpp`, `main.tsx`) — no conflict with Phase 1's `FileLibraryManager.cpp`. **Dispatch in parallel with Phase 1.**

1. Edit `src/frontend/FrontendRpc.h`: add the two constants.
2. Edit `src/frontend/FrontendServer.cpp`: use new constants in library callbacks.
3. Edit `frontend/src/main.tsx`: update subscription names.

### Phase 3 — Verification (orchestrator)
1. `cmake --build build --config Debug` (G1).
2. `build/Debug/hdaw_tests.exe --gtest_filter=FileLibrary*` (G2-G8).
3. `cd frontend && npm test` (G9).
4. `cd frontend && npm run build` (G10).
5. grep checks (G11, G12).

## Risk Notes
- Fix #7 changes the on-disk JSON shape (adds a field). This is append-only and backward compatible (old entries get `modifiedTime=0` → one rescan). No migration needed.
- Fix #2 adds a `removedIds` set that grows monotonically during a session. Acceptable (ids are tiny strings; a user won't remove thousands of libraries). Could prune on `addLibrary` if the same id is reused (UUIDs make collision impossible, so skip).
- Fix #4 changes `detectKey` for both MIDI and audio paths. MIDI key detection was already correct (integer note counts → doubles lose nothing). Audio key detection becomes meaningful.
- The subagent must NOT touch the audio thread, routing graph, or any ValueTree — this is purely the offline library scanner.
