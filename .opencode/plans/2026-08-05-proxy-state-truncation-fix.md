# Fix: "preset is corrupted" when adding VST FX — proxy state truncation

Date: 2026-08-05
Status: planned

## Symptom

Adding VST effects to the FX chain produces one or more plugin-side dialogs:
"There was an error opening the preset. The preset is corrupted." (Serum-style
alert). Happens repeatedly — once per affected plugin per rebuild.

## Root cause (verified in code)

Plugin isolation (default ON) runs plugins in a child `PluginHost.exe`. Plugin
state crosses the process boundary via named-pipe messages with a FIXED 244-byte
payload (`ProxyCommon.h` — `ProxyMessage::data[244]`, `ProxyResponse::data[244]`).

Both directions silently truncate:
- `PluginProxySlot::setStateInformation` clamps `copySize` to `sizeof(msg.data)`
  (src/proxy/PluginProxySlot.cpp:240-242).
- `PluginProxySlot::getStateInformation` clamps to `sizeof(resp.data)`
  (src/proxy/PluginProxySlot.cpp:226).
- Child `PluginHost` GET_STATE handler clamps likewise (src/proxy/host/PluginHost.cpp:293-294).

VST3 plugin state is typically KB–MB. Trigger sequence:

1. User adds plugin A → fresh slot, no state → OK.
2. User adds plugin B → `addFxSlot` → `rebuildTrackFX` → `Track::rebuildFXChain`:
   - Save-back loop (Track.cpp:83-104) calls `getStateInformation` on existing
     isolated slot A → returns only first 244 bytes → written base64 into
     `FX_SLOT.pluginState` (Track.cpp:99). This also corrupts project saves.
   - All slots destroyed + recreated; A receives its truncated state via
     `setStateInformation` (Track.cpp:166-173) → plugin rejects → dialog.
3. Each further add/bypass/reorder/routing-rebuild repeats it for every existing
   isolated plugin → "lots of" dialogs.

Crash recovery is affected too: `saveStateToTemp`/`respawnIsolatedSlot`
(PluginManager.cpp:697-699) route through the same truncated path.

### Secondary bugs in the same path (fixed together)

- **B: save-back matches by pluginID, first match wins** (Track.cpp:94-102).
  Two instances of the same plugin collide: both write to the first matching
  tree child; the second child keeps a stale `pluginState`.
- **C: `setFxSlotPlugin` (plugin swap) does not clear `pluginState` /
  `pluginStateB`** (AudioEngineCommands_Fx.cpp:276-291). The swapped-in plugin
  receives the previous plugin's state → same corruption dialog.

## Goal

Plugin state of any size round-trips the isolation proxy byte-exact, and the FX
rebuild save/restore path never feeds a plugin stale or partial state.

## Success Gates (all must pass)

- [ ] G1: `cmake --build build --config Debug` succeeds (HDAW.exe AND
      PluginHost.exe rebuilt — both ship together, protocol change is safe).
- [ ] G2: New integration test round-trips a >244-byte patterned state
      (≥100 KB) through a live `__stateecho__` child via
      `PluginProxySlot::set/getStateInformation` and asserts byte-exact equality.
- [ ] G3: New integration test round-trips a small (≤244-byte) state byte-exact
      (non-chunked path preserved).
- [ ] G4: Full `build/Debug/hdaw_tests.exe` passes (no regression in the
      existing PluginIsolation / track-FX / plugin-state suites).
- [ ] G5: grep confirms no remaining 244-byte clamp in the state paths.
- [ ] G6: Two same-plugin instances in one chain get distinct states after
      rebuild (consumed-match) — covered by code inspection + existing suite;
      note as manual-verify item for real-plugin session.
- [ ] G7: `setFxSlotPlugin` removes `pluginState`/`pluginStateB` when the
      pluginID changes (unit-level grep/test).

## Dependency Map

- Blast radius: proxy IPC protocol (parent `PluginProxySlot` + child
  `PluginHost`), Track FX rebuild save/restore, setFxSlotPlugin command.
- Upstream callers of `setStateInformation`: Track::rebuildFXChain (restore),
  FrontendRouter A/B (lines ~1014/1027), PluginManager::respawnIsolatedSlot,
  PluginProxySlot::restoreStateFromTemp (currently uncalled).
- Upstream callers of `getStateInformation`: Track::rebuildFXChain save-back,
  PluginProxySlot::saveStateToTemp (5 s timer + crash), FrontendRouter A/B.
- Downstream: project save (`pluginState` base64 in FX_SLOT), crash recovery
  temp files.
- Projections affected: audio graph projection only (live processors). No
  ReadModel/frontend snapshot shape change → no delta/fullSync impact.
- SPSC/audio thread: NOT touched. `processBlock` unchanged. State transfer runs
  on message thread (parent) / control thread (child). Latency impact: none
  (lesson 7). Fidelity: strictly improved — bytes preserved exactly (lesson 8).
- God nodes: none modified (Track::rebuildFXChain is hot but change is local to
  the save-back matching loop).

## Pitfall Gates Triggered

- **Gate 1 (state restore on rebuild):** This IS the rebuild restore path.
  Tests assert on the live proxy round-trip, not the ReadModel.
- **Gate 3 (audio-thread safety):** No allocation/locks added to any audio path.
  Child-side accumulation buffer lives on the control thread only.
- **Gate 4 (stale binaries):** PluginHost.exe must be rebuilt alongside
  HDAW.exe — verify timestamps; protocol change requires both.
- **Gate 9 (validation):** Chunked receive guards against runaway totals
  (sanity cap) and wrong-type messages (abort, no deadlock).

## Anti-pattern scan

- No N-RPC loops, no full-tree walks added, no CSS, no DBG (use HDAW_LOG if
  logging needed).

## Design

### Protocol: chunked state transfer (append-only message type)

Add `MessageType::STATE_CHUNK` (append after HEARTBEAT — parent and child are
built/versioned together, no compat concern).

**SET_STATE (parent → child):**
- Initial `SET_STATE` msg: `dataSize` = TOTAL state size; `data` = first
  min(244, total) bytes.
- If total > 244: parent sends `STATE_CHUNK` msgs, each `dataSize` = chunk
  bytes (≤244), until all bytes sent.
- Child accumulates (members `pendingStateTotal`, `pendingState`); when
  accumulated == total → ONE `plugin->setStateInformation(...)` call → ONE
  `SET_STATE` response (result=1).
- Any non-STATE_CHUNK message arriving mid-transfer: discard pending, handle
  normally.

**GET_STATE (parent ← child):**
- Child builds full `MemoryBlock`; sends `GET_STATE_RESULT` with result=1,
  `dataSize` = TOTAL size, first chunk in `data`; then `STATE_CHUNK` responses
  for the remainder.
- Parent receives until it has `total` bytes; sanity cap 512 MB; abort on
  unexpected message type.

### Files

Task A — proxy protocol (subagent 1):
- `src/proxy/ProxyCommon.h` — add STATE_CHUNK.
- `src/proxy/host/PluginHost.h` — pending-accumulation members.
- `src/proxy/host/PluginHost.cpp` — SET_STATE accumulation; GET_STATE chunked
  send; new `StateEchoProcessor` test plugin (`__stateecho__`: stores set state,
  returns it verbatim from getStateInformation) wired into loadPluginByPath.
- `src/proxy/PluginProxySlot.cpp` — chunked setStateInformation (send all
  chunks, then wait for the single response); chunked getStateInformation.
- `tests/integration/proxy/isolation_integration_test.cpp` — G2/G3 tests:
  spawn `__stateecho__`, construct PluginProxySlot over the live pipe,
  round-trip 100 KB patterned buffer + small buffer, byte-exact compare.

Task B — engine save/restore correctness (subagent 2, no shared files):
- `src/engine/Track.cpp` rebuildFXChain save-back: consumed-match (track which
  tree children already matched via std::vector<char>) so duplicate pluginIDs
  don't collide; only write `pluginState` when state.getSize() > 0 (don't clobber
  last-good state when the child is dead and returns nothing).
- `src/engine/AudioEngineCommands_Fx.cpp` setFxSlotPlugin: when pluginID
  changes, `removeProperty(IDs::pluginState)` and
  `removeProperty(juce::Identifier("pluginStateB"))` so a swapped-in plugin
  never receives the previous plugin's state.

### Verification commands

```
cmake --build build --config Debug
build/Debug/hdaw_tests.exe --gtest_filter=PluginIsolation.*
build/Debug/hdaw_tests.exe            (full suite)
```

## Follow-ups (out of scope, noted)

- Stable per-slot identity (FX_SLOT id property) would make rebuild save-back
  exact across reorder/duplicate; current consumed-match by pluginID is best
  effort and strictly better than first-match.
- `PluginProxySlot::restoreStateFromTemp()` is dead code (no callers) — remove
  or wire into respawn in a future pass.
