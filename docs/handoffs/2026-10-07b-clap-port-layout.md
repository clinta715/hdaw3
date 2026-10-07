# Handoff — 2026-10-07b: the silent CLAP export — a CLAP audio-port contract bug

**Status:** diagnosed, fixed, verified end-to-end on this box.
`McpServer.ExportAudioWithClapPluginDoesNotHang` — red for months and repeatedly written off as
environmental — **passes**. Plan/evidence: this file; lesson 54 in `docs/lessons-learned.md`;
baseline note in `docs/build-and-testing.md`.

**Reported symptom:** an isolated CLAP slot published its full parameter set (775 params for
Surge XT), `audition_plugin`/`export_audio` reported success, and the rendered audio was exactly
zero.

---

## 1. Root cause

`CLAPPluginInstance` collapsed **every** output port a CLAP plugin declares into **one** host
audio port whose channel count was the **sum** of all of them:

```cpp
// buildBuses()  — before
for (i < audioPortsExt->count(plugin, /*is_input=*/false)) {
    audioPortsExt->get(plugin, i, false, &info);
    numOutputs += info.channel_count;                   // SUM of every port
}
// processBlock() — before
audioOut.channel_count = min(numOutputs, buffer.getNumChannels());   // 6 for Surge XT
process.audio_outputs = &audioOut;
process.audio_outputs_count = 1;                        // ONE port
```

Surge XT declares **three** output ports — `Output` 2ch, `Scene A` 2ch, `Scene B` 2ch. Handed a
single 6-channel port it returns **`CLAP_PROCESS_ERROR` (= 0)** from every `process()` and writes
nothing. The status was discarded, so this was silent by construction.

## 2. How it was found — a control host, not by reading HDAW

Every isolation-layer hypothesis (proxy transport, state round-trip, MIDI delivery, wrong-thread
lifecycle calls, the patch's own `volume = 0`) was tested and cleared. The answer came from
`tools/clap_min_host.c` — ~340 lines of C against `clap/clap.h`, **no JUCE, no HDAW** — which
renders one plugin and prints the peak *and* the `process()` status. One variable at a time:

| host output layout | `process()` status | peak |
|---|---|---|
| 1 port, 2ch | 1 `CONTINUE` | 0.444 |
| **1 port, 6ch (HDAW's shape)** | **0 `ERROR`** | **0.000** |
| 3 ports × 2ch (the fix) | 1 `CONTINUE` | 0.422 |

## 3. The fix

- **One host port per declared plugin output port** (`CLAPPluginInstance::processBlock`), built in
  port order: port channel *c* of port *p* maps to host channel `runningCh + c`, spilling to a
  preallocated `portScratch_` (sized in `prepareToPlay`, never allocated on the audio thread) when a
  port's channels exceed what the host buffer has left. Port 0 still maps to the DAW's first
  channels, so **single-port plugins are unaffected by construction**.
- **The status is no longer discarded**: `CLAP_PROCESS_ERROR` logs throttled (first 5, then every
  500) with the plugin id — this failure class can never be silent again.
- **`params->flush(plugin, nullptr, nullptr)` guarded.** Null event lists are illegal per the CLAP
  spec and clap-helpers-based plugins dereference them: against Surge XT that exact call
  **segfaults** (exit 139, isolated in the control host). Now passes valid empty lists
  (`EmptyInputEvents` / `EmptyOutputEvents`).
- Input side left deliberately unchanged (one port) — every installed plugin matches or tolerates
  it; the asymmetry is commented as unmeasured rather than "fixed" blind.

## 4. Evidence

**Live, through HDAW's own paths** (engine built here, HTTP 18765):

| check | result |
|---|---|
| `McpServer.ExportAudioWithClapPluginDoesNotHang` | **PASSED** (was FAILED) |
| `audition_plugin` on a Surge XT slot | `rms=0.061 peak=0.265 audible=1` (was `rms=0 peak=0 audible=0`) |
| full `export_audio` of that slot → `mix_report` | `peak 0.264 / rms 0.053`, bands sub 2.0 / bass 2698 / body 5410 / high 179, `clipping:false` |

**Port matrix** (`tools/clap_port_matrix.py`, max peak over 4 runs per shape — these emulators have a
free-running clock and vary run to run):

| plugin | ports | old (1 summed port) | new (1 port per plugin port) |
|---|---|---|---|
| **Surge XT** | 3 | **0.00000, 4/4 ERROR → SILENT** | **0.47438 → AUDIBLE** |
| Dexed | 1 | 0.13157 | 0.13157 (identical) |
| JE8086 | 1 | 0.06009 | 0.06009 (identical) |
| Vavra | 3 | 0.04028 | 0.04028 (identical) |
| Xenia | 2 | 0.18730 | 0.25326 (both audible) |
| NodalRed2x | 2 | 0.42469 | 0.44726 (both audible) |
| Osirus | 3 | 0.00014 | 0.00014 — **silent in BOTH, see §5** |
| OsTIrus | — | crash | crash — **see §5** |

The old layout was a **no-op** for the six single-port plugins (bit-identical renders) and fatal for
exactly the one multi-port plugin that validates its layout. That is why "it works for the plugins I
tried" was never evidence, and why the bug survived so long.

**Suites** (after the fix): `hdaw_tests_engine` **1410 passed / 15 failed** — the 15 are the
documented pre-existing environmental set, and the count rose 1464 → 1469 for the 5 new
`CLAPPortLayout` tests; `hdaw_tests_mcp` **425 passed / 0 failed / 4 skipped**; `hdaw_tests_frontend`
3 failures, all in the documented set; new `CLAPPortLayout.*` + existing `CLAPParamEvents.*` 6/6.

## 5. Still open — NOT port-layout, do not conflate

- **`Osirus` renders ~0.00014 in BOTH layouts.** A closed filter at boot: the known "read `Cutoff`
  back and reopen it" trap from `docs/psytrance-va-and-production.md` §5c. A patch/preset concern,
  not a host-contract one.
- **`OsTIrus` crashes inside its own emulator** (`ESAI transmit underrun in slot 0 from frame 0`)
  before any render, in both layouts. Needs its own investigation; nothing in this change touches it.

## 6. Files

| Path | What |
|---|---|
| `src/engine/CLAPPluginInstance.{h,cpp}` | per-port output layout, status logging, `params->flush` guard |
| `tests/unit/engine/clap_port_layout_test.cpp` **(new)** | hermetic stub `CLAPPortLayout` suite (5 tests) pinning the port shape handed to `process()` |
| `tests/CMakeLists.txt` | register the new test |
| `tools/clap_min_host.c` **(new)** | the no-JUCE diagnostic host (header carries the build command and every env knob) |
| `tools/clap_port_matrix.py` **(new)** | drives that host over every installed `.clap` under both layouts |
| `docs/lessons-learned.md` | **lesson 54** (full narrative) |
| `AGENTS.md` | lesson 54 one-liner index |
| `docs/build-and-testing.md` | the mcp baseline row corrected: this was a REAL bug, not environmental |

## 7. Method notes worth keeping

- A silent-render verdict from the system under test is **not** evidence about the plugin. Build the
  smallest host that exercises exactly one plugin and vary **one** host-side variable at a time.
- Have that host write its verdict to a **file**: NodalRed2x emits 300 KB+ of emulator logs, which
  corrupts line-oriented capture (this cost two confusing "regressions" during the investigation).
- Distrust a single non-deterministic run: these emulator plugins vary ±20% in peak run-to-run, so
  take a max over repeats before calling anything SILENT or a regression.
