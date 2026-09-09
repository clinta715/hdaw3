# Handoff — 2026-09-08: CorpusArranger shipped; engine bugs surfaced

## What shipped this session
- **`generate_arrangement_corpus`** (MCP) / `composition.generateArrangementCorpus` (RPC): corpus-sampled arrangement grammar. Deterministic plan sampling length/kick-intro/const-bass/late-novelty/breakdown from measured distributions (source: `compositions/psytrance_corpus_fulltracks.tsv`, n=533). Response includes `plan{barStart,bars,roles[],flags}` so consumers see exactly what was written. One undo unit, one clip per produced role.
  - Files: `src/engine/CorpusArranger.{h,cpp}`, command in `src/engine/AudioEngineCommands_Composition.cpp`, RPC in `src/frontend/router/Router_Composition.cpp`, MCP in `src/mcp/McpTools_CompositionGenerate.cpp`, tests `tests/unit/engine/corpus_arranger_test.cpp` (5) + regression sweep 69 (markov + export suites).
  - `PsytranceClip` gained `double startBeats` (`src/engine/PsytranceGenerator.h`) for chunk-anchored clips.
- **`export_audio.trackIds` fixed** (was schema-only phantom — every "stem" was a full-mix render; RAVE stems were smears of the whole song — the loudness/mud root cause). Now mutes non-selected tracks in the offline copy. `src/mcp/McpExportTool.cpp`, `src/frontend/router/Router_Export.cpp`; regression `McpCoverageTest.ExportAudioTrackIdsFiltersTracks`. Stems verified separated (floor=sub-dominant, drums=transient/body, mel=body, pad=warm).
- **Fader-authoritative used throughout** (`hdaw_set_fader_authoritative`) — volume lanes written by generators override faders otherwise.

## BUG 1 (PRIORITY) — live MIDI note-loss for clips > ~226 notes
- **Symptom:** generated MIDI clips with more than ~226 notes come out with EMPTY note lists in the live tree (`list_notes`/snapshot = 0) exactly at clip-add time; smaller clips survive. `notesTotal` reported by the generator is correct — the notes are dropped during live ingestion, not generation.
- **Proof:** live MCP engine, both generators: markov `bass:256→0`, `arp:448→0`; corpus `pad:4864→0`, `arp:832→0`, `bass:336→0`, `hats:364→0` while `kick:216`, `snare/clap:94`, `stab:132`, `fx:64`, `riser:56` survive. Threshold sits between 226 (survives) and 256 (lost) — likely a fixed ~256-entry cap in the clip-ingestion/write-back path.
- **Why in-process tests can't repro:** `engine.initialize()` in tests yields no live routing graph/device; the loss happens in the routing-clip-ingestion write-back (tree→processor→tree round trip when a MIDI clip is added to a track with an active routing manager).
- **Workaround (in place):** `CorpusArranger::generate` chunks every role into ≤200-note clips anchored at `startBeats` (piece start). 41 clips/arrangement; all 7052 notes verified live. This does NOT fix the engine bug (Markov big clips still lose notes).
- **Fix direction (ENGINE, wide blast radius — discuss first per AGENTS.md standing rule, incl. latency/quality evaluation lessons 7/8):** find the ~256 cap in the MIDI clip ingestion path — grep `Track::addClip` / `MidiClipProcessor` note-list rebuild / clip-processor write-back for a 256/255 constant or a resize-then-truncate; remove the truncation and add a regression test that adds a 400-note MIDI clip via a LIVE-engine path (MCP session) and asserts all notes persist. Candidate clues: `kMaxNotesPerClip = 8192` is the PLAYBACK ceiling (docs/architecture.md) — not this; the loss is at ~256, much lower.
- **Success gates:** live MCP session: generate markov with pad-mapped track, `list_notes` shows full pad count; corpus unchunked (temporarily disable chunking) passes; existing 69-test sweep green.

## BUG 2 (minor) — MCP-created tracks default to volume 0.85
- `src/mcp/McpTools_Track.cpp` lines ~40 and ~177 hard-code `setProperty(IDs::volume, 0.85)` for `add_track` / `add_track_with_fx`. Every MCP-minted track starts 1.4 dB down for no stated reason (default project "Synth" also 0.85 in `ProjectModel.cpp:382` — that one is intentional). Consider defaulting tool-created tracks to 1.0.

## BUG 3 (process) — stale engine binary after lib changes
- `ninja -C build hdaw_tests` relinks tests but NOT `HDAW_headless.exe` after `HDAW_lib` changes; the running MCP engine silently used an old binary (symptom: new tool not in registry despite `engine_info` reporting stale=false AFTER a manual `HDAW_headless` relink+restart). Always: `ninja -C build HDAW_headless hdaw_tests` and then `hdaw_engine_restart` → `mcp.reload` (launcher re-copies + size-verifies). Building the temp copy while the engine runs fails (exe locked) — restart first.

## Session artifacts (compositions/)
- `corpus_track_145_master_v2.wav/.hdaw` — final track (seed 141, 128 bars, E-minor, late-novelty lead), peak 0.867 / RMS 0.076 / kickProm 0.775 / bands sub7420 bass2089 body2291 high44.
- `corpus_rave_{mel,pad}_stem.wav` → `corpus_rave_{mel,pad}_out.wav` (RAVE `psytrance_synths`) → imported on tracks 14/15.
- `corpus_rave_{drums,floor}_stem.wav`; `psytrance_corpus_fulltracks.tsv` (533 tracks); `psytrance_arrangement_corpus.md`; `tools/corpus_arrange/` (sampler JS, distributions.json, INTERFACE.md).

## Next iteration pointers
- Density/velocity arcs from the corpus TSV (MIDI transcriptions are flat; re-impose arcs per section).
- Master loudness (current mix is dry/conservative, RMS 0.076).
- Percussion variation (still 4/4-basic; mine arcs from corpus).
- Second seeds / archetypes; Markov stays (kept, doc-deprecated as structure engine — still the micro-pattern source).
