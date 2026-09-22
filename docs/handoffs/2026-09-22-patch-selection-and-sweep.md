# Handoff: patch-selection tooling + dsp sweep status (2026-09-22)

## Shipped this session

1. **`select_patch` MCP tool + `library.selectPatch` RPC** (commit pending):
   `FileLibraryManager::selectPatch` — cluster-stratified, seeded, exclusion-aware
   patch picker. Role-filtered clusters, per-role recently-used ledger
   (`AppData/HDAW/libraries/patch_selection_ledger.json`, 32-entry ring, auto-cycle
   reset on pool exhaustion), seeded RNG (0 = time-seeded). Regression coverage:
   parity ratchet (`select_patch → library.selectPatch` mapped) + live verification
   (three seeds → three DISTINCT microQ bass patches, ledger 1→2→3; applied one via
   `apply_preset` → tone_verity PASS).
2. **Sweep extension** (`timbre-lib/sweep_dx7_patches.py`): new `vavra_plugin`
   engine profile — isolated Vavra child as the probe host, patches via
   `apply_preset` (F0 3E Waldorf dumps), sidecars `.vavra.json` merge (category
   preserved + dsp vector added). Syntax verified.

## OPEN: sweep engine spawn fails

`sweep_dx7_patches.py --engine vavra_plugin ...` fails with
`engine not reachable: engine did not become ready: None` — the sweep spawns its
OWN engine instance (`build\HDAW_headless.exe --mcp-stdio`) and never sees the
ready marker. The live wrapper engine (`HDAW_headless_mcp.exe` via mcp-launch)
works fine, so the child-spawn path is sound. Debug order:

1. `resolve_engine_bin()` (sweep_dx7_patches.py:345) — which exe does it pick?
   Does `HDAW_headless.exe --mcp-stdio` emit the same READY banner the sweep's
   `_wait_ready` expects? Run it manually and compare stdout with
   `mcp-launch.bat`'s engine (which starts via `mcp-launch-capture.ps1` and
   prints nothing to stdout by contract).
2. `_wait_ready` (sweep_dx7_patches.py:396) — expected marker string vs actual.
3. Possible cause: the ready handshake expects a banner the plain
   `HDAW_headless.exe` prints only under specific env (or the sweep's spawn
   raced the live engine's exclusive audio device).

## Once the spawn works

```bash
py -3 timbre-lib/sweep_dx7_patches.py --engine vavra_plugin --role bass \
  --dir "D:\pdf\rhythm-lab.com_waldorf_micro_q" --bank Bass --limit 500 \
  --window 3 --sidecars --out timbre-lib/sweep_out/vavra_bass \
  --report timbre-lib/sweep_out/vavra_bass/report.json
```

- ~528 microQ patches ≈ 45-60 min. Sidecars `.vavra.json` merge (category/pack
  preserved, 20-key dsp vector added — matches `kDspFeatureKeys` in
  LibraryClusterer.h).
- Then `scan_library {id:'0a52a2e1711f'}` → dsp vectors ingested →
  `cluster_library {method:'dsp'|'hybrid'}` clusters on REAL timbre.
- Then `select_patch` picks are timbre-stratified, not filename-keyword-stratified.

## The other thing today's session proved

Deterministic top-1 ranking (`related_samples`) is why every session used the
same patches. `select_patch` + ledger + clusters is the variety mechanism. The
full agentic loop is now: corpus census → cluster → seeded variety pick →
apply_preset → tone_verity verification → ledger exclusion.
