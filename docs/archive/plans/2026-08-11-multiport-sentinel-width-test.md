# Multi-port width sentinel test (`__multiport__`)

> For agentic workers: implement via subagent; this is the plan the subagent receives.
> Companion: handoff `docs/handoffs/2026-08-11-isolation-triage-multiport-test.md` item #4.

## Goal

Add a sentinel probe plugin with **two stereo output ports (4 channels)** — mirroring
NodalRed2x's bus layout — and a `PluginIsolation` gtest proving the child PREPAREs 4
channels, the shm header reports 4, the parent reports 4, and channel-2 data survives
the shm ring. Closes the gap where the real-plugin matrix
(`McpServer.DiagnosticClapExportMatrix`, ~85 s, needs the CLAPs installed) is the only
multi-port coverage.

## Success Gates (all must pass)

- [ ] Gate 1: `MultiPortProbeProcessor` exists in `src/proxy/host/PluginHost.cpp`
      (anonymous namespace, next to `TransportProbeProcessor`), with 2 stereo output
      buses ("Out AB", "Out CD"), 0 inputs; `processBlock` writes a distinctive payload
      struct to channel 0 AND a distinctive marker constant to channel 2; registered in
      `loadPluginByPath()` as `__multiport__`; `fillInPluginDescription` sets
      `d.fileOrIdentifier = "__multiport__"`.
- [ ] Gate 2: New test `PluginIsolation.MultiPortWidthHandoff` in
      `tests/integration/proxy/isolation_integration_test.cpp` passes:
      `slot.getReportedNumOutputChannels() == 4`, `hdr->pluginNumOutputChannels == 4`,
      channel-0 payload fields AND channel-2 marker read back through the shm ring.
- [ ] Gate 3: Test uses a fresh slot id (9160 — no collision with 9001-9151).
- [ ] Gate 4: Test would FAIL on pre-`738a65c` behavior — verified by `git show
      738a65c^:src/proxy/PluginProxySlot.cpp` (no header refresh → reports 2), NOT by
      reverting the tree.
- [ ] Gate 5: `cmake --build build --config Debug` succeeds;
      `hdaw_tests.exe --gtest_filter=PluginIsolation.MultiPortWidthHandoff` passes;
      `PluginIsolation.*` stays green (4 DISABLED_ + this one).
- [ ] Gate 6: no `sleep_for`-loosening; polls use bounded deadlines (existing pattern).

## Dependency Map

- **Blast radius:** `src/proxy/host/PluginHost.cpp` (new class + one registration
  string in `loadPluginByPath`) and the test file. Graph community: `PluginHost`.
  The sentinel is reachable ONLY via `spawnPluginHost("__multiport__", ...)` from
  tests — no production caller, no upstream/downstream impact.
- **Downstream:** none (test-only entry point).
- **Projections affected:** none (no ValueTree/RPC/frontend).
- **SPSC paths touched:** the shm ring (parent `PluginProxySlot::processBlock` →
  child `audioLoop`). The probe's `processBlock` runs on the CHILD audio thread:
  writes constants to the pre-allocated JUCE buffer only (mirror
  `TransportProbeProcessor` at PluginHost.cpp:381) — allocation- and lock-free.
- **God nodes in scope:** none (PluginHost is a leaf producer for sentinels).
- **Path integrity:** verified by reading both sides: child writes
  `hdr->pluginNumOutputChannels = plugin->getTotalNumOutputChannels()` in
  `loadPlugin()` (PluginHost.cpp:1458-1469); parent refreshes in
  `PluginProxySlot::prepareToPlay` (PluginProxySlot.cpp:60-79) and widens
  `numChannels`; PREPARE carries the width; child `audioLoop` resizes scratch to
  `preparedNumChannels` (PluginHost.cpp:1215-1223) and writes the ring
  channel-major (`outRing[ow + ch*blockSize + s]`, :1417-1421).

## Pitfall Gates Triggered

- **Gate 3 (audio-thread safety):** probe `processBlock` = buffer.clear() + memcpy of
  a fixed struct + `buffer.fill` of a constant. No allocation/lock/IO. Pass.
- **Gate 4 (stale binary):** edited files must have LastWriteTime bumped before build
  (MSBuild skips unchanged sources — lesson 15). Verify the built binary via the test
  run itself.
- **Gate 9 (id collision):** use slot 9160 (fresh; all of 9001-9151 are used).
- Not triggered: Gate 1/6 (no processor state / no rebuild path), Gate 2 (test-only),
  Gate 5 (no frontend), Gate 7 (no windows), Gate 8 (no CSS).

## Anti-Patterns

- No new RPC/command (test-only sentinel) — MCP parity rule N/A.
- No `sleep_for` loosening; no assertion weakening.
- No new `.cpp` file (class goes in existing PluginHost.cpp) — CMakeLists untouched.

## Steps

1. Add `MultiPortProbeProcessor` class in the anonymous namespace of
   `src/proxy/host/PluginHost.cpp` next to `TransportProbeProcessor` (after :436).
   - `BusesProperties().withOutput("Out AB", juce::AudioChannelSet::stereo(), true)
     .withOutput("Out CD", juce::AudioChannelSet::stereo(), true)` — NO input bus
     (instruments are 0-in/N-out in CLAP).
   - `struct MultiPortPayload { uint32_t magic; uint32_t width; };`
     `static_assert(sizeof(MultiPortPayload) <= 64)`.
   - `processBlock`: `buffer.clear()`; write `{magic, buffer.getNumChannels()}`
     via memcpy into channel 0 (guarded by `buffer.getNumSamples()`);
     `buffer.fill` channel 2 with a distinctive marker (e.g. `-1.25e3f`) — a
     2-channel-prepared child would never touch channel 2.
   - `fillInPluginDescription`: name "MultiPortProbe", format "Internal",
     `fileOrIdentifier = "__multiport__"`. `acceptsMidi()=false`,
     `hasEditor()=false`, empty state/getPrograms (mirror TransportProbeProcessor).
2. Register in `loadPluginByPath()` (after the `__throwprepare__` block, :1521-1525):
   `if (path == "__multiport__") { plugin = std::make_unique<MultiPortProbeProcessor>();
   pluginLoaded.store(true); return true; }`.
3. Add test `PluginIsolation.MultiPortWidthHandoff` to
   `tests/integration/proxy/isolation_integration_test.cpp` (model on
   `TransportClockHandoff`, :537), using slot 9160:
   - Spawn `__multiport__`; poll `mgr.isAlive` up to 100×10 ms.
   - **POLL `hdr->pluginNumOutputChannels == 4` BEFORE constructing/preparing the
     slot** (up to 100×50 ms) — the child writes the header in `loadPlugin()`, which
     runs AFTER READY; `prepareToPlay` reads it (race if not polled). This is the
     deterministic-wait pattern (no bare sleep).
   - Construct `PluginProxySlot slot(mgr, 9160, "TestPlugin")`; `slot.prepareToPlay(44100.0, 512)`.
   - `EXPECT_EQ(slot.getReportedNumOutputChannels(), 4)` (parent-side regression
     target; pre-738a65c reports 2).
   - `EXPECT_EQ(hdr->pluginNumOutputChannels, 4u)` (header contract).
   - Push a block: `juce::AudioBuffer<float>(4, 512)` cleared, `slot.processBlock(...)`
     (parent writes 4×512 samples channel-major).
   - Poll the output ring: `totalSamples = 512 * 4` — do NOT use `hdr->numChannels`
     (the child only sets it at audioLoop start; it stays 2; `hdr->blockSize` IS
     synced by the resize guard). Wait `outputWritePos - outputReadPos >= totalSamples`
     up to 200×10 ms, then `shm->readOutput(output.data(), totalSamples)`.
   - Assert: channel 0 (offset 0) decodes `MultiPortPayload{magic, width=4}`; channel 2
     marker at `output[2 * 512]` equals the marker constant (channel-major layout).
   - `mgr.killPluginHost(9160, KillMode::KillHard)`.
4. Gate-3 evidence: `git show 738a65c^:src/proxy/PluginProxySlot.cpp` — confirm the
   header refresh (`pluginNumOutputChannels`) is absent there (pre-fix reports 2),
   and `git show 738a65c^:src/proxy/ProxyCommon.h` — confirm `ShmHeader` lacks the
   field. Report the excerpts in the final message.
5. Build + verify:
   - Kill orphaned build processes first (taskkill cl/MSBuild/link/mspdbsrv).
   - Bump LastWriteTime of the two edited files.
   - `cmake --build build --config Debug` (timeout ≥ 1200000 ms).
   - `hdaw_tests.exe --gtest_filter=PluginIsolation.MultiPortWidthHandoff`
   - `hdaw_tests.exe --gtest_filter=PluginIsolation.*` (all green: DISABLED_ skipped,
     others pass — do this AFTER the triage task has renamed the 4).
   - `hdaw_tests.exe --gtest_filter=PluginIsolation.TransportClockHandoff` (no
     regression on the pattern the test was modeled on).

## Verification commands

```powershell
taskkill /IM cl.exe /F 2>$null; taskkill /IM MSBuild.exe /F 2>$null
(Get-Item src\proxy\host\PluginHost.cpp).LastWriteTime = Get-Date
(Get-Item tests\integration\proxy\isolation_integration_test.cpp).LastWriteTime = Get-Date
cmake --build build --config Debug        # big timeout
& .\build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.MultiPortWidthHandoff
& .\build\Debug\hdaw_tests.exe --gtest_filter=PluginIsolation.*
```
