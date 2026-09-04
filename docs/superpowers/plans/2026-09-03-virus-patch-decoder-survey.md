# Virus Patch Decoder + Sub-Synth Survivability Survey

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the
> `hdaw-guard` skill before any code change.

**Goal:** Write a standalone Python decoder in `timbre-lib/` that parses all
four Access Virus patch containers found in `D:\pdf\Virus Presets` (B/C single
patch, TI bank, TDM plugin chunk, Std-MIDI bank, and the VHC file) and emits,
per patch: (a) the patch name, (b) the sub-synth-mappable parameters as a JSON
that could later drive `sub_synth` slots, and (c) an explicit `unmapped` list
(FM amount, ring mod, LFOs, keytrack, FX) — then surveys the whole library and
reports how much of each patch survives the mapping.

This is **slice 1** of the Virus-adaptation question: it answers "how much of
our actual patch library maps onto the sub_synth" with zero engine risk, before
any C++ loader (slice 2) or DSP additions (slice 3, FM/LFO/keytrack) are
considered. Python-only, confined to `timbre-lib/`, no CMake/MCP/frontend/C++
changes.

**Tech Stack:** Python 3 (numpy, no ML deps; stdlib `wave`-free — this is pure
binary parsing). gtest/pytest for the new tests.

---

## File Structure

### Create
| File | Responsibility |
|------|---------------|
| `timbre-lib/virus_patch.py` | Format parsers + patch→sub_synth param mapping + CLI survey |
| `timbre-lib/test_virus_patch.py` | pytest: format parsing, mapping, survey invariants |

### Modify
| File | Change |
|------|--------|
| `timbre-lib/README.md` | Document the virus decoder + survey usage |

No C++/CMake/MCP/frontend files touched. The existing `virus_library_report.json`
in `D:\pdf\Virus Presets` is stale (path `E:\presets\_virus\`, garbled names)
and is NOT regenerated in place — the survey writes its own report to a new path.

---

## Success Gates

- [x] Gate 1: `python -m pytest timbre-lib/test_virus_patch.py -q` passes. — 34 passed.
- [x] Gate 2: Each format parser has at least one fixture test with an expected
      name + a checksum-free structural assertion (size, sysex delimiters, chunk
      magic) — using real files from `D:\pdf\Virus Presets` copied into the repo
      under `timbre-lib/testdata/virus/` (small: 1×267B, 1×710B, 1×524B slice,
      1× small .mid). — 6 fixtures (267B B/C, 524B TI block, 2×710B TDM, .mid, .vhc); checksum verified against real data.
- [x] Gate 3: TI bank survey: all 24 `.syx` banks parse, 128 patches each =
      exactly 3072 patches, every patch yields a name (non-empty ASCII) and a
      `mapped`/`unmapped` split; report is schema-valid. — CORRECTED with evidence: 19 banks (the `m01`/`multis*.syx` files are 168320B multi dumps, excluded by the 67072B size filter) → 2432 TI patches, 0 failed, name_ok 2432.
- [x] Gate 4: TDM survey: all ~104 TDM patches parse with readable names. — 104/104, name_ok 104.
- [x] Gate 5: Std-MIDI: all 19 `.mid` banks unwrap to sysex bank dumps; patch
      count >= 1 per bank; names extracted. — 2375 patches, 0 failed, name_ok 2344 (blank name fields at legit empty slots 104/105/114).
- [x] Gate 6: Stable output — identical input + args produce byte-identical
      `json.dumps(report, sort_keys=True)` across two runs (mirror the probe
      analyzer's gate). — verified cross-run + matches committed report.
- [x] Gate 7: `python -m py_compile` on both new files; `ruff check` clean
      (if available). — py_compile exit 0, ruff "All checks passed!".
- [x] Gate 8: The survey report is written to `timbre-lib/virus_survey.json`
      (not into `D:\pdf\Virus Presets\`), and `git status` shows the diff
      confined to `timbre-lib/` + this plan doc. — confirmed.

---

## Dependency Map

- **Blast radius:** `timbre-lib/` only. Pure Python binary parsing, no import
  of `lib_analyze.py`/`analyze_probe.py`/`llm_stage.py`. No `.timbre.json`
  sidecar writes.
- **Upstream:** none — standalone module + CLI.
- **Downstream:** slice 2 (a C++ `VirusSysexImport` mirroring `Dx7SysexImport`)
  consumes the *mapped JSON shape* as its spec, but does not import this module.
- **Reference:** `timbre-lib/analyze_probe.py` (CLI/report pattern, stable-JSON
  gate), `src/engine/Dx7SysexImport.{h,cpp}` + `McpTools_FmSynth.cpp`
  (`fm_synth_import_sysex`) as the eventual slice-2 shape.
- **Knowledge graph:** Python scripts are outside codebase-memory's indexed
  scope; no graph refresh needed.

---

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path silently failing):** a parser must either return
  a patch dict with mapped+unmapped+name, or raise/explicitly record a parse
  error — never silently yield an empty patch. Enforced by tests asserting
  `name` non-empty and the error key present on bad inputs.
- **Gate 9 (validation at trust boundary):** file existence checked; loader
  catches read/parse failures into a per-patch `error` field; the survey counts
  parsed vs failed per format and fails the run if a whole format fails.
- **Gate 15 (verify the artifact):** gates run actual pytest output and the
  real survey report, not code reading.

---

## Design

### `timbre-lib/virus_patch.py`

```text
SUPPORTED_FORMATS = {"bcsingle", "tibank", "tdm", "stdmidi", "vhc"}

parse_bcsingle(data) -> dict   # 267B single patch
parse_tibank(data)   -> list[dict]   # 67072B = 128 x 524B sysex blocks
parse_tdm(data)      -> dict   # DigiVirusSS01 chunk; ASCII name
parse_stdmidi(data)  -> list[dict]   # MThd/MTrk, unwrap run-length sysex
parse_vhc(data)      -> list[dict]   # sysex at byte 0 (Virus HE)

map_to_sub_synth(patch) -> {"mapped": {...}, "unmapped": [...]}
survey(paths_by_format) -> report dict
```

### Sub-synth mapping table (the contract slice 2 will implement in C++)

Mapped (existing `sub_synth` params 0–23):
- osc1 wave → param 0, osc1 level → 1
- osc2 wave → 2, osc2 level → 3, osc2 detune cents → 4 (Virus fine/detune → cents)
- sub level → 5, sub octave → 6
- filter cutoff → 7, resonance → 8, drive/saturation → 9
- amp ADSR → 10/11/12/13, output → 14
- legato → 15, portamento → 16
- filter type (LP/BP/HP 12/24dB → our LP/HP/BP/notch) → 17
- filter env ADSR → 19/20/21/22, filter env amount → 18 (Virus filter env → semitones)

Unmapped (reported explicitly, never silently dropped):
- `osc2_fm_amount`, `ring_mod`, `lfo1`, `lfo2`, `keytrack`, `filter_slope_24db`,
  `osc_sync`, `fx_chorus`, `fx_delay`, `fx_reverb`, `mod_matrix`, `noise_level`

Survey output shape:

```json
{
  "generated_at": "...", "sources": {"tibank": 24, "tdm": 104, ...},
  "formats": {
    "tibank": {"patches": 3072, "parsed": 3072, "failed": 0,
               "name_ok": 3072, "mapped_params": {"avg": 18, "min": 12, "max": 24},
               "top_unmapped": ["osc2_fm_amount", ...]},
    ...
  },
  "totals": {"patches": N, "parsed": M, "unmapped_any": K}
}
```

### Note on the byte layouts (from reconnaissance — validate during impl)

- **TI bank** (`Access_Virus_TI\*.syx`, 67072B): `67072 / 128 = 524` exactly.
  Byte 0 starts `F0 00 20 33 01 00 10 01 00 0A`; byte 524 starts a new block
  `F0 00 20 33 01 00 10 01 01 0A`; file ends `... 2D F7`. → each 524B block is
  a self-contained sysex patch (F0…F7), so slice at 524B boundaries.
- **B/C single patch** (`Sysex format\*.syx`, 267B): header
  `F0 00 20 33 01 00 10 01 00 07 00 00 00 62`, single `F7` at 266, name
  (`WELCOME`) ends near byte 258. → 14B header + 252B payload + F7.
- **TDM** (`Virus TDM format\*`, 710B): Pro Tools chunk magic
  `DigiVirusSS01midi`/`DigiVirusSS01vals`, ASCII patch name present
  ("1979 Gangs", "Untitled"), only ~100/710 bytes vary between patches —
  locate the parameter block by diffing two files and reading the name chunk.
- **Std-MIDI** (`*.mid`, ~35–67KB): `MThd` + `MTrk`, sysex events inside are
  run-length (0x8n-style) encoded; unwrap per MIDI spec then parse the F0…F7
  dump (the classic Virus `.mid` bank = one F0 bank dump per file).
- **VHC** (`Virus HE format\*.vhc`, 34176B): begins `F0 00 20 33` at byte 0.

---

## Steps

1. Copy small fixtures from `D:\pdf\Virus Presets` into
   `timbre-lib/testdata/virus/`: one 267B sysex, one 710B TDM, a 524B slice cut
   from a TI bank, one small `.mid`, the `.vhc`. (Keep each <= ~35KB.)
2. Write `test_virus_patch.py` first (failing): format parsing with expected
   names, mapping table spot-checks, stable JSON, error paths.
3. Implement `virus_patch.py` iteratively until tests pass. Validate the TI
   slice boundary and name offsets against real banks; validate TDM name chunk
   by diffing two TDM files.
4. Run the full survey against `D:\pdf\Virus Presets` → write
   `timbre-lib/virus_survey.json`. Verify counts (Gate 3/4/5).
5. Verify gates: stable JSON, py_compile, ruff, git status confined.
6. Update `timbre-lib/README.md` with usage.

---

## Verification Commands

- `python -m pytest timbre-lib/test_virus_patch.py -q`
- `python timbre-lib/virus_patch.py --survey "D:\pdf\Virus Presets" --out timbre-lib/virus_survey.json`
- `python -m py_compile timbre-lib/virus_patch.py timbre-lib/test_virus_patch.py`
- `ruff check timbre-lib/virus_patch.py timbre-lib/test_virus_patch.py` (if installed)
- `git status --short` (diff confined to `timbre-lib/` + this plan doc)