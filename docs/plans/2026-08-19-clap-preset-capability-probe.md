# Plan — Phase 0: CLAP preset-capability probe (2026-08-19)

## Context

Handoff `docs/handoffs/2026-08-19-composer-agenda-remaining.md` agenda #1
("CLAP program wiring") claims CLAP has a program-index API
(`get_program_count` / `set_program` / `get_program_name` + a
`clap-ext/sound-programs` extension). **Verified FALSE:**

- Bundled headers (CLAP 1.2.7, `build/clap-juce-extensions-src/clap-libs/clap/include`)
  contain no `sound-programs` extension (checked `ext/` and `ext/draft/`).
- Upstream `free-audio/clap` main branch `ext/draft/` listing: no sound-programs.
- The REAL CLAP preset mechanism is the entry-level **preset-discovery factory**
  (`clap.preset-discovery-factory/2`, `factory/preset-discovery.h`) + the
  plugin-level **preset-load extension** (`clap.preset-load/2` and compat id
  `clap.preset-load.draft/2`, `ext/preset-load.h`, `from_location` is `[main-thread]`).

Existing evidence (`%TEMP%\hdaw_debug.log` `ext-probe` lines, 2026-08-19):
every instantiated CLAP shows `preset-load=false` EXCEPT **Altitude**
(`preset-load.draft/2=true`). u-he (TyrellN6/Diva/Zebralette3) and Surge XT
were never instantiated by HDAW, so their capability is unknown. The log's
`preset-discovery=false` field probed a plugin extension id that doesn't
exist — the discovery factory is entry-level and has NEVER been probed.

## Goal

Measure, for every installed `.clap`: (a) entry-level preset-discovery factory
presence, (b) plugin-level preset-load extension presence, (c) where the
factory exists — enumerate presets (count, names, load_keys, location kinds).
This decides whether Phase 1 (full wiring) is worth doing or the agenda pivots
to #2 (role defaults) / #3 (namespace guard).

## Success Gates (all must pass with evidence)

- [ ] Gate 1: `tests/unit/engine/clap_preset_probe_test.cpp` added to
      `tests/CMakeLists.txt`; `cmake --build build --config Debug` succeeds.
- [ ] Gate 2: without `HDAW_REAL_PLUGIN_TESTS=1` the probe GTEST_SKIPs
      (run `hdaw_tests.exe --gtest_filter=ClapPresetProbe.*`, expect SKIPPED).
- [ ] Gate 3: with the env var, the probe covers ALL `.clap` files under the
      default search locations + `CLAP_PATH` (recursive), logging one line per
      module (factory presence) and per contained plugin (preset-load presence).
- [ ] Gate 4: for modules exposing the factory, the probe enumerates presets
      via a minimal indexer/receiver (PLUGIN-kind locations directly; FILE-kind
      locations by crawling declared dirs for declared extensions, capped at
      2000 files/location) and logs count + up to 8 sample names/load_keys.
- [ ] Gate 5: shortlist instantiation check — TyrellN6, Diva, Zebralette3,
      Surge XT, Vital, Odin2, Dexed, JC303, Xenia, Altitude get a plugin
      instance created (SEH-guarded) and `get_extension` probed for
      `clap.preset-load/2` + compat id, even if the module has no factory.
- [ ] Gate 6: probe is measurement-only — no capability assertions beyond
      structural sanity (`modulesTested >= 1`); no production code changed.
- [ ] Gate 7: `hdaw_tests.exe` binary timestamp verified post-build (Gate 15 —
      stale-binary trap) before trusting results.

## Dependency Map

- Blast radius: NONE — new test file + one CMakeLists source-list line.
- Upstream consumers of results: the Phase-1 go/no-go decision only.
- Uses (read-only): `CLAPModule` (`src/engine/CLAPPluginFormat.h`),
  `CLAPPluginFormat::getDefaultLocationsToSearch`/`searchPathsForPlugins`,
  clap headers `factory/preset-discovery.h`, `ext/preset-load.h`,
  `ext/thread-check.h`, `universal-plugin-id.h` (all via existing include path).
- Projections affected: none. SPSC paths touched: none. God nodes: none.
- Tier-2 instantiation deliberately uses a minimal static probe host (NOT
  CLAPHost/CLAPPluginInstance) to stay decoupled from production classes;
  provides `thread-check` answering main-thread=true for the probe thread.
  Fallback: if `plugin->init` fails with the minimal host, retry that plugin
  via `CLAPPluginFormat::createInstanceFromDescription` (the proven path).

## Pitfall Gates Triggered

- **Gate 4/15 (stale binary):** verify `build/Debug/hdaw_tests.exe` timestamp
  after build; never run an older binary.
- **Gate 11 (message pump):** no new entry point — `test_main` already starts
  `MessagePumpThread` first. CLAP `entry->init` off the message thread is the
  same pattern the existing plugin scan uses.
- **Gate 16 (thread checks):** Tier-2 probe host provides CLAP_EXT_THREAD_CHECK
  reporting the probe thread as main; SEH translator + try/catch around every
  `plugin->init` (pattern: `CLAPPluginFormat::createPluginInstance`); a
  misbehaving plugin must fail the instance, not the test process.
- **Anti-patterns:** no `DBG` (use `HDAW_LOG` from `common/DebugLog.h`), new
  `.cpp` MUST be added to `tests/CMakeLists.txt`, no comments-free requirement
  (test file — keep comments minimal, match repo style).

## Steps

1. Write `tests/unit/engine/clap_preset_probe_test.cpp`:
   - `clapPresetProbeEnabled()` guard: `HDAW_REAL_PLUGIN_TESTS` set/non-zero.
   - Collect `.clap` files via `CLAPPluginFormat` search paths (recursive).
   - Tier 1 (every file): `CLAPModule::load`; `entry->get_factory` for
     `CLAP_PRESET_DISCOVERY_FACTORY_ID` and
     `CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT`; list plugin descriptors
     (name/id) from the plugin factory; HDAW_LOG + std::cout per module.
   - Tier 2 (factory-present modules OR shortlist): minimal static clap_host
     (clap_version, desc, get_extension → thread-check ext only),
     `create_plugin`, SEH-guarded `init`, then `get_extension` for
     `CLAP_EXT_PRESET_LOAD` and `CLAP_EXT_PRESET_LOAD_COMPAT`;
     `plugin->destroy` before module unload (scope order!).
   - Tier 3 (factory-present): for each provider id —
     `factory->create(indexer)`, `provider->init`, record declared
     locations/filetypes; PLUGIN-kind → `get_metadata(PLUGIN, nullptr,
     receiver)`; FILE-kind → crawl declared dirs (declared extensions,
     cap 2000 files) → `get_metadata(FILE, path, receiver)`; receiver
     collects `(name, load_key)` via `begin_preset`, filters by universal
     plugin id (`abi=="clap"`, `id==<clap plugin id>`), counting
     matched/unmatched; `provider->destroy`. Log totals + 8 samples per plugin.
   - Single test case `ClapPresetProbe.ScanInstalledClaps` (+ optional
     per-tier split if it aids readability). Structural assert only:
     `EXPECT_GE(modulesTested, 1)`.
2. Add the file to `tests/CMakeLists.txt` (after `master_gain_test.cpp`).
3. `cmake --build build --config Debug`; verify binary timestamp.
4. Run without env var → expect SKIPPED. Run with
   `$env:HDAW_REAL_PLUGIN_TESTS=1` → capture full output (10-min budget).
5. Report: per-plugin table (factory / preset-load / preset count / sample
   names), plus the go/no-go reading.
