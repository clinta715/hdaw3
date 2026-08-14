# Handoff: Fix scan/blacklist engine bugs found during the preset-listing task

You are continuing work on HDAW (JUCE 8 DAW, C++ engine + React frontend), repo at
`D:\pdf\roo projects\hdaw3`. **FIRST: invoke the `hdaw-guard` skill** (mandatory
before ANY code change in this project) and read `AGENTS.md` lessons 11–15 (message
pump, thread-safety, stale-`.obj` build trap). State your plan in `docs/plans/`
before editing (see `docs/plans/2026-08-11-fix-plugin-preset-parameter-listing.md`
for the completed task this derives from).

## Context: what is already fixed/landed (do NOT regress)

The `list_plugin_presets` / `list_fx_params` MCP gap was closed and verified live:

- **Root cause 1 (spawn):** `mcp-launch.bat` (repo root) now copies
  `HDAW_headless.exe`, `hdaw_plugin_host.exe`, AND `hdaw_plugin_scanner.exe` to
  `%TEMP%` on every launch — kills the stale-binary class of bugs permanently.
- **Root cause 2 (VST3 scan):** `src/proxy/scanner/PluginScannerMain.cpp` now uses
  the documented discovery flow (`findAllTypesForFile` → desc discovery) plus a
  `probeVst3Module()` raw-Win32 stage, instead of the old hand-rolled
  `PluginDescription` + `createPluginInstance` shortcut. Builds pass; JSON contract
  unchanged; per-plugin table + live scan verified (Zebralette3/TyrellN6/TripleCheese/
  Podolski/BazilleCM = 128 programs each, Serum2 = 128 via manual scanner run).
- CLAP preset enumeration documented as impossible (no extension in CLAP 1.2.7) —
  correct-empty is the accepted answer for CLAP.
- `%TEMP%\hdaw_plugin_scanner.exe` is the current fixed build (8/11);
  `%TEMP%\HDAW_headless_mcp.exe`/`hdaw_plugin_host.exe` refresh via the bat.

## The bugs to fix (your task)

### Bug 1 — `PluginManager::loadBlacklist` never loads anything
`src/engine/PluginManager.cpp`:
- `saveBlacklist()` (:724-738) writes `<BLACKLIST>` as the **document root**
  (`XmlElement root("BLACKLIST")` → `root.writeTo`).
- `loadBlacklist()` (:693-721) parses the file then does
  `xml->getChildByName("BLACKLIST")` — but the root IS `BLACKLIST`, so the lookup
  finds no child → returns early → `blacklistedIDs` always empty. **Blacklisting is
  silently non-functional** (the historic "scan_failure blacklist" never actually
  took effect; the 16 VST3 entries were a file-only artifact). `isBlacklisted`
  (:647-653) is consulted at scan time (:193), listing time (:366/:379), and
  instantiation time (:502) — the fix activates all of these.
- **Fix:** handle the root-is-BLACKLIST case (use the parsed root element when its
  tag is `BLACKLIST`, else look for the named child). Keep `saveBlacklist` format as
  the canonical one. Round-trip test required (see Testing).
- **Files:** `src/engine/PluginManager.cpp` (+ header if you add helpers). Consider
  `src/engine/PluginManager.h` if you surface a `getBlacklistedCount()/getBlacklist()`
  accessor for tests.

### Bug 2 — scanner-timeout kill does not enforce / hung scanners become zombies
Evidence from the live re-scan: a scanner hung inside FM8.vst3 (32-bit-only plugin;
its 2nd discovered type instantiates then wedges the x64 process) survived past the
engine's `child.waitForProcessToFinish(30000)` (:451) kill and remained as an
unkillable "no running instance" zombie (taskkill/Stop-Process both fail; clears only
at reboot; holds no file locks). The engine's scan never advanced.
- **Look at** `PluginManager::scanPluginIsolated` (:419-497): the 30s
  `waitForProcessToFinish` + `child.kill()` path, and the crash/exit-code taxonomy
  (:233-260). Verify WHY the kill didn't land (handle semantics, kill on
  non-exited child, SUCCEEDED-on-zombie quirks) and make the timeout path robust:
  guaranteed kill, a hard overall scan cap, and per-plugin timeout reporting back to
  the caller.
- **Requirement:** a hung scanner must never stall `scan_plugins` indefinitely and
  must be reported (log + result), not silently skipped.

### Bug 3 — 30s per-file cap drops legitimately slow plugins
Serum2 (128 programs!) and WOMBintro (2) fail the engine scan purely because they
instantiate slower than the 30s per-file cap, while scanning fine by hand (see
`build\Debug\hdaw_plugin_scanner.exe --plugin="<path>" --pedal-file=...` with quoted
paths — note the path MUST be quoted, the harness splits on spaces).
- **Fix:** a per-file timeout knob (env var or constant) with sane default (e.g.
  90s), applied in `scanPluginIsolated`. Verify Serum2/WOMBintro then land in
  `list_plugins` with `hasPresets`/`presetCount` populated after a rescan.

### Bug 4 (durable exclusion, wraps 2) — 32-bit/poison plugins
FM8.vst3 hangs the x64 scanner on scan AND instantiates the child (32-bit-only).
Currently excluded ad hoc: a `plugin_cache.xml` pre-entry (repo-side user file) + a
`plugin_blacklist.xml` entry that only takes effect once Bug 1 is fixed.
- **Fix:** make the exclusion principled — e.g. skip `.vst3` files whose binary
  reports a 32-bit image (the raw module probe in `PluginScannerMain.cpp` already
  loads the module: use `GetModuleHandle`/PE header data or the version-info
  `CanLoad32BitOn64Bit` flag) in BOTH scanner and engine, with a log + result reason.
  After this, FM8 must be scannable-skip → "skipped (32-bit)" and never hang.
  Remove the handmade `plugin_cache.xml` FM8 entry once the real guard lands
  (`C:\Users\hapbt\AppData\Roaming\HDAW\plugin_cache.xml`, backup at
  `plugin_cache.xml.bak`; the FM8 `<PLUGIN .../>` element is the LAST one before
  `</KNOWNPLUGINS>`).
- **Note:** keep the FM8 blacklist entry (`reason="32-bit-only hangs x64 scanner"`)
  — it becomes active after Bug 1 and is correct.

### Nice-to-have (only if time permits)
`src/proxy/PluginProxySlot.h`: `ProxiedParameter::getText()` (Phase 4 of the plan
doc) — Option A: percentage string; Option B: `GET_PARAM_TEXT` IPC round-trip.

## Hard constraints (project rules)

1. `hdaw-guard` skill FIRST; write the plan into `docs/plans/` before code.
2. **No regression on the verified preset path** — after your changes, re-run the
   live verification (below) and confirm the 5 VST3 128-program banks still list.
3. Engine change test discipline (AGENTS.md): every fix needs gtest coverage —
   existing suites: `PluginManager`-related tests + `frontend_server_test.cpp`;
   see `docs/testing-mcp.md`. At minimum add: blacklist round-trip test
   (write → reload → isBlacklisted true), timeout path test (feed a hang-script as
   the "scanner", assert bounded completion + reason).
4. `hdaw_tests.exe` must pass fully (714 tests; run filtered first via
   `--gtest_filter=*Blacklist*` etc., then full suite — the full run takes ~7 min).
5. Rebuild: `cmake --build build --config Debug`. **Do NOT run**
   `build/Release/HDAW.exe` (stale). Re-copy binaries to `%TEMP%` if you need a live
   MCP check (or rely on `mcp-launch.bat` on next server start).
6. Report per-plugin scan reasons cleanly (log tags `scanner`, `PluginManager`).

## Live verification recipe (use after rebuilding)

Raw MCP stdio probe (no opencode tool bindings needed): the working harness lives at
`C:\Users\hapbt\AppData\Local\Temp\opencode\mcp_verify.ps1` (pipe-based JSON-RPC over
`%TEMP%\HDAW_headless_mcp.exe --mcp-stdio`; detach it with
`Start-Process powershell -WindowStyle Hidden -ArgumentList ...` and poll
`...\mcp_report.txt`). Protocol: `initialize` (proto 2024-11-05) → `initialized` →
`tools/call` with **unprefixed** tool names (`scan_plugins`, `list_plugins`,
`add_fx`, `list_plugin_presets`, `list_fx_params`). Kill leftover engines first.
Success criteria:
- `scan_plugins` → "scanned N plugins" completing in bounded time (no stall on FM8)
- `list_plugins` → Serum2 + WOMBintro now present WITH presets; the 5 u-he banks
  still there; FM8 absent (or present-but-marked-skips never listed)
- `add_fx` on a VST3 → slot created; `list_plugin_presets` → named list;
  `list_fx_params` → names+values
- `hdaw_tests.exe` → all pass (incl. your new blacklist/timeout tests)

## Codebase pointers

- `src/engine/PluginManager.cpp` — all four bugs live here (blacklist :693-738,
  scan loop :169-260, isolated scan :419-497) + `src/engine/PluginManager.h`
- `src/proxy/scanner/PluginScannerMain.cpp` — probe/scan flow (32-bit detection hook)
- `C:\Users\hapbt\AppData\Roaming\HDAW\plugin_blacklist.xml` — currently: empty
  wrapper + FM8 entry (reason string above)
- `C:\Users\hapbt\AppData\Roaming\HDAW\plugin_cache.xml` — pre-entry FM8 element:
  last `<PLUGIN ... file="C:\Program Files\Common Files\VST3\FM8.vst3" .../>` before
  `</KNOWNPLUGINS>` (remove after Bug 4 lands)
- Plan doc: `docs/plans/2026-08-11-fix-plugin-preset-parameter-listing.md` (Phase 3
  COMPLETE; Step 3.3 "New engine issues discovered" = the four bugs above)