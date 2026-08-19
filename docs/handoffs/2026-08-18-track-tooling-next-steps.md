# Handoff — Track tooling: next steps for a fresh context (2026-08-18)

## Purpose

The session behind `docs/handoffs/2026-08-18-track-tooling-handoff.md` proved
that building a track (pick plugin → addTrack → addFxSlot → generatePhrase →
paintClips → gain-stage → render) is currently done by **LLM hand-coding against
the raw RPC surface**, not by HDAW's own tooling. That handoff posed 9 design
questions. This session fixed the two blockers it surfaced and shipped the first
tool. This file is the briefing for a **fresh context** to continue that work.

## Baseline: committed and verified (all on `main`, version 0.23.1)

Full engine suite **908/908 green** (168 suites, exit 0). Working tree clean
except gitignored `projects/`. Commits:

| Hash | Change |
|------|--------|
| `76bd865` | **fix(engine): scale export render-bake timeout with project size** — the spurious "Render graph bake timed out after 15000ms" on large projects. `ExportManager::computeBakeWaitMs` (floor 15s, 50ms/clip, cap 120s); `HDAW_EXPORT_BAKE_TIMEOUT_MS` still overrides. Plan: `docs/plans/2026-08-18-export-bake-timeout-scale.md`. |
| `1072e09` | **fix(fm):** remove double -69 semitone offset + pitch EG neutral (DX7). |
| `2e37eb6` | **fix(frontend):** guard client disconnect during export (Qt 6.11.1 nested-loop NULL-deref; `ExcludeSocketNotifiers` + QPointer). See `docs/handoffs/2026-08-18-disconnect-during-export-fix.md`. |
| `ddfff36` | docs: track-building automation handoff + `export_volume_bypass_test` diagnostic (DISABLED, needs gitignored polywave project). |
| `9f0526d` | **feat(engine): `setFaderAuthoritative`** — disable ALL Volume automation on a track (`-1` = whole project) so faders win in playback/export. Engine command + `project.setFaderAuthoritative {trackIndex, authoritative}` RPC + MCP `set_fader_authoritative {trackId, authoritative}` (one shared command path). Automation points kept, one undo unit. Plan: `docs/plans/2026-08-18-set-fader-authoritative.md`. |

## The remaining agenda (priority order)

### 1. `composition.addInstrumentPart` + auto gain staging (the big one) — design Q1/Q3
The natural unit of work done by hand. Candidate shape (from the track-tooling
handoff):

```
composition.addInstrumentPart {
  trackName, style ("Lead"/"Arpeggio"/"Pad"/…),
  pluginId (optional → default "internal fm_synth"),
  lengthBeats, placement ("wholeSong" | startBeat+count),
  targetRms, verify: bool
}
```

- All building blocks already exist and are RPC+MCP+UI: `PhraseGenerator`,
  `ArrangementGenerator`, `RhythmPatternGenerator`, `composition.generatePhrase`,
  `project.paintClips`, `project.addFxSlot`, `project.addTrack`.
- **Auto gain staging** is the biggest hand-coded step: solo-render a window →
  measure RMS → fader = target/measured. Edge cases found this session: FM
  instruments clip at fader 1.0 (preL up to 8) → need a global-scale +
  post-scale step (we re-rendered at ×0.85 then post-scaled); transient
  percussion (peak vs RMS targets differ). See `docs/plans/2026-08-18-export-bake-timeout-scale.md`
  for the solo-render/export path.
- Decide the surface first (Q5): engine command (scriptable + testable, gtest +
  MCP in one change) is likely the right primary surface; UI can wrap later
  (`PhraseGeneratorDialog` is the existing pattern).

### 2. Plugin preset selection / audition — design Q2 / handoff bug #4
Default presets are hit-or-miss (Identity/sampler near-silent; we probed
candidates with 8s solo renders + peak check). Engine already exposes
`plugin.getProgramCount / getProgramName / setCurrentProgram` (and MCP tools) —
just not wired into any compose flow. A fast preset-audition tool (render 2-3s,
report peak/character) would unblock silent-at-default plugins.

### 3. `setFaderAuthoritative` — DONE (do not redo)
Reference `9f0526d`. If the UI ever wants a mixer button, wrap the RPC.

### 4. `pluginFormat` missing from the RPC snapshot — handoff bug #3 (minor)
`read.snapshot` FX-slot JSON has pluginId + name but not pluginFormat (the XML
persists it). To assert VST3-vs-CLAP from tooling you must read the XML.
Worth exposing as a snapshot field.

### 5. Design Q5–Q9 (lower priority, think-piece)
- **Q5 surface**: engine vs MCP vs UI — scripting/testing likely wins (see #1).
- **Q6 part templates**: reusable XML/tree fragments; interaction with undo units
  and the delta-vs-fullSync split (track add = fullSync, clip add = delta).
- **Q7 built-in verification**: a composer tool should self-verify (solo peak,
  non-clipping, band-presence, on/off delta) — `compose.verify` companion.
  **Calibration lesson:** "plugin audible ⇒ full-mix RMS delta > 10%" FAILS at the
  song's densest section (RMS adds in quadrature — a gain-staged plugin moves the
  climax full-mix RMS only ~1-3%). Measure at a moderately-dense section or use
  plugin-solo-render evidence.
- **Q8 typed track presets**: `addTrack` currently creates a generic track; typed
  presets (instrument track → default MIDI channel/note/velocity range) cut hand-config.
- **Q9 beats-vs-seconds ergonomics**: RPC clip/note params are beats, export is
  seconds, snapshot reports beats (lesson #1, re-hit every session). A
  `paintToProjectEnd` helper / uniform bars-beats acceptance removes manual math.

### 6. Standing technical debt (from AGENTS.md lessons)
- **Lesson 20**: per-run pipe/shm namespace (or held-name skip) so a stale
  engine can't collide with proxy tests — permanent guard is a standing follow-up.
- **Qt 6.11.1**: the disconnect-during-export fix is a workaround for a genuine
  Qt bug; re-verify if the engine ever moves off Qt 6.11.1.

## Operational context a fresh session MUST know

### Audio-device environment (bit us this session)
A machine-wide audio failure appeared mid-session: fresh processes got
`Error opening Primary Sound Driver: "No driver"` (empty WASAPI scan, lesson
17/22) → `routingManager` null → ~29 device-dependent tests failed
(`getTrack(0)==nullptr`) + FrontendServer tests failed to bind (cross-test
cascade). It was **environmental, not code** (identical failures on a stash
baseline; 908/908 passed before). **Recovery:** elevated
`Restart-Service Audiosrv, AudioEndpointBuilder` (or reboot). **Diagnosis rule:**
if the full suite suddenly shows a cluster of fast `getTrack(0)==nullptr`
failures + 2s FrontendServer bind failures, the audio stack is down again —
verify a fresh engine's log shows `saved audio device restored`, don't blame the code.

### Export / bake specifics
- Large projects legitimately need ~50s to start an export (offline graph build
  + render-sequence bake on the message thread). The timeout now scales; the
  `HDAW_EXPORT_BAKE_TIMEOUT_MS` env override still works for probing.
- `export_volume_bypass_test.cpp` and the polywave-dependent tests are
  **DISABLED** — they need the gitignored `projects/` files (present locally,
  not in git). Don't enable them without the project files.

### Hygiene
- Never leave HDAW/plugin-host processes running (kills don't propagate to
  plugin-host children on Windows; a stale engine breaks proxy tests — lesson 20).
- Probe/automation scripts must stay crash-tolerant (restart engine, resume).
- Commit style: conventional (`fix(engine):`, `feat(fm):` …). Plan-first per
  hdaw-guard; every code change runs through a subagent with success gates.

## Where to look
- `docs/handoffs/2026-08-18-track-tooling-handoff.md` — the 9 design questions + bug list.
- `docs/handoffs/2026-08-18-track-building-automation.md` — the think-piece.
- `docs/plans/2026-08-18-export-bake-timeout-scale.md`, `docs/plans/2026-08-18-set-fader-authoritative.md` — the two executed plans.
- `docs/handoffs/2026-08-18-disconnect-during-export-fix.md` — the Qt bug fix.
- `src/engine/PhraseGenerator.h`, `src/engine/RhythmPatternGenerator.h` — generative toolkit.
- `src/mcp/McpTools_Audio.cpp` (registerAutomationTools), `src/frontend/router/Router_*.cpp` — RPC/MCP wiring patterns.
- `tests/unit/engine/export_bake_timeout_test.cpp`, `tests/integration/mcp/mcp_server_test.cpp` — test patterns for large-project export and MCP tools.
