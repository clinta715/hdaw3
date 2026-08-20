# Handoff — CLAP preset→program wiring shipped; START #2 role defaults + #3 namespace gaps (2026-08-19, session 4)

## Purpose

This session completed agenda **#1 "CLAP program wiring"** from
`docs/handoffs/2026-08-19-composer-agenda-remaining.md` — but NOT as the
handoff described it. **The handoff's premise was factually wrong** (see
§Corrected record). The real mechanism was measured first (Phase 0), then
wired (Phase 1). Per-program audio differences are now REAL and asserted for
CLAP — the "holy grail" the VST3 cosmetic program API never delivered.

**Next session: start with §Remaining agenda — #2 is a small self-contained
composer extension; #3 is RE-SCOPED (its core guard already shipped in
`b9545d1`; read that section before planning).**

## ⚠️ Corrected record — do not re-derive the wrong premise

- **CLAP has NO program-index API.** There is no `get_program_count` /
  `set_program` / `get_program_name` on `clap_plugin_t`, and **no
  `sound-programs` extension exists** — verified in the bundled CLAP 1.2.7
  headers (`build/clap-juce-extensions-src/clap-libs/clap/include`, `ext/` +
  `ext/draft/`) and the upstream `free-audio/clap` main branch.
- **The real CLAP preset mechanism:**
  - Entry-level **preset-discovery factory**
    (`clap.preset-discovery-factory/2`, compat `.../draft-2`,
    `clap/factory/preset-discovery.h`): providers declare locations/filetypes;
    the host crawls and gets `(name, load_key, plugin-ids)` per preset.
    PLUGIN-kind = presets bundled in the DSO; FILE-kind = files on disk.
  - Plugin-level **preset-load extension** (`clap.preset-load/2`, compat
    `clap.preset-load.draft/2`, `clap/ext/preset-load.h`):
    `from_location(plugin, location_kind, location, load_key)` —
    **`[main-thread]`** (Gate 16 applies).
  - clap-helpers has NO host-side preset-load support — host callbacks
    (`on_error`/`loaded`) must be wired manually (done, `presetLoadHub`).

## Phase 0 evidence (capability probe — KEEP, env-guarded)

`tests/unit/engine/clap_preset_probe_test.cpp` (env `HDAW_REAL_PLUGIN_TESTS`)
measured all 52 installed `.clap`s. **6 expose both sides:**

| Module | Presets | Kind | Notes |
|---|---|---|---|
| TyrellN6 (u-he) | 669 | FILE, empty load_key | the reference real-plugin subject |
| Diva (u-he) | 1432 | FILE | names arrive as full file paths |
| Zebralette3 (u-he) | 382 | FILE | |
| Surge XT | 2591 | FILE | 2000-file cap hit at `patches_3rdparty` |
| Altitude (Audio Damage) | 449 | PLUGIN, real load_keys | preset-load via COMPAT id only |
| Psypan 2 (Auburn Sounds) | 16 | PLUGIN | |

Vital, Odin2, Dexed, JC303, Xenia (the old handoff's synth list) implement
NEITHER mechanism — their presets are unreachable via CLAP; they would need
file-based import if ever wanted.

## What shipped (Phase 1)

Plans: `docs/plans/2026-08-19-clap-preset-capability-probe.md`,
`docs/plans/2026-08-19-clap-preset-program-wiring.md` (gates + evidence).

| File | Change |
|---|---|
| `src/engine/CLAPPresetDatabase.{h,cpp}` | NEW — per-module preset cache (registry keyed by module path, `std::once_flag` build, async background build, indexer/receiver/crawl with 2000-file cap, universal-id bucketing, module keepalive until build done) |
| `src/common/RunOnMessageThreadBounded.h` | NEW — bounded marshal (callAsync + deadline, rethrows, inline when already on message thread) |
| `src/engine/CLAPPluginInstance.{h,cpp}` | program virtuals implemented over the database + preset-load; `setCurrentProgram` self-marshals off-main (5 s bound); alive-flag UAF guard for the timeout path; `presetsMutex` for first-access races; host-side `presetLoadHub` for both ext ids |
| `src/engine/CLAPPluginFormat.{h,cpp}` | `CLAPModule::loadedPath` (registry key) |
| `src/proxy/host/PluginHost.cpp` | SET_PROGRAM marshaled via `runLifecycleOnMessageThread` + try/catch + `pluginFailed` (Gate 16, SET_STATE shape) |
| `src/proxy/PluginProxySlot.{h,cpp}` | `pollProgramCount()` on the 100 ms timer (see deviation 1) |
| `src/engine/PluginManager.cpp` | `*.clap` scan now recursive (u-he vendor subfolders) |
| `src/model/ProjectModel.cpp` | `resolvePluginFormat` extension fallback for path ids with stale cache |
| Tests | `clap_preset_database_test.cpp` (6 hermetic, fake C-struct factories), `clap_program_test.cpp` (4 env-guarded, TyrellN6 CLAP, default isolation) |

**No new RPC/MCP/frontend surface was needed** — everything already funnels
through the JUCE program virtuals: `TrackFXSlot.h:753-778`,
`AudioEngine::getFxProgramList` (`AudioEngine.cpp:599`),
`PluginParamServiceImpl`, frontend RPC `pluginParam.listPrograms/
setCurrentProgram` (`Router_Plugin.cpp:123-137`), MCP `list_plugin_presets` /
`load_plugin_preset` (`McpTools_Audio.cpp:200-253`), `auditionPlugin` /
`addInstrumentPart` programIndex.

### Verified results (the money numbers)

- TyrellN6 CLAP: **669 programs** enumerated through the live ISOLATED slot.
- **Per-program audio genuinely differs**: program 0 vs 2 — rms
  0.0744768 vs 0.0724928, peak 0.262474 vs 0.228679 (both diffs > 1e-4).
- Program state **survives `rebuildRoutingGraph()`** via the existing
  `pluginState` blob (`Track.cpp:188-196`): post-rebuild re-render
  deviation = 0 (asserted on the live processor, Gate 1/10).
- Full suite: 957 ran / 949 passed / 8 env-skipped / 0 failures at Phase-1
  acceptance (post-fix run: 1 flake, see #3 briefing — same family).

### Deviations from the plan (measured, not chosen)

1. **Async database build + parent-side poll.** The TyrellN6 crawl takes
   ~9.7 s; the parent proxy's construction-time GET_PROGRAM_COUNT is a 3 s
   bounded exchange and ANY pipe timeout sets `connected=false`
   (`ProxyPipe.cpp:199-202`), permanently killing the slot. So the child
   builds in the background (spec-encouraged), reports 1 program until ready,
   and `PluginProxySlot::pollProgramCount()` refreshes (~1 s cadence, 45
   attempts cap). **Generalizable lesson: long plugin work must never sit
   inside a bounded pipe exchange.**
2. Recursive CLAP scan + `resolvePluginFormat` extension fallback — u-he
   installs into vendor subfolders; raw-path slot creation (audition/
   composition flows) needs format resolution without a cache entry.

### Review fixes applied after the first implementation pass

- Alive-flag UAF guard: the marshal-timeout lambda is still posted and runs
  later; it must never touch a destroyed instance (no `this` capture).
- `presetsMutex` double-checked locking in `ensurePresets` (control vs
  message thread in the isolated child).
- Host-side `getExtension` answers the preset-load COMPAT id too (Altitude).

---

# Remaining agenda — briefing for the next session

## #2 (start here) — Part templates + typed track presets: `role → defaults` in `addInstrumentPart`

**Scope (from handoff #2 item #3, unchanged):** cheapest first step — a
`role → defaults` map inside `addInstrumentPart`; exposed as a `role` param.
Now that global-scale exists, role defaults can also carry sensible
`targetRms`/`allowGlobalScale` presets. **No new processor state. Defer the
full `TRACK_TEMPLATE` store.**

### Where to look (verified pointers)

- Params struct: `src/common/ProjectCommands.h:326` (`InstrumentPartParams`).
- Implementation: `src/engine/AudioEngineCommands_Composition.cpp`
  `addInstrumentPart` (validation block at ~line 417 is where role
  validation/defaulting slots in).
- RPC: `src/frontend/router/Router_Composition.cpp` (`addInstrumentPart`
  case) — add optional `role` string param.
- MCP: `src/mcp/McpTools_Project.cpp` `add_instrument_part` (~line 1051) —
  parity is mandatory (AGENTS.md "MCP feature parity").
- Tests: `tests/unit/engine/instrument_part_test.cpp` (existing patterns,
  incl. real-plugin program handling + rollback assertions).

### Design sketch

- `role` ∈ {`""` (default = today's behavior), `"Bass"`, `"Lead"`,
  `"Chords"`, `"Drums"`} — case-insensitive, unknown role → validation error
  (Gate 9 style).
- Role defaults apply ONLY for fields the caller did not explicitly set
  (explicit params always win). Candidate defaults (tune during planning):
  - **Bass**: fm_synth bass-ish patch, style `BassLine`, low range
    (~36–48), higher density, `targetRms` ~ -18 dB, `allowGlobalScale=true`.
  - **Lead**: fm_synth lead patch, style `Lead`, mid-high range (~60–76),
    lower density.
  - **Chords**: fm_synth pad/keys patch, style `ChordStab` or `Pad`,
    mid range, `beatsPerChord`-friendly lengths.
  - **Drums**: percussive fm_synth patch (or `generate_rhythm_pattern` path
    if the drums role should route to `RhythmPatternBuilder` — decide in the
    plan; the generative toolkit rule in AGENTS.md applies).
- fm_synth caveat (handoff #3 finding #4): the default E.PIANO patch is
  velocity-insensitive — role presets that rely on velocity for dynamics
  won't get them; pick patches/params accordingly or note the limitation.

### Success gates (sketch — finalize in the plan)

1. `addInstrumentPart {role:"Bass"}` (minimal other params) produces the
   SAME effective configuration as a hand-configured Bass part (assert on
   the resulting clip/track/params, not on internals).
2. Explicit params override role defaults (e.g. role:"Bass" + explicit
   style → explicit style wins).
3. Unknown role → clean error, project untouched.
4. RPC + MCP both accept `role`; MCP parity test per repo convention.
5. Full suite green; no new processor state (nothing to restore — Gate 1 N/A
   by construction, say so in the plan).

## #3 (RE-SCOPED) — Lesson-20 namespace guard: core ALREADY SHIPPED; remaining gaps are in-suite + export-domain

**⚠️ Do NOT re-implement the per-instance prefix.** It shipped in
`b9545d1` (2026-08-15): `AudioEngine::initialize` sets
`setProxyNamespacePrefix("<pid-hex>_")` (`AudioEngine.cpp:80`); the export
domain uses `"export_"` (`ExportManager.cpp:145`); plumbing is
`PluginManager::setProxyNamespacePrefix` → `ProxyProcessManager::setNamePrefix`
→ names `\\.\pipe\hdaw_plugin_<prefix><n>` / `hdaw_plugin_shm_<prefix><n>`
(`ProxyProcessManager.cpp:330-334`). At commit time 43/43 proxy tests passed.
Handoff #3's agenda item pre-dated awareness of this.

### What remains (evidence-based)

1. **In-suite collisions inside ONE test process.** The prefix is per-PROCESS
   (`hdaw_tests.exe` pid), but the slot counter restarts at 1 per
   PluginManager/ProxyProcessManager — so an orphaned child from an EARLIER
   test in the same run holds `<testpid>_1` and a later test's slot 1
   collides. This is the most likely vector for today's
   `PluginIsolation.LiveDropDrainsStaleOutput` full-suite failure (passes
   alone) and the historical "known to fail five" CrashRecovery flakes when
   they occur in-suite. (Cross-engine collisions are already fixed.)
2. **Static `export_` prefix** — shared by every offline render of every
   engine; orphaned export children (or overlapping exports across processes)
   can still collide. Candidate: `export_<pid>_` or a per-render suffix.
3. **(Optional second line) held-name skip** — before spawn, probe the pipe
   name and bump the slot id if occupied. Cheap insurance; not required if
   1+2 land.

### Where to look

- `src/proxy/ProxyProcessManager.{h,cpp}` (spawn args at ~line 73, name
  builders at ~330, `setNamePrefix`), `src/proxy/host/main.cpp` (child parses
  `--slot=`/`--pipe=` — the child uses whatever name it is given, so prefix
  changes are parent-only).
- `src/engine/PluginManager.{h,cpp}` (`setProxyNamespacePrefix`, slot
  allocation), `src/engine/AudioEngine.cpp:80`, `src/engine/ExportManager.cpp:145`.
- Affected tests: `tests/integration/proxy/isolation_integration_test.cpp`,
  `tests/unit/proxy/crash_recovery_test.cpp` (already sets `"export_"` on an
  offline copy at line 463 — pattern to extend).

### Suggested approach + gates (finalize in the plan)

- Make the prefix per-PluginManager-instance (e.g. `<pid>_<instanceCounter>_`
  or a GUID suffix) for BOTH live and offline domains; keep it short (pipe
  name length limits).
- Gate A: two PluginManagers in one process get distinct namespaces (unit
  assert on the built pipe/shm names).
- Gate B: simulate an orphan — hold `\\.\pipe\hdaw_plugin_<prefix>1` from a
  test thread/dummy listener, then spawn through the manager and assert the
  slot either bumps or the spawn cleanly reports the collision (depending on
  chosen design).
- Gate C: the five CrashRecovery tests + `LiveDropDrainsStaleOutput` pass in
  the FULL suite (that is the real acceptance — run the whole
  `hdaw_tests.exe`, not filters).
- Gate 14 applies (cross-process protocol): name changes must round-trip
  through the spawn args; verify the child logs/uses the given name.
- Measure first if a flake reproduces: attribute `%TEMP%\hdaw_debug.log`
  lines to pid before concluding anything (handoff #3 operational context).

## Low priority (carried)

- **Beats-vs-seconds ergonomics** (old #4): `paintToProjectEnd` helper /
  uniform bars-beats acceptance.
- Composer follow-ups unlocked by this session (optional):
  `addInstrumentPart`/`auditionPlugin` CLAP preset sweeps (e.g. role→u-he
  preset mapping — pairs naturally with #2); verify the frontend FX-chain
  program selector against a CLAP slot if UI work resumes.

## Operational context (unchanged, still true)

All "Operational context a fresh session MUST know" items from handoff #3
remain valid: audio-device environmental failures, message pump, probe
hygiene + pid attribution, real-plugin env guards (NEVER
`HDAW_NO_PLUGIN_ISOLATION` for real plugins), beats-vs-seconds, 3 empty
default tracks, stale-binary traps, master-gain pinning in render tests.
New: CLAP preset crawls are CACHED per module path for the process lifetime
(`CLAPPresetDatabase` registry) — first instance of a u-he CLAP pays ~10 s
async, later instances are instant.

## Where to look (this session's work)

- Probe: `tests/unit/engine/clap_preset_probe_test.cpp`
- Database: `src/engine/CLAPPresetDatabase.{h,cpp}` (+ hermetic test)
- Program API: `src/engine/CLAPPluginInstance.{h,cpp}` (program virtuals,
  `presetLoadHub`, alive flag)
- Child marshal: `src/proxy/host/PluginHost.cpp` SET_PROGRAM case
- Parent poll: `src/proxy/PluginProxySlot.cpp` `pollProgramCount`
- Real-plugin contract: `tests/unit/engine/clap_program_test.cpp`
- Learnings: AGENTS.md lessons 1–22, `docs/realtime-safety.md`

Per hdaw-guard: every code change gets a plan with success gates first,
dependency analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`, refreshed this session, 9983 nodes), pitfall-gate
scan, and subagent dispatch with verification.
