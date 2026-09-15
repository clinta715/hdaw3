# Async `export_audio` MCP tool (non-blocking handler + completion notification)

**Status:** RESOLVED (2026-08-10). All gates pass. `export_audio` is now async:
the handler returns "export started: <path>" immediately; completion arrives via
`notifications/exportComplete` {success, message, outputPath}; the redundant
cancel-watcher thread is deleted (engine-side per-block cancel already correct);
server lifetime guarded with QPointer. New mid-render cancel test proves
per-block cancellation end-to-end (success=false, partial file deleted, flag
cleared). `McpServer.*` suite: 13/13 green; matrix peaks unaffected
(ShinRonin 0.299 / Gneiss 0.25 / Retrospect 0.300). One test-infra note: queued
notification delivery in loopback tests requires
`QCoreApplication::processEvents` pumping on the main thread (test_main runs no
event loop) — baked into `waitExportComplete` and the matrix main-thread wait.

## Goal

Make the MCP `export_audio` tool **asynchronous**: the handler returns
immediately ("started") instead of blocking on `doneFuture.get()`, so the MCP
server stays responsive during a render; completion is delivered via a new
`notifications/exportComplete` notification. Per-block cancellation is already
implemented in the engine and is retained.

## Current state (verified)

- `ExportManager` (`src/engine/ExportManager.cpp:224-255`) already renders on
  its own worker thread and already does **per-block cancellation**: the render
  loop checks `cancelFlag` every block and calls `onProgress` every block. The
  engine side of the follow-up is DONE — nothing to change there.
- `McpExportTool.cpp` is the gap: the handler sets `onProgress`/`onComplete`,
  spawns a **redundant cancel-watcher thread**, then **blocks the dispatching
  thread** on `doneFuture.get()` until the render finishes. While exporting,
  the server cannot answer any other request.
- `McpServer::notifyFromBackground` (slot, `McpServer.cpp:18`) is already
  thread-safe (queued invocation → main thread → transport). `onProgress` already
  uses it. The same mechanism carries the new completion notification.
- Frontend does NOT call `export_audio` (grep of `frontend/src` = 0 hits) —
  consumers are MCP clients + the test suite. No UI change needed.
- Tests that parse the response: `McpServer.ExportAudioRendersDefaultProject`
  (asserts `exported to`), `McpServer.ExportAudioWithClapPluginDoesNotHang`
  (asserts `exported to` + non-silence), and the matrix
  `McpServer.DiagnosticClapExportMatrix` (`r.exportOk = expText.contains("exported to")`).
  `ExportAudioDryRunReturnsPlan` + `ExportAudioSkipsWhenCancelFlagSet` are
  unaffected (no render starts).

## Design

**Tool response** (immediate): `McpToolResult::text("export started: <path>")`.

**New notification** `notifications/exportComplete`:
`{ "success": bool, "message": "...", "outputPath": "<path>" }`, sent from
`onComplete` (render thread) via `notifyFromBackground` — exactly like the
existing progress notifications.

**Lifetime:** capture `QPointer<McpServer>` instead of the raw `&s`; guard
every render-thread touch (`isCancelRequested` in `onProgress`, the
`notifyFromBackground` calls in `onComplete`) with `if (serverPtr.isNull())
return;`. Qt drops queued calls to deleted QObjects, so a server torn down
mid-export cannot crash.

**Cancel-watcher thread:** delete it. The per-block `onProgress` check already
cancels within one block (~12 ms).

**Cancel flag:** reset in `onComplete` (as today) — and keep the
cancel-before-start short-circuit at the top of the handler.

**Double-start:** keep the `em.isExporting()` guard ("export already in
progress").

## Success Gates (all must pass to declare done)

- [ ] Gate 1: `cmake --build build --config Debug` succeeds (force-touch
      `McpExportTool.cpp` + `mcp_server_test.cpp` first — lesson 15).
- [ ] Gate 2: `export_audio` response arrives **before** the export finishes
      and contains `export started`; `notifications/exportComplete` arrives
      afterwards with `success:true` and the path (asserted by updated tests).
- [ ] Gate 3: new mid-render cancel test: start an 8 s export, send
      `notifications/cancelled` after the response, assert
      `exportComplete.success == false`, message mentions cancel, output file
      removed, cancel flag cleared.
- [ ] Gate 4: `McpServer.*` suite green — updated `ExportAudioRendersDefaultProject`,
      `ExportAudioWithClapPluginDoesNotHang` (non-silence assertion kept),
      matrix (polls exportComplete; the three FX plugins still peak > 0.01),
      plus the new cancel test.
- [ ] Gate 5: `notifications/exportComplete` is emitted exactly once per
      export (assert in tests: no duplicate completion lines).
- [ ] Gate 6: docs updated — `docs/realtime-safety.md` §MCP server v1
      follow-ups: strike the `export_audio` worker-thread line (done), and the
      tool description string in `McpExportTool.cpp` rewritten for async
      semantics (no "blocks until completion").

## Dependency Map

- Blast radius: `src/mcp/McpExportTool.cpp` (tool handler + description),
  `tests/integration/mcp/mcp_server_test.cpp` (3 tests + matrix + 1 new test),
  `docs/realtime-safety.md` (one follow-up bullet).
- Upstream: `McpServer::notifyFromBackground` / `isCancelRequested` /
  `resetCancelFlag` (all exist, unchanged); `ExportManager` callbacks
  (unchanged engine). `mcp::registerExportTool` wiring unchanged.
- Downstream: MCP clients (stdio/HTTP) — response shape + new notification;
  loopback tests.
- Projections affected: none. SPSC/audio-thread: none — callbacks run on the
  render worker thread, same as today.
- God nodes: none (McpExportTool is a leaf registrar; McpServer unchanged).

## Pitfall Gates Triggered

- Gate 2 (unimplemented path): verified full chain for the new notification —
  `onComplete` (render thread) → `QPointer` guard → `notifyFromBackground`
  (queued) → main thread → `transport_->notify` → loopback/stdio/HTTP outgoing.
  Tests assert the notification content end-to-end.
- Gate 4 (stale binaries): force-touch both edited files; rebuild
  `hdaw_tests`; verify the binary picked the change (matrix + export tests run
  the new flow).
- Gate 3 (audio thread): NOT triggered — render thread, not audio thread;
  existing pattern.
- Anti-pattern check: no blocking waits introduced; no new threads (the
  cancel-watcher is REMOVED); no `syncSnapshot`-style races (no frontend).

## Steps

1. `src/mcp/McpExportTool.cpp`:
   - Rewrite the tool description (async semantics + `notifications/exportComplete`).
   - `QPointer<McpServer> serverPtr(&s);` (include `<QPointer>`).
   - Keep: dryRun, cancel-before-start short-circuit, format/sampleRate/bitDepth
     parsing, `em.isExporting()` guard, `onProgress` (add the `serverPtr.isNull()`
     guard around the `isCancelRequested` read), `startExport` failure path.
   - `onComplete`: send `notifications/exportComplete` via
     `notifyFromBackground` (QPointer-guarded), then clear `em.onProgress` /
     `em.onComplete`, then `s.resetCancelFlag()` (via guarded pointer; on null
     server skip the reset — the server is gone anyway).
   - Delete the `donePromise`/`doneFuture`/`cancelWatcher` machinery.
   - Return `"export started: <path>"` immediately after `startExport` +
     the initial progress notification (keep the `starting render` notification).
2. `tests/integration/mcp/mcp_server_test.cpp`:
   - Add a local helper (in the anonymous namespace) that polls the loopback
     for a `notifications/exportComplete` line: loop `tp.waitForOutgoing(
     remaining, &out)` + scan for the substring `"notifications/exportComplete"`
     (with a small `QThread::msleep(10)` between polls to avoid a hot spin;
     drain between polls or scan the accumulated buffer — the loopback
     accumulates). Return the parsed `QJsonObject`. Signature e.g.
     `QJsonObject waitExportComplete(mcp::TransportLoopback& tp, int msec)`.
   - `ExportAudioRendersDefaultProject`: assert response contains
     `export started`; then wait for exportComplete (30 s); assert
     `success == true`, file exists + size > 0, `s.isCancelRequested()` false.
   - `ExportAudioWithClapPluginDoesNotHang`: same pattern (response first,
     then wait exportComplete); keep the non-silent WAV assertion.
   - Matrix worker: after `run(baseId+4, "export_audio", ...)`, poll for
     exportComplete; set `r.exportOk = success` and append
     `complete=ok/failed:<msg>` to the phase note. Keep per-plugin timeout
     budget sane (the 180 s worker cap covers the render + poll).
   - NEW `TEST(McpServer, ExportAudioCancelsMidRender)`: 8 s export of the
     default project, wait for the `export started` response, then
     `tp.pumpIncoming` a `notifications/cancelled` (id null, per the
     existing cancel-notification shape — check how `setCancelFlag` is
     triggered via the transport: notifications/cancelled → dispatchRequest →
     handleMethod → setCancelFlag; reuse that shape), then wait for
     exportComplete; assert success false, message contains "cancel", output
     file absent, `s.isCancelRequested()` false.
3. `docs/realtime-safety.md` (§MCP server v1 follow-ups, lines ~332-343):
   remove/replace the `export_audio` worker-thread bullet with a done note.
4. Build + run gates.

## Verification commands

```powershell
taskkill /IM cl.exe /F 2>$null; taskkill /IM MSBuild.exe /F 2>$null
(Get-Item src\mcp\McpExportTool.cpp).LastWriteTime = Get-Date
(Get-Item tests\integration\mcp\mcp_server_test.cpp).LastWriteTime = Get-Date
cmake --build build --config Debug --target hdaw_tests          # big timeout (~10 min)
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.ExportAudio*   # fast
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix  # ~85 s
& .\build\Debug\hdaw_tests.exe --gtest_filter=McpServer.*        # full MCP suite
```

## Anti-patterns to avoid

- No new threads; no blocking `future.get()` in the handler.
- Do NOT change `ExportManager` (engine per-block cancel already correct).
- Do NOT weaken the matrix `EXPECT_GT` or the hang test's non-silence check.
- No stale-binary runs (lesson 15): force-touch + rebuild + verify the new
  "export started" text actually appears (the old binary would block on
  `doneFuture.get()` and the new tests would time out — that is the probe).
