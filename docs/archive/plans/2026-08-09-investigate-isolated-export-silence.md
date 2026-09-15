# Investigate: isolated-export silence for 5 CLAP instruments

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **This is a discovery-first plan** — Task 1 (root-cause) must complete before any production change. The fix depends on what Task 1 finds.

**Goal:** Root-cause why 5 of 10 tested CLAP instruments render **silence (peak=0)** on isolated export while the other 5 render correctly, then fix it and remove them from the `kKnownSilent` skip-set in `DiagnosticClapExportMatrix` so the test fully asserts every present plugin.

**Tech Stack:** C++17, JUCE 8, CLAP SDK, plugin process-isolation (`hdaw_plugin_host.exe`), GTest

---

## Symptom

`McpServer.DiagnosticClapExportMatrix` (re-enabled in the export-path-lifetime fix) reports:

| Renders correctly (peak) | Renders SILENT (peak=0) |
|---|---|
| Vital (0.58), Dexed (0.16), JC303 (0.26), Identity (0.35), Altitude (0.34) | **Odin2, ShinRonin, Gneiss, Retrospect, NodalRed2x** |

The 5 silent ones are currently in `kKnownSilent[]` with a `SKIP-SILENT` log + TODO. The assertion is **not** weakened for the working ones. The split is plugin-specific (not a routing failure — that would silence all 10), which points at state/lifecycle, not signal path.

---

## Hypotheses (ranked; Task 1 confirms/refutes)

- **H1 (prime): PREPARE reinitializes the plugin and discards state set moments earlier.** In `Track.cpp:182-197`, `setStateInformation` (`:192`) is called BEFORE `slot->prepare()` (`:197`), and the graph's `prepareToPlay` later sends PREPARE again. If the isolated child applies SET_STATE then a PREPARE resets the plugin to its default (silent) patch, plugins that **need** state to produce sound go silent while plugins with a sound-producing default keep working — exactly the 5/5 split. Check the child's `PluginHost.cpp` PREPARE handler and whether CLAP `activate`/`deactivate` re-init discards state.
- **H2: The test project carries no `pluginState`, so this is "default-patch silence", not a round-trip defect.** `DiagnosticClapExportMatrix` does `add_track_with_fx` + `generate_phrase`; if `add_track_with_fx` saves no `IDs::pluginState`, then NO state is restored for any plugin and the 5 silent ones are simply instruments whose factory default is silent until initialized/programmed. (If true, the "fix" is to seed a known-good state/program in the test, or to set a default program — different from H1.)
- **H3: CLAP lifecycle gap in the isolated host.** Some CLAP instruments require `clap_plugin->activate()` then `start_processing()` in a specific order, or require `on_main_thread` callbacks (state load) that the headless child never services. Compare with the in-process `CLAPPluginInstance` path that works.
- **H4: Chunked state truncation (lesson 14 class).** `PluginProxySlot::setStateInformation` (`:576-609`) chunks state over `SET_STATE` + `STATE_CHUNK`; if a large state loses a chunk or the child reassembles incorrectly (`PluginHost.cpp:614-645`), the plugin loads a truncated/corrupt patch → silence. The 5 silent plugins may have larger/non-trivial state.
- **H5: MIDI not reaching the child for these plugins.** Unlikely (5 work) but cheap to rule out — log MIDI bytes the child receives.

---

## Dependency Map

- **Blast radius:** isolated-plugin state/lifecycle path — affects live crash-recovery reload AND export. A fix here may also improve live robustness.
- **The path under investigation:**
  - `Track.cpp:182-197` — state restore + prepare ordering (H1 entry point).
  - `PluginProxySlot.cpp:542-609` — `getStateInformation` / `setStateInformation` + chunked `STATE_CHUNK` (H4).
  - `src/proxy/host/PluginHost.cpp` — child-side `SET_STATE`/`STATE_CHUNK`/`PREPARE` handling + CLAP lifecycle (H1, H3).
  - `CLAPPluginInstance.cpp:802-830` — the in-process CLAP get/setState (reference for what the isolated path should mirror).
- **Upstream:** the test (`mcp_server_test.cpp` `DiagnosticClapExportMatrix`) / project save (`FrontendRouter.cpp:980-1027` writes `pluginState`/`pluginStateB`).
- **Downstream:** `DiagnosticClapExportMatrix` assertion + `kKnownSilent` set.
- **Projections affected:** audio graph (isolated child output). No ValueTree/ReadModel/frontend change expected.

---

## Tasks

### Task 1 — Root-cause (gate for all further work)
Pick **Odin2** (silent) and **Vital** (working) as the comparison pair.
- [ ] Add temporary diagnostic logging (child + parent) for ONE export run of each:
  - size of `pluginState` restored from the ValueTree (is it empty? H2);
  - SET_STATE / STATE_CHUNK message count + total bytes sent to the child (H4);
  - child-side: state bytes reassembled, PREPARE order vs SET_STATE, CLAP `activate`/`start_processing` calls + return values (H1, H3);
  - MIDI bytes received by the child + number of processBlock calls that produced non-zero output (H5).
- [ ] Diff the two logs. Identify the FIRST divergence that explains silence.
- [ ] Record the confirmed root cause + which hypothesis it was. Remove the temp logging.

### Task 2 — Fix (depends on Task 1)
- [ ] Implement the fix indicated by Task 1. Most likely candidates:
  - **If H1:** reorder so SET_STATE lands AFTER PREPARE (or make the child retain state across PREPARE / re-apply after activate). Verify live playback still restores state (lesson 10 — state preserved across the rebuild/prepare path).
  - **If H2:** seed a known-good default program/state for instruments under test (or in `add_track_with_fx`), so silence means a real defect, not a missing init.
  - **If H3:** complete the CLAP lifecycle in the isolated host to mirror `CLAPPluginInstance`.
  - **If H4:** fix the chunk reassembly / bounds (lesson 14).
- [ ] Do NOT weaken `DiagnosticClapExportMatrix`; remove the fixed plugins from `kKnownSilent`.

### Task 3 — Verify all 5
- [ ] Re-run `DiagnosticClapExportMatrix`; confirm the previously-silent plugins now render non-silent and are asserted (removed from `kKnownSilent`). If any remain silent, keep only those in the set with an updated TODO.

---

## Pitfall Gates

| Gate | Why | Mitigation |
|------|-----|------------|
| **Lesson 10 (state restored on rebuild)** | Reordering SET_STATE/PREPARE risks dropping state on live rebuild | Test: set state, trigger a rebuild, assert the live processor still holds it (live-path regression) |
| **Lesson 14 (proxy boundary)** | State transfer is exactly the cross-process path that truncated before | Bounds-check chunk sizes on both sides; assert full state round-trip size equality |
| **Lesson 13 (DSP-state writes)** | Any new state-apply path touches DSP objects | Guard with the established `stateLock` / apply on the message thread, not the audio thread |
| **Gate 2 (unimplemented path)** | A "fix" that silently no-ops | DiagMatrix `EXPECT_GT(peak, 0.01)` is the proof |

## Anti-Pattern Scan
- No `rebuildRoutingGraph()` added.
- No audio-thread allocation/locking in any new state-apply path.
- Don't loosen assertions to make the test green — fix the silence.

---

## Success Gates (all must pass to declare done)

- [ ] Task 1 root cause recorded (which hypothesis, with the log evidence).
- [ ] The previously-silent plugins render non-silent on isolated export and are asserted in `DiagnosticClapExportMatrix` (removed from `kKnownSilent`, or only genuinely-unfixable ones remain with a TODO).
- [ ] Live playback state-restore still works (no lesson-10 regression) — existing `track_mixer_state`/FX-state tests + a manual live check.
- [ ] Full `hdaw_tests.exe` green (only the known `PluginIsolation.*` baseline may fail).
- [ ] If the root cause is a durable, general pitfall (likely), add a numbered lesson to AGENTS.md (candidate lesson 16) pointing at this plan.

## Verification commands
```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix
build\Debug\hdaw_tests.exe
```

## Note
If Task 1 shows the silence is a genuine per-plugin bug (e.g., the plugin itself misbehaves headless) rather than an HDAW defect, document that per-plugin in `kKnownSilent` with evidence and close the plan — don't chase a non-defect.
