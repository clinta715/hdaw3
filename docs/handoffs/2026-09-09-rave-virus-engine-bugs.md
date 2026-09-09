# Handoff: 2026-09-09 — RAVE-in-Mix Workflow, Virus Patch Pipeline, Engine Bugs

**Status (session 2, same day): Bugs 1, 2 and the stem-export filter are FIXED
(see Resolution at the bottom); Bug 3 open, Bug 4 mitigated by workflow. RAVE
is deprecated as of v0.32.0. The "working state" below is the pre-resolution
snapshot kept for context.**

Context: psytrance composition session. The arrangement itself is in good shape
(20 tracks, tail filled, levels balanced). This handoff covers the working
pipelines proven today and the engine bugs that block the RAVE-in-mix workflow.

## Working state right now
- `compositions/corpus_track_77_psy_v3.hdaw` — the track: 20 tracks (rhythm section,
  melodic psy_fm synths, poly sub_synth chords, 5 RAVE Return tracks with imported audio).
- `FINAL_v15_tailfilled.wav` — the best FULL-DRY render (peak -4.7 dBFS, RMS -11.4).
- `FINAL_v18_rave_real.wav` — the RAVE-synth mix (peak -4.7, RMS -14.8; layers quieter — Bug 2).
- Verified-real RAVE stems (auditionable in any player):
  `compositions/rav_{arp,stab,chords,lead,pad}_st.wav` (stereo, 0.7 peak).
- Virus psytrance banks staged: `C:/rave/psyload.syx` (128 voices), `C:/rave/ollie.syx` (119).

## Proven pipelines
1. **Virus bank browsing**: `.mid` banks in `d:/pdf/virus presets` → extract sysex with
   `C:/rave/extract_syx4.py` (the .mid omits F7 and counts it in the varint; blocks are
   exactly 524 bytes = F0 + 8-byte header + 512 data + cs + F7) → `sub_synth_import_sysex
   {trackId, slotIndex, filePath, voiceIndex}` — returns the patch NAME; 23 params map to
   sub_synth 0-22, unmapped Virus features are reported honestly. 22 psytrance voices
   triaged (PsyLead Pl, FmLead Pl, Tbarp Pl, chordarp P, Aciddance, GoaBass 2/3, ...).
2. **RAVE transforms**: run `tools/rave/rave_transform.py --input X --model Y --output Z`
   OUT-OF-ENGINE with `\.venv-rave\Scripts\python.exe` (torch 2.14 CPU, ~1:1 speed,
   deterministic). NEVER drive it through the bridge (Bug 4). Output is MONO 16-bit and
   quiet (peak ~0.02-0.04): normalize + convert to stereo before importing.
3. **Mute-solo stem rendering**: the trackIds solo export was unreliable when this
   was written (schema-only filter — it rendered the FULL mix); FIXED in session 2
   with `McpCoverageTest.ExportAudioTrackIdsFiltersTracks`. Habit that survives:
   ALWAYS verify the rendered WAV has audio (`C:/rave/wavlevel.py`) before consuming
   it — file size lies (pre-allocated + buffered writes).

## Engine bugs to fix (the actual work)

### Bug 1: Mono audio-clip sources break the offline export COMPLETELY
Repro: `add_audio_clip` with any MONO wav → every subsequent export renders pure zeros
(success=1, rms 0). Removing the clip restores audio instantly. Stereo sources are fine.
Suspect: offline render graph audio-clip channel handling (ClipSourceProcessor /
DecodedSoundPool decode on 1-channel sources). High priority — blocks all RAVE imports.

### Bug 2: Offline audio-clip rendering is level-broken after a restart
Repro: import a stereo wav, solo-export → full level. Restart the engine, reload,
solo-export the SAME clip → ~1/28th level (sparse blocks → "tiny bursts of static").
Re-importing in the same session does NOT restore it. The dry mix reads -24 to -36 dB
quieter than the source. This is what destroyed the RAVE layers audibly.
Suspect: the offline export's audio streaming depends on warm state (pool decode cache /
message-pump park timing) that a restart loses.

### Bug 3: list_notes truncates large clips (~250+ notes)
Repro: a clip with 300+ notes → list_notes returns empty; get_clip returns the full list.
Fix: raise/remove the response cap or paginate.

### Bug 4: The bridge's 10s server timeout kills the engine mid-render
pi-mcp-adapter hdaw_invoke_command = 10s timeout; long renders/transforms through it get
the engine killed and the launcher respawns an EMPTY default project (several "silent
exports" today were renders of that empty project). Mitigation: export_audio WITHOUT
wait (fire-and-forget worker) + poll; never invoke long-blocking commands through the
bridge. Proper fix: async-job pattern for all long commands.

### Bug 5 (earlier, minor): DBG() is #if JUCE_DEBUG-only (use HDAW_LOG_ALWAYS);
HDAW_LOG takes (tag, msg). Documented in pitfalls-juce.md already.

## Corpus arranger notes (workaround in place, real fix pending)
- Arranger placed the kick clip at beat 128 (moved to 0 manually); distributions.json
  introMaxBars reduced 24→8; kick must start within the first bars — enforce in the
  arranger.
- Clips span to beat 384 but layer NOTES stop early (hats 299.5, snare 299, stab 301,
  arp 303.8) — filled via place_patterns tiling each track's own last-4-bars pattern;
  the arranger should fill its layer windows completely (the pad's chained window clips
  are the model to follow).
- Key-filter gate added in RoleCtx::add (snapToScale, percussion exempt) — generation
  is now scale-safe; 473 off-key notes were the pre-fix symptom.

## Suggested fix order
1. Bug 2 (offline audio-clip level) — blocks the RAVE-in-mix workflow
2. Bug 1 (mono sources) — same area, likely related
3. Bug 4 (bridge kill) — stops the state-loss churn
4. Corpus arranger window fill
5. Bug 3 (list_notes cap)

## Resolution (session 2, 2026-09-09)

- **Bug 1 FIXED** — mono audio-clip sources decode correctly in the offline
  render graph; `OfflineAudioClipExport.MonoClipExportsToBothOutputChannels`
  covers it.
- **Bug 2 FIXED** — stereo clip level survives project tree reload / engine
  restart; `OfflineAudioClipExport.StereoLevelSurvivesProjectTreeReload`
  covers it.
- **`export_audio` `trackIds` filter FIXED** — was schema-only (stem exports
  rendered the full mix); `McpCoverageTest.ExportAudioTrackIdsFiltersTracks`
  now pins the behavior.
- **RAVE import offset contract FIXED** — `rave_import_result` /
  `rave_transform_clip` / `rave.importResult` accept `sourceOffsetBeats` and
  `timelineAligned:true` (beats→seconds via the existing `setClipOffset`
  path; explicit `sourceOffsetBeats` wins over `timelineAligned`), response
  adds `sourceOffsetSeconds`. Tests: `McpCoverageTest.RaveImportTimelineAligned
  AndExplicitSourceOffset`, `RaveRpc.ImportResultTimelineAlignedSetsSourceOffset`.
  **Root-cause correction to the Bug 2 note above:** the audible destruction of
  the RAVE layers was mostly NOT level-broken streaming — it was (a) every
  import writing clip offset 0, so full-timeline stems played their silent
  first segment, and (b) the project file having persisted the stem-audition
  state (all source tracks muted, only RAVE returns audible at 1.1 with
  +14/+12 dB saturators). Lesson: verify the SAVED mixer state and clip
  `offset`/`sourceDuration` from the .hdaw before blaming the engine; a
  render whose RMS pins at the masterGain ceiling on one layer then goes
  digital-silent is a muted-source artifact, not an engine bug.
- **Bug 3 (list_notes ~250+ note cap) still OPEN** — diagnostic logging was
  added during diagnosis and removed before commit; cap/pagination fix pending.
- **Bug 4 mitigation** (documented): never invoke long-blocking commands
  through the 10s bridge — `export_audio` fire-and-forget + poll file size /
  `engine_info` `exporting` flag. Async-job pattern for long commands remains
  the proper fix.
- **Master limiter is NOT transparent**: enabling it at -1 dB dropped mix RMS
  ~10 dB. Use fader/headroom staging instead; fix the limiter before
  recommending it.
- **Mix discipline that worked**: gain must be applied in the BANDS where an
  instrument should be heard (lead: ~400 Hz–3 kHz EQ presence), not by fader
  alone; melodic lines that get filtered need a stacked basis — chord, or
  note + octave-down + 7th-up + octave-up — so sweeps/automation have harmonics
  to shape. A 16 ms single-line blip under an open filter is inaudible.
- **RAVE DEPRECATED (v0.32.0)**: real-model results in mix were overdriven and
  incoherent even after the import/level fixes; native psy_fm/sub_synth tracks
  are the recommended path. RAVE RPC/MCP/Neural-UI stay functional for
  compatibility; do not build new workflows on them.