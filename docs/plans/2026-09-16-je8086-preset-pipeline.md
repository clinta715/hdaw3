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
- [ ] G1: `pytest timbre-lib/test_je8086_patch.py` green (incl. real-library
      spot checks when the bank dir is mounted).
- [ ] G2: sweep parses **46/46 files**, 0 checksum failures, ≥4200 named
      entries; every sidecar's `engine`/`schema`/`mappedParams`/
      `roleCheck`/`unmapped` present.
- [ ] G3: survey reports per-bank counts, role histogram, placeholder (INIT)
      share, duplicate-name cross-bank matches, and the psy-relevant shortlist.
- [ ] G4: byte-stable re-run (same inputs → identical JSON) and no absolute
      path leakage in committed artifacts.
- [ ] G5: `git status` shows only `timbre-lib/` + docs additions.

## Effort/risk
~0.5d. **Risk LOW** — pure Python tooling + JSON artifacts; no engine, DSP,
RPC, or ValueTree surface is touched. The only HDAW-side change is the
1-line sidecar-extension add (deliverable 5), which lives in the file-library
scan path, not the audio graph.
