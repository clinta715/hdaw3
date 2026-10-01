# Handoff — 2026-09-30 evening: O1 device fix, internal-engine device maps, `dub_stab_122` composed

**Status:** all three work items complete, verified, committed separately. Supersedes the
"open issues" ordering of `2026-09-30-va-recipes-and-remaining-issues.md` for **O1** (fixed)
and documents a new silent-acceptance defect found on the way.

---

## 1. O1 — `set_audio_output_device` unknown-name refusal (P6 CLOSED)

`src/common/AudioDeviceNameApply.h` (new, header-only): validation + apply + ROLLBACK in one
place; both surfaces (`McpTools_Settings.cpp`, `Router_Audio.cpp`) call it, so refusal text is
identical by construction. Unknown name → `unknown output device "X" — available: "A", "B"`;
empty `""` is the documented close spelling; open failure rolls back to the snapshotted setup
(rollback failure is loud). QSettings skipped on every failure. 6 gtests
(`AudioOutputDeviceRefusalTest.*`), green across openable-device and listed-but-unopenable
runs on this box (the latter exercises the rollback branch). Plan Phase 6 gates ticked
in `docs/plans/2026-09-30-bug-fixes-from-ion-rift-session.md`.
**Same defect remains in `set_audio_input_device` / `audio.setInputDevice` — open follow-up.**

## 2. Internal-engine device maps (191 params / 15 engines)

`list_device_params` previously served only the 5 VA CLAPs; `fm_synth`/`sub_synth`/core FX
answered "file not found". `timbre-lib/build_device_map.py` gained a source-table evidence path
(parses the C++ param defs in `TrackFXSlot.h` / `InternalDelay.h` / `ChainLibrary.cpp` — provenance
stronger than the VA corpus route) and now emits all 15 internal `fxType` maps (802 params across
the 20-engine index, unclassified 0, every entry citing `file:line`). One semantic defect found
and fixed in review: delay `Feedback` was intented `riser` by the generic grammar → now
`delay-throw` (per-engine override + regression pin); zero `riser` intents remain in internal maps.
`DeviceParamMap.cpp` projects `index/default/min/max/source/enum` to BOTH surfaces (parity ledger
byte-unchanged). Pins: route/durability (`set_internal_fx_param`/`valuetree` — never the VA
plugin route), lossless projection, delay enum doc. 14/14 device-params tests.
**Live E2E verified on the rebuilt binary** — first restart served the stale 12:32 binary
(`dsh-build-fast.bat test` HAD relinked at 20:06; lesson 15 in reverse: trust the mtime, not the
build command's mode name); second restart picked it up.

## 3. `dub_stab_122` — dub techno composition (user's spec, delivered)

`compositions/dub_stab_122/dub_stab_122.hdaw` + `_v7.wav`. Am7 stabs + first inversion, 3/16
piano chops (DESMOS C4 one-shot on a sampler — the FM engine's DEFAULT seeded patch is
full-sustain (`FmSynthEngine.cpp:44-58`); rather than author and audition a decay-envelope
DX7 patch, the verified DESMOS one-shot was used — loaded DX7 patches can carry decay
envelopes), per-onset
delay automation (230 onsets,
mix/fb cycling, deduped timestamps, Division 4 = dotted-1/8), descending A-G-F-E bass, periodic
sub, sparse percussion. **`mix_verdict ok:true`** (audible/clipping/introBlast/modulation all
green; peak 0.665, ceilingHit 0, master limiter threshold 0 dB ceiling 0.95, master gain 0.70).
Note: v3's "pump 1.32" was measured on hard-clipped audio — not a valid reference.

## 4. NEW DEFECT (filed via report_issue, NOT fixed): relative-path `save_project` silent no-op

On the stdio surface, `save_project {"filePath":"relative/path.hdaw"}` returns **`ok` and writes
NOTHING** (checked repo `compositions/`, engine CWD `%TEMP%`). Absolute path works (after the
parent dir exists; a missing dir correctly errors). Lesson-34 class — accepted-arg-dropped with a
success payload. A composition session that trusts the relative `ok` loses everything on the next
engine relaunch.

## 5. Incident + process notes

- **Engine died mid-session** (process gone, auto-relaunched empty). **Cause UNPINNED** — no
  fresh WER dump or capture; the lazy-mcp idle-sleep (clean exit 0) is consistent with the idle
  gap but is NOT confirmed, and a code crash is not excluded. Cost: the first composition pass
  (never saved). The phased checkpoint-save discipline (save after every slice) made recovery
  mechanical; keep it for any long composition.
- **graphify marker hygiene:** `.graphify_python` briefly pointed at a nonexistent interpreter
  and carried a BOM; restored to the verified-working uv-tools interpreter, BOM-free. `.graphify_root`
  rewritten to `.` by `update --force` and manually restored to the absolute path (documented gotcha).
- `report_issue` filed: the `save_project` silent no-op above.
- Scratch: `.tmp_compose/` (delay-automation payloads), superseded renders `_v2..v6.wav` kept as
  clipping/gain-anomaly diagnostics. `set_audio_input_device` fix is the natural next small item.
