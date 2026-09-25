# Handoff — psy-dub composition closeout, parity ledger closed, v0.38.0

**Date:** 2026-09-25 · **Base:** `4afe1fc` (start of the 2026-09-24 track-identity session) ·
**HEAD at writing:** `b1114b8` + the v0.38.0 version-bump/docs commit.
**Supersedes:** nothing — this is the closeout of the work recorded in
[`docs/handoffs/2026-09-24-track-identity-and-parity.md`](2026-09-24-track-identity-and-parity.md)
(**read its §8 first** for the B3 stable-id design, the send-address decision and the ledger
close; this file does not repeat those details).

## 1. State

- Version **0.38.0** (`CMakeLists.txt` `project(HDAW VERSION 0.38.0)` +
  `frontend/package.json`, verified in sync by the `check_version` target /
  `cmake/CheckVersionSync.cmake` — those two files are the only live version
  declarations the check compares; no other `0.37.0` remains in src/tests/cmake).
- Deliverable: `compositions/psy_dub_test_2026-09-25.wav` — 5:07.35, truePeak →
  finalPeak 0.877–0.901, RMS ≈ 0.093. **Rendered locally, NOT in git**
  (`compositions/` is gitignored); `PsytranceComposition.PsyDubFiveMinutes`
  reproduces it.
- Uncommitted files owned by ANOTHER session — leave alone: `.gitignore`,
  `AGENTS.md`, `docs/skills/README.md`, `docs/skills/psy-song-session/SKILL.md`.
- AGENTS.md pending updates are listed in §4f below (that file was dirty all session).

## 2. What landed (oldest → newest, `4afe1fc..b1114b8`)

| commit | scope | verified by |
| --- | --- | --- |
| `55ab21a` | seven RPC twins for the no-route tools + the ledger generator now sees chained `m == "A" \|\| m == "B"` predicates | `MissingRouteParityTest` 12 twins, focused 147/147, full 1967/284 (12 env) |
| `ebe727c` | **B3**: durable track refs = stable ids (`parentTrackID`/`childTrackIDs`/`cellTrackID`), one-way load migration, remap machinery deleted, removeTrack prunes refs | DurableRefMigration suite + `RemoveTrackPrunesRefs…`; focused 271/271, full 1968/285 (12 env) — detail in the 09-24 handoff §8 |
| `87040b5` | send-address decision: `2000 + sendIndex` stays permanently (bus-pid collision at send id 1000) | docs: `architecture.md` pid-space + B3 plan decision 3 CLOSED |
| `0c96e7e` | **parity ledger CLOSED** — all 10 unresolved rows mapped (307 tools / 411 methods / mapped 295 / mcp-only 12 / unresolved 0); shared `src/common/` shapers; `PresetRoute.h` becomes an adapter | `FmLibraryParityTest` 9 + `CapabilityRouteParityTest` 18, full 1952/285 (11 env) |
| `f088bfb` | docs restructure: psytrance guide split → `psytrance-va-and-production.md`, hardware §9 → `va-suite-status-log.md`, MCP ops → `mcp-server-ops.md`, `handoffs/INDEX.md` added, parallel-slice build rule recorded | cross-references retargeted repo-wide |
| `d9a2c57` | **pi coding agent via MCP stdio**: `.mcp.json` direct-stdio `hdaw` entry + `mcp.startupTimeoutMs: 0` (the 250 ms default could never cover the ~4 s cold handshake) | live: initialize OK, tools/list = 305, `engine_info` + `list_tracks` answered from the fresh %TEMP% copy in 3.9 s |
| `cbe5e1d` | `PsyDubFiveMinutes` — 700 beats @ 138 BPM, F minor, 10 tracks incl. folder + SONG_PLAN cells; exercises B3 ids through splices and save/load migration | gates: finalPeak 0.9 / RMS −20.5 dB, 307.3 s, zero legacy properties after round-trip; deliverable wav rendered |
| `bbc7f81` | psy_fm trap #15 retired (all paths deviceless-safe today) | docs: `psytrance-va-and-production.md` |
| `e632738` + `cd8d678` | **proxy state-path fix**: marshal-timeout now signals failure (result=0, not a fake empty state), parent handshake retries 3×/150 ms, latent UAF (`done`/`ep` captured by reference past the timeout path) fixed, `ProxyPipe` no longer poisons `connected=false` on a bounded-receive timeout | 3 deterministic failure-path tests (`__slowstate__`/`__slowstateset__`); 5× solo + 5× under parallel CPU load 10/10; `PluginIsolation.*` 49/49 |
| `da8cc92` → `8b06eee` → `e5c072c` → `431eb8d` → `8b769a0` | PsyDub musical fixes: off-key D naturals + F#-minor lead loop replaced, sampler roots retuned to measured pitch classes, full V5 sound-design rewrite (degree-based harmony, band ownership, velocity articulation, real half-time/breakdown), skank → WS#2 Lead FM Fx 2 (64% F-minor chroma), both lead tracks on the FM pluck | per-commit renders + note-level key audit |
| `a96f0f9` | corpus palette: registry restored after the trap-#4 clobber (23 pack entries re-registered), `select_psy_samples.py` ported off WSL-era `/mnt/` paths to native `%APPDATA%`, key-aware role ranking (F/Fm first, relative Ab, dominants; prefers one-shots, skips previews) | 15 F-minor-matched samples across packs; render verified |
| `63c6a88` | stem-audit diagnostic (solo-per-role 104 s exports → RMS audit.tsv) — all six roles AUDIBLE; **exposed the export-isolation bug** (§4a) | `.tmp_dnb_theme/psy_dub_stems/audit.tsv` |
| `58f355d` | export-isolation bug recorded in `docs/testing-mcp.md` | — |
| `bf4e8d6` | **PsyDub v3 — every tonal role on a core synth engine**: sub_synth (sub), psy_fm (growl), Osirus CLAP (stabs via CC0+PC40), NodalRed2x CLAP (lead via `load_nord_bank`), Vavra CLAP (pads via `apply_matrix_preset`); kick/hat stay sampler | per-engine `list_fx_params` non-empty (sub 33, psy_fm 33, Osirus 3086, NodalRed2x 362, Vavra 7557) + capture receipts + solo `verifyPart` rms > 0.001 for every engine; 5:07.35, finalPeak 0.902 |
| `b1114b8` | **PsyDub v3.1** — listen-pass fixes: skank echo dotted-1/8 fb 0.62 mix 0.5, lead echo fb 0.45, lead reverb room 0.75, SkankSweep automation lane on the stab LP filter (pid 500) rising 12 bars into each drop, bass midrange cut (cutoff 450 Hz, LP 1200), kick saturator + body EQ 82 Hz, growl Output 0.85/fb 0.50 | render verified: bass low/mid separation 10.1 dB, skank echo fills between 16ths, breakdown tail decay smoothed (level still low — honest partial, §4c) |

## 3. The v0.38.0 deliverable

`compositions/psy_dub_test_2026-09-25.wav` — the final listen-pass render of the
PsyDub v3.1 state. Regenerate: run `PsytranceComposition.PsyDubFiveMinutes`
(`build\hdaw_tests.exe --gtest_filter=PsytranceComposition.PsyDubFiveMinutes` —
needs the sample libraries on `E:\samples` and CLAP ROMs under
`C:\Program Files\Common Files\CLAP\`; set `HDAW_RENDER_WINDOW_WAIT_MS` /
`HDAW_EXPORT_BAKE_TIMEOUT_MS` as documented at the test entry — three CLAP
children boot sequentially and the default bake budget assumed ONE Virus warmup).
The committed test IS the source of truth; the wav is disposable output.

## 4. OPEN issues (each with repro + what a fix needs)

### a. Export-isolation bug: exports 3+ in one session ignore live tree changes

Deterministic. **Repro:** `PsytranceComposition.PsyDubFiveMinutes`'s stem-audit
pass — solo each role via `cmds.setTrackVolume(t, 0)` on the others, then
`startExport` a 104 s window; outputs land in
`.tmp_dnb_theme/psy_dub_stems/audit.tsv`. The first two stems isolate correctly;
stems 3+ re-render a FIXED tree state (deterministic md5s across runs).
`rebuildRoutingGraph()` + `drainPendingRoutingRebuild()` before each export do not
help. Raw `IDs::volume` tree writes never reach the bake at all — only the command
API does, and then only for the first two exports.

**Suspect:** the dedicated export domain's baked graph
(`ExportManager::usesDedicatedDomain`) caches its state after the first two uses;
the per-export invalidation that rebuilds it is missing or keyed wrongly.

**What a fix needs:** root-cause the dedicated-domain bake cache — find where the
second→third export stops receiving graph updates (likely a listener disconnected
or a bake-version counter not bumped), then add a regression test that exports
3+ times with an interleaved volume change and asserts each export reflects it.
Recorded in `docs/testing-mcp.md` ("OPEN BUG (2026-09-24)"). Until fixed: keep
multi-export workflows to ≤ 2 exports per engine session or re-create the engine
between exports.

**Impact:** stems/A-B renders silently render stale mixes after the second export.

### b. Registry clobber mechanism (trap #4)

An engine restart rewrites `%APPDATA%\HDAW\libraries\registry.json` from memory,
dropping externally-written entries — this session it dropped the 23 psy pack
entries, which were re-registered manually (`register_library.py`).

**Repro:** write registry entries via script while an engine is stopped, start an
engine, stop it → entries gone. Documented in
`docs/psytrance-va-and-production.md` trap #4 and `docs/psytrance-composition-guide.md`.

**What a fix needs:** registry **merge-on-save** instead of overwrite — the
FileLibraryManager should union its in-memory set with the on-disk registry at
save time (per-library-id merge, keeping externally-added entries), or at minimum
diff-and-preserve on restart. Keep the documented guidance (register via MCP
`add_library` when an engine may run) as the fallback.

### c. Breakdown tail still quiet

The lead-reverb room went 0.60 → 0.75 and wet 0.24 (`b1114b8`), and the tail
onset decay is smoothed — but the tail LEVEL itself still sits around the −50 dB
floor after the decay.

**Repro:** listen to the breakdown of
`compositions/psy_dub_test_2026-09-25.wav`; instrumented RMS measurement in the
test's tail window.

**What a fix needs (pick one):** a longer room on the breakdown-only reverb slot,
a dedicated tail clip (sustained pad/riser covering the decay), or a higher wet
level on the breakdown only (a global wet raise would wash the rest). This is a
musical-polish item, not a bug.

### d. B2b: stable-id acceptance on fx/automation/plugin tools (declined for now)

An agent holding only a `trackID` must still resolve it to an index for the
fx/automation/plugin tools. Extend the stable-id acceptance (B3 scope) to those
tool families when picked up. Detail: 09-24 handoff §8.2.

### e. AGENTS.md pending updates (another session owns the dirty file)

Once free, update: (1) the stale test baseline at AGENTS.md:320 (1865/277 → the
2026-09-24 reference 1971/285, 11 environmental failures); (2) the Feature-parity
section (§149-165) — ledger closed, route-addition recipe = shared `src/common/`
shaper + twin test + ledger regen; (3) the two stale `hardware-va-suite.md §9`
pointers (AGENTS.md:28 table row, AGENTS.md:180) → `docs/va-suite-status-log.md`;
(4) doc-table rows for the three split-out docs (`psytrance-va-and-production.md`,
`va-suite-status-log.md`, `handoffs/INDEX.md` — and the testing-mcp row trimmed,
plus a `mcp-server-ops.md` row). Source: 09-24 handoff §8.4.

### f. Composition-workflow lesson: drive the engine over MCP, freeze into a test last

Iterative music-making (listen → tweak → listen) rebuilt the test binary and
re-rendered 5 minutes of audio per note — hours lost. **Next time:** compose
iteratively by driving the running engine over MCP (now wired via stdio,
`d9a2c57`), save `.hdaw` checkpoints per section pass, and only freeze the final
state into a gtest at the end. The test is a regression pin, not a sketchpad.

## 5. Traps learned this session

1. **Parallel-slice build ownership** — concurrent ninja/cmake in one `build/`
   tree corrupts outputs (RC1109/C1083/LNK1136/LNK1168). Slices edit only; the
   orchestrator owns the one build. Detail: 09-24 handoff §8.3.1 +
   `docs/testing-mcp.md` → "Parallel agent slices".
2. **Automation values are normalized 0..1** on `setFxSlotParam`/automation lanes —
   the delay "division 4" and reverb "room 0.75" in PsyDub are normalized indices
   / fractions, not raw units. Check `list_fx_params` ranges before writing.
3. **Stale index after track splices → resolve by stable id** — never cache a
   positional track index across `moveTrack`/`removeTrack`; use `trackID`/the
   id→index map. B3 made ids durable; positions still shift.
4. **CLAP render budget env overrides** — 3 CLAP children boot sequentially but
   the bake budget assumed one Virus warmup; raise `HDAW_RENDER_WINDOW_WAIT_MS` /
   `HDAW_EXPORT_BAKE_TIMEOUT_MS` (defaults at `psytrance_composition_stress_test.cpp`
   entry) or multi-CLAP exports time out.
5. **Detached renders die** — an export kicked off from a detached/backgrounded
   process loses its render when the parent exits (the lazy-mcp/lifecycle class of
   loss: `docs/mcp-server-ops.md`). Run renders in one foreground call and wait.
