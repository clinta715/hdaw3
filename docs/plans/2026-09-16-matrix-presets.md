# Plan: Per-Plugin MATRIX PRESETS (handoff 2026-09-16)

## Goal
Execute `docs/handoffs/2026-09-16-matrix-presets-handoff.md`: harvest the FX /
modulation-matrix configurations that exist inside each core synth's own patch
corpus, turn them into named matrix presets (`hdaw.matrix.preset.v1`), and wire
the FX workflow so agents check a plugin's matrix presets FIRST.

## Session split (G2 reality check)
G2's apply/ear verification requires the live Windows engine + isolated plugin
children. This plan's session delivers Phases A-C (pure tooling + docs).
Phase D (live apply/verify + listening curation) is the follow-up session with
the engine up; presets shipped before it are marked unverified in their file.

## Success Gates (this session)
- [ ] G1: >=10 presets per engine with evidence trail (counts + example patches)
      for the nameable engines (je8086, virus, nodalred2x, xenia); vavra
      harvested offset-keyed and flagged `appliesVia: "unmapped-pending-offset-map"`.
- [ ] G5: vavra dump-offset->name mapping either solved with file:line citations
      + a machine-readable map for the FX/matrix subset, or a deferral memo with
      the concrete blocker.
- [ ] G3 (mandatory doc half): `docs/skills/psy-song-session/roles/fx-automation-engineer.md`
      and `docs/hardware-va-suite.md` name the matrix-preset lookup step. The
      optional MCP tools (`list_matrix_presets`/`apply_matrix_preset`) are
      DEFERRED to a follow-up (C++ + RPC + gtest churn; doc hook is the G3 half
      that ships now).
- [ ] G4: `git status` shows ONLY `timbre-lib/` + `docs/` changes — zero engine,
      DSP, RPC, ValueTree surface.
- [ ] pytest suite for the harvester passes; emitted JSON is byte-stable on
      re-run and contains no absolute paths.

## Dependency Map
- Blast radius: none — Python tooling + markdown docs; no C++ graph nodes touched.
- Upstream: sidecar libraries (je8086/vavra/xenia/nl2x/virus) written by the
  existing `*_patch.py` decoders; vocabularies from parameterDescriptions_*.json.
- Downstream: FX role doc consumers (song sessions), future MCP tools, Phase D
  apply scripts.
- Projections affected: none. SPSC paths: none. Community boundaries: none.
- God nodes: none.

## Pitfall Gates Triggered
- Gate 2 (unimplemented path): N/A this pass — no new RPC/command. The deferred
  MCP tool MUST NOT be declared without its full path + gtest (follow-up).
- JSON5 vocabularies resist strict JSON parse -> reuse the regex
  `load_vocabulary` approach from `timbre-lib/harvest_fx_presets.py`.
- Bank sidecars carry ONE PARAM SET PER PATCH (`params` dict), not per file —
  count configs per patch (the je8086/xenia trap).
- Naming/curation is not automatic: cluster frequency != quality (handoff trap 8).
  Phase D ear-pass is the quality gate; this pass only clusters + names.
- G4: no engine/DSP change (handoff G4); `apply` scripts may only emit MCP call
  recipes, never link the engine.

## Steps
1. (A) `timbre-lib/harvest_matrix_presets.py` + `test_harvest_matrix_presets.py`;
   run over the real roots; emit `timbre-lib/matrix_presets/<engine>.json`.
2. (B) microQ offset mapping research (read-only on gearmulator/retromulator
   sources) -> `timbre-lib/matrix_presets/vavra-offset-map.md` (+ JSON map) or
   blocker memo.
3. (C) Docs hook in fx-automation-engineer.md + hardware-va-suite.md (lookup
   step, file format, apply-path table, Phase D verification checklist).
4. (D, follow-up session) live apply/verify per engine + listening curation;
   optional MCP tools with gtest.

## Live-engine session findings (2026-09-16, engine build/HDAW_headless.exe md5 462dddf2a3a7, pids 25400/27308)

The demo session ran the pipeline against the live MCP engine (138 BPM, F, 24-bar plan; JE8086 lead + Osirus bass + NodalRed2x pad; render + mix report + save all succeeded: compositions/matrix-presets-demo.{wav,hdaw3}). Three engine-surface bugs were uncovered — all reproducible, all blocking matrix-preset APPLY paths (the harvest side is unaffected):

### BUG A (blocker): isolated-CLAP plugins expose ZERO host params
`list_fx_params` returns {"params":[]} for every isolated plugin slot (JE8086 should expose 461 — the handoff's one nameable device; Osirus/NodalRed2x/Vavra behave the same). Internal FX slots list fine (control: a delay slot returns its full table), so the command surface works; only the isolated-child param bridge is empty. Consequence: `set_fx_param` fails with "unknown paramName" / "param index out of range" — the JE8086 name→index apply path (handoff deliverable 3, the ONLY parameter-level path) is unusable. `toggle_plugin_editor` does not populate the cache. NOT a stale binary: the %TEMP% engine copy's md5 equals build/HDAW_headless.exe (462dddf2a3a7). NOT the track-creation path (add_instrument_part and add_track_with_fx slots behave identically). Suggested first probes: (a) param-cache population timing in the proxy-child handshake (queried before the child publishes params?); (b) does HDAW_NO_PLUGIN_ISOLATION=1 restore params (in-process path)?

### BUG B (blocker): device loaders cannot resolve any track
`load_virus_preset` / `load_nord_bank` fail with "track not found: <id>" for EVERY valid id (0..3), while `add_fx` / `set_fx_param` / `verify_part` resolve the same ids fine. The loaders evidently use a different (live-graph / plugin-registry) lookup than the command layer — consistent with the deferred-rebuild seam (AGENTS.md lesson 9) but persistent across transport/editor activity. Blocks the verified Nord and Virus apply paths (handoff deliverable 3).

### BUG C (intermittent crash): export with isolated children — plugin-host processBlock hang killed the engine mid-export
`hdaw_plugin_host_processBlock hung for 1s.dmp` (323 MB) in %TEMP%; the engine pid died during the first export attempt (ExportDebug shows healthy rendering to ~61 s; FxStateRead for slot 1 completed moments before). A retry on a fresh engine completed cleanly — intermittent, consistent with the mutation/hang race family (postmortem lessons 14/21). Treat every export as may-fail; the MCP client must relaunch and re-save. Also noted: `remove_track` was the call in flight when the server died the first time (unconfirmed as the trigger — the dump implicates the plugin host — but avoid removing plugin tracks mid-session until Bug C is root-caused).

### Recommended HDAW revisions (facilitating the matrix-preset workflow)
- R1 (fixes Bug A): publish the isolated child's host-param table eagerly at slot READY; make `list_fx_params` re-query instead of serving a cached empty.
- R2 (fixes Bug B): route load_virus_preset / load_nord_bank track resolution through the same command-layer lookup as set_fx_param (or drain pending routing first).
- R3: `list_matrix_presets` / `apply_matrix_preset` MCP+RPC tools reading timbre-lib/matrix_presets (or an embedded copy) — with gtests (G3's tool half).
- R4: `load_je8086_bank` MCP tool (already planned phase-2 in the je8086 pipeline).
- R5: emit `paramIndex` alongside names in matrix-preset sheets, and align the je8086 decoder enum names with the plugin's parameterDescriptions names (apply scripts then survive name drift; even with Bug A fixed, several preset names would not match today).
- R6: matrix-preset sheets should carry source file + program for patch-level engines (vavra/xenia/nord examples name patches but not the bank file to load).
- R7: the Virus sidecar decoder must carry per-patch FX/mod values (today: tone params + a constant presence list only), or the virus corpus stays at 1 preset (shortfall recorded in virus.json).
- R8: xenia vocabulary index-space collision (the patch section shares indices with later global sections; first-wins plus the EffectParamA→DelayTime alias) — make the decoder section-aware to prevent latent mislabels.
- R9: vavra FX sub-param aliases (bytes 137–150 / 153–166) need verification against FXType values before alias-suffixed naming is safe.
- R10: document the WSL pytest invocation (PYTHONPATH=/tmp/hdawpylib) or install python3-pytest system-wide — recorded in timbre-lib/README.md, but a repo-level convention would stop the next session re-deriving it.

### Demo verification evidence (this session)
- verify_part A/B on the lead (delay movement applied via the working internal-FX path): soloRms 0.04336→0.04387, soloPeak 0.1908→0.1752 (audible delta, non-clipping).
- Export: 42 s, 48 kHz/24-bit, peak 0.278, rms 0.0212, bands present, no clipping (mix_report measurementSuspicious=false).
- Blocked by the bugs above and therefore NOT demonstrated live: JE8086 matrix params via set_fx_param, Virus CC/PC preset load, Nord bank load. Their expected effects are documented per-preset in the shipped sheets, which all carry unverified:true until a post-fix engine pass re-runs them.
