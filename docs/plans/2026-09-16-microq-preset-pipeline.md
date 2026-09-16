# MicroQ (Waldorf microQ / Vavra) Preset Pipeline

## Goal
Give the rhythm-lab microQ library the same treatment as the JP-8080 and NL2X banks:
a decoder, sidecars and a survey, so 528 Waldorf patches become searchable and
curatable in HDAW.

## Evidence (gearmulator 2.2.9 source/mqLib + all 528 real files, 2026-09-16)
- **528 files, every one exactly 392 bytes** = `Dumps[DumpType::Single].dumpSize`
  (mqstate.h). Header `F0 3E 10 00 10 <buffer>` (Waldorf 0x3E, IdMicroQ 0x10,
  `SysexCommand::SingleDump = 0x10`).
- Name: 16 chars at offset **370** (`mq::g_singleNameOffset`); category: 4 chars at
  **386** (`mq::g_categoryOffset`, written by `State::setCategory`). 521/528 names
  match their filenames, and offset 371 (the 'q' variant) matches 0/528.
- Parameters: bytes 7..369 (`IdxSingleParamFirst = 7`), a dense block. There is **no
  sound-parameter name table** in mqLib, so parameters are reported raw and listed
  under `unmapped` (never dropped).
- Checksum: **not validated by the emulation** (`wLib/wState.h convertTo` compares
  size only); these files do not follow the Waldorf 7-bit-sum rule (249/528), so it
  is informational and never rejects a file.
- Categories in the data: Arp 147, Pad 86, Lead 81, Bass 79, Atmo 36, FX 26, Keys 24,
  Poly 19, Misc 18, Perc 4 + singles - a much stronger role signal than a name guess.
- Provenance: "MicroQ Patches by Chris Jones" (rhythm-lab.com), free to use with credit.

## Deliverables
1. `timbre-lib/microq_patch.py` - `--dump/--json/--survey/--sidecars/--role/--with-bytes/--verify`.
2. `<patch>.vavra.json` sidecars next to every patch (schema `hdaw.microq.patch.v1`,
   engine `vavra`) whose `mappedParams` carry `category:<X>` and the pack, because
   FileLibraryManager turns the first two entries into search tags.
3. `timbre-lib/microq_survey.json` + a README section.
4. `FileLibraryManager::applyPatchSidecar` also reads `.vavra.json` (engine fallback
   `vavra`), with a test.

## Success Gates
- [x] G1: decoder parses **528/528** files, 0 failures; `--verify` reports
      **528 ok / 0 bad** (name, category, parameters and sha1 all match).
- [x] G2: 528 sidecars written; `FileLibraryPatch.VavraPatchSidecarIngested` proves a
      patch-library scan surfaces `patchEngine=vavra`, `roleVerdict` and a
      `category:Bass` tag, and that a search finds it (51 tests green in the suite).
- [x] G3: survey reports categories, roles, folders, duplicate names (9) and the
      informational checksum census.
- [x] G4: no engine/DSP change beyond the sidecar-extension lookup; no render path
      touched. Loader NOT shipped (below).

## Loader: measured, NOT shipped
Injecting a real dump into a live Vavra slot works at the transport level
(`send_fx_midi {kind:"sysEx", bytes:[...]}` -> `queued=1`; the state captures at
440 B), but:
- the exported audio did not change measurably (peak 0.02603 -> 0.02591, rms 0.001293
  -> 0.001284), and
- **Vavra exposes no host parameters** (`list_fx_params` -> `{"params":[]}`), so the
  param-cache method that works for OsTIrus/JE8086 is unavailable here.

The emulation's own State *does* receive external dumps
(`mqLib/device.cpp`: `m_state.receive(responses, _ev, State::Origin::External)`),
unlike the JP-8080, so the patch may well apply - but there is no observation channel
in HDAW to prove it. Remaining check (interactive): open Vavra's editor, inject, and
watch the LCD. A `load_vavra_preset` tool is a small addition once that passes.

## Effort/risk
`0.5 day, all tooling/mostly Python plus a two-line sidecar-lookup extension. Risk
LOW: no DSP, no render/export path, no isolation change.
