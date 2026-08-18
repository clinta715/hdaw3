# Handoff — Disconnect-during-export crash (2026-08-18)

## Result

Fixed the engine crash when a WebSocket client is killed (aborted, e.g. browser
tab closed) while an `export.audio` request is in flight. **The plan's prescribed
fix (QPointer guard in `FrontendServer`) alone does NOT fix it** — the crash is a
Qt 6.11.1 bug triggered by re-entrant socket processing inside the export
handler's nested event loop. The real fix is in `Router_Export.cpp`.

## Root cause (proven, not theorized)

- `Router_Export::dispatchExport` spins `QEventLoop::exec()` (nested loop) to
  keep the UI alive during a multi-second export.
- If a client **abruptly** dies (RST — `socket.destroy()`, killed browser tab)
  during that window, the nested loop processes the disconnect re-entrantly:
  `QWebSocketServerPrivate::onSocketDisconnected` → `deleteLater()` on the
  accepted `QTcpSocket`.
- The DeferredDelete runs while `QWebSocketPrivate::processData` is mid-iteration
  (`while (m_pSocket->bytesAvailable())`, qwebsocket_p.cpp:1374). The socket's
  `destroyed` signal nulls `m_pSocket`; the loop re-checks it unguarded →
  `NULL_POINTER_READ` (READ_ADDRESS 0x0), `processData+0xfb`, crash 0xc0000005.
- Confirmed identical on the ORIGINAL unmodified code under cdb (Qt 6.11.1
  PDBs: `C:\Qt\6.11.1\msvc2022_64\bin`). Upstream Qt dev branch has the same
  code — a genuine Qt bug, not an HDAW bug.
- **A graceful WS close never reproduces it** (the close handshake doesn't
  complete inside the export window); only an abrupt kill does.

## Fix (3 files)

1. `src/frontend/router/Router_Export.cpp` — **the actual fix.** Nested loop now
   runs with `QEventLoop::ExcludeSocketNotifiers`, so socket events (including
   disconnects) are deferred to the outer loop, where the connection tears down
   cleanly after the handler unwinds. Progress notifications are queued
   invocations, not socket events, so they still stream live.
2. `src/frontend/FrontendServer.cpp` — the plan's QPointer guard + `send` lambda
   (4 raw `socket->sendTextMessage` replaced). Now inert after fix #1 but kept
   as requested defense-in-depth.
3. `tests/unit/frontend/frontend_server_test.cpp` — new
   `FrontendServer.DisconnectDuringExportDoesNotCrash`: real 60s export, client
   aborts (`socket->abortConnection()`) 200ms in via a timer that fires INSIDE
   the nested loop, polls `engine.getMainProcessor()->isExporting()` until the
   export unwinds, then reconnects a fresh client and round-trips. Includes
   `TestClient::abortConnection()`.

## Verification

- New test: **passes 5/5 standalone and 5/5 in the FrontendServer suite** (~3.6s
  each). The earlier one-off flake was a timing race in the test (c2's 2s
  connect window while the handler unwound) — fixed by polling
  `isExporting()` C++-side before reconnecting.
- Negative proof: reverting ONLY the Router_Export.cpp line back to
  `loop.exec()` → test crashes with `SEH 0xc0000005` (gtest catches it and
  continues; exit -1073741819). The test reliably catches the bug.
- Full FrontendServer suite: 13/13. `*Export*` (incl. McpServer export matrix):
  9/9. **Full suite: 902/902, exit 0.**
- Runtime check (G5): launched `build/Debug/HDAW.exe` (`HDAW_NO_BROWSER=1`),
  ran a raw-TCP deterministic repro
  (`%TEMP%\opencode\crash_repro_client.cjs` — manual WS handshake + RST via
  `socket.destroy()` 250ms into a 60s export), then reconnected and confirmed
  `export.isExporting => false`. Engine process stayed alive. PASS.
- Left no stale engine running (cleaned up after the check).

## Notes for next session

- Working tree still carries the **pre-existing unrelated FM-pitch session**
  changes (`ExportManager.cpp`, `FmSynthEngine.*`, `MidiFx.h`,
  `msfa/dx7note.*`, `tests/CMakeLists.txt`, `fm_synth_test.cpp`,
  `midi_fx_test.cpp`, untracked `docs/handoffs/2026-08-18-fm-pitch-fix.md`,
  `projects/`, `export_volume_bypass_test.cpp`) — NOT part of this fix.
- The plan's G4 gate said "two file edits" — this lands with three (the
  Router_Export.cpp deviation is the mandatory root-cause fix; the FrontendServer
  guard alone provably cannot fix the crash).
- `crash_repro_client.cjs` lives in `%TEMP%\opencode\` and is reusable for the
  runtime check; it requires a raw-TCP RST (global WebSocket `close()` is
  graceful and never reproduces).
- If the engine is ever moved off Qt 6.11.1, re-verify: the ExcludeSocketNotifiers
  change is a workaround for the Qt bug; the ideal fix is a Qt patch/upgrade.