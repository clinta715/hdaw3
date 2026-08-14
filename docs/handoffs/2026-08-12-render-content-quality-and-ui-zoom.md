# HANDOFF: Render content quality (drums/intonation), project-data overlaps, timeline zoom

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, MSVC/VS generator, JUCE 8 via CMake FetchContent, React 19 + TS frontend.
> **MANDATORY:** before ANY code change, load the `hdaw-guard` skill (AGENTS.md) — plan-first, success gates, subagent execution, lesson-15 stale-binary discipline.
> **Predecessor session (same day):** `docs/handoffs/2026-08-12-vst3-inproc-resolution-and-fxtest-failure.md` — read its RESOLUTION section first. All engine/plugin plumbing work is done and verified (746/746 tests green); the working tree is UNCOMMITTED. Do not re-litigate it.

## Context

The user listened to the delivered 240 s render
(`D:\pdf\roo projects\hdaw3\polyrhythm_aminor_120bpm.wav`, rendered 13:30 from
`polyrhythm_aminor_120bpm.hdaw3` in default isolated mode) and reported four
problems. The project is a 10-track / 475-clip generative demo committed as raw
files in `ff04bb7` (no generator script in-repo; it was built via MCP/composition
tools in an earlier session):

| # | Track | Instrumentation (FX_CHAIN plugin slots) |
|---|-------|------------------------------------------|
| 0 | Lead | Identity VST3 + Toxic VST3 + WD Echo VST3 + reverb |
| 1 | Bass | Identity VST3 + chorus + delay |
| 2 | Pad | Identity VST3 + reverb + delay + chorus |
| 3 | Kick | JE8086 CLAP + compressor — **101 clips** |
| 4 | Snare | JE8086 CLAP + reverb — 80 clips |
| 5 | Closed Hat | JE8086 CLAP — 120 clips |
| 6 | Open Hat | JE8086 CLAP + delay — 72 clips |
| 7 | Clap | JE8086 CLAP + reverb — 72 clips |
| 8 | Sub Bass | Identity VST3 |
| 9 | Piano Keys | Identity VST3 + reverb |

## Mission (4 items, in priority order)

### #1 — Percussion parts didn't play, or played tonal sounds instead (audio-correctness)

**User observation:** the drum tracks are silent or produce pitched/tonal content
where drums should be.

**Established facts (verified by parsing the project XML):**
- MIDI data uses correct GM-style note numbers per track: Kick=36 (444 notes),
  Snare=38, Closed Hat=42, Open Hat=46, Clap=39. Each drum track hosts its OWN
  JE8086 instance.
- Plugin states (`FX_SLOT pluginState`, base64): Kick/Snare/Open Hat/Clap carry
  **byte-identical** states (315 chars → 144 decoded bytes = 36 floats, md5
  `3a4c00e4`). **Closed Hat has EMPTY state.** Lead/Bass/Pad Identity states are
  also identical to each other (md5 `007bfa8a`, 5519 chars).
- Today's spectral verification only proved energy/attacks PRESENT — it cannot
  judge timbre. The user's ears are ground truth.

**Hypotheses (test in this order):**
1. JE8086 sound selection is preset/kit-based rather than note-mapped: one shared
   state ⇒ all four instances make the same (tonal?) sound; the stateless Closed
   Hat falls back to factory default ⇒ per-track symptom differences match the
   user's "didn't play OR played tonal".
2. The 36-float state puts JE8086 into a melodic mode (or a non-drum kit).
3. Note-mapping is fine but the state resets it.

**Diagnostic steps:**
1. Open the project in the UI (isolated mode), open a JE8086 editor on the Kick
   track, and LOOK at what the plugin thinks it is (kit? mode? per-note mapping?).
   Repeat for Closed Hat (the stateless one) and compare.
2. Build solo variants per drum track (pattern: predecessor session's
   `solo_kick.hdaw3` etc. were built by deleting tracks; `py_repro2.py` renders
   them) and listen/analyze each.
3. Decode the 144-byte state against JE8086's parameter list (36 floats — likely
   a param dump); identify which params differ from factory default.
4. Experiment: clear the state on one drum slot (empty `pluginState` attr) and
   re-render solo — does it become a proper drum? If yes, the committed states
   are the bug; regenerate/repair them (see #3 — the file needs repair anyway).

**Success gate:** a render where Kick/Snare/CH/OH/Clap each produce their obvious
percussive sound (verify per solo-track render + listening), and the full render's
drum bus passes a listening check.

### #2 — Out-of-key tonal sounds (possibly detuned presets)

**User observation:** pitched content outside A minor; suspects detuned oscillators
in presets.

**Established facts:**
- Sub Bass and Piano Keys Identity slots have EMPTY `pluginState` ⇒ those two load
  the Identity synth's **factory init patch** — first suspects.
- Lead/Bass/Pad share one identical Identity state; Toxic (Lead) has its own 8972b
  state.
- Note ranges: Lead 65–88, Bass 36–52, Pad 53–67, Sub 29–36, Piano 65–79. Morning
  narrowband checks confirmed Pad A3/C4/E4 and Sub F1 (F is diatonic in A natural
  minor) — so the composition data is at least partially in-key; the off-key content
  may come from patch behavior (detune/unison drift, wrong scale quantize) or from
  the two stateless tracks.

**Diagnostic steps:**
1. Solo-render each tonal track (variant projects + `py_repro2.py`), then
   pitch-track the output (FFT peak / autocorrelation per note segment) and diff
   against the A-minor scale and the clip's written `noteNumber`s.
2. Correlate off-key content with Sub Bass / Piano Keys (stateless) first.
3. Open the Identity editor on a stateless slot vs a stated slot; compare osc
   detune/unison/fine-tune params.
4. If the Identity factory patch detunes: fix the STATES in the project (preferred)
   rather than the plugin, unless the plugin default is genuinely wrong.

**Success gate:** pitch analysis of every tonal track shows only A-minor scale
tones (allowing intentional chromatic passing notes if the composition declares
them — it doesn't here), confirmed by listening.

### #3 — Overlapping clips in the project data (visible in UI on load)

**User observation:** loading the project in the UI shows overlapping parts.

**Established fact (verified by XML parse):** this is REAL data, not a display bug:
Kick track — clip `Kick Build` spans **32–64 s** while `Kick BBlock` spans
**40–42 s** inside it. (Scan script: `%TEMP%\opencode\proj_inspect.py` — rerun it;
also scan `demo_aminor_120bpm.hdaw3`, the other committed demo.)

Notes:
- Interactive edits can't create this: clip placement goes through
  `moveClipWithOverlap` (frontend pitfall #6). The generator wrote clips directly,
  bypassing overlap resolution.
- The clips are extremely fragmented (Kick 101, CH 120 clips) — keep lesson 6 in
  mind if any fix triggers per-clip rebuilds; batch everything.

**Fix options (decide with the user):**
a) Repair the project file(s): trim/move the offending clip(s) so no same-track
   overlaps remain (one batched command or direct XML surgery + reload).
b) Add a load-time sanitization pass or a generator-side invariant so future
   generated projects can't contain overlaps.
**Success gate:** `proj_inspect.py` reports zero overlaps on both demo projects,
and the UI shows no overlapping clips on load.

### #4 — Timeline zoom: mouse-wheel and grab-drag don't work

**User observation:** "(still) does not work" — previously reported, unfixed.

**Established facts (code read):**
- Wheel zoom exists ONLY as **Ctrl+wheel**: `frontend/src/hooks/useTimelineZoom.ts`
  lines 34–45 (native non-passive listener on `bodyRef`, `if (!e.ctrlKey &&
  !e.metaKey) return;`). Plain wheel does nothing timeline-related. Wired via
  `TimelineMinimal.tsx:65`.
- Grab/drag zoom on the ruler is **not implemented at all**:
  `TimelineMinimal/useTimelineRuler.ts` implements click/drag = seek/scrub,
  Ctrl+click = loop start, Alt+click = loop end. No zoom gesture exists.

**Work items:**
1. Verify Ctrl+wheel actually works in the running app (check `bodyRef` attachment
   timing; the effect captures `bodyRef.current` once — if the element mounts
   later, the listener is never bound; the dep array is `[bodyRef]` which never
   changes — prime suspect for "doesn't work").
2. Product decision (Bitwig/Ableton idiom, AGENTS.md UI section): plain wheel =
   horizontal scroll? Ctrl+wheel = zoom? Implement consistently + status-bar hint
   (keyboard-first principle).
3. Implement ruler grab-zoom (e.g. drag with a modifier, or a dedicated zoom
   interaction) — design must respect spatial stability (no layout reflow).
4. **Regression wall (AGENTS.md):** add Playwright E2E coverage for both gestures
   (`frontend/e2e/`), plus Vitest for any new hook logic.

**Success gate:** wheel zoom + grab zoom work in the browser AND Electron shell,
each with an E2E test; `npm test` + `npm run test:e2e` green.

## Repro assets & commands (from the predecessor session, still in place)

- Render driver: `python %TEMP%\opencode\py_repro2.py <isolated|inproc> <end_seconds> <project_path>`
  → `render_<tag>_<mode>.wav` + log. `inproc` sets `HDAW_NO_PLUGIN_ISOLATION=1`.
  A 240 s isolated render takes ~5 min; export wait budget is 600 s.
- Project XML inspector: `%TEMP%\opencode\proj_inspect.py` (tracks/clips/overlaps/
  note ranges/FX states). State decoder: `%TEMP%\opencode\state_check.py`.
- Audio analysis: `%TEMP%\opencode\spec_check.py` (narrowband bands),
  `%TEMP%\opencode\verify_240.py` (sectioned RMS/attacks/bands). 24-bit WAV decode:
  RIFF → `data` chunk, 3-byte LE, sign-extend at 0x800000.
- Variant projects (drums_only/synths_only/solo_kick/solo_pad) in
  `%TEMP%\opencode\*.hdaw3`; reference renders alongside.
- Plugin cache: `%APPDATA%\hdaw\plugin_cache.xml` (Identity/Toxic/WD Echo VST3,
  JE8086 CLAP). Blacklist: FM8 only.

## Environment warnings (learned today — will bite you)

- **RDP audio is ephemeral.** This box is an RDP session; the ONLY audio endpoint
  is "Remote Audio", and it VANISHES when the session disconnects (Get-PnpDevice
  shows zero AudioEndpoint devices). Symptom: every engine device init fails
  (`Error opening Primary Sound Driver: "No driver"` for both (2,2) and (0,2)) and
  all device-dependent tests fail with `getTrack(0)==null`. If a test batch suddenly
  fails that way, check `query session` / `Get-PnpDevice -Class AudioEndpoint`
  BEFORE suspecting code. (This caused 9 false failures in today's first full-suite
  run; the rerun after reconnection was 746/746.)
- `juce::Logger::writeToLog` → OutputDebugString ONLY (never stderr, never
  `hdaw_debug.log`). For device/JUCE errors use HDAW_LOG or cdb breakpoints
  (`da poi(@rcx)` on `juce::Logger::writeToLog`).
- cdb crash/hang toolkit that worked today: procdump `-ma <pid>` + `~* k` for hangs;
  live-attach with breakpoints on `ucrtbased!abort` (`~* k 50; q`) for Debug-CRT
  aborts; `eb <mod>!juce::juce_isRunningUnderDebugger 33 c0 c3` silences JUCE int3
  assert noise. JUCE asserts print to OutputDebugString, so MSVC dialog spam =
  Debug CRT abort = attach cdb, don't click dialogs.
- Debug builds: bash-tool default 120 s timeout kills builds mid-link and orphans
  `cl.exe`/`MSBuild.exe` — ALWAYS pass big timeouts; killed builds lock source files.
- `%TEMP%\hdaw_debug.log` is the shared engine log (append, UTC; local = UTC−5).

## Out of scope (noted, don't fix here)

- Intermittent `Render graph bake timed out after 15s` in isolated export (seen once
  today, retry clean; pre-existing race documented at `ExportManager.cpp:214-218`).
- The predecessor session's teardown-hang observation for 12-plugin exports did not
  reproduce today; treat as dormant.
- JUCE_ASSERT_MESSAGE_THREAD spam in test output (AudioDeviceManager ctor on the
  test main thread) — pre-existing, debug-only, harmless.

Related docs: `docs/handoffs/2026-08-12-vst3-inproc-resolution-and-fxtest-failure.md`
(predecessor, incl. RESOLUTION + new lessons 17–19 in AGENTS.md),
`docs/postmortem-silent-clap-export.md`, `docs/pitfalls-frontend.md`,
AGENTS.md UI design section (spatial stability, keyboard-first).
