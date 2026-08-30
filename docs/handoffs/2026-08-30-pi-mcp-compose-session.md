# Handoff — Pi-hosted psytrance composition via hdaw MCP + launcher fix (2026-08-30)

Session: first composition run hosted through the **pi coding agent** (pi-mcp-adapter),
not opencode. Root-caused and fixed a broken `mcp-launch.bat` (the hdaw MCP server had
been dead for every MCP host since commit 667f108), then composed a full F-minor
psytrance track end-to-end via the hdaw MCP over the pi adapter (202 tools) using the
verified recipes from `docs/psytrance-composition-guide.md` + the engine's clustering
features for palette selection. This handoff captures (1) the launcher fix, (2) recipe
deltas verified over MCP, (3) the MCP feature gaps / contract friction that would
replace the remaining manual work, (4) every bug hit with status.

## 0. Session inventory

- **Launcher fix (committed):** `ba21483` — `mcp-launch.bat` cmd quote-pairing abort →
  crash-capture logic extracted to new `mcp-launch-capture.ps1` (see B1). This un-broke
  the hdaw MCP server for BOTH hosts (opencode config `~/.opencode/opencode.json` and pi
  config `~/.config/mcp/mcp.json` share the same `command: …mcp-launch.bat`).
- Deliverables in `.tmp_dnb_theme/psytrance_cluster_fmin/` (140 BPM, 400 beats ≈ 2:54,
  48 kHz/24-bit WAV + reloadable .hdaw):
  - `psytrance_cluster_fmin.wav/.hdaw` — 8 tracks: PsyKick, PsyBass, PsyHats (offbeat
    clap), PsyStabs, PsyArp (+ breakdown melody), PsyPads, PsyFx (riser), PsyDown
    (tonal-reverse downlifter); 2,686 notes over 8 clips.
  - `canary.wav` — intermediate mix probe (master 0.25).
- Palette derived from **clustering**: `cluster_library` on Antinomy Vol.2 (k=8, hybrid)
  cleanly separated role clusters — c1 low-end (kick+bass families), c2 clap+riser,
  c4 stab, c7 atmosphere pad, c8 tonal-reverse downlifter — with the 12-key bass
  multisamples shunted to `unassigned` as redundant. Saved preset `cp_d323bc93`.

## 1. Launcher root cause + fix (the session's headline bug)

- Commit `667f108` (2026-08-30 08:41 CDT) rewrote the crash-capture block of
  `mcp-launch.bat` as an inline `powershell -Command "..."` containing **unescaped inner
  double quotes** (`"$($engine.Id)"`, `"$dir\procdump.log"`). cmd.exe has no `\"`
  escape; quote pairing broke, and cmd aborted the whole batch with
  `… was unexpected at this time` **before** reaching `"%DST%" --mcp-stdio`. No engine →
  every MCP host saw `hdaw (failed)`. `HDAW_NO_CRASH_CAPTURE=1` could not bypass it
  (cmd must scan the `if(...)` block body for its closing paren regardless).
- Evidence trail that mattered: git blame pinned the breaking commit exactly; the last
  healthy engine boot (pid 16596, 08:38 CDT) predated 667f108 by 3 minutes — the gods of
  "started working, then didn't" are just a commit landing mid-session.
- **Fix:** capture logic extracted to `mcp-launch-capture.ps1` (repo root), invoked via
  `powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mcp-launch-capture.ps1"` —
  zero cmd quoting hazards. Behavior preserved: engine started with inherited stdio
  (stdout stays pure JSON-RPC), procdump attached as watcher with banner redirected to
  `%TEMP%\hdaw_crash_captures\engine_<rand>\procdump.log/.err`, status via
  `[Console]::Error`, exit-code propagation. Also restored the mini-dump flag 667f108
  orphaned (`DUMPFLAGS` was set but unused; `-ma` hard-coded → the ps1 honors
  `HDAW_CRASH_DUMP_TYPE=mini` → `-mm`).
- Verified: direct `cmd /c` run (exit 0, clean boot), adapter-equivalent cross-spawn
  probe (initialize → `serverInfo hdaw 0.25.0`, stdout pure JSON), live procdump
  command line showing `-mm -e -g <pid>` under mini, opt-out path clean, and the **live
  pi adapter connect** (202 tools; `hdaw_get_transport` round-trip).

## 2. Psytrance recipe deltas verified over MCP (fold into the guide)

Everything from `docs/psytrance-composition-guide.md` held up when driven over MCP.
New specifics worth recording:

- **Cluster-driven palette flow (replaces TSV-first):** `cluster_library {libraryIds,
  k, saveAs, method:"hybrid"}` → read cluster membership/labels → build the kit straight
  from member paths. Cluster labels were semantically useful (c1 "dark" = low-end,
  c2 "bright" = clap/riser, c7 "pad", c8 "soft" = tonal reverse), and sample-key hints
  in filenames (`…_C.wav`, `…_F#_9.wav`) fed natural-pitch rootNotes.
- **MCP tool-name map (the contract surface we kept re-deriving):**
  - `add_midi_clip {trackId, start, length, name}` → `"clipId=N"` text; clips are beats.
  - `add_notes {clipId, notes[{start,duration,pitch,velocity}], relative}` — note starts
    are **clip-local** by default; `relative:false` accepts timeline-absolute beats
    (clip at start 0 ⇒ identical). Returns full `noteIds` array — big.
  - `sampler_set_sample {trackId, slotIndex, filePath, rootNote}`; sampler must be
    slot 0 (add_fx first). All sampler slots on a track share the MIDI stream.
  - `set_internal_fx_param {trackId, slotIndex, paramIndex, value}` — REAL units
    (EQ Frequency Hz, Threshold/Ratio linear dB/ratio); `list_fx_params` reveals
    names/indices/min/max/default but **requires slotIndex** and returns no current value.
  - LFO surface exists and works: `add_lfo {trackId}` → `"lfoIndex"`, then
    `set_lfo_param {trackId, lfoIndex, param, value}` (waveform 0/1/2, rate, rateSync,
    depth, bipolar, phaseOffset, targetParamID: 1=volume; FX = 100+slot*100+paramIndex).
  - Automation: `add_automation_lane {trackId, laneName, paramID}`; points via
    `set_automation_points {trackId, lane, points[{time,value}], mode:"replace"}`
    (key is `time`, in beats); `set_automation_enabled {trackId, lane, enabled}`.
    Lanes are **disabled by default**; the per-track built-in Volume lane (paramID 1)
    blocks adding a second paramID-1 lane. Volume lane values = raw gain; FX lanes
    normalized 0..1.
  - `mix_report {filePath, bpm, sections[{name,start,end}]}` — peak/RMS overall +
    per-section, 4-band energy, `pumpDepth`, `kickProminence` (band cutoffs sub 40-110,
    bass 90-300, body 300-2000, high >6000 — do NOT match the guide §6 numpy numbers).
- **Numbers from this track** (reference target set for one more verifiable render):
  canary at master 0.25 → peak 0.407 ⇒ truePeak ≈ 1.63 ⇒ final `min(0.90/1.63,1.0)`
  = 0.55; final peak 0.852, RMS arc intro .030 / build .082 / mainA .095 / mini **.023**
  / mainB .096 / breakdown .040 / finale **.113**; `pumpDepth` 0.74, `kickProminence`
  0.74. Sub-band power high (kick+bass HW), body/high present.
- **Fader set that rendered safely at master 0.55:** kick .85, bass .80, hats 1.0,
  stabs .95, arp .90, pads .75, riser .90, down .90.

## 3. HDAW feature gaps — what would eliminate the remaining manual work

Prioritized; each removes a step done BY HAND this session.

### P1 (workflow-defining)
1. **MCP composition generator for the verified psytrance recipe.** The stress-test
   recipe (`psytrance_composition_stress_test.cpp`, FullProduction*) exists server-side,
   but composing over MCP means hand-computing 2,686 notes client-side. Add a tool like
   `generate_psytrance {bpm, key, sections, paletteTrackIds, density}` implementing the
   guide §4 grammar (key-disciplined scales, offbeat bass, arp glints, stab voicings,
   breakdown melody, riser/downlifter schedule) → 1-2 calls instead of a JS scoring
   layer. Synergizes with the jungle handoff's P2.7/2.8 (section-aware arrangement +
   scale helpers): a shared `composition` generator with genre grammars would serve both.
2. **Multi-sampler note routing.** All sampler slots on a track share the MIDI stream,
   so riser + downlifter (and any FX bank) each need their own track. A per-slot key
   range (note→slot map) would collapse FX palettes into one track and cut the track
   count/inventory noise.
3. **Internal FX param read-back.** `list_fx_params` returns metadata only — no current
   values. After `set_internal_fx_param` there is no way to verify state without a
   render. Add `get_internal_fx_param {trackId, slotIndex}` (or a slot-state dump).

### P2 (speed/robustness)
4. **Automation/notes contract consistency pass**: unify `laneName`(create) vs
   `lane`(mutate), `time`(set_points) vs `beat`(add_point), and `add_*` vs `set_*`
   naming (`add_automation_points` does not exist — it is `set_automation_points`;
   unknown tool names cost a full script abort this session).
5. **Output-size discipline**: `add_notes` returns the complete `noteIds` array
   (1,129 ids for the arp clip; over MCP these blow past client output guards);
   `list_notes` routinely exceeds the guard and returns nothing usable; `list_clips`
   has no `noteCount` (orphan-empty-clip detection required list_notes round-trips).
   Add `includeIds:false` default on add_notes, `noteCount` on list_clips,
   count/limit on list_notes.
6. **`render_and_autoscale`:** canary → truePeak → master rescale was done by hand
   (0.407/0.25 → 0.55). A wrapper doing canary→`min(0.90/peak,1.0)` + final render saves
   a poll/report cycle per mix pass; also reconcile guide §6 target bands with
   `mix_report`'s actual cutoffs (currently two different measurement schemes).
7. **`cluster_library` safer defaults.** Omitting `libraryIds` clusters ALL audio
   libraries → ~1 MB response (hit twice by accident). Require `libraryIds` or return a
   compact summary (cluster label + count + representative member) with opt-in detail.

### P3 (polish)
8. **MCP-side cheat sheet**: populate the server's MCP `instructions` (or a `hdaw_guide`
   tool) with the composition workflow + contract map (§2 above) — a fresh session
   burned ~10 describe-calls reconstructing shapes that already live in
   `docs/psytrance-composition-guide.md`.
9. **`prune_empty_clips`** — orphan hygiene was a 4-step dance (list per track → count
   notes → remove by id).
10. **New automation lanes default-enabled** with an explicit `enabled:false`
    confirmation in the add response (the silent-default trap is documented but keeps
    biting fresh sessions); validation errors should name the failing tool (saw bare
    `notes[4].pitch is not a finite number`, which was un-attributable across a burst).

## 4. Bugs found this session

| # | Bug | Status / workaround |
|---|---|---|
| B1 | `mcp-launch.bat` aborts with `… was unexpected at this time` (cmd quote pairing broken by inline PowerShell inner double quotes from 667f108) → hdaw MCP server never starts for any host. | **FIXED** (committed `ba21483`): capture logic → `mcp-launch-capture.ps1`; bat calls `powershell -File`. Verified end-to-end incl. live pi adapter connect + tool round-trip. |
| B2 | `hdaw_add_automation_points` tool does not exist (real: `hdaw_set_automation_points`); unknown tool name inside mcpScript throws and aborts the whole script with zero output (while invalid-args returns `{ok:false}` — inconsistent error model). | Workaround: correct name; mcpScript unknown-tool should be caught (or return `{ok:false}` like other failures). |
| B3 | `add_automation_lane` param is `laneName`; mutations use `lane`; `set_automation_points` points key is `time`, `add_automation_point` uses `beat`. | Workaround: three different spellings memorized; contract pass = P2.4. |
| B4 | Adding a lane on paramID 1 fails `lane name or paramID already exists` (every track has built-in Volume lane). Not obvious that you should target the built-in lane. | Workaround: `set_automation_points {lane:"Volume", mode:"replace"}` + enable. (Same as jungle handoff B4 — reconfirmed.) |
| B5 | `list_fx_params` requires `slotIndex` and reports it as missing on bare calls (not in its describe shape). | Workaround: pass slotIndex. Include shape in describe. |
| B6 | `list_notes`/`add_notes` responses exceed the MCP client's output guard → unusable payloads; `list_clips` lacks note counts. | Workaround: remove-empty-clips by known id range. P2.5. |
| B7 | `cluster_library` with omitted libraryIds clusters every audio library (≈1 MB response) — no guard, no summary default. | Workaround: always pass libraryIds. P2.7. |
| B8 | No way to read current internal FX param values after setting them. | P1.3. |

Not bugs, noted for players: `add_notes` ALREADY has `relative:false` absolute-beat
mode (jungle handoff P1.4 requested this — exists); `sampler_get_state.hasSound` still
not a render predictor (documented); the engine's `midi()` NaN in an early score draft
was client-side degree-arithmetic, caught by the validator (albeit un-attributably,
B-ish above).

## 5. Ambiguous / needs follow-up findings

- **`related_samples` "entry not found"** for a path the cluster output contained verbatim
  (`ANTINOMY_06_Atmosphere\ANTINOMY_01_Atmosphere_Texture_C.wav`). Either win-vs-WSL path
  normalization mismatch (sidecars store both `win_path` and `wsl_path`) or an index
  entry-key difference. Unconfirmed; worth a ticket before relying on seed-file
  neighbor flows across the E:\ packs.
- **Cluster preset round-trip unverified:** `cp_d323bc93` saved, never reloaded/checked.
  Verify `cluster_library {saveAs, clusterId}` retrieval returns the same membership.

## 6. Suggested follow-ups (priority order)

1. **File a plan (`docs/plans/`)** for P1: MCP `generate_psytrance` (or shared
   `composition` generator), multi-sampler key-range routing, `get_internal_fx_param`
   — plus the P2.4 contract-consistency pass (small, high value, could ship alone).
2. **Investigate the `related_samples` entry-not-found** (indexing/path normalization)
   and verify the cluster-preset round-trip (§5).
3. **Fold §2 deltas into `docs/psytrance-composition-guide.md`**: MCP tool-name map,
   cluster-driven palette flow, this track's canary numbers, and the note that the
   launcher is fixed + committed (the guide's §8/§9 references to capture-on-opt-in
   should be re-read against `mcp-launch-capture.ps1` semantics).
4. **Optional:** a `PsytranceMcpComposition` gtest mirroring the MCP-driven flow
   (kit + score + production stack + render + mix-report gates), so the over-MCP path
   is regression-protected like the in-process recipes.