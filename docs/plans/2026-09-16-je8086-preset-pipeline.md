# JE8086 (Roland JP-8080) Preset Pipeline + Sidecar Sweep

## Goal
Mirror the shipped Nord Lead 2x / Virus patch pipelines for the **new JP-8080
bank library in `D:\pdf\je8086`** (46 files: 36 .mid/MID SMF-wrapped, 10 .syx
raw SysEx): a standalone decoder (`timbre-lib/je8086_patch.py`) that turns
every bank into per-patch records with **named parameters**, writes
`<bank>.je8086.json` sidecars (schema `hdaw.je8086.bank.v1`, engine
`je8086`) next to each bank so FileLibraryManager / `search_library` can index
them, and emits a library survey (`timbre-lib/je8086_survey.json`) plus a
role/category analysis for the Sound Selector role.

## Evidence (verified against gearmulator 2.2.9 sources + all 46 real files)

Sources: `D:\pdf\retromulator-main\source\ronaldo\je8086\jeLib\`
(`jemiditypes.h` = message + `enum class Patch` parameter map; `patch.cpp`).

- **Message**: Roland DT1 `F0 41 10 00 06 12 <a0 a1 a2> <data..> <cks> F7`
  (model 0x0006 = JP-8080, 7-bit address bytes, checksum
  `(128 - (sum(addr)+sum(data)) % 128) % 128`). `.mid` banks wrap the same
  messages in SMF `F0 <varlen>` events (the varlen prefix must be stripped
  first — varlen high-bit continuation byte, e.g. `81 7D`).
- **Census (2026-09-16)**: 46 files, **6144 DT1 messages, 0 checksum failures**,
  4276 name-bearing entries (1856 patch-area, 2420 performance-area).
- **Address unit = 256 B (one page)**, patch = 0x200 B = 2 pages ⇒ 64-patch bank
  = 128 pages, bank stride = 128. Patch area base `0x020000`: bank =
  `(v-base)//128`, slot = `((v-base)%128)//2 + 1`. Performance area base
  `0x030000`, stride 128 pages: `off = v-base`, `off < 64` = performance
  common (carries the performance name), else patch = `(off-64)//2 + 1`
  (`PatchUpper = 0x4000`/page 64, `PatchLower = 0x4200`/page 66 per
  jemiditypes.h).
- **The dump carries ONE leading byte** before the documented patch body: the
  16-char name is `data[1:17]` and `Patch.<Param> = 0xNN` lives at
  `data[1+0xNN]`. Validated over **3893 name-bearing page-0 messages**: all 9
  tight-range params (Lfo1Waveform 0-3, RingMod 0-1, Osc1Waveform 0-6,
  Osc2Waveform 0-3, Osc2Sync 0-1, Osc2Range 0-0x32, FilterType 0-2,
  CutoffSlope 0-1, Lfo2DepthSelect 0-2) in range for **99.9%** at
  `data[off+1]` vs **0.0%** at `data[off]` and `data[off-1]`.
- **Parameters** (from `enum class Patch : uint16_t`): name 0x00-0x0F, LFO
  0x10-0x1A, osc 0x15-0x26 (waveforms/sync/range/fine/balance/ring/x-mod),
  filter 0x27-0x32, amp 0x33-0x39, pan/tone 0x3A-0x3C, multi-FX 0x3D-0x3E,
  delay 0x3F-0x42, bend/portamento/mono 0x43-0x49, CC-control depths
  0x4A-0x116, `MorphBendAssign` 0x118, then `ControlPortamentoTime` 0x119+
  and the Velocity/Morph blocks to ~0x16C (2-page patch).
- **Sidecar contract** (FileLibraryManager::applyPatchSidecar): keys
  `engine`, `description`, `roleCheck.verdict`, `unmapped[]`,
  `mappedParams` (object; first 2 `param`/`name` values become search tags;
  the whole object becomes `patchParams` = the replay payload), optional
  `dsp` (20-key probe vector). Discovered by filename
  `<file>.virus.json` / `<file>.dx7.json` — **`.je8086.json` needs a 1-line
  add to that lookup** (see Deliverables 5).

## Deliverables
1. `timbre-lib/je8086_patch.py` — container detection (jp8080 .syx / .mid),
   DT1 parse + checksum validation, SMF varlen strip, page→bank/slot math,
   patch-body param decode via the embedded `Patch` map (raw + normalized +
   bipolar values), role classification (name keywords + parameter evidence),
   sidecar writer, survey mode, `--dump/--survey/--sidecars/--role/--explode`.
2. `timbre-lib/test_je8086_patch.py` — pytest: synthetic DT1 banks (also
   SMF-wrapped), checksum + varlen edge cases, bank/slot math, param decode +
   the `data[off+1]` convention, role classification, sidecar contract keys,
   stable-JSON + no-absolute-path-leak checks, survey invariants; real-library
   spot checks skip when `D:\pdf\je8086` is absent.
3. Sweep `D:\pdf\je8086` → 46 `<bank>.je8086.json` sidecars +
   `timbre-lib/je8086_survey.json` + role/category analysis.
4. `timbre-lib/README.md` entry (usage + sweep results).
5. Follow-up (separate, small): `FileLibraryManager::applyPatchSidecar`
   accepts `.je8086.json` (engine `je8086`) so the sidecars are searchable
   in-app; and (phase 2) `load_je8086_bank` MCP tool mirroring
   `load_nord_bank` (PresetFileParser + PresetRoute + gtest) so a curated
   patch can be injected into a JE8086 slot and rendered.

## Success Gates
- [x] G1: `pytest timbre-lib/test_je8086_patch.py` → **15 passed** (incl.
      real-library spot checks: 6144 messages, 0 checksum failures); full
      timbre-lib suite **106 passed** (no virus/nl2x regression).
- [x] G2: sweep parses **46/46 files**, 6144 messages, **0 checksum failures**,
      4276 named entries; all 45 sidecars validated for `engine`/`schema`/
      `mappedParams`/`roleCheck`/`unmapped` + no absolute-path leakage.
- [x] G3: survey reports per-bank counts, role + family histograms, placeholder
      share, cross-bank duplicate names, the psy-relevance ranking and a per-role
      `roleShortlist` (see Analysis below).
- [x] G4: byte-stable re-run (asserted in
      `test_bank_sidecar_contract_and_stability`) and zero path leakage
      (asserted in the survey + sidecar tests).
- [x] G5: committed as `e6006c5` + the analysis commit, touching only
      `timbre-lib/` and `docs/`.

## Effort/risk
~0.5d. **Risk LOW** — pure Python tooling + JSON artifacts; no engine, DSP,
RPC, or ValueTree surface is touched. The only HDAW-side change is the
1-line sidecar-extension add (deliverable 5), which lives in the file-library
scan path, not the audio graph.

## Analysis results (2026-09-16 sweep)

**Inventory** — 46/46 files parsed, 6144 DT1 messages, **0 checksum failures**,
4276 named entries: **2674 usable patches**, 383 performance names, the rest
`INIT PATCH` padding (these are 64/128-slot ROM-style containers). 3572 entries
are single-page dumps, so the 0x100+ `Control*` params are unavailable for them
while every core parameter (0x00-0x0FE) still decodes.

**Role histogram** — lead 1345, pluck 1333, bass 560, pad 501, performance 383,
fx 91, arp 8, other 55; SUPER SAW is the dominant family.

**Banks worth mining (usable patches)** —

| Bank | Usable | Top roles |
|---|---|---|
| Cinematica Performances.syx | 128 | 75 pad, 34 lead, 14 bass |
| Mystery Islands EDM Vol 1.mid | 128 | 91 lead, 17 pluck |
| Techno 2.syx | 126 | 46 lead, 37 pluck, 26 pad |
| Mystery Islands - Always Alive.syx | 107 | 81 lead, 19 pluck |
| Kulshan Altitude.mid | 100 | 58 lead, 39 pluck, 18 fx |
| Kulshan Mystical Psytrance.mid | 100 | 45 pluck, 40 lead, 30 bass, 9 fx |
| JayB_JP-8080.MID | 66 | 67 pluck, 44 lead |

**The placeholder trap (the finding that matters for curation)** — the three
banks whose *names* shout psytrance carry almost no patches: `Goa Psytrance.syx`,
`Spectro Senses Goa Psytrance.syx` and `Psytrance.syx` each hold 64 genuine
performance names but their patch bodies are `INIT PATCH` (1-2 usable patches
each). Name-based selection would have picked exactly the emptiest banks; the
survey's `nonInitPatches` counter is what exposes it. The psy content that does
exist is in `Kulshan Mystical Psytrance.mid` (100 usable: 30 bass, 9 noise-family
fx) and `Techno 2.syx`.

**Per-role picks** (survey `roleShortlist`: bank psy-score, then classification
confidence; `ref` is unambiguous for patch banks and performances):

- bass — `Kulshan Mystical Psytrance.mid` perf016/part2 LITTLEDIRT,
  perf026/part1 WARBLY, perf001/part1 SQUISH2
- lead — Kulshan perf015/part2 DEEPSAW, perf007/part2 SQUISH4, perf037/part1
  SEARCHING (all SUPER SAW)
- pluck — Kulshan perf005/part2 SQUISH2, perf014/part2 UNIVERSE, perf009/part2
  DARK2
- pad — `Goa Psytrance.syx` perf001/part1 GOA 80, Kulshan perf035/part1 DEEP3
- fx (noise) — Kulshan perf023/part1 ZAP3, perf031/part1 DIVINE, perf041/part1
  MORNING
- arp — `Alan Marcero - Trance Soundset B.mid` bank0/slot25 LD Trance Maker,
  `Techno 2.syx` Metalic Arp

**Two bugs found and fixed while building this** (both would have shipped a
plausible-looking but wrong pipeline): (1) the area bases were written in
absolute-byte space while `page_value` packs the three 7-bit address bytes, so
the first pass decoded **0 entries** from all 46 files; (2) a performance
*common* block was decoded as a patch body, inventing parameters and roles for
performance names (now `role: performance`, no params). Both were caught by
checking the decoder against the known census rather than by trusting the output.

**Follow-ups — both DONE (2026-09-16)**

- (a) `FileLibraryManager::applyPatchSidecar` now also reads `.nl2x.json` and
  `.je8086.json` (with a filename→engine fallback to nodalred2x / je8086), so the
  NL2X and JP-8080 bank sidecars are searchable in-app — commit `0089bed`, tests
  `FileLibraryPatchTest.Je8086BankSidecarIngested` and
  `Nl2xBankSidecarIngestedWithoutEngineKey`.
## Live E2E (2026-09-16) — what it verified, and what it disproved

Ran the loader against a live engine with a real JE8086 instance (audition keepTrack
-> 4-note lead part -> render -> inject -> render -> compare):

**Verified**
- Loader delivery: `queued N DT1 message(s) ... capturedToTree=0` then
  `get_fx_capture_status` -> `status=ok stateBytes=233 hasPluginState=1`.
- **Index alignment**: the loader's unit numbering now matches the survey's —
  survey unit 44 == "performance bank 15 slot 1" == Kulshan perf015/part1 DEEPSAW
  (both enumerate 192 units: 64 named performance commons + 128 parts).
- Validation is atomic: a corrupt checksum, an out-of-range preset, a Clavia file
  and a bogus path are all rejected before anything is queued.

**Disproved, then corrected (the important part)**
- **DT1 patch dumps are not applied** (holds up): the plugin's 461-entry
  parameter list was byte-identical before/after an injection. Root cause from the
  gearmulator sources: `jeLib/device.cpp` gives live host MIDI to the DSP thread
  and offers SysEx only to `SysexRemoteControl` (LCD/Button/Rotary/SetParam —
  `sysexRemoteControl.h`); the DT1 patch protocol lives in `State`
  (`state.cpp`: `CommandIdDataSet1`), reachable only from the device→host and
  state-restore paths.
- **CORRECTION: the parameter API works.** `set_fx_param` on index 30
  (`A OSC WAVEFORM`) and 39 (`A OSC1 HARMONICS`) changed the plugin's parameter
  list exactly as commanded. My first conclusion ("JE8086 ignores host control")
  was too broad: it is specifically the *patch-dump* path that is dead, and the
  parametric path is live.
- **The real blocker is the export path, not the injection.** Every `export_audio`
  — baseline, after DT1 injections, after parameter writes, and even with
  `MASTER VOLUME` = 0.0 plus a fresh `capture_fx_snapshot` — rendered a
  bit-identical signal (peak 0.2455155849456787). The captured state is
  `captureBytes="233"` and contains no patch data (the proxy chunk is 244 B, so
  this is not the lesson-14 truncation); a freshly instantiated render plugin
  restores that stub and plays its default patch. So renders were the *wrong
  instrument* for every A/B in this section: **audition ≠ export for JE8086**.
  Follow-up: decide whether JE8086 parts are audition-only (use internal engines
  for deliverable parts) or whether the plugin's state serialization can be
  worked around.
**WITHDRAWN (2026-09-16, fourth iteration): the capture was never the problem.**
Holding the slot fixed (isolated, via `add_fx {pluginId}`) and varying only whether a
state had been captured: both exports were identical to 16 digits (peak
0.10575640201568604). Restoring a captured state does not change the render, and the
earlier poison readings compared an in-process slot against an isolated one. What
remains true: an isolated render restores the plugin's own state into a FRESH child,
so an export matches the live sound only if that plugin's `getStateInformation`
round-trips its patch - the JP-8080 emulation's 233-byte state does not (plugin-side).
**RESOLVED (2026-09-16): the suspected add_fx/audition isolation difference does
not exist.** Both tools isolate while `isolationEnabled` is on (default): a
`hdaw_plugin_host.exe` child exists after each, `add_fx` routes through
`ProjectCommands::addFxSlot` into the same `Track::rebuildFXChain` decision
(`wantIsolated = pluginManager && pluginManager->isolationEnabled`), and the
in-process-slot reading came from a saved `pluginState`, which is written in both
modes. No code change; the guide now states which tools isolate and why it matters
(an isolated render restores `pluginState` into a fresh child, so a plugin whose state
does not round-trip its patch exports differently from what you audition).

D-lite shipped as hygiene only (skip a capture unchanged since the instance appeared;
report `captureStatus="unchanged"`). The per-plugin boot-baseline registry (item b)
was NOT built, because this measurement removed its premise.

**Superseded root-cause notes (kept for the audit trail)**

**Final root cause (third iteration)**
1. Isolation ON, no capture yet: `export_audio` follows the live plugin
   (peak 0.2063, bass 4372, `stateBytes=0`).
2. Isolation ON, after a capture: every render is frozen at peak
   0.2455155849456787 / bass 5353 (the plugin's default patch) regardless of DT1
   injections, parameter writes, or even `MASTER VOLUME` = 0. The captured state is
   `captureBytes="233"` (== the proxy chunk size 244 is not the limit).
3. Isolation OFF (in-process): the render always follows the live plugin
   (peak 0.2111) and reports `stateBytes=0` — no state transfer involved.

=> The isolated child's captured state is a remote-control-only stub, and
restoring it in the render path overrides the live patch. The render was never
the right instrument for the earlier A/Bs *because a capture had been taken*.
Recipe: `captureToTree:false` for JE8086 slots (or in-process) keeps renders
aligned with the live instance; the engine-side follow-up is to capture after the
child's slow ROM/DSP boot and to reject/flag an implausibly small state rather
than restoring it.

## --explode shipped (2026-09-16)

`--explode` now writes a per-patch tree (one DT1 `.syx` per unit under
`<DIR>/exploded/<bank>/`, `--explode-dir` to relocate, `<ref> <name>.syx` naming
where the ref is `bank0-slot25`/`perf015-part1`), a `<file>.je8086.json` sidecar per
patch, and a browsable `exploded/index.json` manifest; `--verify-exploded` is the
round-trip gate. Sweep: **46 banks -> 3689 files, 3689 ok / 0 bad**; suite 109
passed.

Two things the first real run exposed (again by cross-checking, not by trusting it):
1. **Self-ingestion**: `_bank_files` walked the root recursively, so the second run
   treated every exploded patch as a bank (3735 "banks", nested output, 3625
   verifier failures). Bank discovery now prunes `exploded/` (and any configured
   explode dir); `test_explode_is_idempotent_and_does_not_ingest_its_own_output`
   locks it down.
2. **Filename collisions**: the original `"<slot> <name>"` names collide across
   performances (every performance has a slot 1). The unique ref prefix removes the
   whole class, and the ref also matches the loader's `preset` enumeration order.

Verified in-app: the exploded tree registered as a **patch** library returns
individual patch hits from `search_library` with `patchEngine=je8086`, the role
verdict, tags and the parameter-derived description — i.e. the sidecar contract
works end to end (decode -> sidecar -> scan -> search).


## Proposal: the isolated state capture must not persist a boot-time stub (NEEDS SIGN-OFF)

Per AGENTS.md this touches plugin isolation + the render/export restore path, so it
is written up rather than implemented.

**Mechanism (pinned 2026-09-16, three-way measurement).** `Track.cpp` rebuild path:
for each plugin slot it calls `instance->getStateInformation(state)` and, when
`state.getSize() > 0`, writes `IDs::pluginState` (the "empty state must not clobber
last-good" guard passes a 233-byte stub). Slot creation then reads `pluginState` and
calls `setStateInformation(...)`, and the export builds its own graph — so the
render's fresh instance receives the stub. Measured with a real JE8086: isolation ON
with no capture -> renders follow the live plugin (peak 0.2063); after a capture ->
frozen at the plugin's default patch (peak 0.2455155849456787) even with MASTER
VOLUME = 0; in-process -> always follows the live plugin (stateBytes=0).

**Options**

| Option | Effort | Risk | What it changes |
|---|---|---|---|
| **C** diagnostic only | `45 min | low | receipt gains a "state equals the post-boot baseline" flag; no behaviour change; docs keep the `captureToTree:false` recipe |
| **D-lite** wait-for-change capture (recommended next) | `2-3 h | medium | deferred capture polls the child (bounded) and persists only a state that *changed* from the post-boot baseline; otherwise leaves `pluginState` unset and says so in the receipt |
| **D-full** explicit child readiness | `1 d | medium-high | new proxy message type + child hook; capture gated on "ROM/DSP boot finished" instead of a baseline diff |
| **A** document only | 0 | none | keep the recipe: `captureToTree:false` (or in-process) for emulations whose state does not round-trip |

**Why D-lite is the right shape**: a state identical to the fresh-boot state carries no
information, and restoring it demonstrably *resets* the plugin to a default patch
(rather than being a harmless no-op), so skipping both the persist and the restore is
strictly better. It needs no per-plugin size magic numbers: the baseline comes from
the child itself.

**Verification plan**: capture before/after injecting into real OsTIrus / NodalRed2x /
JE8086 slots; assert (a) the receipt reports the unchanged-baseline case, (b) the
rendered window follows the live instance (the same A/B harness used here in
`compositions/je8086-e2e`), (c) existing FX state save/load tests stay green
(`fx_midi_injection_test.cpp`, `plugin_state_save_load_test.cpp`, export suites).

**Open question for the user**: is JE8086 worth pursuing at all given DT1 dumps cannot
apply in this emulation (parameter writes work live but are not in the 233-byte state)?
If JE8086 parts are audition-only by decision, D-lite is still worth doing for the other
emulations (any slow-booting child captured early has the same hazard).

**Two real bugs the E2E cross-check caught** (both invisible to the unit tests)
1. `mcp-launch.bat` aborted before launching the engine: unescaped parentheses in
   an `echo` inside a parenthesized `if` block (the tool-surface sentinel message)
   made cmd run the remainder as a command -> "- was unexpected at this time." ->
   the launcher exited 255 -> every MCP call failed with "fetch failed", which
   looks exactly like a missing-DLL/engine-startup fault. Fixed by escaping the
   parens (commit f5f4cb9); found with a traced copy of the bat (echo on).
2. `timbre-lib/je8086_patch.py` silently dropped **41%** of SMF-wrapped messages:
   the varint length prefix was only stripped when its first byte had the high
   bit set, so payloads under 128 bytes (one-byte prefix) failed the DT1 header
   check and vanished. The C++ loader (320 SysEx events in Kulshan) vs the survey
   (128) is what exposed it. Census corrected: **10368 messages / 4980 entries**
   (was 6144/4276), 1087 performance names (was 383); usable patches unchanged at
   2674. Fixed in commit 814f898 (retry with one byte stripped), regression test
   added, survey + 46 sidecars regenerated.

- (b) `load_je8086_preset {trackId, slotIndex, filePath, preset?, recall?}` —
  DT1 bank → validated atomically → ONE patch unit injected into a JE8086 slot +
  CC0(USER)+PC recall — commits `a9d2807` (loader) and `4573d0f` (MCP-surface
  test). Per-patch by design (a 64-patch bank is 128 messages vs the 64-event
  cap and a drop-on-busy sysex lane). Remaining live verification: inject → save
  → export → measure against a real JE8086 instance, following the env-gated
  `FxMidiInjection` probe pattern.

