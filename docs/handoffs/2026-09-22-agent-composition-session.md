# Session handoff: 2026-09-22 — verification pipeline, sound corpus, crash forensics

Consolidated record of the 2026-09-22 sessions. Detailed technical records:
`2026-09-22-param-verity-pipeline.md` (plan + gates),
`2026-09-22-engine-heap-corruption-investigation.md` (the one real crash),
`2026-09-22-core-synth-isolated-children-silent.md` (silent children, RESOLVED),
`2026-09-22-patch-selection-and-sweep.md` (sweep status).

## Shipped (all committed)

| Commit | What |
|---|---|
| 75f1a4b | Dogfood fixes: fill_cells refits reused clips to new plan windows; movement reuses the param-bound lane; mix_report gate ignores finale/outro |
| 6b4f00c | **ParamVerity pipeline** — `param_verity` / `tone_verity` / `param_verity_corpus` (+ RPC twins): deterministic audibility sweeps (variance-floor verdicts), tone verification (envelope/AM/centroid/f0), batch corpus sweeps with sidecars |
| 55d35d6 | Phrase cells support `params.tileBeats` — melodic layers no longer skeletal over long sections |
| 8d5ab04 | Launcher: procdump kill-after-dump + `scripts/crash-diag.ps1` |
| 9f813c2 | **Silent-children root cause fixed**: bare `.clap` names now resolve against the scan database |
| c0c5dce | WER LocalDumps full-dump net + exit-code forensics script |
| 1901797 | **`select_patch`** — variety-aware cluster-stratified patch selection with per-role ledger |
| 2f40440 | Sweep: capture-wait (no more init-patch race) + prose-status parsing |

Plus: 6 libraries registered (amen breaks, Avalon one-shots, JE8086/microQ/Xenia/NL2x
patch corpora — 10k+ patches indexed), WER LocalDumps armed (full dumps for engine +
plugin host), AGENTS.md lessons 28-31.

## The verification pipeline (how to use it)

- **`param_verity`**: does parameter X audibly change the render? (variance-floor
  verdicts, ≥3× spread, silent = inconclusive)
- **`tone_verity`**: does the track render sound right? (envelope, AM rate,
  centroid trajectory, f0) — with optional expectation checks
- **`param_verity_corpus`**: sweep a whole slot; sidecar output
  (`hdaw.param.verity.corpus.v1`)
- **`select_patch`**: variety-aware patch selection from the corpora
- Full loop: select (variety) → apply_preset → **tone_verity** → corpus audit →
  fix → re-verify

## Track state

`compositions/mcp-dogfood-2026-09-22-5min-remix.hdaw` — 10 tracks / 41 clips,
182-bar structure, tiled melodic cells, register-budgeted bass, sampled
percussion (kick/hats/clap from Avalon pack, real 172 BPM amen on the break,
transposed −3 st), Vavra bass with Bass Glue chain, per-role chains.
Renders: `v4-final` (validated byte-identical post-crash), `v5-revoiced`
(Vavra bass), **`v6-sampled`** (sampled percussion, current).

## The five findings that matter

1. **Silent isolated children = unresolved bare plugin names** (lesson 28).
   Every "the engine doesn't work" symptom (0 params, stub state, silence)
   traced to `resolveIdentifierToPath` skipping resolution for `.clap`-suffixed
   identifiers. Fixed + regression-tested; `loadPluginByPath` now logs the
   whole load path.
2. **Exit code 0x2A (42) ≠ crash** (lesson 29). The MCP wrapper restarts the
   engine on its 10 s call timeout — wiping unsaved state. Read procdump's
   "Process Exit" line first; batch small; save immediately; never blind-retry
   a timed-out call.
3. **Melodic phrase cells were skeletal over long sections** — one sparse pass
   per 128-beat section while rhythm cells tiled. `params.tileBeats` fixes it
   (lesson-adjacent: the corpus renderers' density semantics differ per source).
4. **Bulk child removal storms the listener** (lesson 30): swap the container
   node, never iterate removeChild under a listener-heavy parent.
5. **Deterministic patch ranking = no variety** (lesson 31): `select_patch` +
   clusters + ledger; dsp-vector sweep upgrades clustering from filename
   keywords to real timbre.

## Open items (next session)

1. **Full microQ dsp sweep running** — `timbre-lib/sweep_out/vavra_full/`
   (~528 patches, sidecars write per-patch to `D:\pdf\rhythm-lab.com_waldorf_micro_q\`).
   When done: `scan_library {id:'0a52a2e1711f'}` →
   `cluster_library {method:'hybrid'}` → timbre-stratified clusters.
2. **Finish the re-voice**: T3 arp → JE8086 + patch; T4 pad → Xenia + patch;
   T6 hook → OsTIrus + ROM preset; T7 growl → NodalRed2x + FAT WAH bank
   (all blocked earlier by the silent-children bug, now unblocked). Register
   budgets + select_patch per role.
3. **Mix pass**: kick prominence 0.60 → 0.19 after the Vavra bass landed
   (120–320 Hz overlap) — sidechain/bass tuning; modulation-taming pass
   (some lanes are theatrical: growl amDepth 18).
4. **Heap-corruption root cause** still open (one occurrence, dumped):
   `scripts/heap-diag.ps1 pageheap-on` + replay the fill sequence if it recurs.
5. Sweep TODO: the probe phrase for sustained roles should velocity/multiply
   vary (single held note under-tests plucky patches); analyzer silence-gate
   zeroes spectral features on mostly-silent renders (centroid 0) — gate on
   active frames instead.
