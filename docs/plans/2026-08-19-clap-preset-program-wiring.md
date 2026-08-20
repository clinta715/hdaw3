# Plan — Phase 1: CLAP preset → program wiring (2026-08-19)

## Context & Phase 0 evidence

Handoff agenda #1 assumed a CLAP program-index API — it does not exist.
The real mechanism (verified in bundled CLAP 1.2.7 headers + upstream):
entry-level **preset-discovery factory** (`clap.preset-discovery-factory/2` +
compat `.../draft-2`) enumerates presets; plugin-level **preset-load**
(`clap.preset-load/2` + compat `clap.preset-load.draft/2`) loads one via
`from_location(plugin, location_kind, location, load_key)` — `[main-thread]`.

Phase 0 probe (`tests/unit/engine/clap_preset_probe_test.cpp`,
`docs/plans/2026-08-19-clap-preset-capability-probe.md`) measured 52 installed
CLAPs: **6 expose both sides** — Altitude (449 presets, PLUGIN-kind, compat id
only), Auburn Sounds Psypan 2 (16, PLUGIN-kind), Surge XT (2591, FILE-kind,
empty load_key, cap hit at patches_3rdparty), Diva (1432, FILE-kind),
TyrellN6 (669, FILE-kind), Zebralette3 (382, FILE-kind). Vital/Odin2/Dexed/
JC303/Xenia implement neither. **GO decision confirmed with user.**

## Goal

Expose CLAP presets through the EXISTING JUCE program API on
`CLAPPluginInstance` so the entire existing downstream machinery works for
CLAP unchanged: `applyPluginProgram`, `auditionPlugin` program reporting,
`addInstrumentPart` programIndex, `PluginParamServiceImpl`,
`TrackFXSlot::getNumPrograms/getProgramName/setCurrentProgram`
(`TrackFXSlot.h:753-778`), `AudioEngine::getFxProgramList`
(`AudioEngine.cpp:599`), frontend RPC `pluginParam.listPrograms/
setCurrentProgram` (`Router_Plugin.cpp:123-137`), MCP `list_plugin_presets` /
`load_plugin_preset` (`McpTools_Audio.cpp:200-253`), and the isolated proxy
path (`PluginProxySlot.cpp:254-322` → child `PluginHost.cpp:1139-1215`).
NO new RPC/MCP surface is needed — parity is automatic.

## Success Gates (all must pass with evidence)

- [ ] Gate 1: `cmake --build build --config Debug` succeeds; binary timestamp
      verified post-build (Gate 15).
- [ ] Gate 2 (hermetic): `ClapPresetDatabase.*` unit tests pass with FAKE
      clap factories (no real plugins): PLUGIN-kind enumeration, FILE-kind
      crawl + extension filter, universal-id filtering, file cap, cache-once
      semantics, empty/no-factory module → 1 program (VST3-parity behavior).
- [ ] Gate 3 (env-guarded, isolated default path, TyrellN6 CLAP
      `C:\Program Files\Common Files\CLAP\u-he\TyrellN6.clap`):
      `getFxProgramList` returns > 100 entries with non-empty names.
- [ ] Gate 4 (Gate-16 thread contract): `setCurrentProgram(1)` through the
      live slot works under default isolation — the child's SET_PROGRAM
      handler marshals `from_location` to the child message thread;
      `getCurrentProgram()` returns 1 afterwards; state blob after the switch
      differs from before.
- [ ] Gate 5 (the handoff's holy grail — measure first): audition program 0
      vs program 2 — both audible; if renders differ, assert rms/peak differ
      (> 1e-4); if byte-identical (would be surprising for u-he), weaken to
      state-level assertion and REPORT the finding.
- [ ] Gate 6 (Gate 1/10 rebuild restore): set program → snapshot state to
      tree (applyPluginProgram pattern) → `rebuildRoutingGraph()` → live
      processor re-render is audible and matches pre-rebuild character
      (program audio survived via the `pluginState` blob, `Track.cpp:188-196`).
      Assert on the LIVE path (re-render), not the ReadModel.
- [ ] Gate 7: full `hdaw_tests.exe` run (no env var) green — no regressions;
      probe + real-plugin tests SKIP cleanly.
- [ ] Gate 8: env-guarded run (`HDAW_REAL_PLUGIN_TESTS=1`) of
      `ClapProgram.*` + `Audition.*` + `ClapPresetProbe.*` green.

## Dependency Map (verified via grep/read + codebase-memory)

- Blast radius: `CLAPPluginInstance` program stubs (only consumer: the JUCE
  program API surface above — all verified to route through it); child
  `PluginHost` SET_PROGRAM handler; `CLAPHost::getExtension` (additive).
- Upstream: `CLAPPluginFormat::createPluginInstance` (constructs instance);
  `Track::rebuildFXChain` (restore via setStateInformation — unchanged).
- Downstream: everything listed in Goal — verified by grep, all funnel into
  `AudioPluginInstance` program virtuals. NO frontend changes.
- Projections: ReadModel unaffected (program state is volatile cache like
  VST3; persistence rides the existing `pluginState` blob). SPSC: none new.
- God nodes touched: none (MainAudioProcessor NOT modified — its local
  `runOnMessageThread` stays; we add a shared bounded helper instead).
- Community boundaries crossed: engine ↔ proxy child (pipe protocol UNCHANGED
  — SET_PROGRAM message already exists and is format-agnostic; only the
  child-side handler gains marshaling).

## Pitfall Gates Triggered

- **Gate 1/10 (rebuild restore):** program audio persists via the EXISTING
  `pluginState` blob restore (`Track.cpp:188-196`) because
  `applyPluginProgram` snapshots state AFTER `setCurrentProgram`
  (`AudioEngineCommands_Composition.cpp:387-409`). Gate 6 test proves it on
  the live processor. `currentProgram` index itself is volatile (resets to 0
  on rebuild) — same semantics as VST3 wrapper; document in code comment.
- **Gate 2 (no silent no-op):** full chain verified above; Gates 3–6 are the
  live-processor/DOM-level assertions.
- **Gate 3 (audio thread):** program calls NEVER touch processBlock;
  `from_location` runs on the CLAP main thread only. `currentProgram` is
  `std::atomic<int>`. Preset vector is immutable after one-time build.
- **Gate 14 (cross-process):** pipe protocol unchanged; SET_PROGRAM payload
  already fits (uint32). Child response already bounded-waited.
- **Gate 15 (stale binary):** verify timestamps before trusting test runs.
- **Gate 16 (thread contract — THE gate for this change):**
  - `from_location` is `[main-thread]` (preset-load.h:19).
  - In-process: `CLAPPluginInstance::setCurrentProgram` self-marshals —
    direct call when `host->threadCheckIsMainThread()` (message thread OR
    export render thread), else bounded marshal to the message thread
    (PluginHost::runLifecycleOnMessageThread shape, 5 s deadline; timeout →
    log + no change, never hang the caller).
  - Isolated child: `PluginHost::controlLoop` SET_PROGRAM handler wraps
    `plugin->setCurrentProgram` in `runLifecycleOnMessageThread` + the
    existing try/catch + `pluginFailed` second line (exact SET_STATE shape,
    `PluginHost.cpp:903-934`). GET_* handlers stay unmarshaled: they read the
    immutable preset cache + atomic index (cache built once; reads from the
    control thread are safe; provider crawl is spec-legal off-main —
    "indexing in background threads is encouraged").
  - Discovery factory calls (`count`/`get_descriptor`/`create`) are
    `[thread-safe]` per spec header; provider methods are single-thread-use —
    the per-module `std::once_flag` guarantees one builder at a time.
- **Lesson 7/8 (latency/quality):** no signal-path change; preset load may
  cause a brief plugin-side glitch while playing — same as any DAW preset
  switch, acceptable.
- **Anti-patterns:** no DBG (HDAW_LOG); new .cpp added to BOTH HDAW_lib list
  (root CMakeLists.txt:73-177 — covers engine AND plugin-host child, child
  links HDAW_lib) and test list (tests/CMakeLists.txt); no comments-free rule
  violation (match repo style).

## Design

### New: `src/engine/CLAPPresetDatabase.{h,cpp}`
- `struct CLAPPresetEntry { juce::String name, loadKey, location; uint32_t locationKind; }`
- `class CLAPPresetDatabase` — static registry keyed by module path:
  `static std::shared_ptr<ModulePresets> getForModule(const juce::String& modulePath, const clap_plugin_entry_t* entry)`;
  `ModulePresets` builds ONCE per module (`std::once_flag`): for each
  provider — create/init with a minimal indexer (records declared
  locations/filetypes), then PLUGIN-kind locations →
  `get_metadata(PLUGIN, nullptr, receiver)`; FILE-kind → crawl declared dirs
  for declared extensions (juce::RangedDirectoryIterator, recursive,
  **cap 2000 files/location**, log cap-hit) → `get_metadata(FILE, path,
  receiver)`. Receiver buckets presets by universal plugin id
  (`abi=="clap"` + id match; presets declaring NO plugin id count for every
  plugin of the module). `provider->destroy` after each.
  API: `const std::vector<CLAPPresetEntry>* presetsFor(const juce::String& clapPluginId) const`.
- Test seam: `ModulePresets` constructible from an explicit
  `clap_preset_discovery_factory_t*` + plugin-id list + cap override (the
  hermetic tests drive this with fake C-struct factories; production passes
  the entry-derived factory + default cap).
- Enumeration logic mirrors the proven Phase-0 probe
  (`tests/unit/engine/clap_preset_probe_test.cpp`).

### New: `src/common/RunOnMessageThreadBounded.h`
- Header-only bounded marshal (callAsync + deadline loop + exception rethrow,
  inline when already on the message thread / no MessageManager) — same shape
  as `PluginHost::runLifecycleOnMessageThread` (`PluginHost.cpp:796-814`).
  Does NOT modify MainAudioProcessor's local unbounded copy.

### Modified: `src/engine/CLAPPluginInstance.{h,cpp}`
- Ctor/initialize: query `presetLoadExt` (`CLAP_EXT_PRESET_LOAD`, then
  `CLAP_EXT_PRESET_LOAD_COMPAT`); fetch `CLAPPresetDatabase::getForModule`
  lazily on first program access (module path = desc.fileOrIdentifier —
  note `fillInPluginDescription` currently clears it; use the path stored by
  the format: add a `modulePath` member set in ctor from
  `module->loadedPath` — CLAPModule must remember its load path; add
  `juce::String loadedPath` set in `CLAPModule::load`).
- Members: `const clap_plugin_preset_load_t* presetLoadExt`;
  `std::shared_ptr<CLAPPresetDatabase::ModulePresets> presets`;
  `std::atomic<int> currentProgram{0}`.
- `getNumPrograms()` → list non-empty ? size : 1 (VST3-parity default).
- `getProgramName(i)` → entry name or {}.
- `getCurrentProgram()` → atomic load.
- `setCurrentProgram(i)`: bounds-check list + presetLoadExt; build args
  (`loadKey.isEmpty()` → nullptr — u-he/Surge FILE-kind use empty load_key);
  if `host->threadCheckIsMainThread()` call directly, else
  `RunOnMessageThreadBounded(5000ms)`; success → `currentProgram.store(i)`,
  failure/timeout → HDAW_LOG, index unchanged.
- `changeProgramName` stays no-op.

### Modified: `src/engine/CLAPHost` (`CLAPPluginInstance.{h,cpp}`)
- Provide host-side `clap_host_preset_load_t` in `getExtension` (manual
  struct — clap-helpers has NO preset-load support, verified): `on_error` →
  HDAW_LOG with msg/os_error; `loaded` → HDAW_LOG. Diagnostics only;
  `from_location`'s return value remains the authoritative result.

### Modified: `src/proxy/host/PluginHost.cpp`
- SET_PROGRAM case (~line 1187): wrap `plugin->setCurrentProgram(index)` in
  `runLifecycleOnMessageThread(..., 5000)` + try/catch + `pluginFailed` —
  exact SET_STATE shape. `resp.result` = 0 on timeout/exception.

### Tests
- `tests/unit/engine/clap_preset_database_test.cpp` — hermetic (Gate 2),
  fake factories as plain C structs in the test TU.
- `tests/unit/engine/clap_program_test.cpp` — env-guarded
  (`HDAW_REAL_PLUGIN_TESTS` + TyrellN6.clap exists, audition_test.cpp:27-36
  pattern), Gates 3–6 via the DEFAULT isolated path (never set
  HDAW_NO_PLUGIN_ISOLATION). Uses AudioEngine + PluginParamService +
  getFxProgramList + auditionPlugin/applyPluginProgram patterns from
  audition_test.cpp / instrument_part_test.cpp.
- Register both in `tests/CMakeLists.txt`.

## Steps

1. `CLAPModule::loadedPath` + `CLAPPresetDatabase.{h,cpp}` + CMakeLists.
2. `RunOnMessageThreadBounded.h`.
3. `CLAPPluginInstance` program API + `CLAPHost` preset-load host ext.
4. Child `PluginHost` SET_PROGRAM marshal.
5. Hermetic unit tests → run.
6. Env-guarded TyrellN6 tests → run (measure Gate 5 before hardening it).
7. Full suite (no env) + env-guarded suite; verify binary freshness.
8. Report with evidence per gate.
