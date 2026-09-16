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
- (b) `load_je8086_preset {trackId, slotIndex, filePath, preset?, recall?}` —
  DT1 bank → validated atomically → ONE patch unit injected into a JE8086 slot +
  CC0(USER)+PC recall — commits `a9d2807` (loader) and `4573d0f` (MCP-surface
  test). Per-patch by design (a 64-patch bank is 128 messages vs the 64-event
  cap and a drop-on-busy sysex lane). Remaining live verification: inject → save
  → export → measure against a real JE8086 instance, following the env-gated
  `FxMidiInjection` probe pattern.

