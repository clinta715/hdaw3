# Handoff — agent-workflow currency + VA-suite verification + `ion_rift`

**Date:** 2026-09-29/30 · **Continues:** [`2026-09-28-agent-mechanization-shipped.md`](2026-09-28-agent-mechanization-shipped.md)
**Scope:** (A) brought the agentic song-composition workflow up to date with the 2026-09-28
mechanization release; (B) verified the hardware VA suite end-to-end; (C) composed, mixed and
rendered one new track (`ion_rift`) to a **passing** release verdict.
**Engine source touched: NONE.** Every change is docs/tooling or composition state.

## 1. State

- **Track:** `compositions/ion_rift/` — `ion_rift.wav` (80.4 MB, 48 kHz/24-bit, 4:53),
  `ion_rift.hdaw`, `brief.json`, `cells-plan.json`, `session-report.md`.
  `mix_verdict` → **`ok:true`, zero issues**, all 7 gates (audible, clipping, introBlast,
  loudness, modulation, structure, targets) green.
- **Workflow docs:** 16 files updated (skill tree + composition docs + `AGENTS.md` + `README.md`
  + `docs/mcp-server-ops.md` + 4 VA-suite docs).
- **New tooling:** `scripts/hdaw_mcp_http.py` (HTTP-MCP CLI for DSH-hosted agents; the stdio
  `scripts/mcp_call.py` spawns a fresh engine and must not be used by roles).

## 2. The three findings that mattered

1. **"Deviceless" is an unselected audio device, not a property of the headless engine.**
   With `output:""`, `processBlock` never runs (`LiveClockDiag … dBlocks=0 devState=none`), the
   live graph has **zero tracks**, and every live-slot tool fails with **`track not found: N`**
   for tracks `list_tracks` lists. `ensureLiveRouting` does not rescue it (`rebuildRoutingGraph()`
   no-ops without a RoutingManager — `src/engine/AudioEngine.cpp:1877-1909`). One call —
   `set_audio_output_device` — restores param enumeration, patch loading and capture.
   Documented in `docs/skills/psy-song-session/reference.md` § "Deviceless engine" +
   `docs/mcp-server-ops.md`.
2. **The stable `trackID` is 1-BASED** (`allocateTrackID()` = max+1, floor 1 —
   `src/model/ProjectModel.cpp:157-170`) and **no read tool reports it**: `list_tracks`'s `id` is
   the positional index (`McpTools_Read.cpp:73`), `snapshot_project` omits `trackID`
   (`McpTools_ProjectSaveLoad.cpp:136-163`). Passing a positional index in the `trackID` key
   silently addresses the **previous** track. Safe rule: positional `trackId`/`trackIndex` unless
   you hold the id from an `add_track*` return.
3. **Master gain is POST-master-FX.** Raising `set_master_gain` clips rather than adding loudness
   (measured: gain 1.6 → rms 0.46, 22.7% FS frames). Loudness must be driven INTO the limiter:
   master **EQ slot 0 (pre-limiter) as drive**, **limiter slot 1** holding the ceiling
   (0.97 → peak locked, 0 ceiling hits), `set_master_gain` as output trim only.

## 3. VA suite verification (all engines boot, render and load patches)

Boot + offline render (temp-probe `audition_plugin`, `style:"Lead"`):

| engine | rms | peak | audible |
|---|---|---|---|
| OsTIrus (Virus TI) | 0.0286 | 0.3056 | ✅ |
| Osirus (Virus C) | 0.0327 | 0.2563 | ✅ |
| Vavra (microQ) | 0.0083 | 0.0292 | ✅ |
| Xenia (Microwave XT) | 0.0260 | 0.3240 | ✅ |
| NodalRed2x (NL2x) | 0.0379 | 0.2714 | ✅ |
| JE8086 (JP-8080) | 0.0249 | 0.1014 | ✅ |
| Dexed (retired, still loads) | 0.0249 | 0.1637 | ✅ |
| Wavetable | 0.0176 | 0.1186 | ✅ |

All boot ROMs are present in `C:\Program Files\Common Files\CLAP` (`virus_*.bin`,
`Ti2 firmware.bin`, `firmware.bin`, `nord_lead_2x.bin`, `mq_2_23.mid` + `lower/upper_Am29F010.bin`,
`mw2_xt_xtk_sys_v233.mid` + 50 `mw2v*.mid`, `jp8080_v1.04.bin`, `wa/wb/wc_r1520939x.bin`).

Patch load + capture (live slot → `IDs::pluginState`, so it survives into renders):
OsTIrus `queued bank=0 program=0` → `stateBytes=263406`; Osirus → `34245`; JE8086 `2 DT1` →
`5320`; Vavra `392-byte Waldorf dump` → `832`; Xenia `{"bytes":265,"route":"device_dump"}`
(capture `unchanged` **by design**); NodalRed2x `110 sysex dumps` (capture `unchanged`
**by design**).

**Route constraints measured:** `apply_preset` sends the Access SysEx header to the INTERNAL
`sub_synth`, not to a plugin slot; `send_fx_midi`'s contract states **"plugin slots ignore
injected SysEx"** — so a real Virus takes **ROM programs (CC0+PC) only**, plus `set_fx_param`
writes (which persist to `appliedParamOverrides` and reach renders).

**End-to-end proof for the track's bass:** probe clip → Virus on = rms 0.106, bass-dominant
(bass 490.9 vs high 57.2); Virus bypassed = **digital silence**.

## 4. Traps found in the docs (fixed)

- `set_track_volume` is not a tool (×3 docs) → `set_track`; `lock` is not a tool →
  `set_cell {locked:true}`; `load_dexed_cartridge` removed 2026-09-14; **`poll_fx_capture` never
  existed** (7 live docs) → `get_fx_capture_status`.
- `SKILL.md` claimed a wrapper timeout yields **exit 42** in two places — 42 is the deliberate
  `engine_restart` tool ONLY; a timeout relaunches onto a fresh empty project.
- `apply_song_brief` reads **only** `sections[].type`; a `kind` key is silently ignored
  (`AudioEngineCommands_Song.cpp:425`).
- `scripts/hdaw_mcp_http.py` crashed (exit 1, no output) on any non-ASCII payload (cp1252 stdout)
  — fixed at source.
- PowerShell cannot parse `tools/list` with plain `ConvertFrom-Json` (68 of 317 tools declare
  both `trackID` and `trackId`) — use `-AsHashtable`.

## 5. Engine incidents (no source change)

- Engine exited **cleanly (exit 0)** at 08:20:55 mid-session, right after a fresh 2 GB
  `hdaw_plugin_host_processBlock hung for 1s.dmp` (Virus child under render load). Restarted with
  `HDAW_headless_mcp.exe --mcp-http`; project reloaded from the checkpoint. Two 1.5–2 GB watchdog
  dumps were produced during the session (disk hygiene: `scripts/cleanup-stale.ps1`).
- `audition_plugin`'s `style` argument is a **PhraseGenerator style name** (`"Lead"`, not `"lead"`).

## 6. OPEN

1. **The final master peaks at 0.53 (−5.5 dBFS)** — the honest consequence of the `masterRms 0.18`
   brief target on this dense material. Normalise on delivery for a hotter master, or lower the
   brief target next time.
2. **`begin_batch` remains stdio-only**, so HTTP/CLI agents cannot batch; the Arranger phase must
   split into small groups and checkpoint between them (documented).
3. The `down` downlifter cell was removed from the intro (it was the t=0 intro blast); if a
   downlifter is wanted there it must not land on beat 0.
4. The workflow docs now carry the device-first rule; a future session should call
   `set_audio_output_device` as part of setup rather than discovering the failure.
