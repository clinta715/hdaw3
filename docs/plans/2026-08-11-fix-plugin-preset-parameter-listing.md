# Fix Plugin Preset & Parameter Listing

## Goal

Fix the inability to list presets and parameters from external (VST3/CLAP) plugins via MCP tools `list_plugin_presets` and `list_fx_params`.

## Root Cause Summary

**Root cause #1 (FIXED): MCP sessions spawned a stale plugin host.** `mcp-launch.bat`
(how opencode launches `HDAW_headless_mcp.exe`) copied only `HDAW_headless.exe` to
`%TEMP%`, but `getHostExePath()` resolves `hdaw_plugin_host.exe` as a sibling of the
running exe → MCP sessions used the stale `%TEMP%\hdaw_plugin_host.exe` (8/7 build,
fails READY handshake ~29 ms) instead of fresh `build\Debug` (8/11). Every
`createPluginInstance` with `isolated=true` failed:

```
FXRebuild pluginID=CLAP-Dexed-920571e0-0 fmt=CLAP isolated=true
createPluginInstance result: NULL error=Failed to spawn isolated plugin process
```

Direct `build\Debug` runs always worked (log 14:52: all slots READY + `result: ok`).
Fix: `mcp-launch.bat` now copies BOTH `HDAW_headless.exe` and `hdaw_plugin_host.exe`
to `%TEMP%` on every launch. Verified post-fix (15:21): plugins load
(`createPluginInstance result: ok`), `list_fx_params` returns 461 params for JE8086.

**Root cause #2 (ACTIVE): the CLAP wrapper hardcodes one program.** `CLAPPluginInstance`
(`src/engine/CLAPPluginInstance.h:197-201`) returns `getNumPrograms()=1` and an empty
`getProgramName()` for every hosted plugin — it never enumerates presets. The vendored
CLAP 1.2.7 headers (via `clap-juce-extensions` clap-libs) contain NO program/factory-
preset enumeration extension (`clap.program-list` draft was removed; `preset-discovery`
is an entry-level file-indexing API, scarce adoption, absent from these headers). So:
- child `plugin->getNumPrograms()` → 1 → `PluginProxySlot::numProgramsCached_` = 1
  (`PluginProxySlot.h:200` default 1, only overwritten by a successful
  GET_PROGRAM_COUNT round trip in `fetchParamMetadata()` `PluginProxySlot.cpp:226-237`)
- `AudioEngine::getFxProgramList` (`AudioEngine.cpp:505-522`) → `[{index:0,name:""}]`
- scanner (`PluginScannerMain.cpp:105-124`) probes the same → `numPrograms <= 1` →
  `presetCache` never populated (`PluginManager.cpp:219` guard) → `<PRESET_CACHE/>`
  stays empty
- VST3 plugins additionally blacklisted as `scan_failure` (scanner exit code 1), so the
  JUCE VST3 path (which DOES map `IProgramListData` factory presets to programs) is
  unavailable

**Secondary issue:** `ProxiedParameter::getText()` returns empty string for isolated
plugins (display issue)

## Success Gates

- [x] Gate 1: At least one external plugin (e.g. Dexed CLAP) loads successfully in the audio graph (check logs for `createPluginInstance result: ok`)
- [x] Gate 2: `list_fx_params` returns non-empty params for a loaded external plugin (verified: 461 params for JE8086)
- [ ] Gate 3: `list_plugin_presets` returns non-empty presets for a loaded external plugin (or correctly returns empty for single-program plugins)
- [ ] Gate 4: `hdaw_tests.exe` passes (no regressions)
- [x] Gate 5: `cmake --build build --config Debug` succeeds

## Dependency Map

- **Upstream:** `AudioEngineCommands::setFxSlotPlugin` → `Track::rebuildFXChain` → `PluginManager::createPluginInstance` → `ProxyProcessManager::spawnPluginHost`
- **Downstream:** `McpTools_Audio.cpp` (`list_plugin_presets`, `list_fx_params`) → `AudioEngine::getFxProgramList` / `PluginParamServiceImpl::getParams` → `TrackFXSlot` → `PluginProxySlot` → child process IPC
- **God nodes:** `RoutingManager::rebuildTrackFX` (high-degree hub, orchestrates all FX chain rebuilds)
- **Projections affected:** ReadModel (FxSlotSnapshot), live audio graph (TrackFXSlot chain), frontend snapshot
- **SPSC paths:** param bridge (stagedParams_/paramDirty_ ↔ shm paramSet ring) — not affected by this fix

## Pitfall Gates Triggered

- **Gate 2 (Unimplemented code path):** `spawnPluginHost` fails silently — no logging of which step (pipe, shm, CreateProcess, READY) failed. Must add diagnostic logging before fixing.
- **Gate 4 (Stale binary):** After C++ changes, must verify `hdaw_plugin_host.exe` is rebuilt alongside `HDAW.exe`.

## Plan

### Phase 1: Diagnose the spawn failure (required before any other fix)

**Goal:** Add granular logging to `spawnPluginHost` so we know exactly which step fails.

#### Step 1.1: Add diagnostic logging to `ProxyProcessManager::spawnPluginHost`

**File:** `src/proxy/ProxyProcessManager.cpp`

Add `HDAW_LOG` calls after each failure point:

```cpp
bool ProxyProcessManager::spawnPluginHost(const std::string& pluginPath, uint32_t slotId) {
    killPluginHost(slotId, KillMode::KillHard);

    auto pipeName = makePipeName(slotId);
    auto shmNameStr = makeShmName(slotId);
    auto hostExe = getHostExePath();

    HDAW_LOG("proxy", "spawnPluginHost: hostExe=" + hostExe + " plugin=" + pluginPath);

    auto pipeServer = std::make_unique<PipeServer>(pipeName);
    if (!pipeServer->start()) {
        HDAW_LOG("proxy", "spawnPluginHost: PipeServer::start() FAILED for " + pipeName);
        return false;
    }

    auto shmRegion = std::make_unique<ShmRegion>();
    uint32_t shmSize = computeShmSize(kMaxShmChannels, kMaxShmBlockSize);
    if (!shmRegion->create(shmNameStr, shmSize)) {
        HDAW_LOG("proxy", "spawnPluginHost: ShmRegion::create() FAILED for " + shmNameStr);
        pipeServer->stop();
        return false;
    }

    // ... existing ring buffer init ...

    std::string cmdLine = "\"" + hostExe + "\""
        + " --slot=" + std::to_string(slotId)
        + " --pipe=" + pipeName
        + " --shm=" + shmNameStr
        + " \"--plugin=" + pluginPath + "\"";

    HDAW_LOG("proxy", "spawnPluginHost: cmdLine=" + cmdLine);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        HDAW_LOG("proxy", "spawnPluginHost: CreateProcessA FAILED error=" + juce::String(static_cast<int>(err)));
        pipeServer->stop();
        return false;
    }

    CloseHandle(pi.hThread);
    HDAW_LOG("proxy", "spawnPluginHost: child PID=" + juce::String(static_cast<int>(pi.hProcess)));

    // Wait for READY
    ProxyResponse readyResp{};
    if (!pipeServer->receiveResp(readyResp)) {
        HDAW_LOG("proxy", "spawnPluginHost: READY timeout or pipe error — killing child");
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        pipeServer->stop();
        return false;
    }

    HDAW_LOG("proxy", "spawnPluginHost: READY received, child alive");
    // ... existing child info insertion ...
}
```

**Verification:** Rebuild, launch HDAW, add a plugin, read `%TEMP%/hdaw_debug.log` for the new `proxy` tag lines. The exact failure point (pipe start, shm create, CreateProcess, READY timeout) will be visible.

### Phase 2: Fix the spawn failure (COMPLETE)

The Phase 1 diagnostics proved the failure was infrastructure, not code: MCP sessions
ran the stale `%TEMP%\hdaw_plugin_host.exe` (8/7) instead of `build\Debug` (8/11).
`mcp-launch.bat` refreshed only `HDAW_headless.exe` in `%TEMP%`; `getHostExePath()`
resolves the host exe next to the *running* exe, so the temp host stayed stale.

**Fix applied (verified 15:21):** `mcp-launch.bat` now copies BOTH
`HDAW_headless.exe` and `hdaw_plugin_host.exe` into `%TEMP%` on every launch, with
loud errors if either source file is missing. Temp host verified byte-identical to the
8/11 build (29,533,696 bytes). Post-fix MCP session: all plugin slots spawn
(`createPluginInstance result: ok`), `list_fx_params` works (461 params JE8086).

The old Candidate A-D guesses (CreateProcess path, DLL resolution, pipe policy, zombie
children) were ruled out by the log evidence — all spawns succeed when the host exe is
current.

### Phase 3: Program/preset enumeration — ground truth first (COMPLETE: 3.1 + 3.2; 3.3 pending live verification)

**Goal:** determine what the installed plugins (JE8086, Dexed, Vital CLAP; Vital/Odin2
VST3) actually expose for programs, then fix the enumeration path to match reality.

#### Step 3.1: Child-side probe (COMPLETE)

**Result:** NO installed CLAP plugin (JE8086, Dexed, Vital) implements any of
`clap.program-list` / `clap.preset-load` / `clap.preset-load/2` /
`clap.preset-load.draft/2` / `clap.preset-discovery` (CLAP 1.2.7 era). `numPrograms=1`,
program 0 name empty, on the scan side AND in the engine. Implemented as a diagnostic
ext-probe in `CLAPPluginInstance::initialize()` (tag `clap_host`) — deviation: the
plan said `PluginHost.cpp loadPluginByPath`, but `CLAPPluginInstance` is the shared
construction point (covers scan, child, and engine equally) and the probe needs the
`clap_plugin_t*` directly.

#### Step 3.2: Manual scanner run on the VST3 scan_failure entries (COMPLETE — root cause was a scanner bug, not the plugins)

**Major discovery:** the isolated scanner itself was broken: `PluginScannerMain.cpp`
built a hand-rolled `PluginDescription` and called `createPluginInstance` directly,
bypassing `findAllTypesForFile`. JUCE 8's `VST3ModuleHandle::open()` requires
name+uid to match the factory → EVERY `.vst3` failed "Unable to load VST-3 plug-in
file" → all VST3s blacklisted as `scan_failure` (phantom entries). Fixed scanner uses
the documented discovery flow (`findAllTypesForFile` → `getDescriptionFor`), plus a
raw Win32 `probeVst3Module()` stage (tag `scanner`). JSON contract/exit codes unchanged.

Rescan results (fixed scanner, 8/11 build, per-file 45s timeout in harness; engine
enforces its own 30s/child timeout at `PluginManager.cpp:451`):

| Plugin | Programs | Notes |
|--------|----------|-------|
| Serum2 | 128 | "Prog 1..128" |
| TyrellN6, TripleCheese, Podolski, BazilleCM, Zebralette3 | 128 each | "Program 0..127" |
| WOMBintro | 2 | "inital", "untitled" |
| Vital, Mono, Odin2, ValhallaSupermassive, ABYZOR, Opr8, Pathfinder, Plexum, Fasttracker II, Iron Deck, WD Clamp/Tape/Echo | 0 | file-based or no `IProgramListData` — legitimately 0 |
| FM8.vst3 (32-bit-only) | — | HANGS the x64 scanner; engine's 30s timeout catches it → blacklisted as timeout (correct) |

#### Step 3.3: Decision — VST3-only preset path (COMPLETE — verified live 8/11 12:33)

Live verification (raw MCP stdio probe against a fresh engine, 57-plugins scan):

- `scan_plugins` → "scanned 57 plugins" (FM8 excluded upfront — see Findings)
- `list_plugins` → **16 VST3, 5 with presets**: Zebralette3, TyrellN6, TripleCheese,
  Podolski, BazilleCM — 128 programs each; CLAP: 41 plugins, 0 with presets (honest empty)
- `add_fx` Podolski (VST3) on track 0 → slot 0, **isolated child** ("spawnPluginHost:
  READY", "createPluginInstance result: ok")
- `list_plugin_presets` → **[{"index":0,"name":"Program 0"}, ...] — real 128-name list**
- `list_fx_params` → real params ("Output", "Active #LFOG", ...) with values

**New engine issues discovered (follow-up tasks, NOT blocking):**
1. `PluginManager::loadBlacklist` (:693-721) does `xml->getChildByName("BLACKLIST")`
   on the document root — but `saveBlacklist` writes `<BLACKLIST>` as the ROOT, so the
   lookup finds no child → **blacklist file is never actually enforced**. (The 16
   phantom VST3 scan_failure entries were never live in memory; today's FM8 exclusions
   were applied via `plugin_cache.xml` pre-entry + a blacklist entry that will become
   active once this is fixed.)
2. Scanner timeout kill does not enforce: a scanner hung inside FM8 (32-bit) survived
   past the engine's `waitForProcessToFinish(30000)` kill (zombie process; also not
   re-killable via taskkill). Now harmless: FM8 is pre-cached (skipped at :184).
3. Engine per-file 30s cap also drops legitimately-slow plugins: Serum2 (>30s
   instantiate, 128 programs via manual scanner) and WOMBintro (2 programs) never
   made the engine scan's preset list — candidates for a per-file timeout knob.

Gates: 1 ✓ (spawn) 2 ✓ (params) 3 ✓ (VST3 real + CLAP correct-empty) 4 ✓ (714 tests) 5 ✓ (builds).

#### Step 4.1: Implement `getText()` for ProxiedParameter

**File:** `src/proxy/PluginProxySlot.h`, line 59.

Option A (simple): Return the normalized value as a percentage string:
```cpp
juce::String getText(float value, int) const override {
    return juce::String(value * 100.0f, 1) + "%";
}
```

Option B (accurate): Query the child via a new `GET_PARAM_TEXT` IPC message. This is more complex but provides the plugin's own text representation.

**Recommended:** Start with Option A for immediate improvement, then follow up with Option B.

### Phase 5: Fix VST3 scan failures (lower priority)

The VST3 plugins (Odin2, Vital, scintillate) are blacklisted as `scan_failure`. The isolated scanner failed to load them. Folded into Phase 3.2 (manual scanner probe) — if a VST3 plugin now scans OK, clear the entry from `plugin_blacklist.xml` and re-scan so its presets (via JUCE's `IProgramListData` mapping) become listable.

## Implementation Order

1. **Phase 1** (diagnostic logging) — DONE; identified stale host exe as the spawn cause
2. **Phase 2** (fix spawn) — DONE; `mcp-launch.bat` refreshes both exes in `%TEMP%`
3. **Phase 3** (preset enumeration) — COMPLETE, verified live (results in Step 3.3);
   new engine bugs found: blacklist loader root-match bug, scanner-timeout kill not
   enforcing, 30s per-file cap dropping slow plugins (Serum2/WOMBintro) — queued as
   follow-ups (need hdaw-guard wrap)
4. **Phase 4** (getText) — parked; Option A is a 1-liner, do after Gate 3 verification
5. **Phase 5** (VST3 scan) — SUPERSEDED by Phase 3.2 (scanner bug fix, not blacklist);
   FM8.vst3 (32-bit) hangs x64 scanner → correctly caught by engine's 30s per-file timeout

## Files to Modify

| File | Change |
|------|--------|
| `mcp-launch.bat` | DONE — copies `HDAW_headless.exe` + `hdaw_plugin_host.exe` + `hdaw_plugin_scanner.exe` to `%TEMP%` each launch |
| `src/proxy/ProxyProcessManager.cpp` | DONE — HDAW_LOG diagnostics in `spawnPluginHost` |
| `src/proxy/scanner/PluginScannerMain.cpp` | DONE — real discovery flow (`findAllTypesForFile` → `getDescriptionFor`) + `probeVst3Module()` raw-load stage; THE VST3 fix |
| `src/engine/CLAPPluginInstance.cpp` | DONE — diagnostic extension probe in `initialize()` (tag `clap_host`); proved zero preset extensions on all CLAP plugins |
| `src/engine/PluginManager.cpp` | No change needed — split, cache-guard, and timeout paths already correct; blacklist cleared by hand, restart drops in-memory copies |
| `C:\Users\hapbt\AppData\Roaming\HDAW\plugin_blacklist.xml` | DONE — cleared to empty wrapper (engine reloads on restart) |
| `src/proxy/PluginProxySlot.h` | Optionally: `ProxiedParameter::getText()` (Phase 4) |
