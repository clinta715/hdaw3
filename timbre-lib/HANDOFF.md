# TimbreLib + HDAW — Session Handoff

Handoff for a fresh session. Covers: what exists, how to run everything, verified
HDAW-MCP tool semantics, known issues/workarounds, and suggested next steps.
Last updated: 2026-08-25 (after building `sampler_demo_long.hdaw3`).

---

## 1. What this project is

A local, ML-backed sample library ("TimbreLib") plus an HDAW MCP workflow that
builds DAW projects from that library using ONLY HDAW's internal sampler
instrument, with generators / modulators / remix tools.

Pipeline (all local, WSL):
DSP descriptors (timbre.py, numpy/scipy)
  -> CLAP captions + AudioSet tags (clap_stage.py, CUDA)
  -> Qwen2.5-3B prose (llm_stage.py, llama-cpp CPU)
  -> timbre_index.json + optional <file>.timbre.json sidecars

## 2. Working directory & key files

    /mnt/d/pdf/roo projects/timbre-lib/   (Windows: D:\pdf\roo projects\timbre-lib\)

    README.md            pipeline docs & usage
    timbre.py            stage 1 DSP descriptors (numpy/scipy only, portable)
    clap_stage.py        stage 2 CLAP captions/tags
    llm_stage.py         stage 3 LLM prose (Qwen2.5-3B GGUF)
    lib_analyze.py       orchestration, incremental cache, index/sidecar writer
    lib_search.py        text search over timbre_index.json
    analyze.sh / search.sh   wrappers using the ML venv python
    samples/             the analyzed library (17 files, index inside)
    sampler_demo.hdaw3   8s/4-bar demo: 6 sampler tracks (kick/hat/stabs/bass/pad/bells)
    sampler_demo_long.hdaw3   66s/32-bar composition (the main deliverable)
    sampler_demo_long_render.wav  final verified render (44.1k/24-bit stereo)

## 3. Running the pipeline

    ./analyze.sh <folder> [--limit N] [--no-llm] [--sidecars]
    ./search.sh "dark gritty pad" [--min-dur S] [--max-dur S]
    TIMBRE_PY=/home/hapbt/.prime/agent/kernel-venv/bin/python   (default, has ML stack)
    Re-analyze everything: delete <folder>/.timbre_cache/ and re-run.
    Per-file output keys: dsp (20 descriptors), dsp_words, captions, tags, prose,
    durationSeconds, sampleRate, channels, format, size, mtime, wsl_path, win_path.

**Pitch mapping used for sampler root notes** (from dsp.f0_hz -> MIDI):
    108 Hz -> A2  -> rootNote 45   (lp_saw_pad, chorus_pad, square_110)
    215 Hz -> A3  -> rootNote 57   (pluck_220, detuned_saws)
    431 Hz -> A4  -> rootNote 69   (sine_440, saw_440)
    912 Hz -> Bb5 -> rootNote 82   (fm_bell)
    Drums/noise -> rootNote 60.

## 4. HDAW MCP — how to attach and recover

Generic MCP server name: hdaw. Tool list: 187 tools (list with
`await mcp.list_tools("hdaw")`). Execute: `await mcp.call_tool("hdaw", name, args)`.

    Library registrations that matter:
      f2538111f7cd  TimbreLib   audio  D:\pdf\roo projects\timbre-lib\samples (17 files)
      e731d934c141  112718      audio  D:\projects\112718 (40 x 120bpm OGG/FLAC loops,
                                       most in Dm, described keys/bpm; long texture loops)
    search_library {libraryId, query} — substring matches on name/description/tags;
      also accepts durationMin/Max, bpmMin/Max, key. bpm/key fields are HDAW's own
      metadata; files without embedded metadata report defaults (120 / Dm).

**Recovery (IMPORTANT):** the HDAW MCP daemon can drop mid-session
("Connection closed"). Fix: `await mcp.reload("hdaw")`, then
`await mcp.call_tool("hdaw","load_project",{"filePath": "D:\\...\\file.hdaw3"})`.
All edits survive as long as you save_project to disk first.
After any kernel restart: re-create the `call()`/`parse_result()` helpers
(MCP results come back as JSON strings OR "key=value" strings like
"trackId=3 routed=1").

## 5. Verified tool semantics (unit of truth — these were probed empirically)

Timing:
- add_midi_clip {trackId,start,length}: start/length in BEATS (XML stores seconds).
- add_notes/add_note {start,duration}: BEATS. Velocity 0-127 (stored /127).
- set_automation_points {time,...}: BEATS (stored as seconds). Values are lane units.
- generate_automation_envelope {start,end,...}: units looked like seconds; prefer
  set_automation_points for predictable results.
- generate_clip_gain_envelope: values get clamped (e.g. endValue 1.4 -> ~0.56);
  use track-Volume automation instead for predictable boosts. Envelopes live in
  <GAIN_ENVELOPE> inside the CLIP element.

Sampler:
- Track: add_track -> add_fx {fxType:"sampler"} (slot 0) -> sampler_set_sample
  {filePath, rootNote} -> set_sampler_mode {classic|one-shot|slice}.
- Params: set_sampler_param paramIndex 0=Attack 1=Decay 2=Sustain 3=Release
  4=Transpose 5=SampleStart 6=Hold 7=Glide 8=Reverse 9=SampleEnd.
- Behavior: plays sample on note-on, GATES at note-off + release (even in
  one-shot mode). Long loops = long notes. OGG loads fine.
- Transpose automation lane values are semitones.

Generators:
- duplicate_region {startBeat,endBeat}: RIPPLE-INSERTS — copies [start,end) to
  `end` AND shifts all later content down by (end-start). Do all region
  duplications BEFORE placing content at later positions.
- generate_arrangement {bars,style,seed,enableX,targetTrackIds}: creates one clip
  per enabled role on the MAPPED track (unmapped roles create new tracks — pass
  enableOpenHat:false enableSnare:false to avoid strays). Parts land at beat 0;
  move with move_clip {clipId,start}. Generated templates are staccato
  (dur 0.1-0.7 beats) -> quiet through a gating sampler; lengthen with set_note.
  scaleRoot/scaleMode exist (get_scale_modes for the list).
- generate_progression {trackId,pattern,beatsPerChord,start,durationBeats}: makes
  a 16-beat "Generated" chord clip at `start` (durationBeats seemed capped; call
  again with start+16 to extend).
- generate_rhythm_pattern {trackId,bars,grid,pulseA/B,rotationA/B,start}: new clip
  at requested start. NOTE: has NO `seed` param (errors are "unknown property").
- generate_phrase {trackId,style,length,density,start,...}: new clip at start;
  26 styles (Standard, Arpeggio, BassLine, Euclidean, MarkovMelody, ...).

Modulation / automation:
- Lanes default DISABLED (automationEnabled="0"). Without
  set_automation_enabled {trackId,lane,enabled:true}, renders ignore them.
  This one cost a full debugging cycle.
- Volume lane gains > 1.0 are allowed and stored literally.
- add_automation_lane {trackId,laneName,paramID} for sampler params (e.g.
  Transpose=104) then set points.

Remix tools:
- capture_fx_snapshot / swap_fx_snapshot: FAIL on the internal sampler
  ("slot is not a plugin") — 3rd-party plugins only.
- arranger regions: add_arranger_region {name,startTime,duration} (seconds),
  get/set bounds, duplicate_region, ripple_delete, flatten_arranger, chains
  (add_arranger_chain / set_arranger_chain_active / get_arranger_chains).
- set_note_pitch_offset does NOT persist (silent no-op). Retune with
  set_note {noteId, pitch} (works, persists in XML).

Export:
- export_audio IGNORES trackIds (always renders the full project).
- Output path appears CACHED — re-exporting to the same path returned a stale
  render. ALWAYS use a fresh output filename per render and wait until the file
  stops growing before analyzing.
- Exports must be serialized ("export already in progress" otherwise).
- Output WAVs are 24-bit PCM — parse as 3-byte LE ints (wave module + manual
  int24 decode), NOT int32 (int32 parsing yields garbage/noise-like data and
  half durations). This misled the session for a while.

## 6. sampler_demo_long.hdaw3 — what was built (reference)

10 sampler tracks, 120 BPM, 4/4, 128 beats (32 bars), 66s render.
Sections (beats): Intro 0-32 | Groove In 32-64 | Build 64-96 (arrangement
seed 101) | Drop 96-128 (arrangement seed 202).

    Track      id  Source sample                     Section coverage
    Kick       3   kick_thump.wav                    groove 32-64 + generative 64-128
    Hat        4   hp_noise_hihat.wav                pattern 0-64 + generative 64-128
    PluckStab  5   pluck_220.wav (root 57)           stabs 32-64 + generative Clap 64-128
    Bass       6   lp_saw_pad.wav (root 45)          groove 32-64 + generative 64-128
    Pad        7   chorus_pad.wav (root 45)          chords 0-64 + generative 64-128
    Bells      8   fm_bell.wav (root 82)             melody 0-64 + generative Lead 64-128
    TexA-Intro 9   #31 (Dm 35.8s) @beats 0  (vol auto 0->1)
    TexB-Groove 10 #16 (Dm 47.9s) @beats 32
    TexC-Build 11  #35 (Dm 40.4s) @beats 64 (riser envelope + fade up)
    TexD-Drop  12  #17 (Dm 40.7s) @beats 96 (fade in)
Modulation: pad volume swell 0->0.85 (intro), pad Transpose LFO (semitones,
build/drop), bass sidechain pump (0.45/0.95 per beat, scaled 1.0/1.3/1.5 per
section), section gain ramps to 1.55 (drop) on groove tracks, TexC/TexD fades.
Arranger regions named Intro/Groove In/Build/Drop (16s each).

Render profile (verified rms per 2s): intro 0.16->0.39, groove 0.42->0.49,
build ~0.22-0.44, drop ~0.24-0.37 (peaks 0.53). The drop is deliberately
sparser/syncopated (generated) vs the solid four-floor groove.

## 7. Open issues / rough edges

1. Build & drop render QUIETER than the groove in mean level (peaks are the
   loudest in the song). Generated staccato templates + sample gating. Options:
   raise build/drop section gains further (1.55 -> ~1.8), duplicate generated
   hats/kicks for density, or re-generate with higher complexity.
2. Same-path export returns stale file (cache) — workaround: fresh path. A real
   bug worth reporting in HDAW.
3. set_note_pitch_offset is a dead tool (no persistence) — either fix or remove.
4. capture/swap_fx_snapshot unusable on the internal sampler; if section-based
   sampler morphing is wanted, emulate with Transpose/SampleStart automation
   (works) instead of snapshots.
5. generate_clip_gain_envelope clamps values unexpectedly; prefers
   set_automation_points on the track Volume lane.
6. generate_progression ignores durationBeats beyond one 16-beat pattern; call
   per 16-beat chunk.
7. duplicate_region ripple semantics are surprising (danger of shifting textures
   downstream); document in HDAW or add a non-ripple option.
8. Environment-flaky exports: gtest McpServer.ExportAudio* / DiagnosticClapExportMatrix
   suites are KNOWN-FLAKY (see global memory hdaw_mcp_isolated_clap_export_tests_are_environment_flaky_shinronin).
9. HDAW per-file bpm/key metadata defaults (120/Dm) when file metadata is absent;
   timbre sidecars do NOT include bpm/key yet (README's native FileLibraryManager
   sidecar integration is still a TODO).

## 8. Suggested next steps

Short:
- Polish the drop: density/level pass, then fresh-path render + verify with the
  24-bit envelope analysis (ops: export, wait-for-stable-size, env24, band RMS).
- Add a second movement (different key/tempo, e.g. 100 BPM C minor) to the long
  project using the same sampler+generator playbook.
- Update README with: sampler-project construction recipe, MCP gotchas summary,
  and the handoff file reference.

Deeper:
- Add BPM/key detection to timbre.py (librosa tempo + chroma/Krumhansl) so the
  timbre index and sidecars expose real bpm/key (user asked earlier whether the
  pipeline captures bpm/key — it does not today).
- Native integration: extend HDAW FileLibraryManager to ingest <file>.timbre.json
  sidecars (tags/description into LibraryEntry, match in search, surface in
  search_library) — requires hdaw-guard skill + rebuild.
- Characterize the remaining 112718 loops (stage-1 DSP) and build a categorized
  "texture palette" doc so future compositions pick loops by role.

## 9. Scratch notes for the next session

- Kernel helper snippets to recreate after restart: `call`/`parse_result`,
  `load24` (int24 WAV parser), `env24` (envelope), `export_full` (serialized
  export + wait-for-stable-size, fresh filename each time), `add_texture`.
- Patterns used for the groove (16 beats, 120bpm) are in the .hdaw3 files;
  if you need them in Python, re-extract from XML <MIDI_NOTE> lists.
- Keep sampler_demo.hdaw3.bak as the pristine 4-bar base.
