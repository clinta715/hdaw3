# Plan: JE8086 UserPatch DT1 probe + retarget fix

**Status:** **COMPLETE (2026-09-21)** — wrapper retarget fix built + installed
(md5 `84427AEA…`), L2b (HDAW recall removal) shipped, full `FxMidiInjection`
suite + the validation suites green. Readback contract established — see
"Readback finding" below.
**Owner:** orchestrator session; implementation via subagent (none registered in
this environment — executed directly by the orchestrator; see note below).
**Repos touched:** `D:\pdf\gearmulator-git` (primary), `D:\pdf\roo projects\hdaw3` (measurement harness + docs).

---

## Goal

Determine whether retargeting injected JE8086 **UserPatch** DT1 SysEx into the
**temp performance** patch area (the retarget `Controller::sendSingle` already
performs for the plugin's own browser) makes raw `.syx` patch-file loads change
the rendered sound — and if so, ship it as a wrapper fix.

---

## Root cause (proven in source, not inferred)

HDAW's `apply_preset` / `send_fx_midi` for JE8086 inject the patch `.syx`
**verbatim**. A real JP-8080 patch file carries address
`jeLib::AddressArea::UserPatch` (`jemiditypes.h:11`, `0x02000000` — the user
bank). The emulated device drops it:

| Evidence | Location | What it shows |
|---|---|---|
| `State::receive()` writes only `System` and `PerformanceTemp`; every other area returns `false` | `source/ronaldo/je8086/jeLib/state.cpp:489-526` | A UserPatch DT1 is **silently discarded** — correct hardware behaviour (a bank write does not change the sounding temp-performance patch) |
| `case jeLib::AddressArea::UserPatch: { } break;` | `source/ronaldo/je8086/jeJucePlugin/jeController.cpp:194-197` | The wrapper's matching no-op case |
| `Controller::sendSingle()` rewrites `UserPatch` → `PerformanceTemp \| PatchUpper/PatchLower`, `setAddress` + `updateChecksum`, sends, then `sendTempPerformanceRequest()` | `jeController.cpp:546-611` | **The retarget already exists** — the UI browser path |
| `PatchManager::activatePatch()` → `m_controller.sendSingle(...)` | `jePatchManager.cpp:234-239` | Confirms the UI uses `sendSingle` |
| `processMidiMessages()` called from `timerCallback()` | `jucePluginLib/controller.cpp:294-297`, `:668-675` | Parsing runs on the child's **message thread** → allocation in a retarget helper is safe (VERIFY, don't assume) |

This is the same class of bug already fixed twice in this codebase: Vavra dumps
retargeted to the `0x20` edit buffer, and `VirusController::activatePatch`
doing `modifySingleDump(_sysex, BankNumber::EditBuffer, program)`
("re-pack, force to edit buffer", `VirusController.cpp:926`).

**Baseline facts**
- git HEAD: `adba9cba` ("devices: JE8086 JPAR state chunk, Xenia injected-dump state mirror, public param sets")
- Installed `C:\Program Files\Common Files\CLAP\JE8086.clap` md5 = `15001C1FE9139F5FA83F4EDCFF5D7750`
- Build dir: `temp/cmake_vs2026`; target pattern `*JucePlugin_CLAP` (xenia bat uses `xtJucePlugin_CLAP`) — JE8086 target to be confirmed with `--target help`
- Prior additive harness to model: `source/ronaldo/je8086/jeStateProbe/` (untracked, standalone jeLib console)
- ROMs present: `jp8080_v1.04.bin`, `jp8000_v1.05.bin` (in `Program Files\Common Files\CLAP`)
- Real patch corpus: `D:\pdf\je8086` (3689 `.syx`)

---

## Hypothesis and falsification

**H:** An injected JE8086 DT1 fails to change the render *only* because it targets
the UserPatch bank area, which the emulator discards; retargeting it to
`PerformanceTemp | PatchUpper` (as `sendSingle` does) makes it audible.

**Falsified if:** the retargeted dump still produces no audible change at the
jeLib level (L1). Then the DT1 route is genuinely dead and the correct answer is
the HDAW-side decode→params route — not a wrapper change.

---

## L1 result (2026-09-20) — **CONFIRMED, reproducible**

Probe: `source/ronaldo/je8086/jeUserPatchProbe/` (new, additive, untracked;
modelled on `jeStateProbe`). Patch used: `D:\pdf\je8086\jp-8080 trance bank.syx`,
first patch block `0x02000000` (2 DT1 messages, 254 B + 18 B, name
`rb2k1 themystery`).

Method correction made during the probe: free-running oscillator/LFO phase makes
the **audio md5 unusable** as a criterion (two identical baselines differ
bit-for-bit). The verdict therefore uses **rms/peak deltas against a 3× measured
baseline drift** (three identical baseline windows), plus the `getState()` md5.

| Window | rms | peak | notes |
|---|---|---|---|
| A1 baseline | 0.00431577 | 0.013029 | baseline |
| A2 baseline | 0.00391796 | 0.012934 | baseline |
| A3 baseline | 0.00339960 | 0.011798 | baseline drift rms `0.000916`, peak `0.001231` |
| **B verbatim UserPatch DT1** | 0.00404877 | 0.012084 | Δrms `0.000267`, Δpeak `0.000945` — **inside drift** |
| **C retargeted → `PerformanceTemp|PatchUpper`** | 0.00577577 | 0.026529 | Δrms `0.001460`, Δpeak `0.013500` — **11× the peak drift** |

State mirror (`device.getState`, `StateTypeGlobal`):

- `S0` baseline: `641 B` md5 `6edbef02…`
- `S1` after verbatim: `641 B` md5 `6edbef02…` — **identical to S0**
- `S2` after retarget: `661 B` md5 `f0c77737…` — **changed**

```
VERDICT: verbatim=NOCHANGE retarget=CHANGE
VERDICT_STATE: verbatim=NOCHANGE retarget=CHANGE
```

Stability: run 3 and run 4 (fresh CWDs) produced **byte-identical**
rms/peak/md5 for every window — the emulator is deterministic once the
measurement timeline is fixed, so the within-run drift is reproducible phase
drift rather than randomness, and the verdict reproduces exactly.

Conclusions:

1. The failure is **purely the address area**: a UserPatch DT1 is discarded /
   never reaches the sounding temp-performance patch, both in the mirror
   (`State::receive`) and audibly.
2. The **existing `sendSingle` retarget is the fix**: rewriting each message to
   `PerformanceTemp | PatchUpper` (preserving the intra-block offset) with a
   recomputed checksum makes the dump apply — audio clearly changes and the
   mirrored state changes.
3. Since the retarget must be applied **per DT1 message** (the patch is split
   across messages), the wrapper fix must operate inside the SysEx parse path,
   not on the raw file.

Artifacts: `probe-out/userpatch/run{1..4}/` (logs + `userpatch-probe.wav` +
`ram_dump.bin`). No tracked file was modified — the temporary
`add_subdirectory(jeUserPatchProbe)` wiring in
`source/ronaldo/je8086/CMakeLists.txt` was reverted (`git status
--untracked-files=no` clean; git HEAD still `adba9cba`).

---

## L2a result (2026-09-20) — wrapper retarget fix SHIPPED

`jeController::parseSysexMessage`'s empty `case AddressArea::UserPatch` now
routes host-sourced DT1 dumps through `sendSingle` (the exact retarget the
plugin's own browser has always used):

```cpp
case jeLib::AddressArea::UserPatch:
    {
        constexpr size_t commandIndex = std::size(jeLib::g_sysexHeader);
        if (_source != synthLib::MidiEventSource::Device &&
            _sysex.size() > commandIndex &&
            _sysex[commandIndex] == static_cast<uint8_t>(jeLib::SysexByte::CommandIdDataSet1))
        {
            const auto part = getCurrentPart();
            sendSingle(_sysex, part < getPartCount() ? part : 0);
        }
    }
    break;
```

Dispatch facts verified before the edit (not assumed):

| Fact | Evidence |
|---|---|
| Injected host MIDI reaches `parseSysexMessage` | `Processor::addMidiEvent` routes `Enabled(Host, Editor, SysEx)` → `enqueueMidiMessages` → `processMidiMessages` → `parseMidiMessage`; `MidiRoutingMatrix` ctor enables `Host→Editor All` |
| Host MIDI carries `MidiEventSource::Host` | `processor.cpp:782` (both in-process processBlock and the isolated child) |
| The remote-control path does not swallow patch dumps | `SysexRemoteControl` requires manufacturer id `0x7D`; a JP-8080 dump is `0x41` |
| Device-origin output is never retargeted | guard `_source != Device` |
| Only DT1 (0x12) is retargeted; RQ1 (0x11) is left alone | command byte at `std::size(g_sysexHeader)` |

Build/install hygiene (Gate G6):

| Artifact | md5 |
|---|---|
| Backup (pre-fix, installed) | `15001C1FE9139F5FA83F4EDCFF5D7750` |
| New build + installed | `84427AEA4EE5F95E7E8CA8639C90DD20` |

Backup: `C:\Program Files\Common Files\CLAP\backup-hdaw-20260920-je8086-userpatch\JE8086.clap`.
Built from `D:\pdf\gearmulator-git` HEAD `adba9cba` + this one-file edit
(target `jeJucePlugin_CLAP`, Release, 36 158 464 bytes).

---

## L2b — the HDAW route's `CC0=1 USER + PC` recall must go (BLOCKER, found by run6)

`load_je8086_preset` / `apply_preset` append `CC0=1` (USER bank) + `PC`
"so it sounds immediately". On the emulated OS a program change **loads the
bank program into the current patch**, i.e. it overwrites the retargeted dump.
Probe run6 (fresh CWD, deterministic) adds three windows to settle it:

| Window | rms | peak | meaning |
|---|---|---|---|
| A1 boot | 0.00431577 | 0.013029 | boot patch |
| **E** recall only (`CC0=1 USER+PC`), from boot | 0.01328643 | 0.029898 | the recall alone loads a **different, louder** bank program |
| **B** verbatim UserPatch DT1 (after E) | 0.01329551 | 0.029954 | **NOCHANGE** (Δrms 0.000009) |
| **C** retargeted DT1 | 0.00561514 | 0.032155 | custom patch applied |
| **D** recall AFTER the retarget | 0.01326190 | 0.030053 | Δ vs C 0.00765, Δ vs E **0.0000245** → **lands back on E** |
| **F** retargeted DT1 again | 0.00551726 | 0.027289 | rms restored to C (Δ 0.000098; peak differs by transient phase) |

```
VERDICT: verbatim=NOCHANGE retarget=CHANGE
VERDICT_RECALL: changedBoot=YES clobbersDump=YES
```

So with the fixed wrapper the shipped route still plays the **wrong patch**: the
recall discards the dump and the persisted state would capture the recalled bank
program, not the imported one. **Decision:** remove the recall from
`runJe8086PatchFile` (and the `recall` tool arg) — the retargeted DT1 is now the
whole mechanism, exactly as the plugin's own browser does it.

**L2b shipped:** `runJe8086PatchFile` lost the `recallUserPatch` parameter and the
`CC0=1 USER + PC` emission; `McpTools_FxSlot.cpp` lost the `recall` tool arg (both call
sites) and its tool description now explains the retarget. No test passed `recall`.

### Success gates (L2b + verification)

- [x] **G2 PASSED (2026-09-21)** `FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`
      (~25–37 s): `presetSysexLen=383 pluginStateLen=7099`; fresh-boot rms 0.00257 vs
      patch 1 **0.00384** (peak 0.0757) vs patch 33 **0.00131** → the *dump content*
      drives the sound; `rebuildRoutingGraph()` child re-renders patch 33 to
      `|Δrms| = 5.8e-08` (persistence/replay, asserted). Extended to gate the agent
      readback contract (below).
- [x] **G5 PASSED (2026-09-21)** full `FxMidiInjection.*` with
      `HDAW_REAL_PLUGIN_TESTS=1`: **20 OK / 1 SKIP / 0 FAILED** (690 s). The SKIP is
      the pre-existing `XeniaEditBufferDumpChangesOfflineRender` env gap
      (`xenia-ab` dumps not present), unrelated to JE8086.
- [x] **G7 PASSED (2026-09-21)** `McpServer.SendFxMidiValidation`,
      `ApplyPresetResolver.*` (12) and `FxMidiInjection.Je8086LoaderValidatesBeforeQueueing`:
      **14/14 OK** (969 ms) after the signature change.
- [x] **G8 DONE** docs corrected: `hardware-va-suite.md` (capability row, §3 heading +
      caveat, §7 automation row + JE8086 `appliesVia`, new §9 subsection, 2026-09-18
      provenance superseded note), `core-synths-agentic-guide.md` (§2 table, §5
      loader table, §8 non-goals), `psytrance-composition-guide.md` (loader list,
      `load_je8086_preset` block, DT1 root-cause paragraph), `README.md`,
      `docs/plans/2026-09-16-je8086-preset-pipeline.md` (superseded note),
      `timbre-lib/README.md`, `fx-automation-engineer.md`, and the **stale top-level
      claim in `AGENTS.md`** (the hardware-VA loader-status paragraph still said "JE8086
      DT1 dumps do NOT apply"), plus `hardware-va-suite.md` §1 grid fact and two
      `2026-09-16-matrix-presets-handoff.md` lines annotated superseded.

### Readback finding (2026-09-21) — how an agent confirms a load landed

While gating the deferred capture, the gate polled three host-visible observables
after a `load_je8086_preset` and found only one usable:

| Observable | Result after the load | Verdict |
|---|---|---|
| Host param cache (`get_plugin_params` → 461 JE8086 params) | `hostParamsChanged=0` | **NOT a readback route** |
| Live `getStateInformation` (proxy `GET_STATE`) | constant 5320-byte blob, hash identical across 20 polls/10 s | **NOT a readback route** |
| Captured slot state — `IDs::presetSysex` (383 B) + `IDs::pluginState` (7099 chars) | present; a rebuilt child re-renders the **exact** last patch (`|Δrms| 5.8e-08`) | **the readback route** |

Neither of the first two is JE8086-specific: the param cache never echoes
SysEx-loaded patch parameters for these emulations (identical finding, and the
same "diagnostic only" treatment, in `OsTIrusPresetChangeReflectsInChildParams`).
The durable mechanism is the 2026-09-18 **preset-sysex persistence**: `sendFxMidi`
stores the raw DT1 dumps in `IDs::presetSysex`
(`HDAW::encodeFxPresetSysex`), and `Track.cpp` **replays** them into every fresh
child at rebuild/restore — which is why the patch survives even though the live
child's serialized state can look unchanged.

**Agent contract:** after `load_je8086_preset`, confirm with
`poll_fx_capture` (status/`stateBytes`/`hasPluginState`), then verify by render
(`save_project` → `export_audio`) — do **not** expect the param list or the live
child state blob to move. The gate asserts this via the rebuilt-from-tree render
equality (see the `rawState` comment in `fx_midi_injection_test.cpp`).

### Dependency map (graphify, HEAD `95187a47` — graph current)

- Blast radius of `runJe8086PatchFile`: `registerFxSlotTools()` (tool surface),
  `resolvePresetRoute()`, `PresetRoute.h` community. Not a God node.
- Callers after L2b: `McpTools_FxSlot.cpp` `load_je8086_preset` and `apply_preset`
  (the `recall` arg was removed from both). No other callers; no test passed
  `recall`.
- Downstream: `ProjectCommands::sendFxMidi` (unchanged) → TrackFXSlot MIDI queue
  → isolated child → patched CLAP.
- Projections: the FX-slot ValueTree (`captureToTree`) and the JPAR plugin state.
  No ReadModel/audio-graph change. No SPSC change.
- Pitfall gates: none of 1–16 apply beyond the existing test-harness seams
  (message pump already started by `test_main`; no engine/DSP/render change).

---

## Probe design — two levels, cheapest decisive first

### L1 — jeLib console probe (no CLAP build, no install, deterministic)

New additive target `source/ronaldo/je8086/jeUserPatchProbe/`, modelled on
`jeStateProbe` (RomLoader::findROM, 88200 Hz, block 128, held note, RMS windows,
`AsyncWriter` WAV + state md5).

1. Boot `jeLib::Device`; send `State::createSystemRequest()` +
   `sendPerformanceRequest(PerformanceTemp, UserPerformance01)` (as
   `Controller::onStateLoaded`).
2. Held note → **window A** (baseline RMS/peak).
3. Pick a real `.syx` from `D:\pdf\je8086`; deliver the **verbatim UserPatch
   DT1** → **window B**. Expect **no change** (reproduces the bug at jeLib level).
4. Deliver the **same dump retargeted** to `PerformanceTemp | PatchUpper` with
   checksum recomputed (replicate `sendSingle`'s lines 572-591 verbatim) →
   **window C**. Expect **audible change**.
5. Emit per-window rms/peak/md5 + `device.getState()` before/after.

This is decisive, runs in seconds, and carries **zero risk to the installed CLAP**.

### L2 — CLAP wrapper fix (only after L1 confirms)

1. Implement the `AddressArea::UserPatch` case in
   `jeController::parseSysexMessage`: retarget via a helper extracted from
   `sendSingle` (avoid duplicating address/checksum logic), then
   `parsePatch(_sysex, part)` (`jeController.cpp:396-444`) so host params update.
   Part selection: mirror `activatePatch`'s non-multi behaviour (current part).
2. Build `jeJucePlugin_CLAP` Release from `temp/cmake_vs2026`.
3. Back up the installed CLAP + record md5; install; record the new md5.
4. HDAW MCP A/B (measurement harness only — no HDAW code change):
   `add_instrument_part JE8086` → baseline `export_audio` → `apply_preset` with
   the same `.syx` → `export_audio` → delta. **Control:** load the same patch
   through the plugin's own browser (uses `sendSingle`); both routes should match.
5. Regression: the 20 `FxMidiInjection.*` gates; param writes + JPAR round-trip.

---

## Dependency map

- **Blast radius:** gearmulator only — JE8086 wrapper + jeLib probe. No HDAW
  source changes. HDAW is used solely as the measuring instrument via MCP.
- **Upstream:** `PatchManager::activatePatch` (UI) and HDAW-injected SysEx both
  funnel through `Controller::parseSysexMessage` / `sendSingle`.
- **Downstream:** host param cache (`updateHostDisplay`), the JPAR state chunk,
  and the rendered audio.
- **Thread:** `parseSysexMessage` ← `parseMidiMessage` ← `processMidiMessages`
  ← `timerCallback` (message thread). **VERIFY at probe time**; if any path is
  the audio thread, the retarget must be allocation-free or hoisted.
- **Projections:** none in HDAW (no ReadModel / audio-graph change).
- **Cross-process/state:** the retarget changes the *live* sound; the existing
  `JPAR` chunk (git HEAD `adba9cba`) must still carry it to fresh offline
  children (G3). No bulk state over the control pipe — unchanged.
- **God nodes in scope:** none (gearmulator has no graphify graph; HDAW `src/` untouched).

---

## Pitfall gates (hdaw-guard)

- **Gate 2 — unimplemented path:** the empty `UserPatch` case is exactly an
  unimplemented path. Satisfied by tracing to the observable (G1/G2).
- **Gate 15 — stale binary:** after install, verify the **installed** md5 matches
  the built artifact before trusting any measurement (the docs' md5 discipline).
- **Gate 3 — audio-thread safety:** *conditionally* applies; resolved by the
  thread check above. If message thread → allocation safe.
- **Gate 14 — cross-process state:** the retarget must not regress the JPAR
  round-trip; covered by G3.
- Non-applicable: 1/6/10 (no HDAW processor state), 11/12 (no message pump or
  graph mutation), 13 (no DSP-state write), 16 (no lifecycle call).

---

## Success gates (all must pass with evidence)

- **G1 (L1, decisive): PASSED 2026-09-20.** verbatim UserPatch DT1 → no change
  (inside the measured baseline drift, state md5 unchanged); retargeted →
  audible change (Δpeak 11× drift; state md5 changed). Evidence table above.
- **G2 (L2):** HDAW offline render after injected `.syx` differs from boot AND
  from a second patch; the persisted slot state replays the exact patch after a
  rebuild. **PASSED** (asserted via the rebuilt-from-tree render equality). NB:
  the `list_fx_params` readback does **not** reflect the patch — see
  "Readback finding".
- **G3:** state round-trips — `poll_fx_capture` reports `ok` with bytes > 0, and
  a save→reload→render reproduces the patch (`presetSysex` replay + JPAR).
- **G4:** the plugin-browser control path still works and produces an equivalent
  render to the retargeted injection.
- **G5:** full `FxMidiInjection` suite green (**21 gates**, 2026-09-21), no new reds.
- **G6:** binary hygiene — prior CLAP backed up, installed md5 recorded, build
  reproducible, probe harness additive/untracked.

---

## Decision rule

- **L1 confirms** → implement L2, ship the wrapper fix, update
  `docs/va-suite-status-log.md` + a handoff; consider upstreaming (the empty
  case is a genuine upstream gap).
- **L1 falsifies** → abandon the wrapper fix; implement the HDAW-side
  decode→params route using the existing `je8086_patch.py`
  (`parse_dt1`/`decode_params`) + `je8086_param_index_map.json` + the
  `appliedParamOverrides` ledger.

---

## Non-goals

- Writing to the UserPatch **bank/flash** — inert by design in both the emulator
  and on hardware for the live sound.
- Any HDAW source change (unless L1 falsifies, which swaps the plan).
- Other devices (Xenia/Vavra/Nord/Virus) — separate workstreams.

---

## Steps (dispatch order)

1. **Subagent A:** L1 probe target (`jeUserPatchProbe/`, additive; CMakeLists +
   main.cpp). Run all three windows; report rms/peak/md5 per window. **Gate G1.**
2. **Orchestrator:** review G1 evidence; decide go/no-go on L2.
3. **Subagent B (if go):** L2 wrapper retarget + build + install (backup + md5).
   **Gates G6.**
4. **Orchestrator:** HDAW MCP A/B + control + save/load. **Gates G2/G3/G4.**
5. **Orchestrator:** run `FxMidiInjection` suite. **Gate G5.**
6. **Orchestrator:** docs (`va-suite-status-log.md` + handoff) and graph/dirty-tree note.

---

## Effort and risk

| Phase | Effort | Risk |
|---|---|---|
| L1 probe | ~2-4 h | **Very low** — additive, untracked, no install |
| L2 wrapper + build + install | ~3-5 h | **Medium** — patches an installed binary; reversible via backup + md5; wrapper-only, no audio-thread/DSP change |
| Measurement + regression + docs | ~2-3 h | Low |

Total ≈ 1 day. The only correctness risk is the retarget target assumption
(`PatchUpper` for injected dumps, matching UI part-0); G4's control path pins it.

---

## Artifacts

- gearmulator (`D:\pdf\gearmulator-git`):
  - `source/ronaldo/je8086/jeUserPatchProbe/{main.cpp,CMakeLists.txt}` (new, untracked,
    **not wired into the tree** — re-add `add_subdirectory(jeUserPatchProbe)` to
    `source/ronaldo/je8086/CMakeLists.txt` to build it, and copy
    `jp8000_v1.05.bin` / `jp8080_v1.04.bin` next to the exe).
  - `source/ronaldo/je8086/jeJucePlugin/jeController.cpp` — the L2 retarget fix
    (uncommitted).
  - Probe runs: `probe-out/userpatch/run1..run6/{probe.log,ram_dump.bin,out/*.wav}`
    (run6 is the recall evidence; each run needs a **fresh CWD** because jeLib
    persists `ram_dump.bin` there).
  - Built/installed CLAP: `bin/plugins/Release/CLAP/JE8086.clap` (md5 `84427AEA…`).
- hdaw3: this plan; `src/mcp/PresetRoute.h` + `src/mcp/McpTools_FxSlot.cpp` (the
  `CC0=1 USER + PC` recall removed, `recall` arg dropped);
  `tests/unit/engine/fx_midi_injection_test.cpp`
  (`FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`); doc updates in
  `docs/va-suite-status-log.md`, `docs/core-synths-agentic-guide.md`,
  `docs/psytrance-composition-guide.md`, `README.md`, plus a handoff.
