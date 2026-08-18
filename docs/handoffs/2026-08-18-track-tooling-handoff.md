# Handoff — Track-building via HDAW tools vs LLM hand-coding (2026-08-18)

## Context: what triggered this

This session we added a 14th track to the Polywave Shift project ("Test TyrellN6
Part" — a VST3 instrument part) and rendered it together with the whole
composition. It worked end-to-end, but almost every step was **LLM hand-coding
against the raw RPC surface**, not HDAW's own tooling. This handoff captures the
workflow we actually ran, the design questions for making future tracks "more by
HDAW, less by hand", and the bugs/quirks the task surfaced. It is a thinking
artifact — no code changes were made for it.

## The workflow as it ran today (the thing to compress)

Each step maps to the RPC we had to call by hand:

| Step | What we did (by hand) | RPC / mechanism |
|---|---|---|
| 1. Pick plugin | Query instruments, probe 3 candidates with 8 s solo renders + peak check to find an audible default patch | `plugin.getInstrumentPlugins`, `export.audio` + WAV peak scan |
| 2. Create track | `addTrack`, name it | `project.addTrack` |
| 3. Add instrument | `addFxSlot` with the plugin's exact JUCE identifier | `project.addFxSlot { type:"plugin", pluginId }` |
| 4. Compose part | Generated a Lead phrase (A minor), 32 beats at beat 0 | `composition.generatePhrase { style:"Lead", scaleRoot:9, scaleMode:0 }` |
| 5. Extend across song | Painted 14 copies, spacing 32, covering beats 0–448 | `project.paintClips` |
| 6. Make faders authoritative | Disabled Volume automation on ALL 14 tracks (tracks 8/10 had ENABLED volume automation that silently overrides faders — the earlier "export volume bypass" rabbit hole) | `project.setAutomationEnabled { lane:"Volume", enabled:false }` |
| 7. Gain stage | Solo-rendered the new track, measured RMS, set fader = target/measured; reused the previously-measured balanced faders for the other 13 | `export.audio` solo window + `project.setTrackVolume` |
| 8. Render | Full 218 s export, then verify | `export.audio` + WAV stats/band analysis |
| 9. Verify | Solo peak > threshold, full-mix non-clipping, kick/hats bands present, plugin on/off RMS delta | `verify_final.cjs`, `band_check.cjs` |

Everything the engine can already do for us: generate phrases/chords/progressions
(`PhraseGenerator` + `ArrangementGenerator` + `RhythmPatternGenerator`, all
already RPC + MCP + UI), batch paint/duplicate clips, render sections, host
plugins (isolated or in-process). What's missing is the **glue** — a tool that
performs the multi-step unit of work as one command.

## Design questions to think about

### 1. A one-shot "compose instrument part" tool
The natural unit of work we did by hand was: *pick instrument → add track → add
instrument FX → compose a phrase → place it across the arrangement → gain-stage →
render*. That should be one tool. Candidate shape:

```
composition.addInstrumentPart {
  trackName, style ("Lead"/"Arpeggio"/"Pad"/…),
  pluginId (optional → default "internal fm_synth"),
  lengthBeats, placement ("wholeSong" | startBeat+count),
  targetRms, verify: bool
}
```

Do we scope it to "one track with one part", or extend it to "compose a whole
section/arrangement for N tracks" (ArrangementGenerator already does sections)?

### 2. Plugin preset selection
Today we use the plugin's **default preset** and probe for audibility. The engine
already exposes `plugin.getProgramCount / getProgramName / setCurrentProgram` (and
MCP tools) — so preset selection is available but not wired into any compose flow.
Should the part-composer accept `{ presetName }` / `{ programIndex }`, and should
there be a fast preset-audition tool (render 2–3 s, report peak/character) so
silent-at-default plugins aren't a blocker?

### 3. Auto gain staging (the biggest hand-coded step)
Our gain staging was a closed-form, mechanical loop: solo-render a window → measure
RMS → set fader = target/measured. The engine could do this itself:

```
track.autoGainToTarget { targetRms, windowSeconds }   // solo render → set fader → report
```

or a "loudness match" variant (fader so a track matches a reference track's RMS).
Edge cases found this session: tracks that clip at fader 1.0 (FM instruments,
preL up to 8), transient percussion (peak vs RMS targets differ), and the
master-sum clipping at transients — the loop needs a global-scale + post-scale
step (we re-rendered at ×0.85 then post-scaled). This is the step that most
obviously wants automation.

### 4. "Make fader authoritative" tool
The recurring gotcha: **enabled volume automation overrides the fader in playback
and export** (Tracks 8/10 of Polywave Shift; this exact confusion produced the
earlier "export volume bypass" investigation). A one-line tool
`track.setFaderAuthoritative` (disable all Volume lanes, like we did across the
whole project) would stop future sessions re-discovering it.

### 5. Where does the composer live — engine, MCP, or UI?
HDAW's rule is MCP parity for any user-facing capability. A `composition.*`
engine command is scriptable and testable (gtest + MCP tool in one change), and
the UI can later wrap it (PhraseGeneratorDialog is the existing pattern — a dialog
mode for "add instrument part" fits the bottom-panel/transport-bar idiom). Decide
the primary authoring surface first (scripting/testing likely wins).

### 6. Reuse: part templates
The TyrellN6 part is a reusable pattern (plugin track + phrase + painted
placement + gain-staged fader). Saving it as a **part template** (an XML snippet /
tree fragment) and instantiating on demand turns composition into assembling
building blocks. Questions: how do templates interact with undo units and the
delta-vs-fullSync split (track add = fullSync; clip add = delta)? Should default
projects ship templates?

### 7. Verification belongs in the workflow
We verified with: solo peak > threshold, full-mix non-clipping, band-presence,
plugin on/off delta. A composer tool should either self-verify or have a
`compose.verify` companion returning the same numbers — automated verification is
what makes "add a part" trustworthy as a testing tool.

### 8. Track-type defaults
`addTrack` currently creates a generic track. Typed presets (instrument track →
default MIDI channel, note range, velocity range, name/color conventions) would
give subsequent steps sane defaults and cut the hand-config.

### 9. Time-unit ergonomics
Beats vs seconds is the recurring friction (lesson #1, re-hit every session):
RPC clip/note params are beats, export is seconds, snapshot reports beats, and a
painted 448-beat pattern extended the render to 218 s vs the "211.3 s" mental
model. A tool that accepts bars/beats uniformly plus a `paintToProjectEnd` helper
removes the manual arithmetic.

## Bugs and issues surfaced during this task

1. **Engine crash on client disconnect mid-export — FIXED.** A killed WS client
   during `export.audio` crashed the whole engine (Qt 6.11.1 nested-loop bug:
   `QWebSocketPrivate::processData` NULL-deref; socket freed re-entrantly inside
   the export handler's `loop.exec()`). Fixed in `Router_Export.cpp`
   (`QEventLoop::ExcludeSocketNotifiers`) + `FrontendServer.cpp` (QPointer guard)
   + regression test. Full writeup:
   `docs/handoffs/2026-08-18-disconnect-during-export-fix.md`.
   **Resolved (follow-up session, same day):** the residual "engine died after
   ~6 exports" was NOT a separate teardown leak — repeated failing exports do
   not crash the engine and leak no plugin-host children. The remaining failure
   was a NEW, deterministic bug: the render-sequence bake on a large project
   (13 tracks / 771 clips) legitimately takes ~17–21 s, exceeding the export's
   FIXED 15 s bake-wait timeout, so every export of a loaded polywave project
   spuriously failed with `Render graph bake timed out after 15000ms` — which
   the probe scripts read as engine instability. Fixed in
   `ExportManager::computeBakeWaitMs`: the default bake-wait now scales with
   clip count (floor 15 s, 50 ms/clip, cap 120 s); `HDAW_EXPORT_BAKE_TIMEOUT_MS`
   still overrides. Plan + evidence:
   `docs/plans/2026-08-18-export-bake-timeout-scale.md`.
2. **"Export volume bypass" was NOT a bug** — the project ships ENABLED volume
   automation on Perc Low + DX7 Pad that overrides faders. Documented as
   project-intent. The recurring confusion argues directly for idea #4
   (setFaderAuthoritative).
3. **`pluginFormat` missing from the RPC snapshot.** `read.snapshot` FX-slot JSON
   exposes pluginId + name but not pluginFormat; the project XML persists it. To
   assert VST3-vs-CLAP from tooling (which we needed for the test) you must read
   the XML or infer from the identifier. Minor API gap — worth exposing.
4. **Plugin default presets are hit-or-miss.** Identity (sampler) and others are
   near-silent at default; we had to probe each candidate with a render + peak
   check to find an audible instrument (TyrellN6). No fast preset-audition
   tooling exists. See idea #2.
5. **Engine instability under rapid probe workloads.** Observed engine death
   during probe batches (`probe_all`/`probe_all2`). Confirmed root causes:
   bug #1 (killed clients → disconnect crash, fixed) plus the bake-timeout bug
   above — every probe render of the loaded polywave project spuriously failed,
   which looked like engine death. Both are fixed. Probe scripts should still
   stay crash-tolerant (restart engine, resume) as a defensive habit.
6. **Verification-threshold miscalibration (test-design lesson, not a code bug).**
   "Plugin audible ⇒ full-mix RMS delta > 10%" fails at the song's densest
   section: RMS adds in quadrature, so a gain-staged plugin can only move the
   climax's full-mix RMS by ~1–3% even at fader 1.0. Measure at a moderately-dense
   section, or use plugin-solo-render evidence instead.
7. **Beats vs seconds friction** (re-hit of lesson #1) — see idea #9. The
   painted 448-beat part made the final render 218 s, not 211.3 s.

## Status

- All session work is complete and verified: engine crash fix (tests 902/902),
  and the plugin-part test render (`projects/polywave_shift_plugintest.hdaw` →
  218 s `polywave_plugintest.wav`, peak -1.1 dBFS, 0 clips, plugin audible).
- **Follow-up (same day) — bake-timeout fix landed** for bug #1's open question
  and issue #5: the export render-bake wait now scales with project size
  (`ExportManager::computeBakeWaitMs`), so large-project exports no longer
  spuriously fail with "Render graph bake timed out after 15000ms". Verified:
  904/904 engine tests, `*Export*` 11/11, runtime repro (load polywave + export
  with default settings) succeeds. See
  `docs/plans/2026-08-18-export-bake-timeout-scale.md`.
- This handoff is a design-thinking artifact only — no code was changed for it.
- Next session: pick one idea to prototype (likely #1/#3 — the compose-part tool
  with auto gain staging — or #4 the one-line fader-authoritative tool).