# Psytrance VA & production — hardware VA detail, production stack, mix, evidence (HDAW MCP)

Split verbatim from [`psytrance-composition-guide.md`](psytrance-composition-guide.md)
on 2026-09-24 (former §4D + §5–§10). Section numbering is unchanged. A §-reference
without a file name points at the master guide (`docs/psytrance-composition-guide.md`),
where §0 style canon, §0.5 sound-design canon and §1–§4 workflow/scoring live.

## 4D. Hardware VA suite — the gearmulator CLAPs (verified 2026-09-11/12)

Real synth firmware running as isolated CLAP plugins — installed in
`C:\Program Files\Common Files\CLAP\` with their ROMs:

**Patch pipelines + loader reality (2026-09-16).** Every bank library now has a
decoder writing searchable sidecars: JP-8080 `timbre-lib/je8086_patch.py` (4983
entries / 2676 usable patches, plus an exploded per-patch tree), microQ
`microq_patch.py` (528 patches, categories carried in the dump), Nord 2x
`nl2x_patch.py` (6841 sidecars), Virus `virus_patch.py`. Loaders:
`load_nord_bank` is the one **verified** bank loader (a test asserts the render
changes); `load_virus_preset` (CC0+PC) works; **JE8086 DT1 dumps apply since
2026-09-20** (wrapper retargets UserPatch→temp performance; use `load_je8086_preset`),
and `set_fx_param` covers 461 params; **Vavra's FX sub-params cannot be exposed**
(derived-parameter collapse onto the type-level params) — use its 392-byte device dump
or the `FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix` params. Rendering caveat: an
isolated export restores `pluginState` into a **fresh child**, so a plugin whose
state does not round-trip its patch exports differently from what you audition.
Device matrix, per-device modulation/FX recipes and the modulation-first policy:
`docs/hardware-va-suite.md`.

| Plugin | Emulates | Isolated-render status (measured) |
| --- | --- | --- |
| OsTIrus | Access Virus TI | ✅ voices at default (peak 0.33–0.47) — the workhorse |
| Vavra | Waldorf microQ | ✅ voices (0.056–0.078) |
| Xenia | Waldorf Microwave II/XT | ✅ voices (0.30) |
| JE8086 | Roland JP-8000 | ✅ voices (0.23–0.27) |
| Osirus | Access Virus A/B/C | ✅ voices; state-apply on load rejected by the plugin (see durability rules) |
| NodalRed2x | Clavia Nord Lead 2x | ✅ renders audibly since the multi-port fix (v0.34); banks load via `load_nord_bank` SysEx injection |
| Dexed | Yamaha DX7 | ❌ preset pipeline removed 2026-09-14 (ignores injected state; probed silent) — use internal `fm_synth` for DX7 voices |

### Internal FX are HOST-VISIBLE parameters (probe 2026-09-16)

The gearmulator CLAPs expose their INTERNAL FX as fully-named, automatable CLAP
params with real-unit text — the FX stage does NOT need MIDI for them:
`list_fx_params` on the slot shows e.g. Osirus `Ch N Chorus Mix/Rate/Depth/Delay/`
`Feedback/LfoShape`, `Ch N Delay/Reverb Mode` (0-26 types), `Ch N Phaser
Mode/Mix/Depth/Frequency/Feedback/Spread`, `Ch N Distortion Curve (0-11)/`
`Intensity`, per-channel EQ; JE8086 `A/B CHORUS TYPE/LEVEL`, `A/B DELAY
TYPE/TIME/FEEDBACK/LEVEL/SYNC`, `VOCAL MIX`; NodalRed2x per-slot `Distortion`
+ `ChPrs Amount`; Dexed has none (DX7 architecture). All automatable with
hasRange=true (real units via text; value space is normalized 0-1, matching
`set_fx_param`). LFO/automation pids (100+slot*100+paramIndex) target them too.
Persist for offline renders via the standard save snapshot (audition workflow).
Also note: the dedicated effect editions (**OsirusFX, OsTIrusFX, VavraFX,
XeniaFX**) are excluded from effect lists and rejected by `add_fx` — synth-only
shadow builds that would silence a slot; making them usable (fix a) would
require a cross-repo gearmulator rebuild + MIDI forwarding — tracked as a
limitation (measurement below).

#### The `*FX` editions: no longer insertable (excluded 2026-09-23) — the measurement below is why

The four `*FX` CLAPs (OsirusFX, OsTIrusFX, VavraFX, XeniaFX) are scanned as
`kind: effect`, but inserting them inline on a track is no longer possible — they
are rejected by `add_fx` (see the first bullet). **The reason is the 2026-09-22
measurement (`dub_embers`): they do not work as insert effects:**

- **That invocation is no longer possible (fixed 2026-09-23).** The four names are
  rejected by `add_fx` on BOTH surfaces through the shared gate
  (`src/common/FxPluginIdCheck.h`, order: shadow-edition match BEFORE resolvability)
  and dropped from every effect list (`list_plugins` `kind:effect`/`all`,
  `plugin.getEffectPlugins`, the frontend FX picker), as an explicit four-name family
  list (case-insensitive) — so neither the qualified-id load nor the bare-name dead
  slot can be reproduced. The measurement below stays as the reason for the exclusion:
  before the gate they loaded **only with the format-qualified scan id** (`list_plugins`
  → `add_fx {trackId, pluginId: "CLAP-VavraFX-a405fdaa-0"}`), while the **bare name**
  (`"VavraFX"`) left a dead slot: `list_fx` reported `"pluginFormat": ""` and
  `"paramCount": 0`, and `list_fx_params` returned `{"params": []}`; with the
  qualified id the same slot reported `"pluginFormat": "CLAP"` and exposed the full
  surface (**VavraFX: 7,557 params**, oscillators through FX).
- **After the instrument in the chain they silence the track.** A/B with
  `set_fx_bypass` on the same window: bypassed `soloRms 0.0595 / audible=1`, active
  `soloRms 0 / audible=0` (VavraFX on a modal part); the same pattern held for
  XeniaFX. Placed **before** the instrument they pass audio unchanged (a no-op:
  identical RMS to 5 decimals either way).
- They also will not resolve a program/state via the usual preset tools in this
  build, so there is nothing to "fix" by loading a patch first.

**`paramCount` is no longer a broken-slot signal (fixed 2026-09-23).** It reports the
**internal FX defs-table size** for an internal FX slot (`none`/unknown types → 0) and the
**live instance param count** for a plugin slot — in-process and isolated-proxy alike —
and **0 while the instance is not loaded** (`TrackFXSlot::paramCount`, projected by
`ReadModelImpl`). So the `"paramCount": 0` above now reads as "instance never loaded",
not "slot silently broken"; regression test:
`FxSurface.ParamCountReportsInternalDefsNotTreeChildren`.

**Working path for that hardware FX character:** use the **instrument** build
(Osirus / OsTIrus / Vavra / Xenia / JE8086) as the sound source and automate *its own*
internal FX params — `Ch N Chorus Mix/Rate/Depth/Feedback`, `Ch N Delay/Reverb Mode`
(0-26 types), `Ch N Phaser Mode/Mix/Depth/Frequency`, per-channel EQ, distortion —
exactly as the section above describes. Those are real, automatable, and they render.

#### Gearmulator internal-FX recipes (probe-verified indices, 2026-09-16)

Before hand-picking a chain below, check the device's harvested matrix presets
first (`timbre-lib/matrix_presets/<engine>.json`, `docs/va-suite-status-log.md`)
— they are corpus-derived device-native configs.

Address any internal-FX param as a plugin param: `list_fx_params {trackId,
slotIndex}` → `paramID` (formula `100 + slotIndex*100 + paramIndex`) → drive it
with `set_fx_param` (normalized 0-1, by paramName or paramIndex), automation
(`add_automation_lane` + `automation_preset` / `apply_movement_plan`), or an
LFO (`add_lfo` targetParamID). Pids below assume **slot 0**; recompute per slot.
Plugin-slot writes **persist** into the slot's `appliedParamOverrides` ledger,
which every fresh export child replays — so `set_fx_param` reaches `export_audio` /
`audition_plugin` / `verify_part` and survives save/load (2026-09-21; before this a
plugin-slot write reached the LIVE child only and no render could see it).
`clear_fx_param_overrides` drops the ledger, `list_fx_params` flags `overridden`
entries, and the opt-in `liveParamState` argument on `audition_plugin` renders
unpersisted live-only writes (what you currently hear) instead of tree state.

| Goal | Synth | Params (slot 0 pids) | Move it with |
| --- | --- | --- | --- |
| Delay riser over a build | Osirus | base `Ch 1 Delay/Reverb Mode`=188 (set a delay type), ramp `Ch 1 Delay Time`=3156, feedback `Ch 1 Delay Feedback`=3157, clock `Ch 1 Delay Clock`=3165 | `automation_preset riser/macro` on 3156 over the build window (apply_movement_plan) |
| Chorus sweep | Osirus | `Ch 1 Chorus Mix`=182, Rate=183, Depth=184, Feedback=186 | LFO sine on 183 (depth 0.3) or openClose on 182 |
| Phaser movement | Osirus | `Ch 1 Phaser Mode`=260, Mix=261, Rate=262, Depth=263, Frequency=264, Feedback=265, Spread=266 | LFO on 263/264; band-pass character via 260 (0-6 stages) |
| Distortion gating | Osirus | `Ch 1 Distortion Curve`=275 (set 1-11), `Intensity`=276 | square preset on 276 per beat window |
| Delay throw at a drop | JE8086 | `A DELAY TYPE`=184 (PANNING L->R…), TIME=185, FEEDBACK=186, LEVEL=187 (>0 enables) | delayThrow preset on 186 |
| Vocal FX gating | JE8086 | `VOCAL MIX`=542, `EXT TO VOCAL SEND`=424 | pump on 542 |
| FX slot switch + movement | Vavra (microQ) | type-level host params (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`) — the Fx1/Fx2 chorus-phaser-delay sub-params are **bit-aliases** (derived-parameter collapse) and still cannot be set with `set_fx_param` (the full surface is 7557 host params since 2026-09-19; the old `{"params":[]}` reading is superseded). **CORRECTED 2026-09-21: `set_fx_param` writes DO reach renders** — the public route persists into `appliedParamOverrides` and every fresh export child replays it (`VavraHostParamPersistedWriteAffectsExport`), while unpersisted live-only writes show up through the opt-in `liveParamState` probe (`LiveParamStateProbeReflectsUnpersistedWrite`). The earlier "LIVE host-param writes do NOT move the render" reading was a harness artifact (renders use a tree copy in a fresh child) | bake the FX into the patch or load a `waldorf_dump` when the change belongs in the patch; `set_fx_param` automation is budgetable for movement again (durable ledger route). Sub-params also via dump; the device's remote-control SysEx (`EmuButtons`/`EmuRotaries` — a `SetParam` message is ignored) is the remaining route but is not yet driven from HDAW |
| Distortion accents | NodalRed2x | `A Distortion`=154 (0/1), `ChPrs Amount A`=444 (0-7 chor./pres.) | square/steppedGate on 154 for rhythmic grit |

`list_fx_params` text gives real units (0-127, -64..+63, type enums like
"SUPER CHORUS SLW" / "Pattern X+Y" / "5.1 Delay Clocked") — verify the value
range before writing (lesson 23 discipline: no out-of-range writes).

### Injection tools (all verified end-to-end)
- `send_fx_midi {trackId, slotIndex, messages[]}` — PC / CC / note / **sysEx**
  into the slot's LIVE instance (parent → SHM → child, byte-exact round-trip
  tested). Background re-apply with backoff for slow-booting children (the
  OsTIrus DSP boot takes seconds).
- `load_virus_preset {trackId, slotIndex, bank 0-7, program 0-127}` — CC0 bank
  select + PC (Virus banks A–H singles).
- ~~`load_dexed_cartridge` removed 2026-09-14~~ — Dexed ignores injected cartridge state (probed: peak 0, state byte-identical). For DX7 voices use `fm_synth_import_sysex` into an internal `fm_synth` slot (same DX7 engine, fully controllable).
- `load_nord_bank {trackId, slotIndex, filePath, program?}` — Nord Lead 2x
  banks (`.syx` raw Clavia SysEx, `.mid` SMF-wrapped) into NodalRed2x:
  ATOMIC — every dump validated (F0 33 <dev> 04 header, F7-terminated,
  ≤32768B) BEFORE queueing, and a harmless CC125 is appended to trigger
  the deferred state capture AFTER the bank is fully consumed by the
  child. No separate send_fx_midi call needed. Sidecar pipeline:
  `timbre-lib/nl2x_patch.py` writes `<patch>.nl2x.json` descriptions
  over `D:\pdf\NL2x Banks` (6841 sidecars, 29436 dumps) — searchable
  by FileLibraryManager.
- `load_je8086_preset {trackId, slotIndex, filePath, preset?}` — Roland
  JP-8080 banks (`.syx` raw DT1 SysEx, `.mid` SMF-wrapped) into JE8086: ATOMIC —
  every DT1 message validated (F0 41 10 00 06 12 header, F7-terminated, Roland
  checksum, ≤32768B) BEFORE queueing. The dump keeps its UserPatch bank address, and the
  JE8086 wrapper retargets it onto the sounding temp performance, so the file's patch
  sounds immediately. No `CC0=1 USER + PC` recall is sent any more — a JP-8080 PC
  *loads* the emulator's bank program into the current patch and overwrote the dump
  (probe: 2026-09-20). Confirm the load with `poll_fx_capture` + a render: a patch
  dump does not appear in the param list, and the persisted `IDs::presetSysex`
  replay is what carries it into exports. `preset` is the 1-based patch unit
  in file order — choose one from the je8086 survey `roleShortlist` refs
  (`perf016/part2`, `bank0/slot25`). PER-PATCH by design: a 64-patch bank is 128
  DT1 messages while the injection carries ≤64 events over a single sysex lane
  that drops (not queues) when busy. Sidecar pipeline:
  `timbre-lib/je8086_patch.py` writes `<bank>.je8086.json` over `D:\pdf\je8086`
  (45 sidecars, 4276 entries + role shortlist) — searchable by FileLibraryManager.
  LIVE VERIFICATION (2026-09-16; the DT1 part is superseded 2026-09-20) — at that time
  DT1 dumps did not apply, the PARAMETER API did, and the plugin's saved state was a
  233-byte stub:
  - **Which tools isolate (RESOLVED 2026-09-16).** `add_fx {pluginId}` and
    `audition_plugin` BOTH create isolated slots while
    `pluginManager->isolationEnabled` is on (the default; the `--mcp-http` launcher
    does not change it). Verified two ways: a `hdaw_plugin_host.exe` child exists after
    each (`spawnPluginHost: slotId=1/2/3 plugin=C:/Program...` in the log), and the
    code path is shared - `add_fx` -> `ProjectCommands::addFxSlot` -> ValueTree change
    -> routing rebuild -> `Track::rebuildFXChain` sets
    `wantIsolated = pluginManager && pluginManager->isolationEnabled`. An earlier note
    that `add_fx` produced an in-process slot was wrong (it was inferred from a saved
    `pluginState`, which is written in both modes).
    This matters because isolation decides the capture/render semantics: an isolated
    render restores `pluginState` into a FRESH child, so a plugin whose state does not
    round-trip its patch will export differently from what you audition; an in-process
    slot (isolation off) renders from the live instance.

  - **Parameter writes work live.** `set_fx_param` on `A OSC WAVEFORM` (index 30)
    and `A OSC1 HARMONICS` (39) changed the plugin's own parameter list exactly as
    commanded (verified by diffing `list_fx_params` before/after). This is the
    supported control surface — it is what the JE8086 recipes above already use
    (delay type 184, vocal mix 542).
  - **DT1 patch dumps apply since 2026-09-20 (CORRECTED).** The 2026-09-16 reading
    ("never applied") rested on a byte-identical param list — which a *bank* write never
    changes anyway. The real cause: a real patch file addresses the UserPatch **bank**
    (`0x02000000`) and the wrapper's `jeController::parseSysexMessage` had an empty
    `case AddressArea::UserPatch`, so the write went nowhere and the sounding
    temp-performance patch stayed untouched. `JE8086.clap` now retargets host DT1s to
    `PerformanceTemp | PatchUpper` (the plugin browser's own `Controller::sendSingle`
    transform), and `load_je8086_preset` no longer appends a `CC0=1 USER + PC` recall (a
    PC *loads* the bank program and discarded the dump). Device probe + gate:
    `docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md`,
    `FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`. **Confirming a load:**
    use `poll_fx_capture` + a render A/B — the param list does **not** move for a
    patch dump (a bank write never changed it, and the SysEx path does not echo back),
    and the live child state blob stayed constant too (2026-09-21). The durable
    readback is the persisted `IDs::presetSysex` replay, which is why a rebuilt child
    reproduces the patch exactly.
  - **Captured state and renders (CORRECTED 2026-09-16).** An isolated render
    instantiates a fresh child that restores `IDs::pluginState`, so an export only
    sounds like the live instance when the plugin's own `getStateInformation`
    carries the patch. The JP-8080 emulation's 233-byte state did not (2026-08 build;
    superseded by the custom `JPAR` CLAP 2026-09-18 and the DT1 retarget 2026-09-20) -
    which is why exports then played its default patch. Measured directly by
    holding the SLOT fixed (isolated, created via `add_fx {pluginId}`) and varying
    only the presence of a captured state: both renders were identical to 16 digits
    (`peak 0.10575640201568604`, rms 0.032181 vs 0.032177). Restoring a captured
    state is therefore a NO-OP for the render. The earlier reading was that a
    captured state poisons the render (peak 0.2455 default vs 0.2063 live); that
    comparison was between an **in-process** slot and an **isolated** one - a
    slot-type effect, not a state effect - so those numbers are withdrawn.
    Engine hygiene that did ship (D-lite): a capture that merely echoes the state an
    instance reported when it appeared is not persisted, and the deferred capture
    reports `captureStatus="unchanged"` instead of writing a fake ok. That is
    tidiness plus an honest receipt; it is not a fix for hear-not-equal-export.

### The audition workflow (inject → save → export → measure)
1. `send_fx_midi` (CC0 + PC) on the plugin slot.
2. `save_project` — REQUIRED: the preset lives in the plugin's live state; the
   save serializes it into the tree (`pluginState`), and offline exports render
   from the tree.
3. `export_audio {start, end, wait:true}` → measure (wavpeak / mix_report).
4. Compare RMS/peak fingerprints across presets; keep the distinct ones.

### State-durability rules (read before relying on presets)
- The serializer REFUSES to overwrite a substantial `pluginState` blob with a
  tiny read (size-regression guard, shipped 2026-09-12) — a load→save cycle
  once shrank 177KB Virus states to 262B stubs this way.
- After `load_project`, re-apply presets via `load_virus_preset` (the loaded
  state may be rejected by the plugin's own `setStateInformation` — OsTIrus
  silently falls back to its default; documented in
  `docs/plans/2026-09-12-plugin-state-durability.md` Phase 4b).
- Dexed/OsTIrus `setStateInformation` rejections are plugin-side (same family
  as Serum 2 — see the 2026-09-08 Serum investigation handoff).
- **Serum 2 is RETIRED (2026-09-14): do not pick it for sessions.** The state
  path is dead end-to-end — param tweaks, program switches (128 programs),
  and persisted-tree renders all produce a byte-identical 3076 B blob and
  identical audio (default patch only). No host-side control surface moves it;
  see `docs/plans/2026-09-14-b10-verdict-serum-probe.md` for the probe
  evidence. Use OsTIrus / Osirus / NodalRed2x / Dexed / sub_synth / psy_fm
  instead.
- **VES (Vintage Emulator Studio) is PARKED (2026-09-15).** Loads and runs
  isolated (MAME emulation executes, ~1 core) but the audio bridge delivers
  silence to the host; fails to load in-process. Patch data is never in the
  plugin state (machine+media paths only), the program API is a stub, and
  MAME NVRAM is transient per boot — presets would only ever be reachable via
  MIDI injection or disk images, and only after the silent-bridge question is
  resolved. Do not pick it; see
  `docs/plans/2026-09-15-ves-preset-loading-probe.md`.

### Isolated children render non-deterministically (known limitation)

The emulated synths run real firmware inside a DSP56300 emulator with
free-running oscillator phase — two exports of the same project produce
different sample data (~±2% RMS). The internal engines (psy_fm, sub_synth,
fm_synth, sampler) ARE deterministic.

Gate margins must tolerate ±2% when isolated plugins are in the project.
A/B comparisons should use spectral properties (centroid, band energies),
not sample-level equality. See `docs/realtime-safety.md` for the full
documentation.

### Known limitations
- TI bank switching via CC0+PC: unverified/ineffective on OsTIrus (all
  bank/program combos rendered hash-identically). TI part singles likely need
  TI-specific sysex or the plugin UI.
- `Percussion` phrase style on single-sample tracks: multi-pitch output
  (36/38/42) — pair with `set_sampler_key_range` per-role kits.

## 5. Production stack (the difference between a sketch and a psytrance track)

Per-role internal FX chains + LFOs — this is what the "too stripped down"
first renders lacked. Verified full recipe (v3/v4/v5):

| Role | FX chain (slots) | LFOs / automation |
| --- | --- | --- |
| Kick | compressor (slot1: thr −18, ratio 4) → EQ (slot2: freq 3600) | — |
| Bass | EQ (slot1) → compressor (slot2: thr −20, ratio 3) | LFO0: 2 cycles/beat sine → EQ cutoff (targetParam 200), depth 0.28; LFO1: 1/beat pump → Volume (target 1), depth 0.6, phase 180 |
| Hats | reverb (mix 0.75, size 0.30) | — (flanger-rate automation in stress variants) |
| Lead | flanger (rate 0.5, depth 0.6) → compressor → reverb (mix 0.9, size 0.22) | LFO: saw 0.5 → flanger rate (200), depth 0.4 |
| Stabs | reverb (mix 0.85, size 0.42) → delay (feedback 0.19, dry 0.45, wet 0.22) | — |
| Pads | chorus (params 0/1/4 = 1.4/0.65/0.55) → reverb (params 0/2 = 0.95/0.35) | LFO0: slow volume swell (→ Volume, target 1, depth 0.22); LFO1: 1/beat pump (depth 0.4, phase 180); pad generator now prefers full triad/7th voicings and can pulse on 8th- or 16th-note grid steps when gated |

**LFO contract (verified):** `add_lfo(track)` then `set_lfo_param` with
`waveform` (0=sin,1=tri,2=saw), `rateSync:1`, `rate` in the units the
samples above use, `depth`, `bipolar:1`, `phaseOffset` (180 = pump feel),
`targetParamID`. Volume = paramID 1; FX params use the automation ID scheme
below. LFOs on the SAME target accumulate (pump + swell on pads sum).

**Automation lanes (verified):** `add_automation_lane(track, name, paramID)` +
`add_automation_point(track, name, beat, value)` +
**`set_automation_enabled(track, lane, true)` — lanes default OFF and
silently do nothing until enabled.** (Exception: the Volume fades written by
`generate_psytrance_markov` enable the target Volume lane themselves — and
`automation_preset` does the same for the lane it writes.) Lane values are
NORMALIZED 0..1 (except Volume-lane raw gain and sampler-Transpose semitones).
FX-param lane IDs:
`100 + slotIndex*100 + paramIndex` (e.g. bass EQ slot1 → 200 = cutoff freq;
lead phaser slot1 → 201/202 = depth/CF; flanger slot1 → 200 = rate).
Automation DOES drive internal FX (EQ/phaser/flanger) end-to-end — verified
audible in renders (bass cutoff sweeps per section, pad riser into breaks).

Macro sweep recipe: points every 32 beats, values 0.1→0.7 across the track
for energy growth (bass EQ freq), plus a breakdown riser point-cluster
(0.1 @ 0, 0.12 @ 188, 0.3 @ 192, 0.7 @ 206, 0.35 @ 224).

**Drive recipe (saturator, verified):** chain order matters - shape the tone
at the source first, saturate second, level last:
growl clip (ClipType/Drive) -> saturator (SoftTanh, ~18 dB Drive, Mix 1) ->
compressor (4:1). The growl's own waveshaper (growl_bass param 4 ClipType /
param 5 Drive dB, or `psy_fm` growlBass preset) provides the coarse grit;
the saturator adds the final "teeth" without third-party plugins.

```python
t = await mcp_call("add_track", {"name": "Growl Drive"})
await mcp_call("add_fx", {"trackId": t, "fxType": "growl_bass"})    # slot 0
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 0,
                                         "paramIndex": 4, "value": 2})   # ClipType = Hard
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 0,
                                         "paramIndex": 5, "value": 30})  # Drive 30 dB
await mcp_call("add_fx", {"trackId": t, "fxType": "saturator"})     # slot 1
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 1, "value": 0})   # Type = SoftTanh
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 0, "value": 18})  # Drive 18 dB
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 3, "value": 1})   # Mix 1 (full wet blend)
await mcp_call("add_fx", {"trackId": t, "fxType": "compressor"})     # slot 2, ratio 4:1
```

Saturator params (`set_internal_fx_param`, REAL units: Drive dB 0-40,
Type 0-3 (0=SoftTanh, 1=SoftAtan, 2=Hard, 3=Bitcrush), Asymmetry -1..1,
Mix 0-1, Output dB -24..24 (trims the WET path only), Bits 2-16 (Bitcrush
only); Mix=0 is a bit-identical bypass). The 2x oversampler adds a
4-sample latency that is reported by the slot and summed into track PDC
automatically - no manual compensation.

### Preset toolkit (factory chains + preset tools)

HDAW ships 8 built-in factory chains (internal FX only, seeded to
`_factory/*.json` on first run, edits on disk survive upgrades, never
deletable). `list_fx_chains` returns them with `source:"factory"` and ids
`_factory/<File_Name>.json`; user chains carry `source:"user"`.

| Chain | Role | Slots (key params) |
| --- | --- | --- |
| `Kick Punch` | kick | saturator 14 dB SoftTanh mix 0.6 → eq 55 Hz −3 dB (sub) → eq 4 kHz +3 dB (click) |
| `Bass Glue` | bass | eq 120 Hz +1.5 dB → compressor −18 dB 3:1 → saturator 8 dB gentle |
| `Hat Air` | hats | eq 9 kHz +3 dB → reverb size 0.25 damp 0.3 wet 0.30 (short/bright) |
| `Pad Shimmer` | pads | chorus 0.6 Hz depth 0.45 → reverb size 0.92 wet 0.42 (large/lush) |
| `Acid Lead` | lead/arp | filter LP 1.2 kHz res 4.5 → delay SyncToTempo dotted-1/8 fb 0.45 |
| `Arp Width` | arps | chorus subtle → delay SyncToTempo 1/16 fb 0.30 |
| `Stab Snip` | stabs | eq 1.8 kHz Q 3.5 −2.5 dB (narrow mids) → phaser subtle |
| `Riser Sweep` | risers | filter LP 400 Hz res 6 (sweep it with an automation lane) → reverb size 0.95 wet 0.5 |

| Tool | Notes |
| --- | --- |
| `list_fx_chains` / `load_fx_chain` | ids may be `_factory/<File_Name>.json`; name resolution covers factory presets too |
| `save_fx_chain` | saves to the user dir — save tweaked variants as the project's own palette |
| `delete_fx_chain` | user presets only; factory ids are refused and the file stays |
| `list_plugin_presets` / `search_plugin_presets` / `load_plugin_preset` | host-enumerable plugin programs |
| `load_plugin_preset_file` | load .fxp/.syx from disk |
| `automation_preset` | movement recipes: `pump`, `macro`, `openClose`, `riser`, `sine`, `square`, `subtleLife`, `randomDrift`, `steppedGate`, `phaseSweep`, `delayThrow` |
| `fm_synth_load_preset` / `fm_synth_import_sysex` | DX7 voice bank load / SysEx import |
| `list_cluster_presets` / `get_cluster_preset` | saved `cluster_library` presets |

Workflow: compose (markov/phrases) → `load_fx_chain` per role from the
factory roster above → tweak individual knobs (`set_fx_param` normalized /
`set_internal_fx_param` real units) → `save_fx_chain` the variant.

## 5a. Reusable FX chain preset: Jordan cave-dub

For a Jordan cave-dub voice, build `sampler -> filter -> delay`, then save the
three slots as **Dusty Skank**. Use `list_fx_params` before
`set_internal_fx_param` to choose the filter cutoff/resonance and delay time,
feedback, and mix in their real-unit ranges.

```python
track_id = await mcp_call("add_track", {"name": "Jordan Cave Dub"})
await mcp_call("add_fx", {"trackId": track_id, "fxType": "sampler"})
await mcp_call("sampler_set_sample", {"trackId": track_id, "slotIndex": 0,
                                      "filePath": win_path, "rootNote": root})
await mcp_call("add_fx", {"trackId": track_id, "fxType": "filter"})  # slot 1
await mcp_call("add_fx", {"trackId": track_id, "fxType": "delay"})   # slot 2

# Track LFOs are separate from FX-chain presets. Configure each property with
# one set_lfo_param call; filter slot 1 param 0 has targetParamID 200.
lfo = await mcp_call("add_lfo", {"trackId": track_id})
lfo_index = lfo["lfoIndex"]
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "waveform", "value": 0})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "rateSync", "value": 1})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "rate", "value": 0.25})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "depth", "value": 0.35})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "targetParamID", "value": 200})

saved = await mcp_call("save_fx_chain", {"trackId": track_id,
                                          "name": "Dusty Skank"})
presets = await mcp_call("list_fx_chains", {})
await mcp_call("load_fx_chain", {"trackId": another_track_id,
                                  "id": saved["id"]})
# Recreate the LFO on another_track_id with add_lfo + set_lfo_param; loading
# the preset replaces its FX slots but does not copy track modulation.
# Delete only when the reusable preset is no longer wanted:
await mcp_call("delete_fx_chain", {"id": saved["id"]})
```

## 5b. FM synthesis for psytrance (internal instrument)

HDAW has a psytrance-focused FM synthesizer (`ActiveType::PsyFm`) alongside the
classic DX7 FM synth (`ActiveType::FmSynth`). The PsyFm engine is designed
specifically for psytrance timbres: growl basses, acid leads, metallic plucks,
and risers.

### Quick start: FM growl bass

```
# 1. Create a MIDI track and add the PsyFm FX slot
trackId = await mcp("add_track", {"name": "FM Growl"})
await mcp("add_fx", {"trackId": trackId, "fxType": "psy_fm"})

# 2. Load a preset routing (sets algorithm, mod matrix, default params)
await mcp("psy_fm_load_preset", {"trackId": trackId, "slotIndex": 0, "preset": "growlBass"})

# 3. Add a MIDI clip with notes
clipId = await mcp("add_midi_clip", {"trackId": trackId, "start": 0, "length": 32})
await mcp("add_notes", {"clipId": clipId, "notes": [
    {"start": 0.5, "duration": 0.4, "pitch": 36, "velocity": 110},
    {"start": 1.5, "duration": 0.4, "pitch": 36, "velocity": 110}
]})

# 4. Add an LFO to modulate the feedback. targetParamID 106 = track FX slot 0,
#    param 6 = psy_fm base feedback. NOT 306: the advertised 300-308 FM targets
#    are unreachable (see "Track-level modulation targets" below).
await mcp("add_lfo", {"trackId": trackId})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "waveform", "value": 0})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "rate", "value": 1})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "depth", "value": 0.4})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "targetParamID", "value": 106})
```

### Available presets

| Preset | Algorithm | Character | Best for |
| -------- | ----------- | ----------- | ---------- |
| `growlBass` | op6→op5→op1 | Feedback-modulated growl, settling envelope | Offbeat rolling bass, acid bass |
| `acidLead` | op6→op1 | High feedback, near self-oscillation | Screaming leads, filter-sweep-style performance |
| `metallicPluck` | op4→op2→op1 | Non-integer ratios, fast transient | Metallic stabs, alien plucks, percussive FM |
| `riser` | op5→op3→op1 | Ratio-sweep LFO, nested modulation | Risers, FX sweeps, tension builders |

### Track-level modulation targets

An LFO's `targetParamID` is a track-wide pid, decoded by the SAME chain the
automation lanes use (`src/engine/Track.cpp`; `docs/adr-automation-model.md`):

| targetParamID | Destination |
| --------------- | ------------- |
| 1 / 2 / 3 | Volume / Pan / Mute |
| `100 + slotIndex*100 + paramIndex` | Track FX param |
| `1000 + slotIndex*100 + paramIndex` | MIDI FX param |
| `2000 + sendIndex` | Send level |
| `3000 + busID*8 + paramIndex` | Bus FX param |

**The `300..308` FM targets listed here previously (OP1 Ratio … Ratio Sweep
Rate) do NOT work.** `Track.cpp` tests `pid >= 100` (the track-FX compound)
BEFORE the FM branch, so `306` decodes as track-FX slot 2 param 6 — with the
`psy_fm` of this recipe in slot 0 it drives nothing at all, or, worse, another
slot's param 6. That shadowing is pre-existing and deliberately out of scope,
so the FM branch under it is unreachable from every `targetParamID`.

**Reachable route for the same intent:** modulate the `psy_fm` slot's own
automatable params with the track-FX compound — pid `100 + slotIndex*100 +
paramIndex` where the psy_fm indices are `0..5` base ratios, `6` base feedback,
`7..30` operator ADSR, `31` output level, `32` algorithm (see
`src/engine/PsyFmState.h`; `list_fx_params` reports the indices). The recipe
above is slot 0, so base feedback is pid `106` and OP6's attack is pid `127`.
For modulation of the engine's internal *matrix* destinations
(`op1Ratio..op6Ratio`, `op6Feedback`, `ratioSweepRate`) use
`psy_fm_set_mod_route`, which runs inside the synth.

### Operator envelopes (set_internal_fx_param indices)

Each of the 6 operators has its own ADSR envelope:

| Indices | Operator | ADSR |
| --------- | ---------- | ------ |
| 7–10 | OP1 | Attack, Decay, Sustain, Release |
| 11–14 | OP2 | Attack, Decay, Sustain, Release |
| 15–18 | OP3 | Attack, Decay, Sustain, Release |
| 19–22 | OP4 | Attack, Decay, Sustain, Release |
| 23–26 | OP5 | Attack, Decay, Sustain, Release |
| 27–30 | OP6 | Attack, Decay, Sustain, Release |

Envelope recipes: pluck = atk 0.001, dec 0.15, sus 0.0, rel 0.1; pad = atk 0.5, dec 2.0, sus 0.9, rel 1.0; growl = atk 0.005, dec 0.4, sus 0.8, rel 0.1.

### Combining FM with the production stack

- **Bass:** `psy_fm` + `growlBass` preset (the preset already arms its own
  `feedbackLFO → op6Feedback` route). A TRACK LFO on that feedback uses the
  slot's own pid — `106` for slot 0 — never 306; see "Track-level modulation
  targets" above.
- **Lead:** `psy_fm` + `acidLead` preset. Mod wheel → feedback for performance control.
- **Stabs:** `psy_fm` + `metallicPluck` preset. Fast envelope on non-integer operators.
- **Risers:** `psy_fm` + `riser` preset. Bar clock auto-speeds ratio-sweep LFO.

## 5c. Psytrance internal instruments (new in v0.25.1)

Two purpose-built psytrance synths ship as internal FX types. They
complement the `psy_fm` synth (§5b) — use them as the primary instruments
for the corresponding roles.

### growl_bass — dedicated offbeat rolling bass

```
add_fx { trackId, fxType: "growl_bass" }
set_internal_fx_param { trackId, slotIndex, paramIndex: N, value: V }
```

| Index | Name | Range | Role in §0.5 canon |
| ------- | ------ | ------- | -------------------- |
| 0 | Fundamental Hz | 20–200 (def 55) | Bass register |
| 1 | Mod Ratio | 0.5–8 (def 1.5) | FM depth/grit |
| 2 | Mod Depth | 0–1 (def 0.6) | FM intensity |
| 3 | Mod Shape | 0=Sin,1=Tri,2=Sq | Waveform color |
| 4 | **Clip Type** | 0=SoftTanh,1=SoftAtan,2=Hard,3=Bitcrush | **Principle 2: waveshaping** |
| 5 | **Drive dB** | 0–40 (def 18) | **Principle 2: distortion intensity** |
| 6 | Asymmetry | −1–1 (def 0.15) | Odd-harmonic color |
| 7 | Bitcrush Bits | 2–16 (def 8) | Digital grit (ClipType=3 only) |
| 8 | **Filter Cutoff** | 20–20000 (def 800) | **Principle 2: first filter** |
| 9 | Filter Res | 0.1–20 (def 4) | Resonance sweep |
| 10 | Filter Env Amt | 0–1 (def 0.7) | Pluck/open feel |
| 11 | Filter Type | 0=LP, 1=BP | Tone color |
| 12–15 | ADSR | ms (0.1–100/1000) | Envelope |
| 16 | Output Level | 0–1 (def 0.4) | Volume |
| 17–19 | Unison | enable/voices/detune | Width/spread |
| 20–21 | Ratio Jitter | enable/amount | Organic pitch wobble |
| 22–23 | Formant | enable/morph | Vocal vowel quality |
| 24–25 | Sidechain | drive/amount | Kick pump (principle 4) |

**Key principle 2 recipe:** ClipType=2 (Hard), Drive=25–35, Filter
Cutoff=600–1200, Res=6–10. Add an external `filter` FX slot after
for the second filter pass (principle 2: instrument filter → external
filter → …). Automate the external cutoff across sections.

### psyarp — built-in arpeggiator synth (principle 5)

```
add_fx { trackId, fxType: "psyarp" }
set_internal_fx_param { trackId, slotIndex, paramIndex: N, value: V }
```

| Index | Name | Range | Role |
| ------- | ------ | ------- | ------ |
| 0 | Osc Shape | 0=Saw,1=Sq,2=SuperSaw | Timbre |
| 1 | Unison Voices | 1–4 (def 2) | Width |
| 2 | Unison Detune | 0–50 (def 8) | Spread |
| 3 | **Pattern Shape** | 0=UpDown,1=Asym332,2=Random | **Arp pattern** |
| 4 | Octave Range | 1–4 (def 3) | Register spread |
| 5 | Bars Per Motif | 0.5–8 (def 2) | Phrase length |
| 6 | Filter Cutoff | 20–20000 (def 600) | Built-in filter |
| 7 | Filter Res | 0.1–20 (def 7) | Resonance |
| 8 | **Filter Sweep Bars** | 0.5–16 (def 4) | **Principle 7: cutoff evolution** |
| 9–12 | Delay | time/feedback/ping-pong/wet | Stereo depth |
| 13–15 | Reverb | size/wet-on-dry/wet-on-delay | Space |
| 16–18 | Phaser | enable/rate/depth | **Principle 7: L/R movement** |
| 19 | Output Level | 0–1 (def 0.4) | Volume |

**Key principle 5 recipe:** feed notes from `scaleNote` in F harmonic
minor (scale mode 7). Pattern Shape=0 (UpDown) or 2 (Random). Octave
Range=2–3. Bars Per Motif=2–4. Filter Sweep Bars=4–8 (cutoff drifts
across the motif, principle 7). Phaser Enable=1, Rate=0.1–0.5
(principle 7: L/R phasing). Add an external `filter` FX slot after for
the second filter pass (principle 2).

**Root notes for arp:** use `scaleNote(degree, octave)` from the project
scale (F harmonic minor mode=7, root=5). Degrees 0–6 map to
{F,G,Ab,Bb,C,Db,E}. Feed the resulting MIDI pitches into the psyarp clip.

### sub_synth — modulation-matrix factory presets

```
add_fx { trackId, fxType: "sub_synth" }
apply_sub_synth_mod_preset { trackId, slotIndex, presetId: "<id>" }
```

One atomic, undoable call rewrites ONLY the internal LFO params (27–32:
wave/rate/cutoff/pitch/amp/FM amounts) — the loaded patch, oscillators,
filter, and envelopes (params 0–26) are untouched. Use it to give a staged
sub_synth voice its long-form movement (principle 7) without hand-writing
six `set_internal_fx_param` calls; the track-level ModulationManager LFOs
(§5b) remain the tool for cross-track/FX routing.

| presetId | Character | Psytrance use |
| ------ | ----------- | ------------- |
| `off` | Static, pure | Reference/audit; dry sub layer |
| `slow_filter_drift` | 0.12 Hz cutoff drift ±12 st | Rolling bass evolution (principle 7) |
| `vibrato` | 5.5 Hz, 18 cents | Lead/stab expression |
| `tremolo` | 6 Hz amplitude | Offbeat pluck pulse, percussive beds |
| `fm_motion` | 2 Hz triangle → osc2→osc1 FM | Growl texture, alien timbre motion |
| `animated_sweep` | 0.25 Hz cutoff+pitch+amp+FM | Build/riser beds, section transitions |

**Trap: verify `Cutoff` after any `sub_synth_import_sysex`.** The Virus→sub_synth
mapping writes the patch's filter as-is, and Virus patches with a closed filter land
at `Cutoff = 20 Hz` (the parameter minimum) — the slot then renders **near-silent**
while reporting a successful import with "24 params mapped". Observed 2026-09-22 on
two `Access_Virus_TI/*.syx` banks: solo RMS 0.0029 (inaudible under a kick), and
opening `Cutoff` (param 7) to 300 Hz took the same part to 0.0446 — a 15× change from
one parameter. So after importing, read the slot back
(`list_fx_params`) and set `Cutoff` for the role (sub ≈200–400 Hz, pad ≈1.5–3 kHz)
before auditioning; `audition_plugin {trackIndex, slotIndex}` reports `audible` and
is the gate, not the import's `ok`.

### Combining the instruments

| Role | Instrument | Why |
| ------ | ----------- | ----- |
| Bass | growl_bass | Dedicated growl, built-in waveshaper + filter + sidechain |
| Arp lead | psyarp | Dedicated arpeggiator, built-in sweep + phaser + delay |
| Stab/acid lead | psy_fm (acidLead preset) | High-feedback screaming tones |
| FX/blips | psy_fm (metallicPluck preset) | Non-integer ratios, alien perc |
| Pad | external sampler (chorus+reverb FX) | Sustained texture |

After the internal instrument, add 1–2 `filter` FX slots for the
second/third filter pass (principle 2), then an EQ or compressor for
final tone shaping.

---

## 6. Mix + master (all measured)

- **Clipping is PRE-master.** If peaks pin at 1.000 and master gain changes
  don't move them, the sum clips before the master — cut per-track faders /
  velocity, not master.
- **Canary render (verified technique):** render once at master 0.25, read
  the TRUE peak (= canaryPeak / 0.25), then re-render at
  `finalGain = min(0.90 / truePeak, 1.0)` for ~−1 dBFS. A 24-bit WAV pins
  at full scale, so you cannot see headroom at master 1.0.
- **Sanity band targets (v3/v4 measured):** sub≈40–275, bass≈15–89,
  body≈14–21, mid≈7–15, high≈4–5 — bass-weighted but every band present.
  First renders were sub:high ≈ 300–1000:1 (kick+bass only). If bands are
  sub-dominant, it's usually rootNotes/velocities, not the master.
- `set_master_gain` after faders; keep master ≤ ~0.9 pre-canary.
- **The drop must be the loudest point (gate).** `mix_report {fromPlan:true}` returns
  `loudnessGates`: each drop section's RMS against the build preceding it (pass at ≥ 0.9×, override
  with `dropBuildRatio`). A build reading RMS-hotter than its drop FAILS — thin the build cells
  (fewer arp notes / shorter riser, the musical fix) or lift the drop layers; never "fix" it with
  master gain. `audit_song_structure` carries the structural proxy (`dropsThinnerThanBuild`).
- **Never modulate pitch/ratio on melodic parts.** `psy_fm` param 0..5 are `OP1..OP6 Ratio` — an
  LFO or lane on pid 100+0 sweeps the carrier off-integer and reads as discord. Same for sub_synth
  semitone/pitch and sampler Transpose. Static detune ≈≤10 cents is fine; moving pitch is not.
- **Volume-lane authority (fader writes can be ignored):** an ENABLED Volume
  automation lane makes automation authoritative for that track, so `set_track_volume`
  afterwards is overridden (this masked a whole round of gain corrections).
  `audit_modulation_coverage` reports it per track (`faderOverridden`,
  `volumeLanes.enabled`) and in `summary.faderOverriddenIds`; call
  `set_fader_authoritative {trackId, authoritative:true}` before gain staging
  when a movement plan (which writes/enables Volume lanes) has already run.

**Verified canary numbers (2026-08-30 F-minor session, 140 BPM, 400 beats):**

- Canary at master 0.25 → peak 0.407 → truePeak ≈ 1.63 → final `min(0.90/1.63,1.0)` = 0.55.
- Final peak 0.852; RMS arc: intro .030 / build .082 / mainA .095 / mini .023 / mainB .096 / breakdown .040 / finale .113.
- `pumpDepth` 0.74, `kickProminence` 0.74.
- **Fader set that rendered safely at master 0.55:** kick .85, bass .80, hats 1.0, stabs .95, arp .90, pads .75, riser .90, down .90.

## 7. Verify + iterate loop (do this on EVERY render)

Read the WAV back with numpy: peak (≤0.95, ≥0.35), RMS per section (the arc
must have a real dip at the breakdown: v3.4's "breakdown" was 0.165 vs main
0.170 — a fake break; v3.5 dropped it to 0.039), per-beat pump strength,
and band energy. Inspect waveforms around section boundaries for the kick
hit/transition shape. Render per-role isolation files (mute everything but
one role) when a single voice is suspect — fastest way to hear a bad
sample/rootNote/FX chain. All this work lives in the session's Python cells;
the gtest suite keeps `RoleIsolationDiag` + `FxExplosionDiag` as permanent
diagnostics.

## 8. Export + deliverable housekeeping (verified)

- `export_audio`'s `trackIds` is a TRACK-INDEX filter that shipped 2026-09-08 — the
  handler compares each value against the track's position in `TRACK_LIST`
  (`McpExportTool.cpp:92-104`), so `trackIds: [2]` renders that track alone and any
  non-index value (a role name, e.g. `"PAD"`) matches nothing and mutes EVERY track.
  Omit the arg for a full-project render; serialize calls
  ("export already in progress"); use a FRESH filename per render and wait
  for the file size to stabilize; output is 24-bit PCM.
- Since v0.25.2: `export_audio` uses atomic CAS guard; `queue:true` waits for previous export (120s timeout) instead of immediate reject; `cancel_export` still aborts.
- Render the REAL project duration (see §4), not a fixed window.
- Save `.hdaw` projects next to renders: `.tmp_dnb_theme/<name>.hdaw` +
  `<name>.wav`. Renders from the keep-flag run land there by convention.
- Long renders with automation + save/load accumulation are crash-stress
  scenarios (see handoff §3 'unreproduced abort'); run under
  `%TEMP%\hdaw_capture\run_with_capture.ps1` (procdump) when iterating on
  engine changes, and keep the automation lanes' normalized values in the
  safe 0.01–0.85 range.

## 8.5. MCP launcher (fixed 2026-08-30)

The `mcp-launch.bat` launcher was broken by commit `667f108` (unescaped inner
double quotes in cmd.exe). Fix: crash-capture logic extracted to
`mcp-launch-capture.ps1` (repo root), invoked via `powershell -File`. The bat
calls it with `powershell -NoProfile -ExecutionPolicy Bypass -File`. Behavior
preserved: engine started with inherited stdio (stdout stays pure JSON-RPC),
procdump attached as watcher, exit-code propagation. `HDAW_NO_CRASH_CAPTURE=1`
bypasses the procdump attach.

## 9. Contract traps that WILL bite again (from the 8/26–27 handoff)

1. **Note caps:** clip caches are large now (8192 ceiling) — but keep parts
   under it; a part past the cap or notes written with ABSOLUTE starts into
   a clip at start>0 silently misplays. Clip-local beats, always.
2. **`sampler_set_sample` re-set** used to kill the live sound (fixed: now
   rebuilds the FX chain and re-loads from the tree — and any re-set still
   needs a rebuild to be live). In HEADLESS MCP with no audio device,
   `sampler_get_state.hasSound` reads false for EVERYTHING — the tree is
   the truth; hasSound is NOT a render predictor.
3. **`duplicate_region` ripple-inserts:** copies [start,end) at end AND
   shifts later content. Extend grooves BEFORE placing later material.
4. **Registry clobber:** an engine restart rewrites
   `%APPDATA%\HDAW\libraries\registry.json` from memory, dropping
   externally-written entries. Register libraries via MCP `add_library`
   while engines may run; scripted registry writes only when idle.
5. **Daemon drops:** "Connection closed" mid-project → `await mcp.reload(
   'hdaw')` then `load_project` from the last save. Save often on long
   builds (`.tmp_dnb_theme/<name>.hdaw` after every section pass).
6. **`add_track` returns plain text** `trackId=N routed=1` — parse it; other
   tools mix "ok" text and JSON; try both. `add_midi_clip`/`add_audio_clip` now return JSON `{"clipId":N}` since v0.25.2 (parse both).
7. **Key discipline:** derive every pitch (bass roots, arp tones, stab
   triads, pad voicings, breakdown melody, even kick root) from ONE scale's
   degree set — v5's `fMinorDeg(degree, octave)` helper over
   {F,G,Ab,Bb,C,Db,Eb} produced an entirely in-key track with progressions
   like i–VII–VI–VII (A) and VI–VII–i–i (B). Don't hand-type chromatic
   pitches into a long score.
8. **`related_samples` path normalization:** the tool does case-sensitive,
   exact string matching on paths. Cluster output paths may use different
   case or separators than the stored entries. Fixed in LibraryClusterer.cpp
   (2026-08-30) — now normalizes through `juce::File` for case-insensitive,
   separator-agnostic comparison.
9. **Output-size discipline:** `add_notes` returns the complete `noteIds`
   array (1,129 ids for a large arp clip). Over MCP these blow past client
   output guards. Prefer `includeIds:false` when available; `list_notes`
   routinely exceeds the guard. `list_clips` lacks `noteCount` (orphan
   detection requires `list_notes` round-trips).
10. **Unknown MCP tool names abort the script:** calling a non-existent tool
    (e.g. `hdaw_add_automation_points` — real name is `set_automation_points`)
    throws inside mcpScript with zero output, while invalid-args returns
    `{ok:false}`. Always verify tool names with `tools.search` first.
11. **`export_audio` start/end are SECONDS. AGAIN.** (2026-09-01 session.)
    Four renders cut short by passing beats. Convert: `seconds = beats × 60
    / BPM`, and verify the produced file size ≈ `duration × sampleRate ×
    channels × bytesPerSample` before analyzing. The WAV byte count is the
    ground truth for what you actually rendered.
12. **Async exports cancel in-flight work silently.** Starting a second
    export while one renders aborts the first with no error surfaced to the
    caller — two "export started" acks, one missing file. Serialize: wait
    for the output file to reach its expected byte size before starting
    the next export, never pipeline them.
13. **Verify the arrangement by scanning the rendered WAV, never the score
    bookkeeping.** Section maps, note ranges, and beat math in the build
    script can all be wrong while every tool call returns "ok". The 2026-09-01
    session found a full-instrumentation block inside a "breakdown" that the
    score claimed was empty — and a breakdown dip that the map said didn't
    exist. Decode the WAV, compute an RMS envelope (mind: interleaved stereo
    → per-sample time = index×stride/(sr×ch)), and locate the quiet regions
    empirically.
14. **Layering the same register/rhythm = masking, not reinforcement.** The
    FM growl doubled the sample bass (same offbeats, same pitches, same
    octave) and was completely inaudible in the mix despite measuring RMS
    0.23 soloed. Distinct synth layers need distinct register, rhythm, or
    role (growl OWNS the low offbeats in its sections; sample bass rests or
    moves register). Solo-probe each synth layer (`export_audio trackIds`)
    before judging it in the full mix.
15. **`psy_fm` live-engine MCP tools fail with "track not found"** (open bug
    2026-09-01): `psy_fm_load_preset` and friends go through
    `getMainProcessor()->getTrack()` → null, while the ValueTree path
    (`set_internal_fx_param`) works. Workaround: configure via
    `set_internal_fx_param` indices (see §5b). The frontend Router_PsyFm
    shares the same pattern and likely fails the same way. See
    `docs/handoffs/2026-09-01-psyfm-bugs-handoff.md`.
16. **Don't trust response-less side effects.** A tool call whose result you
    don't inspect may have errored (`remove_notes` silently no-oped once —
    wrong assumption, breakdown stayed full). Check the response text ("removed
    N notes"), use `dryRun` first for destructive ops, and re-`list_notes` to
    confirm the range is actually empty.

## 10. Where the evidence lives

- **Session logs** (agent): `01a03f69-…` (Antinomy MCP track, first dose of
  every contract trap), `01a04356-…` (new packs + production v3/v4/v5 +
  EQ-gain engine bug found mid-track).
- **Handoff:** `docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md`
  (bugs §1–§5, cheat-sheet §5, full session inventory §6).
- **Recipes:** `tests/unit/engine/psytrance_composition_stress_test.cpp`
  (FullProductionArrangement / FullProductionV4 / DarkForestV5 /
  RoleIsolationDiag / FxExplosionDiag; requires
  `timbre-lib/psy_sample_selection.tsv`).
- **Sample tooling:** `timbre-lib/analyze_psytrance.py`,
  `select_psy_samples.py`, `register_library.py`, `analyze_multi.py`,
  `analyze_targeted.py`, `lib_analyze.py` (the `--library` entry point).
- **Deliverables:** `.tmp_dnb_theme/` (antinomy_*, psytrance_production_v3/4,
  psytrance_darkforest_v5.wav + .hdaw projects).
