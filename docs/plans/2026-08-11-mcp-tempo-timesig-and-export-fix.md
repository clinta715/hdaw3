# Handoff: BPM / Time-Signature MCP + UI wiring, and MCP WAV export fix

Date: 2026-08-11 · Version context: HDAW 0.15.1 · Status: **plan, not implemented**

Two deliverables:
1. **Feature-parity gap:** wire `setTempo` / `setTimeSignature` into the MCP surface
   (and finish the time-signature UI, which is display-only and hardcoded today).
2. **Bug:** the MCP `export_audio` tool is permanently blocked by a stale cancel
   flag (`export cancelled (flag was already set)` every call).

---

## Part 1 — BPM / time signature wiring

### Current state (verified 2026-08-11)

| Layer | Tempo | Time signature |
|---|---|---|
| Engine command | `AudioEngineCommands::setTempo(double)` — `src/engine/AudioEngineCommands_Transport.cpp:7` (writes `IDs::tempo` on root tree, with undo) | `AudioEngineCommands::setTimeSignature(int,int)` — same file `:103` (writes `IDs::timeSigNumerator` / `IDs::timeSigDenominator` on transport tree, with undo) |
| Frontend RPC | `project.setTempo` — `src/frontend/router/Router_Project.cpp:348` (arg `bpm`) | `project.setTimeSignature` — same file `:353` (args `numerator`, `denominator`) |
| UI setter | ✅ TransportBar double-click BPM + tap tempo (`frontend/src/components/TransportBar.tsx:186-203`), Timeline context menu "Set Global BPM..." (`TimelineContextMenu.tsx:274-278`) | ❌ **No setter anywhere** — display-only `{n}/{d}` at `TransportBar.tsx:204-206` |
| UI state | `useTransportStore` ← `notify.transport` push (`frontend/src/main.tsx:39-41`) | ❌ Hardcoded 4/4 in `frontend/src/store/transportExtrasStore.ts:16-18` with TODO: *"Sync time signature from backend when TransportSnapshot includes it"* |
| Snapshot | `TransportSnapshot.bpm` — `src/common/ReadModel.h:83`, populated in `ReadModelImpl::getTransport()` `src/engine/ReadModelImpl.cpp:369`, serialized in `src/frontend/FrontendRpc.h:158-170`, typed in `frontend/src/rpc/types.ts:1-11` | ❌ **Not in the snapshot at all** (C++ struct, toJson, or TS type) |
| MCP | `get_project_summary` reports `tempo=` (`src/mcp/McpTools_Project.cpp:38`); `get_transport` does **not** report bpm (`src/mcp/McpTools_Transport.cpp:15-25`) | ❌ No MCP tool reads or writes it |

### Work items

#### 1a. MCP tools: `set_tempo` and `set_time_signature` (required — parity rule)

File: `src/mcp/McpTools_Project.cpp`, inside `registerWriteTools` (or the same
registration block as `set_scale`, which is the template — see `:738-745`).

Follow the `set_scale` pattern exactly (schema + handler):

```cpp
s.registerTool({"set_tempo", "Set the project tempo in BPM.",
    objSchema({{"bpm", QJsonObject{{"type","number"},{"minimum",1.0},{"maximum",999.0}}}}, {"bpm"}),
    [e](const QJsonObject& a) {
        e->getProjectCommands().setTempo(a.value("bpm").toDouble());
        return McpToolResult::text("ok");
    }});

s.registerTool({"set_time_signature", "Set the project time signature.",
    objSchema({{"numerator",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}},
               {"denominator", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}}},
              {"numerator","denominator"}),
    [e](const QJsonObject& a) {
        e->getProjectCommands().setTimeSignature(a.value("numerator").toInt(),
                                                 a.value("denominator").toInt());
        return McpToolResult::text("ok");
    }});
```

- `getProjectCommands()` returns `AudioEngineCommands&` (used everywhere in
  `McpTools_*`), which already has `setTempo`/`setTimeSignature`. No engine work.
- Note pitfall #2 (AGENTS.md): `setTempo` writes a ValueTree property; if the
  value is unchanged the listener won't fire — acceptable for a setter, but a
  round-trip test should change the value from its default (120 / 4-4).
- **Also extend `get_transport`** (`src/mcp/McpTools_Transport.cpp:15-25`) to
  include `bpm`, `timeSigNumerator`, `timeSigDenominator` (read from
  `m.getTree().getProperty(IDs::tempo, 120.0)` and the transport tree's
  `IDs::timeSigNumerator`/`IDs::timeSigDenominator`, mirroring
  `ReadModelImpl::getTransport()` at `src/engine/ReadModelImpl.cpp:366-392`).
  Keeps the read side complete so a client can set → read back.

#### 1b. Frontend: put time signature on the wire and make it settable

1. **C++ snapshot:** add `int timeSigNumerator = 4; int timeSigDenominator = 4;`
   to `TransportSnapshot` (`src/common/ReadModel.h:82-92`); populate in
   `ReadModelImpl::getTransport()` (`src/engine/ReadModelImpl.cpp:366-392`)
   from the transport tree; add both keys to `toJson(const TransportSnapshot&)`
   (`src/frontend/FrontendRpc.h:158-170`).
2. **TS type:** add `timeSigNumerator: number; timeSigDenominator: number;` to
   `TransportSnapshot` (`frontend/src/rpc/types.ts:1-11`) and to the store
   initializer (`frontend/src/store/transportStore.ts:9-19`).
3. **Kill the hardcode:** in `frontend/src/store/transportExtrasStore.ts`,
   remove `timeSignatureNum/Den` (or source them from `useTransportStore` via a
   selector) and delete the TODO at line 16. `TransportBar.tsx:30-31` should
   read the numbers from the transport store instead of the extras store.
4. **UI setter:** make the `{n}/{d}` display at `TransportBar.tsx:204-206`
   editable the same way BPM is (double-click → inline input → blur/Enter calls
   `rpc.call("project.setTimeSignature", { numerator, denominator })`), or add
   a small popover with `n`/`d` selects. The `project.setTimeSignature` RPC
   already exists on the backend — **no router work needed**.

#### 1c. Tests

- **gtest** (`tests/integration/mcp/mcp_server_test.cpp`, follow the existing
  `McpServer.*` tests' loopback pattern): `McpServer.SetTempo` (set 132 → read
  back via `get_project_summary` contains `tempo=132`), `McpServer.SetTimeSignature`
  (set 3/4 → `get_transport` reports 3/4), plus a round-trip through the undo
  manager (`undo` → back to 120 / 4-4).
- **Vitest:** `transportStore.test.ts` — new fields present in `update()`.
  `TransportBar.test.tsx` (if one exists — otherwise add) — time-signature
  display reflects store, inline edit calls `project.setTimeSignature`.
- **E2E** (optional but per regression-wall): transport-bar time-signature
  edit → transport readout updates.

---

## Part 2 — MCP `export_audio` stuck on stale cancel flag (bug fix)

### Symptom

Every `export_audio` call returns the error `export cancelled (flag was already
set)` — no WAV is ever rendered via MCP.

### Root cause (verified)

- `src/mcp/McpExportTool.cpp:47-50`: at tool entry the handler checks
  `s.isCancelRequested()`; if set, it **resets the flag and refuses to start**.
- The MCP client (opencode) dispatches `notifications/cancelled` routinely
  (e.g. between tool calls); `McpServer::handleMethod` (`src/mcp/McpServer.cpp:77-80`)
  sets `cancelFlag_` on every such notification.
- Result: the flag is (re)set between my tool calls → every export attempt hits
  the refusal → permanent lockout. Confirmed live: 3 consecutive calls, all
  refused, each after the handler itself reset the flag.
- Note: the *mid-render* cancel path works correctly via the `onProgress`
  callback checking `isCancelRequested()` (`McpExportTool.cpp:80-84`) — that
  must be preserved.

### Fix

In `src/mcp/McpExportTool.cpp`, replace the refuse-on-entry check (lines 47-50)
with **consume-and-proceed**: reset the stale flag and start the export. A
cancel that arrives *during* the render is still honored by the existing
`onProgress` check. Optionally log/return a note instead of failing.

```cpp
// (replaces lines 47-50)
// Consume any stale cancel notification from the client. Cancels that
// arrive *during* the render are honored by the onProgress hook below;
// a cancel that landed before the export started must not permanently
// block new exports (the client re-sends notifications/cancelled
// routinely, so refusing here would lock out export_audio forever).
s.resetCancelFlag();
```

Keep everything else (dryRun path, `onComplete` flag reset at line 107, etc.).

### Test updates (required — current test asserts the buggy behavior)

- `tests/integration/mcp/mcp_server_test.cpp:434-460` —
  `TEST(McpServer, ExportAudioSkipsWhenCancelFlagSet)` currently arms the flag
  and asserts the tool refuses + writes nothing. **Rewrite** it to
  `ExportAudioConsumesStaleCancelFlag`: arm `s.setCancelFlag(true)`, call
  `export_audio`, assert the response is `export started`, the flag is cleared,
  and the WAV is actually written (`waitExportComplete` helper, like
  `ExportAudioRendersDefaultProject` at `:462-515`). This encodes the real
  contract: stale pre-call cancel ≠ refusal; mid-render cancel still aborts.
- Keep `ExportAudioCancelsMidRender` (`:517-549`) unchanged — it already proves
  the onProgress cancel path.

### Verification

- Build: `cmake --build build --config Debug`
- Tests: `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.*Export*`
- Manual: `hdaw_export_audio` via MCP → response `export started: <path>`,
  WAV exists and is non-empty (this was the original blocker).

---

## Dependency map / blast radius

- **Touched files:** `src/mcp/McpTools_Project.cpp`, `src/mcp/McpTools_Transport.cpp`,
  `src/mcp/McpExportTool.cpp`, `src/common/ReadModel.h`, `src/engine/ReadModelImpl.cpp`,
  `src/frontend/FrontendRpc.h`, `frontend/src/rpc/types.ts`,
  `frontend/src/store/transportStore.ts`, `frontend/src/store/transportExtrasStore.ts`,
  `frontend/src/components/TransportBar.tsx`, `tests/integration/mcp/mcp_server_test.cpp`,
  `frontend/src/store/transportStore.test.ts`.
- **Upstream of `setTempo`/`setTimeSignature`:** only `Router_Project.cpp:348,353`
  calls them today; adding MCP callers is additive (no signature change).
- **Downstream of `TransportSnapshot`:** `FrontendServer::onTransportTimer`
  (`src/frontend/FrontendServer.cpp:258-282`) broadcasts `toJson(transport)`
  with an equality short-circuit — new fields make it strictly more informative;
  the centisecond quantization (`:270-277`) still applies.
- **SPSC:** none — tempo/signature live on the ValueTree/ReadModel only.
- **Undo:** `setTempo`/`setTimeSignature` already use the undo manager; MCP
  callers get undo/redo for free.

## Pitfall gates

- **Gate 2 (unimplemented path):** both commands have full RPC→ValueTree→
  ReadModel→frontend-push paths; the MCP tools simply need to call the existing
  commands. Verify with the round-trip gtests.
- **Gate 4 (stale binaries):** after C++ changes, verify `build/Debug/HDAW.exe`
  / `hdaw_tests.exe` timestamps; never test against `build/Release/HDAW.exe`.
- **Gate 9 (validation):** MCP schemas constrain `bpm` 1-999, sig 1-32 each —
  mirrors the RPC layer's bounds expectations; `requireDouble`/`requireInt` are
  already in place on the router side.

## Anti-pattern alerts

- None expected. Do **not** add N separate `setProperty` calls — use the
  commands (single undo unit each).
- Do not reintroduce a refuse-on-stale-cancel path in `McpExportTool.cpp`
  (that *is* the bug).
