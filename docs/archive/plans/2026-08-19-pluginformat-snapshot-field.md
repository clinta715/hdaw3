# Plan — expose `pluginFormat` in the FX-slot snapshot (handoff item #4)

Date: 2026-08-19
Status: COMPLETE (2026-08-19) — all gates passed; 929 tests, 926 passed / 3 pre-existing
env-guarded skips / 0 failures; frontend build + 349 Vitest green. Uncommitted.

## Goal

Expose the persisted `pluginFormat` (VST3/CLAP/…) on `FxSlotSnapshot` so
`read.getFxSlots` RPC and the MCP `list_fx` tool report it — tooling no longer
needs to read the project XML to assert VST3-vs-CLAP.

## Success Gates (all must pass)

- [ ] G1: `FxSlotSnapshot` (src/common/ReadModel.h) carries `std::string pluginFormat`;
      `ReadModelImpl::getFxSlots` populates it from `IDs::pluginFormat`.
- [ ] G2: RPC wire JSON includes `pluginFormat` (`toJson(const FxSlotSnapshot&)`,
      src/frontend/FrontendRpc.h).
- [ ] G3: MCP `list_fx` includes `pluginFormat` for plugin slots (parity rule).
- [ ] G4: Frontend TS `FxSlotSnapshot` interface includes `pluginFormat: string`
      (frontend/src/rpc/types.ts); `npm run build` passes.
- [ ] G5: gtest — `Commands.SetFxSlotPlugin` asserts `pluginFormat == "VST3"`;
      new `FrontendServer.FxSlotPluginFormatExposed` RPC round-trip asserts the
      wire field; `GuiFuncTest.AddFxAndRemove`-adjacent MCP assertion covers
      `list_fx` pluginFormat presence (plugin slot via `set_fx_slot_plugin`-equivalent
      path, or assert empty-string absence for internal FX — see steps).
- [ ] G6: `cmake --build build --config Debug` succeeds; targeted filters pass,
      then FULL `hdaw_tests.exe` suite green (baseline 928/928 + new tests).
- [ ] G7: Debug binary freshness verified (timestamp) before running tests (Gate 4/15).

## Dependency Map (verified via grep + graph)

- Property source: `IDs::pluginFormat` (DECLARE_ID, ProjectModel.h:196). Writers:
  `addFxSlotInternal` (AudioEngineCommands_Fx.cpp:90), int-type `addFxSlot` path
  (:47), `setFxSlotPlugin` (:319), ProjectModel.cpp:428, Track.cpp:745.
- Consumers of `getFxSlots`: Router_Read.cpp:41 (`read.getFxSlots`),
  McpTools_Audio.cpp (`list_fx` + param tools, read-only), Router_Sampler.cpp:35,
  multiple tests. Adding a field is additive — no consumer breaks.
- Projections: ReadModel only. No audio graph, no SPSC, no ValueTree mutation,
  no delta/fullSync impact (read-only snapshot field).
- God nodes touched: none. Community boundaries crossed: none new
  (same engine→frontend seam already used by every other snapshot field).

## Pitfall Gates Triggered

- Gate 2 (silent no-op path): full chain verified — tree property → getFxSlots →
  toJson → RPC/MCP. Round-trip gtest is the proof.
- Gate 4/15 (stale binary): rebuild Debug and check timestamp before test run.
- Gates 1/3/6/10–16: NOT triggered (no processor state, no audio thread,
  no lifecycle, no rebuild path).
- Anti-pattern scan: none applicable (no RPC loops, no CSS, no new .cpp files).

## Steps

1. `src/common/ReadModel.h` — add `std::string pluginFormat;` to `FxSlotSnapshot`
   (after `pluginName`, line ~116).
2. `src/engine/ReadModelImpl.cpp` `getFxSlots` (~line 424): add
   `s.pluginFormat = slot.getProperty(IDs::pluginFormat, "").toString().toStdString();`
3. `src/frontend/FrontendRpc.h` `toJson(const FxSlotSnapshot&)` (~line 181): add
   `{ "pluginFormat", QString::fromStdString(f.pluginFormat) },`
4. `src/mcp/McpTools_Audio.cpp` `list_fx` lambda (~line 52, inside `isPlugin`
   branch): add `o["pluginFormat"] = QString::fromStdString(s2.pluginFormat);`
5. `frontend/src/rpc/types.ts` `FxSlotSnapshot` (~line 138): add
   `pluginFormat: string;`
6. Tests:
   - `tests/unit/common/commands_test.cpp` `Commands.SetFxSlotPlugin` (~line 617):
     add `EXPECT_EQ(fxSlots[0].pluginFormat, "VST3");`
   - `tests/unit/frontend/frontend_server_test.cpp`: new
     `TEST(FrontendServer, FxSlotPluginFormatExposed)` — add an eq slot via
     `project.addFxSlot`, then `project.setFxSlotPlugin` with
     `{trackIndex:0, slotIndex:<idx>, fxType:"plugin", pluginID:"test.plugin",
     pluginFormat:"VST3", pluginPath:"/path/test.vst3}`, then `read.getFxSlots`
     and assert the slot object has `pluginFormat == "VST3"`. (No real plugin
     needed — setFxSlotPlugin only writes tree properties; matches existing
     `Commands.SetFxSlotPlugin` pattern.)
   - `tests/integration/mcp/mcp_functionality_test.cpp`: extend
     `GuiFuncTest.AddFxAndRemove` (or add a sibling test) — for an internal-FX
     slot `list_fx` reports `type=="eq"`; if a plugin slot is created via the
     MCP `set_fx_slot_plugin`-equivalent tool path, assert `pluginFormat`.
     Keep it real-plugin-free: if no MCP tool can mint a fake plugin slot
     cheaply, assert the field is present-and-empty for internal slots is NOT
     required (list_fx only emits pluginFormat for plugin slots) — instead add
     the assertion wherever an existing test already creates a plugin slot
     without a real plugin, else cover MCP parity via the commands-level test
     and note it. Do NOT spawn real plugins in this test.
7. Build: `cmake --build build --config Debug`. Verify binary timestamp.
8. Run: targeted filters first
   (`--gtest_filter=Commands.SetFxSlotPlugin:FrontendServer.FxSlotPluginFormatExposed:GuiFuncTest.AddFxAndRemove`),
   then the FULL suite. Frontend: `cd frontend && npm run build` and `npm test`.

## Out of scope

- No changes to how `pluginFormat` is resolved/written (already correct).
- No CLAP wiring (handoff item #5, separate).
- No frontend UI consumption of the field (types only).
