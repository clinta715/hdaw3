# Handoff: end-to-end composition demo (sweep → libraries → cluster/search → markov tracks)

**Date:** 2026-09-04
**Status:** demo complete, process documented
**Branch:** feat/fx-presets-saturator (clean, pushed)

## Goal

"Put it all together": rescan a subset of Virus + Dexed/DX7 patches and psytrance
sample libraries + MIDIs in `e:\samples`, add them as HDAW libraries, cluster and
search for related sounds, then use the Markov generator and the external
generation script to create example tracks.

## What was done (the pipeline)

1. **Swept patch subsets into searchable sidecars** (each patch rendered through
   the engine, DSP-measured, and written as a sidecar with a `dsp` vector +
   description):
   - DX7: `py -3.14 timbre-lib/sweep_dx7_patches.py --role lead --shuffle 42 --limit 30 ... --sidecars`
   - Virus: `... --engine sub_synth --dir "D:\pdf\Virus Presets" --role lead --shuffle 17 --limit 20 --sidecars`
   - Sidecars land next to sources; the sweep also writes `dsp` (the 20-key
     `kDspFeatureKeys` vector from `timbre.extract`) and refreshes descriptions.
   - Note: Virus TI banks with bad checksums and non-DX7 `.syx` files error
     non-fatally (recorded in the report).
2. **Built focused demo library dirs** (`timbre-lib/demo_libs/{dx7,virus}`): copied
   the swept patches + their sidecars so the libraries are clean and small
   (28 DX7, 12 Virus).
3. **Added + scanned 5 libraries** via a new stdio composition driver
   (`timbre-lib/demo_compose.py`), which spawns `build\HDAW_headless.exe --mcp-stdio`
   and speaks JSON-RPC `tools/call` (the hdaw MCP tools were disconnected after the
   earlier engine-restart crash, so the driver is the reliable engine interface):
   - patch: DX7 Demo, Virus Demo
   - audio: `E:\samples\Prism - Psytrance - Zenhiser`, `Zenhiser Studio Essentials - Psytrance`
   - midi: `E:\samples\Antinomy Psytrance Sounds Vol.2 WAV MiDi-ARCADiA`
4. **Clustered** (`cluster_library`, method `dsp`, k=6, over the demo libs):
   the patch-aware clustering landed DX7 brass by f0/brightness (c1/c3/c4/c5),
   the Virus wc_olo_garb bank voices together at similarity 0.94 (c3), and the
   Prism/Zenhiser sample drum beats/basslines by airiness/darkness (c2/c4/c6).
   Cross-library patch+sample clustering works; entries without a `dsp` sidecar
   go to `unassigned`.
5. **Searched** (`search_library`, all libraries): "dark" → 50 (kicks/basslines),
   "gritty" → 50, "acid" → 32 (APTS acid arps), "tonal"/"bright"/"kick" → hits.
   Multi-word queries work ("gritty dark bass").
6. **Generated example tracks**:
   - `timbre-lib/demo_libs/markov_demo.wav` — `generate_psytrance_markov`
     (64 bars, seed 1337): 4 clips / 1784 notes on palette tracks; DX7 patches
     loaded onto arp (ACCORDION) and stab (*Trombone). 131 s, rms 0.27.
   - `timbre-lib/demo_libs/markov_demo2.wav` — same driver, 48 bars, seed 2026:
     1344 notes. 99 s, rms 0.26.
   - `generate_psytrance.py --engine-bin build\HDAW_headless.exe` — the external
     script now works over stdio (added `--engine-bin` + UTF-8 stdout); built a
     full 6-track "Dark" arrangement (Kick 64, Bass 87, FM Bass 32, Hats 60,
     Lead 192, Pad 24 notes). It builds in the engine but does not export.

## Files / artifacts

- `timbre-lib/demo_compose.py` (new) — stdio composition driver: `--phase setup|cluster|search|compose`, `--seed/--bars/--out`, `--engine-bin`.
- `timbre-lib/generate_psytrance.py` (modified) — `--engine-bin` stdio mode + UTF-8 stdout (the old WS `tools/call` path does not exist on the current engine).
- `timbre-lib/demo_libs/` — demo library dirs, `.lib_ids.json`, the two exported WAVs (demo artifacts).
- `timbre-lib/sweep_out/demo_dx7`, `demo_virus` — sweep reports + probe WAVs (gitignored).
- Swept sidecars were written next to their sources in `d:\pdf\dexed presets` / `D:\pdf\Virus Presets` (the patch library scan reads them).

## How to reproduce

```
py -3.14 timbre-lib/demo_compose.py --phase setup    # add+scan the 5 libraries
py -3.14 timbre-lib/demo_compose.py --phase cluster  # cluster_library dsp, k=6
py -3.14 timbre-lib/demo_compose.py --phase search   # search_library queries
py -3.14 timbre-lib/demo_compose.py --phase compose --seed 1337 --bars 64 --out markov_demo.wav
py -3.14 timbre-lib/generate_psytrance.py --engine-bin build\HDAW_headless.exe --style dark --bars 32
```

## Notes / follow-ups

- Clustering comparability: patch `dsp` vectors are probe-context-specific —
  cluster only patches swept with the same role/seed/window/bpm (the demo swept
  DX7 and Virus both with `--role lead`).
- `generate_psytrance_markov` reported `automationsSkipped` for all volume fades
  in the demo (advisory; the Volume lanes are fader-authoritative). Investigate
  if generated fades matter.
- The demo's kick/hat use `fm_synth` defaults (tones, not kicks); a real
  production would map kick/hat to sampler with drum samples from `e:\samples`.
- The `hdaw` MCP tools were down for this session (engine-restart crash earlier);
  reconnect opencode's `hdaw` server (`/mcp` or session restart) to drive the
  same flow from the interactive MCP tools instead of the stdio driver.
- `timbre-lib/demo_libs/` is demo output — worth adding to `.gitignore` if kept.