# Handoff: MCP cluster-compose session — engine bugs found (6 tracks, ~1h of audio)

Date: 2026-08-26/27 (single long session, agent-driven)
Focus: End-to-end test of the new timbre-clustering MCP tools
(`cluster_library` / `related_samples`) via theme-driven sample selection and
six MCP-composed tracks. This handoff documents EVERY bug hit along the way,
with repro, root cause where known, and workaround. Two bugs (§1, §2) deserve
real engine fixes; the rest are contract traps future agents will re-hit.

## 0. TL;DR for the next context

- **Two engine bugs with code locations**: MidiClipProcessor silently drops
  notes past #512 per clip (§1), and `sampler_set_sample` on a loaded slot
  kills the live processor sound (§2). Both have MCP-side workarounds that are
  baked into nothing yet — every future long composition will re-hit §1.
- One **unreproduced engine crash** (MSVC abort dialog, engine died mid-
  session, §3) — suspected automation-driven internal-FX path in render.
- Everything else in this doc is a *contract trap* (units, async, registry,
  clipping pre-master) — §5 has the verified cheat-sheet.
- Session artifacts (all under `.tmp_dnb_theme/`): 6 finished tracks +
  `.hdaw` projects; 15 audio libraries registered (~4,400 files indexed);
  `timbre-lib/analyze.sh` + `register_library.py` + `analyze_multi.py`
  updated/added; `pack042119` long-version renders left in place deliberately.

## 1. CRITICAL — MidiClipProcessor silently truncates clips at 512 notes

**Symptom:** A 352 s render came back with the final ~90 s silent (groove
tracks dead from ~beat 688, only stabs/hats left). Three exports shipped like
this before it was caught — the notes WERE in the saved XML, remove_notes
dryRun counted them, but playback never fired them.

**Root cause:** `src/engine/MidiClipProcessor.h:42` —
`static constexpr int MAX_NOTES = 512;` and `rebuildNoteCache()` (line ~392):
`int count = (std::min)(nl.getNumChildren(), MAX_NOTES);` — the note cache is
a fixed `std::array` and everything past the 512th note (insertion order) is
silently ignored. No log, no error, nothing in ReadModel.

**Evidence (exact):** Kick clip 624 notes — note #512 lands at beat 687 →
silence from 688. OffBass 672 notes — #512 at beat 647.5. Stab 1360 notes —
#512 at beat 502.75 (its B8/B9/B12 arps were dead). Every measured silence
boundary matched the 512th-note position exactly.

**Workaround used:** split each >512-note part into multiple clips (each
<512 notes). NOTE the second trap: notes must then be **clip-local** — see §1b.

**Suggested fixes (any one):**
- Raise MAX_NOTES and log loudly when truncating (minimum viable fix).
- Convert noteCaches to a heap vector sized to `getNumChildren()`.
- McpTools_Note add_notes should REJECT (error) a batch that would push a
  clip past 512 notes instead of accepting silently.

### 1b. Related trap — note startBeats are CLIP-LOCAL, add_notes does not convert

`MidiClipProcessor::processBlock` computes
`currentBeat = secondsToPpq(now) - secondsToPpq(clipStartTime)` and compares
`currentBeat >= note.startBeat` (line ~190). Notes are therefore stored
relative to the clip's own start. The MCP `add_notes` writes the caller's
`start` into `startBeat` verbatim (McpTools_Note.cpp:~88). This is invisible
for clips at beat 0 (absolute == local) but any clip created at start>0 with
timeline-absolute starts plays its content late by exactly the clip start.
Workaround: subtract clip start from every note start before add_notes.

## 2. `sampler_set_sample` on an already-loaded slot kills the live sound

**Symptom:** First `sampler_set_sample` on a fresh sampler slot works. ANY
subsequent set on the same slot (same file or different) leaves
`sampler_get_state.hasSound == false` for that live engine — but the
ValueTree keeps the new `sampleFile` property, saves/loads fine, and
**export still renders correctly** (the export graph rebuilds its own
processors from the tree).

**Repro:** add_track → add_fx sampler → sampler_set_sample (ok, hasSound
true) → sampler_set_sample again on same slot → hasSound false forever.

**Workaround:** after any re-set, `save_project` + `load_project` (full
routing rebuild restores every sampler from the tree). Pattern used all
session: set every sample exactly once, then one save+load before render.

**Note:** in the headless MCP engine the LIVE graph may never instantiate FX
processors at all (hasSound reads None/False for everything, including slots
that export fine). Do not use hasSound as a render predictor in MCP context;
it reads the live processor, not the tree. This cost a long red-herring debug
(including an unnecessary WAV transcode — the "unreadable" 24-bit/JUNK-chunk
file loaded fine once the processor state was fixed).

**RESOLVED 2026-08-27 (verified in current build):** the re-set path is
correct with a live graph — `setSamplerSample` → `rebuildTrackFX` →
`rebuildFXChain` creates a fresh `TrackFXSlot` and `loadSamplerState`
re-loads from the tree (pool re-acquire). All sampler state commands
(`setSamplerSample`, `setSamplerMode`, `set_sampler_param` properties,
`detectSamplerSlices`) end with `rebuildTrackFX`, and `AudioEngine`'s
FX-slot listener forwards `param_N` live. The residual "hasSound false for
everything" in headless MCP is the no-live-device case: `rebuildFXChain`
gates sampler instantiation on `fxSpec.sampleRate > 0`, so without an opened
audio device the live slot never builds a sampler engine (tree/export
unaffected — hasSound is NOT a render predictor, as noted above). Guard:
`AudioPoolDedup.SamplerResampleUpdatesLiveProcessor` (first set → re-set
different file/root → re-set same file; asserts the live sound changes).

## 3. UNREPRODUCED CRASH — engine died with MSVC abort dialog

During the long-remix session the engine process aborted (user saw the MSVC
crash dialog; MCP showed "Connection closed"). Sequence context: several
352 s exports with 4 newly-enabled automation lanes driving internal FX
(EQ frequency, phaser centre-freq/depth, flanger rate) via
`TrackFXSlot::setAutomationParam` → `applyInternalParamToDsp` on the render
path. After engine restart, disabling then re-enabling the same lanes, all
subsequent renders completed with the engine alive. Cause NOT isolated —
candidate: a normalized automation value denormalizing into an internal
dsp::Phaser/Flanger param edge case. If it recurs, start from
`src/engine/TrackFXSlot.h` `setAutomationParam`/`applyInternalParamToDsp`
and the render-thread call path, with the automation point lists from
`antinomy_remix_FINAL.hdaw`.

## 4. Library/registry contract traps

- **Registry clobber:** `FileLibraryManager::initialize()` auto-scans
  autoScan libraries and calls saveRegistry() — an engine (re)start rewrites
  `%APPDATA%\HDAW\libraries\registry.json` from in-memory state, silently
  deleting entries written externally (a script-registered library vanished
  this way). Rule: while any engine may run, register via MCP add_library;
  `analyze.sh --library` (script writes registry) is safe only when no
  engine is running. Dead `hdaw_tests-*` temp entries accumulate in the
  registry from the gtest suite (they are prunable — see
  timbre-lib/register_library.py).
- **scan_library is async:** returns "scan started"; fileCount/lastScan in
  list_libraries is the completion signal (search_library returning tagged
  entries is the sidecar-ingestion signal). Poll it; don't break early.
- **related_samples/cluster_library:** solid. Hybrid clustering over ~212
  sidecar-analyzed files grouped categories (bass/drums/synths) and merged
  folders from different libraries coherently; related_samples ranked
  same-family neighbours first across libraries. The dsp-vector gate is
  strict (all 20 keys finite) — files analyzed with --no-llm still cluster
  via CLAP tags.

## 5. Verified MCP cheat-sheet (contract as-behaved, source-verified)

- **Units:** add_midi_clip/add_notes start+length and note start/duration =
  BEATS. export_audio start/end = SECONDS (a "128" is 128 s, not 128 beats).
  set_automation_points `time` = BEATS (McpTools_Automation.cpp:81 converts
  via beatsToSeconds). FX automation lane VALUES = NORMALIZED 0..1 (Track.cpp
  ~478 → TrackFXSlot::setAutomationParam denormalizes to the param range);
  Volume lane (paramID 1) values are raw gain; sampler Transpose lane values
  are semitones.
- **Automation lanes:** default `automationEnabled=0` — a lane silently does
  nothing until `set_automation_enabled`. FX-param lane paramID convention =
  `100 + slotIndex*100 + paramIndex` (McpTools_Automation.cpp:124); built-ins
  1=Volume 2=Pan 3=Mute. Automation DOES drive internal FX (eq/phaser/
  flanger...) — verified end-to-end in the final render (EQ cutoff sweep
  audible in section centroids).
- **export_audio:** trackIds IGNORED (always full project); serialize calls
  ("export already in progress"); use a fresh filename per render and wait
  for file size to stabilize; output WAV is 24-bit PCM.
- **MCP daemon drops:** "Connection closed" → `await mcp.reload('hdaw')`,
  then `load_project` from the last save. Save often on long builds.
- **Clipping is pre-master:** when peaks pin at exactly 1.000 and master-gain
  changes don't move them, the sum clips before the master — cut per-track
  velocity/volume, not master. Transposed-up sampler notes get LOUDER (and
  shorter): pitches ≥ +7 semitones on hot samples were the repeat offenders;
  shave velocity for high transpositions.
- **add_track returns plain text** `trackId=N routed=1` (not JSON) — parse
  accordingly; other tools mix "ok" text and JSON, try both.
- **duplicate_region** ripple-inserts (copies [start,end) at end AND shifts
  later content) — extend grooves BEFORE placing later content.

## 6. Session inventory (what exists on disk)

- Tracks (`.tmp_dnb_theme/`): `dnb_dark_v3.wav` (timbre-lib palette),
  `cluster_collage_FINAL.wav` (cross-library clusters), `pack042119_FINAL.wav`
  + `pack042119_long_v9.wav` (160 BPM single-pack jungle), `vgs_adrenaline_
  FINAL.wav` (VGS loop-pack), `antinomy_FINAL.wav` (MIDI-import arp),
  `antinomy_remix_FINAL.wav` (5:52, automation sweeps) + matching `.hdaw`.
- Libraries registered (persisted in %APPDATA%\HDAW\libraries\registry.json):
  TimbreLibSamples + f2538111 dup, 112718, projects-{masters,2011,100414,
  042226,102411,042119,102599,123115,112102,090916,061410,050703,010514},
  VGS-DNBA-Vol1 (audio+MIDI), Antinomy-Psy-Vol2 (audio) + Antinomy-MIDI.
  Sidecars: ~210 files analyzed (DSP+CLAP+LLM) across d:/projects, e:/samples,
  timbre-lib/samples.
- Tooling: `timbre-lib/analyze.sh` gained `--library NAME` (registers in
  HDAW registry; engine-clobber caveat above), new `register_library.py`,
  `analyze_multi.py` (multi-folder, strided sampling, models load once),
  `analyze_targeted.py` (explicit file list).
- The 6-track session used ONLY pack samples per user constraint; the
  Antinomy arp was imported via `analyze_midi_file` (15 bar-aligned patterns
  from the pack's own Cm MIDI) and re-sequenced by hand per section.

## 7. Suggested follow-ups (prioritized)

**Status (2026-08-27):**
1. ✅ DONE — 512-note truncation removed (immutable atomic-shared-ptr cache snapshots sized to the clip, 8192 loud-log safety ceiling, regression tests `MidiClipProcessor.NoteCachePlaysNotesBeyondLegacy512Limit` / `CcCacheEmitsPointsBeyondLegacy512Limit` / `NoteCacheClampsAtSlotCeiling`).
2. ✅ DONE — `add_notes` description documents the 8192-per-clip ceiling + clip-local beats (§1b).
3. 🔍 FIRST PASS 2026-08-27 — §3: added gtest `ExportAutomation.SeedProjectLongRenderDoesNotAbort` (loads the ACTUAL saved seed — 4 automation lanes already enabled in the file — and renders the full project duration 3×). RESULT: 3/3 renders clean (~84 s each), no abort, no debug-heap assertion. NOT reproduced in a fresh-process deterministic full-render burst. Seed lane data is benign (normalized values 0.01..0.85; no out-of-range/denormal inputs); `getValueAtTime` extrapolation is safe and the `value >= 0.0` gate drops NaN. The debug-CRT heap abort seen earlier in ExportBakeTimeout's 771-clip export (no automation!) has the SAME signature as this crash — both are long-render/teardown-path aborts, unreproduced in isolation. PROBES RUN 2026-08-27 (same session, current build): (A) `ExportAutomation.SessionAccumulationStress` — 6× (toggle the 4 FX lanes off/on → save+load → 60 s render) in ONE engine process: CLEAN (~102 s, no heap abort). (B) `ExportAutomation.LiveMutationsDuringExportStress` — 6 iterations of start-long-export + hammer structural live mutations (addTrack/removeTrack cycles → pump-side rebuildRoutingGraph drains+cancels the in-flight render): 19 export-drain/teardown cycles observed in hdaw_debug.log, ZERO aborts (~110 s). CONCLUSION: §3 stays UNREPRODUCED across full-render (3×), accumulation (6×), and teardown-race (19×) stress in the current build. All three probes + the earlier full-render test remain committed as seed-guarded regression gates (GTEST_SKIP when the seed is absent).
CRASH-CAPTURE INFRA (2026-08-27): `%TEMP%\hdaw_capture\run_with_capture.ps1` runs any gtest filter under
`procdump -accepteula -ma -e -g -x <outdir> hdaw_tests.exe --gtest_filter=...` (no admin needed). Per-run folder
under `%TEMP%\hdaw_crash_captures\<stamp>\` with main.log; RESULT=CLEAN / CRASH_DUMP_CAPTURED /
CRASH_MARKER_NO_DUMP (markers: Assertion failed, abort(), _CrtIsValidHeapPointer, is_block_type_valid).
NEW-PACK COMPOSITION STRESS: `PsytranceComposition.NewPacksLongRenderWithFxAutomation` builds a psytrance
arrangement from the 8 new E:\samples packs (role-classified kick/bass/lead/hat/pad samples, 142 BPM, 32-bar
patterns) with the §3 lane set (bass EQ freq, lead phaser depth+CF, hats flanger rate) and 3x 90 s renders with
save/load accumulation. COMBINED CAPTURE RUN 2026-08-27 15:09: seed full-render x3 + accumulation + teardown-race
+ new-pack composition — ALL CLEAN under procdump, 0 dumps. §3 remains unreproduced — capture in place. MCP-LAUNCH CAPTURE WIRED (2026-08-27): mcp-launch.bat now runs the engine under
`procdump -accepteula [-ma|-mm] -e -g -x <dir>` by default (delegated to PowerShell for reliable
quoting with spaced paths); stdio passes through to the MCP client. Per-launch full minidump dir:
%TEMP%\hdaw_crash_captures\engine_<hex8>\ (no dump unless the engine crashes). Disable with
HDAW_NO_CRASH_CAPTURE=1; mini dumps with HDAW_CRASH_DUMP_TYPE=mini. NOTE: cmd-level argument
quoting for procdump inside if-blocks is a rabbit hole (silent Capture-Usage failures) - keep the
PowerShell delegation; verified: capture ON launches under procdump, capture OFF falls through
to the plain launch.
future MCP-engine sessions.
4. ✅ VERIFIED/WORKS — re-set reaches the live processor (`setSamplerSample` → `rebuildTrackFX` → fresh slot → `loadSamplerState` from tree, pool re-acquire); guard test `AudioPoolDedup.SamplerResampleUpdatesLiveProcessor`; headless no-device `hasSound=false` is expected (§2 NOTE) — see §2 resolution above.
5. ✅ DONE — `list_notes` MCP tool shipped (filters mirror `remove_notes`: pitches[], startGte/startLt clip-local beats, noteIds[]; returns count + full operator field set); test `GuiFuncTest.ListNotesWithFilters`.



1. Fix or fence §1 (MAX_NOTES) — silent data loss, worst class of bug; a
   loud log line is the 5-minute version.
2. Make add_notes fail loudly past the cache cap or document the 512 limit
   in the tool description.
3. Investigate §3 crash with the saved project as repro seed.
4. §2: make sampler_set_sample reach the live processor (listener path) —
   the tree already carries the truth.
5. Add a note-level read tool (list_notes with range filters) — remove_notes
   dryRun is the only way to count notes in a range today.
