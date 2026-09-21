# Handoff: JE8086 (JP-8080) UserPatch DT1 — dumps now apply (wrapper retarget + route recall removal)

Written 2026-09-20; full-suite verification + readback contract 2026-09-21.
Self-contained: what shipped, the evidence, the traps, what is left.

## Outcome

`load_je8086_preset` / `apply_preset` on a real JP-8080 `.syx` now **changes the
render to the file's patch**. Two changes were needed, both shipped/installed:

1. **gearmulator wrapper retarget** (`jeController.cpp`, uncommitted in
   `D:\pdf\gearmulator-git`) — host-sourced `UserPatch` DT1 dumps are retargeted to
   `PerformanceTemp | PatchUpper`, the transform the plugin's own patch browser has
   always used (`Controller::sendSingle`). Installed `JE8086.clap` md5
   `84427AEA4EE5F95E7E8CA8639C90DD20` (backup of the pre-fix build:
   `C:\Program Files\Common Files\CLAP\backup-hdaw-20260920-je8086-userpatch\`,
   md5 `15001C1FE9139F5FA83F4EDCFF5D7750`).
2. **HDAW route** (`src/mcp/PresetRoute.h`, `src/mcp/McpTools_FxSlot.cpp`) — the
   `CC0=1 USER + PC` recall appended after the dump is **removed** (and the `recall`
   tool arg deleted). A JP-8080 program change does not just select a patch, it
   **loads** the bank program into the current patch, so it discarded the dump.

## The two wrong beliefs this closed

1. *"`jeLib/device.cpp` routes live MIDI to the DSP thread, so the DT1 patch State is
   never fed."* **Wrong.** The dump is parsed; `jeController::parseSysexMessage` had an
   **empty `case AddressArea::UserPatch`**. A real patch file carries the UserPatch
   **bank** address (`0x02000000`), and neither the emulated OS nor the host state
   mirror folds a bank write into the sounding temp-performance patch — so the write
   was a silent no-op.
2. *"`capturedToTree=0` means the capture failed."* It means the capture did not
   complete **synchronously**; the realtime capture lands after the call returns. Poll
   `poll_fx_capture` (`status=ok`). The JE8086 slot does capture: `presetSysexLen=383`,
   `pluginStateLen=7099` (JPAR).

## Evidence (additive jeLib probe, `jeUserPatchProbe`, deterministic)

`jp-8080 trance bank.syx` patch 1 = 2 DT1s (`0x02000000` len 254, `0x02000172` len 18).
Baseline drift floor over 3 windows: rms 0.00340–0.00432, peak 0.01180–0.01303.

| window | rms | verdict |
|---|---|---|
| B verbatim dump | 0.00405 | **NOCHANGE** (below drift; state mirror unchanged 641 B) |
| C dump retargeted to `PatchUpper` | 0.00578 | **CHANGE** (~11× drift; mirror 661 B) |
| E `CC0=1 USER+PC` recall only | 0.01329 | changes the boot sound (loads a louder bank program) |
| D recall after the retarget | 0.01326 | Δ vs C 0.00765, Δ vs E **0.0000245** → **recall discards the dump** |
| F retarget re-applied | 0.00552 | rms restored to C (last write wins) |

Full trail, per-run logs and the gate list:
`docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md`. Probe artifacts:
`D:\pdf\gearmulator-git\probe-out\userpatch\run1..run6\` (each run needs a **fresh
CWD** — jeLib persists `ram_dump.bin` there).

## Verification

`FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender` (env-gated,
`HDAW_REAL_PLUGIN_TESTS=1`): fresh-boot rms 0.00257 vs patch 1 **0.00384** (peak
0.0757) vs patch 33 **0.00131**; a child rebuilt from the tree re-renders patch 33
to `|Δrms| = 5.8e-08` (asserted). Run:
`build\hdaw_tests.exe --gtest_filter=FxMidiInjection.Je8086*`.

Final suite runs (2026-09-21, final binary):
- `FxMidiInjection.*`: **20 OK / 1 SKIP / 0 FAILED** (the SKIP is the pre-existing
  Xenia env gap).
- `McpServer.SendFxMidiValidation` + `ApplyPresetResolver.*` +
  `Je8086LoaderValidatesBeforeQueueing`: **14/14 OK**.

## Readback: how an agent confirms a load landed (established 2026-09-21)

Gate work on the deferred capture settled which observables are usable:

| Observable | After `load_je8086_preset` | Use it? |
|---|---|---|
| `get_plugin_params` (461 JE8086 params) | no change (`hostParamsChanged=0`) | **no** |
| live child state (`GET_STATE` via proxy) | constant 5320-byte blob / 10 s poll | **no** |
| slot `IDs::presetSysex` (383 B) + `IDs::pluginState` (7099 chars) | present; rebuilt child re-renders the exact patch | **yes** |

Not JE8086-specific: the param cache never echoes SysEx-loaded patch parameters
for these emulations (same "diagnostic only" note in
`OsTIrusPresetChangeReflectsInChildParams`). The durable mechanism is the
2026-09-18 **preset-sysex persistence** — `sendFxMidi` stores the raw DT1 dumps in
`IDs::presetSysex`, and `Track.cpp` **replays** them into every fresh child at
rebuild/restore. That is why the patch survives even though the live child state
blob can look unchanged.

**Agent contract:** `poll_fx_capture` → then verify by render
(`save_project` → `export_audio`). Do not expect the param list, or the live child
state blob, to move.

## Traps for the next session

- **The `CC0+PC` recall pattern is wrong for JP-8080** (and any device where a
  program change is a *load*). Keep it out of loaders; the wrapper retarget is the
  delivery mechanism.
- **Never measure a bank-write effect via the host param cache** — a bank write does
  not change it even when it applies. (That is how the first "does not apply"
  conclusion was manufactured.) Use a render A/B against a drift floor, or
  `getState`. Corroborated 2026-09-21: the live child state blob can also stay
  byte-identical across a load that audibly changed — only the persisted
  `IDs::presetSysex` replay is a durable readback (see the Readback section).
- **Audio md5 is not a valid criterion** for these emulations (free-running
  oscillator/LFO phase makes identical baselines differ bit-for-bit). Use repeated
  baseline windows → a drift floor → a 3×-drift threshold on rms/peak.
- **A silent or un-baselined render makes every A/B equal** (lesson 25 in
  `AGENTS.md`): always assert the baseline is audible first.
- Probe WAVs are gearmulator artifacts (`probe-out/`); HDAW composition renders go to
  the repo-root `compositions/` (AGENTS.md rule).

## What is left (transparency roadmap, not blockers)

The user's goal is to make these limits **transparent to an agent/MCP user**, not
necessarily to remove them. Candidate next slices, in order:
1. **Device Capability Table + normalized apply receipt** — one table of
   `{device, route, verifiedBy, persistsVia, caveats}` and a shared receipt
   `{applied, route, paramsApplied, persistedVia, persistedBytes, audibility:{verdict,
   delta, noiseFloor}, warnings}` so an agent never has to guess whether a preset
   landed.
2. **Shared noise-floor verifier** (the "drift floor / 3× threshold" helper named in
   `docs/handoffs/2026-09-18-gearmulator-custom-builds.md`) so every `*AB` gate uses
   the same audibility criterion.
3. **Intent-level tools** (`set_device_patch` / `set_device_param` /
   `apply_device_character`) resolving to the per-device route.
4. **Warmup pre-warm** for the 12 s Virus OS warmup.
5. Remaining genuinely-unfixable items stay documented, not fixed: Xenia/Vavra
   run-to-run jitter, Nord patch **voice selection**, the 12 s warmup,
   `non-params aren't automatable` (derived-parameter collapse).
