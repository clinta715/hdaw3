# Microwave XT (Waldorf II/XT / Xenia) Preset Pipeline

## Goal
Give `D:\pdf\microwave` the same treatment as the other VA-suite libraries: a decoder,
sidecars and a survey, so 1791 Waldorf microwave patches become searchable in HDAW.

## Evidence (gearmulator 2.2.9 `source/xtLib` + the real library, 2026-09-16)
Two containers:
- **`.µsb` — 64 KB bank image**: 256 records x 256 bytes, the 16-char patch name in the
  last 16 bytes of each record (verified: 6 files, exactly 65536 bytes, a printable
  name at record offset **240**). ~1536 named patches.
- **`.mid` — Standard MIDI File of Waldorf SysEx** (`F0 3E 0E <dev> <cmd> ... F7`,
  IdWaldorf 0x3E, IdMw2 0x0E), matching `xtLib/xtMidiTypes.h`: SingleDump (0x10, 265-byte
  dumps per `xtState.h Dumps[DumpType::Single].dumpSize`), WaveDump (0x12), MultiDump
  (0x11), WaveCtlDump (0x13). Only SingleDump carries patches (255 of them), and the
  patch name sits at a fixed offset **242** inside the dump body
  (`Beachphasing SCDP`, `Fatt Bass    SCDk`, ...) - a trailing-window scan finds binary
  garbage instead, which is why the offset is anchored.

Real library: **7 files, 1791 patches, verify 7 ok / 0 bad**; roles other 1415, bass 107,
pad 71, lead 70, pluck 61, fx 39, keys 28 (roles are name-keyword based - the µsb records
carry no category field, unlike the microQ dumps).

## Deliverables
1. `timbre-lib/microwave_patch.py` — `--dump/--json/--survey/--sidecars/--role/--verify`,
   plus `microwave_patch.parse_usb` / `parse_mid` for the two containers.
2. `<bank>.xenia.json` sidecars (schema `hdaw.microwave.bank.v1`, engine `xenia`) whose
   `mappedParams` carry `patch:<first patch name>` / `patch:<second>`, because
   FileLibraryManager turns those into search tags.
3. `timbre-lib/microwave_survey.json` + README and VA-suite doc entries.
4. `FileLibraryManager::applyPatchSidecar` also reads `.xenia.json` (engine fallback
   `xenia`); the suffix is a data-driven addition to the same list that already covers
   `.nl2x.~ / ~.je8086.~ / ~.vavra.`, so no new test case was added for it.

## Success Gates
- [x] G1: 7/7 files parse; **1791 patches**; `--verify` reports **7 ok / 0 bad**.
- [x] G2: `test_microwave_patch.py` 7 passed (synthetic µsb + SMF, sidecar contract,
      round-trip, duplicate names, real-library spot check with format-level anchors).
- [x] G3: full timbre-lib suite **126 passed**; library suites **51 passed** after the
      sidecar-suffix change.
- [x] G4: no engine change beyond the sidecar lookup; no DSP/render path touched.

## Bugs found while building it (both in my own code, caught by the tests)
1. `verify` compared patches through a **name-keyed dict**, but names repeat heavily
   (256 records named `u101 new`, repeated `Bass`, ...) -> it compared the wrong record
   and reported sha1 mismatches. Now it compares the ordered (name, sha1) sequence.
2. The name check accepted a **pure padding run**: `str.strip()` does not remove NULs, so
   a 16-NUL window passed the printable test. All decoders now require at least one real
   printable character, and the fallback scanner requires a name to START with one (it
   was returning `15 NULs + "H"`).
Also: a stale-sidecar race (I ran the sweep and `--verify` in parallel) and my own
moving-target test expectations (exact patch counts) -> the spot check now anchors on
format-level facts (7 files, >=1535 µsb records, verify clean).

## Effort/risk
~0.5 day, all tooling/mostly Python plus a one-line sidecar lookup. Risk LOW: no DSP, no
render/export, no isolation change. Xenia still publishes **no host parameters**, so this
pipeline buys search/curation, not automation (consistent with the other VA devices
except JE8086).
