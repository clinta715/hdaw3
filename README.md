# HDAW — Holofonic Digital Audio Workstation

A desktop DAW built in C++20 with a React 19 + TypeScript frontend and
JUCE 8 for the audio engine. Versioned as a single self-contained
application — clone, configure, build, run.

**Current version**: 0.27.0

## Quick start

```powershell
# Configure (one-time, with Qt 6 prefix):
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64

# Build (RelWithDebInfo = optimized + debug symbols, recommended):
cmake --build build --config RelWithDebInfo

# Run:
build\RelWithDebInfo\HDAW.exe
```

Or use the build scripts: `frontend\build.bat` (full pipeline) or
`build-fast.bat` (incremental). Both default to RelWithDebInfo;
pass `Debug` for breakpoint debugging.

## What works today (v0.27.0)

### Project & transport
- New / Open / Save / Save-As projects (`.hdaw` files via JUCE
  `ValueTree` XML serialization).
- Multi-track project model with track-level volume, pan, mute,
  solo, and per-track FX chains.
- Audio routing graph with Master bus, FX buses (e.g. Reverb),
  and per-track Sends.
- Play / Stop / Rewind / Record transport, sample-accurate
  positioning, loop region with start/end markers on the ruler
  and right-click context menu to set loop boundaries.
- BPM and time-signature controls; metronome toggle.

### Timeline & arrangement
- Three-panel layout: track headers on the left, timeline
  graphics view in the centre, bottom-stack of detail panels
  (Mixer, Piano Roll, FX Chain, Automation) below.
- Add Track appends a new track below existing ones, all
  aligned to the timeline canvas.
- Per-track clip lanes. Default project ships with a Synth track
  carrying two MIDI clips (the project deliberately does **not**
  ship sample audio files — see "Known limitations" below).
- Drag-drop audio or MIDI files from File Explorer onto a track
  to import them.
- Right-click empty timeline area for Add Track, Add MIDI Clip
  (placed at the mouse position), Set BPM, Add Tempo Change.
- Right-click a clip for Delete / Open in Editor.
- Multi-clip selection (rubber band, Ctrl+click, Shift+click range,
  Ctrl+A). Move, delete, cut, copy, paste, duplicate selected clips
  (Ctrl+C/X/V/D). Paste offsets clips relative to the playhead.
- **Merge MIDI clips**: select ≥2 MIDI clips on the same track,
  then Ctrl+M or right-click → Merge Clips. Creates one clip
  spanning the full range with all notes preserved. Undoable.
- Named markers on the time ruler (right-click ruler → Add Marker).
  Click to seek, drag to move, double-click to rename.
- **Tempo changes on the ruler**: right-click to add/edit/remove tempo
  points; drag tempo markers to reposition. All mutations undoable.
- **Duplicate Track** via context menu (track header + mixer strip)
  and MCP tool. Deep copy with ID-safe clip/note reassignment.
- Zoom-to-fit: press F to fit all clips, Shift+F to fit selection.

### Clip interaction
- **MIDI clips**: double-click opens the Piano Roll. Notes can
  be created (click), moved (drag horizontally = beat, drag
  vertically = pitch), resized (drag right edge), selected,
  and deleted (right-click or Delete key). Ctrl+A selects all.
  Right-click context menu provides Quantize, Humanize, and
  Transpose. Keyboard shortcuts: Up/Down arrows for transpose
  (±1 semitone, ±1 octave with Ctrl), Q for quantize, H for
  humanize. **Ctrl+wheel zoom** in the piano roll grid. Toolbar
  controls: loop toggle, velocity scaling, duration scaling,
  and quantize strength (partial quantize 0–100%).
- **Audio clips**: clips render waveforms from the source file.
  Drag the body to move; drag the left/right 6 px edge to trim
  the start or duration; drag the top corners to set fade-in /
  fade-out. Right-click context menu provides Normalize and
  Reverse operations (creates new WAV files alongside originals).
  Take management via context menu when multiple takes exist.
- **Audio clip editor**: double-click an audio clip to open a
  dedicated editor panel with waveform display, zoom, gain slider,
  fade-in/out controls, offset/duration editing, loop toggle, and
  timestretch controls (Source BPM, Stretch Mode, Manual Ratio,
  Fit to Loop). Region selection via drag on the waveform enables
  copy/cut/paste of audio regions (inserted at the playhead).
  Gain envelope editor for adding visual gain curve automation
  points. Clip slicing at the playhead, at transients (beat-sync),
  or at region selection boundaries.
- Clip edits are wired through `juce::UndoManager`, so
  Edit → Undo / Redo work for every clip change.

### Mixer
- Per-track channel strip with volume fader, pan, mute, solo,
  record-arm, automation toggle, VU meter.
- Per-track FX chain editor in the FX Chain panel. Drag the handle
  to reorder slots, or use the up/down arrows.
- **Whole-channel FX chain presets**: save / list / load / delete,
  stored per-user; applying a preset is one undo unit and rebuilds
  the live chain. Reachable from the FX Chain panel preset bar,
  RPC, and MCP.
- Per-track MIDI channel routing (1-16) via track header context menu.
- Per-track automation lane editor in the Automation panel
  (Volume, Pan, Mute are default lanes; plugin FX parameters
  available via the Add Lane button).
- MIDI CC automation recording (CC Rec toolbar button captures
  incoming controller events during playback).

### Audio engine
- JUCE 8 audio device management with `AudioDeviceManager`.
- Per-track `AudioProcessor` instances summing through buses.
- Internal FX: gain, EQ, compressor, reverb, delay (via
  `juce_dsp`); PsyFm FM synth added to the FX panel list (parity
  gap).
- **Saturator internal FX**: drive with 4 transfer curves
  (SoftTanh/SoftAtan/Hard/Bitcrush), asymmetry, 2× oversampled
  with latency reported into PDC, DC-blocked, dry/wet + output
  trim, per-param clamping.
- Plugin hosting: VST3 and CLAP via JUCE's native format
  loaders. Plugin Manager dialog scans known paths and lists
  detected plugins. Plugin search filter in FX slot combo box.
- Plugin delay compensation (PDC) — track latency is computed
  from the FX chain and compensated automatically.
- Audio file import (WAV, AIFF, MP3, FLAC, OGG) into a project
  pool with thumbnail caching. BPM metadata extracted on import;
  optional auto tempo-match places imported clips at the project
  tempo. **Leading/trailing silence auto-trimmed** at -60 dB on
  import (both menu and drag-drop paths).
- **Transport-synced loop preview**: file browser audio preview
  follows the project transport and loop region when playing.
  Switches to free playback when transport is stopped.
- **Missing source file indicator**: clips with missing .wav files
  show a red "FILE MISSING" label in the timeline and a clear error
  message in the audio editor.
- **MIDI file import** (`.mid`, `.midi`): File → Import MIDI
  (Ctrl+Shift+M) or drag-drop a MIDI file onto the timeline.
  Parses tempo, note pitches, velocities, and durations from all
  tracks in the file. Each MIDI track becomes a new HDAW track with
  a clip containing the imported notes. Alternatively, import into
  an existing track via the Import Dialog track selector.
- Audio export to WAV (Export dialog).
- Pitch-preserving audio clip timestretch via SoundTouch (Manual
  Ratio, Fit to Loop, Tempo Match modes). BPM-aware: matches
  imported clips to project tempo; tracks project tempo changes
  for existing tempo-matched clips.
- Metronome with count-in/pre-roll, time signature support.
- MIDI hardware input with device selection and persistence across
  launches.
- Per-track input monitoring toggle.
- Automation recording — fader/knob/mute movements captured during
  playback.
- Preferences dialog with Audio Settings (driver, output/input
  device, sample rate, buffer size, latency display), MIDI device
  persistence, count-in bars configuration. **Audio device settings
  persist across launches** (driver, device, sample rate, buffer size
  restored on startup with fallback to defaults).
- MCP HTTP server host is configurable in Preferences (defaults to
  `127.0.0.1`).

- Generative composition: PhraseGeneratorDialog (phrase styles, chords, progressions) gained a Rhythm mode — polyrhythmic/euclidean drum patterns, snare toggle, and house/techno/dnb genre styles.

### File Library
- **File Library System** — a centralized registry of external audio and
  MIDI directories. Libraries are registered in Preferences, scanned
  (with progress events pushed to the frontend), and searched with
  filters (name, type, BPM, key, duration).
- File Browser **Library mode** (Library chip in the filter bar) shows
  configured libraries, scan progress, and a metadata-rich search
  results table (Name, Duration, BPM, Key, Size). Entries are draggable
  to the timeline.
- **Auto-play preview** — clicking a library entry loads and plays the
  audio preview when the auto-play toggle is enabled.
- **Default MIDI library** — on first launch, creates
  `%APPDATA%/HDAW/MIDI` with example files (C Major Scale, Drum
  Pattern, Chord Progression) and auto-scans it.
- **Key/BPM detection** — audio scans estimate BPM (file metadata, else
  aubio-based detection) and key (chromagram); TimbreLib sidecar
  `.timbre.json` files (tags, description, key, BPM, DSP features) are
  ingested automatically when newer than their audio.
- **Incremental scanning** — only re-indexes files whose modification
  time has changed; deleted files are pruned automatically.
- **Partitioning** — libraries with >50k entries are saved as
  per-first-character partition files for efficient loading.
- **Error hardening** — corrupt files are logged and skipped; missing
  directories are handled gracefully; FFT uses named constants.
- 7 MCP tools (`library.*`) and 7 RPC methods mirror the full feature
  set.

## MCP server

HDAW exposes an MCP (Model Context Protocol) server so an LLM client
can drive the DAW. 50 tools cover project inspection, transport,
tracks, clips, MIDI notes, composition (PhraseGenerator + arrangement
generation with snare support and Techno/House/DnB genre styles +
polyrhythmic & euclidean rhythm pattern generation
(`RhythmPatternGenerator`) + incremental seeded Markov arrangement
(`generate_psytrance_markov`: role-pool layering, thick chord pads with
rhythmic gating, and a vaguely-timed section-energy structure so
build-ups/breakdowns emerge instead of landing on a fixed grid)), FX,
automation, undo, audio export, and file library.

### Launching the stdio server (Claude Desktop, opencode, etc.)

Most MCP clients launch the server as a subprocess. Add HDAW to your
client's MCP config:

```json
{
  "mcpServers": {
    "hdaw": {
      "command": "C:/path/to/HDAW.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

HDAW detects piped stdio and runs headless (no GUI) with the MCP stdio
transport. The process exits when the client closes the pipes.

### Optional HTTP transport (loopback only)

In the GUI, enable **Tools → MCP HTTP Server**. Defaults: `127.0.0.1:8765`.
Configurable in **Preferences → MCP**. Binds loopback only; no
authentication. Do not expose beyond loopback.

### Safety

- Every destructive tool (`remove_*`, `clear_notes`, `duplicate_clip`,
  `export_audio`) accepts `dryRun: true` and reports what it would do
  without mutating.
- Every mutation is undoable via `undo` / `redo` tools, and the GUI's
  Ctrl+Z.

## Goals (what the project is working toward)

In priority order:

1. *DONE in v0.9.0* — per-clip audio editor with waveform display,
   gain, fade curves, timestretch controls, clip slicing, region
   clipboard, and gain envelope editing.
2. **A complete piano-roll feature set** — controller lanes
   (CC data), velocity editing, piano-roll zoom and scroll,
   snap-to-grid, multi-note selection, copy/paste. The
   underlying model and editor are in place; the UI is
   functional but minimal.
3. **A bundled sample library** — the project deliberately does
   not ship audio samples today, which is why default audio
   tracks appear empty. The goal is a small set of royalty-free
   test tones or samples that ship with the binary, so a fresh
   install shows a non-empty timeline.
4. **Stable project file format** — current format is
   `ValueTree` XML, which is portable but verbose. It now carries
   provenance and schema metadata (`createdWithApp`, `savedWithApp`,
   `formatVersion`, timestamps) with a load-time migration hook — see
   `docs/architecture.md` § "Project File Metadata". Remaining: a
   compact `.hdaw` JSON-or-binary container and forward-compatibility
   migrations across `formatVersion` bumps.
5. **Recording workflow polish** — armed tracks, take
   management, basic comping, and punch-in/out.
6. **Test coverage** — the codebase has no automated tests. The
   goal is unit tests for the model layer (clip math, undo
   transactions) and integration tests for the UI handlers.

## Known limitations

- **Default project has no audio.** The Synth track ships with
  two MIDI clips; the two audio tracks exist as empty lanes
  ready to receive dropped-in audio.
- **No bundled sample library.** See "Goals" item 3.
- **No audio crossfades** — adjacent clips don't auto-crossfade.

## Project layout

```
src/
  main.cpp                       — entry point
  engine/                        — JUCE audio engine
    AudioEngine.{h,cpp}
    MainAudioProcessor.{h,cpp}
    Track.{h,cpp}
    RoutingManager.{h,cpp}
    TransportManager.h
    ProjectPool.h
    ProjectSerializer.{h,cpp}
    PluginManager.{h,cpp}
    ClipClipboard.{h,cpp}        — multi-clip copy/cut/paste
    ...
  model/
    ProjectModel.{h,cpp}         — juce::ValueTree project schema
  ui/                            — Qt 6 widgets
    MainWindow.{h,cpp}           — main window, menus, panels
    TimelineView.{h,cpp}         — top half of the main window
    TimelineScene.{h,cpp}        — QGraphicsScene with clip items
    TimelineInteraction.{h,cpp}  — mouse handling for the scene
    TimelineToolbar.{h,cpp}      — play/stop/record toolbar
    TimeRuler.{h,cpp}            — time ruler
    TrackHeaderWidget.{h,cpp}    — per-track header (M/S/R, vol, pan)
    ClipItem.{h,cpp}              — base class for clip items
    AudioClipItem.{h,cpp}        — audio clip with waveform
    MidiClipItem.{h,cpp}         — MIDI clip wrapper
    MixerWidget.{h,cpp}          — per-track channel strips
    MixerStripWidget.{h,cpp}     — single channel strip
    PianoRollWidget.{h,cpp}      — piano roll editor
    NoteGridWidget.{h,cpp}       — note grid
    PianoKeysWidget.{h,cpp}      — piano keys column
    PianoRollRuler.{h,cpp}       — piano roll time ruler
    VelocityLaneWidget.{h,cpp}   — velocity editor
    FXChainWidget.{h,cpp}        — per-track FX chain editor
    AutomationLaneWidget.{h,cpp} — per-track automation editor
    ProjectPoolBrowser.{h,cpp}   — file pool side panel
    PluginScannerDialog.{h,cpp}  — plugin scanner
    PreferencesDialog.{h,cpp}    — preferences (audio, MIDI, MCP)
    ExportDialog.{h,cpp}         — audio export
    MarkerItem.{h,cpp}           — time ruler markers
    StatusBar.{h,cpp}            — status bar widgets
    VUMeter.{h,cpp}              — VU meter widget
    DebugLog.h                   — HDAW_LOG macro
    Theme.h                      — dark theme tokens
  mcp/                           — MCP server
    McpServer.{h,cpp}            — core server, tool dispatch
    McpTools.{h,cpp}             — tool registrations (36 tools)
    McpTransport.{h}             — transport interface
    McpTransportStdio.{h,cpp}    — stdio transport
    McpTransportHttp.{h,cpp}     — HTTP transport (configurable host)
    McpJsonRpc.{h,cpp}           — JSON-RPC framing
    McpSchema.{h,cpp}            — JSON-Schema validator
cmake/
  JUCEHelper.cmake               — wraps FetchContent for JUCE
CMakeLists.txt                   — top-level build script
AGENTS.md                        — pitfalls and conventions
DEV_PLAN_CPP.md                  — original Rust-to-C++ conversion plan
```

## Changelog

### v0.27.0 — FX chain presets + standalone saturator FX

**FX chain presets:**
- ChainLibrary JSON storage (user dir, sanitized names, duplicate
  uniquification, corrupt-file safe); `exportFxChain`/`applyFxChain`
  engine commands — atomic apply (all validation before mutation),
  single undo unit, sampler file-mode/root and byte-identical plugin
  state preserved.
- 4 RPC routes (`project.saveFxChainPreset`/`listFxChainPresets`/
  `loadFxChainPreset`/`deleteFxChainPreset`) + 4 MCP tools
  (`save_fx_chain`/`list_fx_chains`/`load_fx_chain`/
  `delete_fx_chain`) + docked preset bar in the FX Chain panel
  (stale-response guards, busy-gating).
- Load by id or name (ambiguous name rejected); preset-apply
  failure paths rebuild the live chain.

**Saturator FX:**
- Header-only `SaturatorEngine` (NaN/Inf-immune, sample-rate-aware
  20 Hz DC blocker, 2^bits bipolar bitcrush); `TrackFXSlot`
  integration with 6 clamped params (Drive dB 0–40, Type 0–3,
  Asymmetry −1..1, Mix 0–1, Output dB ±24, Bits 2–16).
- 2× oversampling (polyphase IIR halfband, factor-is-exponent trap
  documented); 4-sample latency summed into track PDC; Mix=0
  renders bit-identical to bypass; clamp sites include the
  automation path (newly closed lesson-23 gap).
- Measured: 18 kHz decimation-fold −42.7 dB, neutral fidelity
  −0.13 dB, harmonics +33.8 dB at Drive 24.

**Tests:** full suite now 1328 tests / 216 suites, 0 failed.

### v0.26.0 — Markov composition mode, vague macro-structure, timbre key/BPM

**Markov composition (`generate_psytrance_markov`):**
- New pure-engine `PsytranceMarkovGenerator`: grows a psytrance arrangement
  2 bars at a time under a seeded Markov chain over a role pool
  (kick/bass/hat/arp/stab/pad/clap). Wired end-to-end: MCP tool →
  `generatePsytranceMarkov` RPC → clip writing, with full unit coverage
  (`psytrance_markov_test.cpp`).
- **Thick chord pads**: pads emit triad/7th chord stacks with rhythmic
  8th/16th-note gating; a `PadVariant` action toggles voicing, pulse grid, and
  a secondary harmony (instead of thin root+fifth pads).
- **Vague macro-structure**: `sectionCycleBars` is a gravity well, not a grid —
  section lengths are drawn seeded per section (sparse/breakdowns short, peaks
  sustain); a state cannot outstay the base cycle (forced advance), section
  transitions are never silent (always a structural/FX action), and a
  staleness ramp pushes swap/remove so elements never sit unchanged for long.
- Key discipline preserved: 1 home key + at most 1 secondary key per track.

**Sample-library key/BPM (timbre pipeline):**
- `timbre-lib` analyzer now emits `key` (filename tag first — `Am`, `F#m`,
  `C min`, `G# Minor` — then Krumhansl chroma estimation) and `bpm` (filename
  tag — `128 BPM`, `126_BPM` — then onset-tempo estimation) into
  `timbre_index.json` and per-file `.timbre.json` sidecars.
- `FileLibraryManager` ingests sidecar key/BPM during scan (sidecar key
  overrides the native chroma guess; sidecar BPM fills only unknown entries),
  and audio scans gain an aubio-based BPM fallback when file metadata carries
  no tempo. `search_library` key/BPM filters now match analyzed packs directly.
- `timbre-lib/backfill_keybpm.py`: fast key/BPM backfill for already-analyzed
  sidecars (~0.2s/file, Windows python). All 248 psy-pack sidecars backfilled.

**Tests:** `PsytranceMarkov.*` (14 tests incl. section jitter/caps/audibility
gates), `FileLibraryTest` sidecar key/BPM + BPM-fallback tests; 41/41 green in
the touched suites.

### v0.25.0 — Long-composition engine fixes, list_notes, crash capture

**Engine fixes (from the 2026-08-27 MCP cluster-compose session, see
`docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md`):**
- **Note/CC cache rework (silent 512-note truncation fixed):** `MidiClipProcessor`
  note/CC caches are no longer fixed 512-entry arrays — they are heap vectors
  sized to the actual list (hard safety cap 8192, truncation now logs loudly).
  Long compositions (chord stabs, 1300+ note parts) no longer lose their tails
  past note #512. Cache snapshots are immutable `shared_ptr`s, so a rebuild on
  the command thread can never free storage the audio callback is reading
  (allocation-free audio path preserved).
- **Internal EQ gain fixed:** the track EQ fed *raw dB* (-24..24) into
  `juce::dsp::makePeakFilter`'s **linear** gain slot — the default 0 dB became
  silence on every track carrying an EQ. Now `Decibels::decibelsToGain` is
  applied (verified end-to-end in the psytrance render sessions).
- **Windowed-render reliability:** `renderTrackWindow` now drains pending
  routing-graph rebuilds before starting an export (a coalesced rebuild landing
  mid-render previously cancelled the export → "failed to read render" with no
  output file), and surfaces the real render-thread result via a new
  `ExportManager::getLastExportMessage()` (`ExportManager.cpp` logs
  success/failure + message).

**MCP tools:**
- New `list_notes` tool — list MIDI notes in a clip with the same filters as
  `remove_notes` (pitches, startGte/startLt, noteIds); returns full note
  properties in clip-local beats.
- `add_notes` contract documented in-tool: start/duration are **clip-local
  beats** and a clip plays at most 8192 notes (split long parts; excess is
  logged and skipped).

**Ops / tooling:**
- `mcp-launch.bat` — optional **procdump crash capture** for the §3 abort class
  (debug-CRT heap asserts / `std::terminate` previously died with only an MSVC
  dialog): full minidumps land in `%TEMP%\hdaw_crash_captures\engine_<rand>\`.
  Disable with `HDAW_NO_CRASH_CAPTURE=1`; use `HDAW_CRASH_DUMP_TYPE=mini` for
  smaller dumps. Stdio still flows straight through to the MCP client.
- `timbre-lib/` — new session scripts: `analyze_multi.py`,
  `analyze_targeted.py`, `analyze_psytrance.py`, `register_library.py`,
  `select_psy_samples.py` (+ `psy_sample_selection.tsv`) for the
  pack-sample selection/clustering workflows; `analyze.sh` extended
  (`--library` registration hardening).

**Tests (+537 lines):**
- `midi_clip_processor_test` — note-cache rework coverage (incl. >512-note clip
  that previously played a silent tail).
- `export_bake_timeout_test` (new) — render-bake timeout + cancelled-export
  paths.
- `audio_pool_dedup_test` — sampler re-set guard: first set → re-set with a
  different file → re-set with the same file updates the live processor sound.
- `mcp_functionality_test` — `list_notes` + note-contract coverage.
- `psytrance_composition_stress_test` (new) — long-arrangement renders with the
  8/27 crash's automation-lane set (EQ freq, phaser centre/depth, flanger rate)
  driving internal FX, under crash capture.

**Docs:**
- `docs/handoffs/2026-08-24-dnb-crash-generator-bugs-backlog.md`,
  `docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md` (the two
  session write-ups behind this release).
- Line-ending policy pinned to LF via `.gitattributes` (`* text=auto eol=lf`);
  session temp artifacts (`.tmp_dnb_theme/`, `renders/`, `clap-libs/`,
  `test.zip`, `.tmp_*`) added to `.gitignore`.

### v0.24.1 — MCP test coverage, set_note_velocities bug fix


**MCP test coverage (37 new tests, ~60 tools):**
- New `tests/integration/mcp/mcp_coverage_test.cpp` with `McpCoverageTest`
  fixture covering previously untested MCP tools: region ops (ripple_delete,
  insert_silence, duplicate_region, loop_clip), track ops (move_track,
  duplicate_track, add_track_with_fx), 12 note operators, tempo points,
  arranger (16 tools), sends, MIDI FX (6 tools), session (5 tools),
  library (7 tools), set_automation_points, generate_arrangement, project_info.
- MCP tool test coverage: 30% → 63% (53/179 → ~113/179 tools with tests).
- 1015 tests across 182 suites.

**Bug fix:**
- `set_note_velocities` and `remove_notes` MCP tools silently skipped all notes
  when no filter (`noteIds`/`pitches`) was provided — defaulted to `match=false`
  instead of `match=true`. Fixed: "all notes" is now the correct default.

### v0.23.2 — Part templates (role defaults), proxy namespace collision fix

**Proxy namespace fix (lesson-20 permanent guard):**
- Every `ProxyProcessManager` now auto-generates a unique namespace prefix
  (`<pid-hex>_<instance-counter>`) in its constructor — two managers in one
  process (or overlapping exports across processes) can never collide on
  pipe/shm/state-file names. `spawnPluginHost` retries with a bumped slot id
  (up to 8 times) when a name is detected as held at spawn time. `KillGraceful`
  now waits for child termination to ensure same-slot re-spawn never hits a
  lingering mapping from the prior child.
- `ShmRegion::create` treats `ERROR_ALREADY_EXISTS` as a hard failure — never
  silently opens a same-size existing region.
- The five CrashRecovery tests + `PluginIsolation.LiveDropDrainsStaleOutput`
  now pass in the **full** suite without needing to kill stale engines first.

**Part templates (typed track presets):**
- `addInstrumentPart` accepts a `role` parameter (`Bass`, `Lead`, `Chords`,
  `Drums`) with sensible defaults (style, note range, density, velocity, target
  RMS, scale mode). Explicit params always win. Exposed via RPC + MCP.
- 963 gtest suite / 0 failures.

### v0.23.1 — WASAPI device enumeration fix (COM init), choppy-audio fix

Fixes the **"only DirectSound devices are selectable / audio is choppy"** bug.
JUCE 8's WASAPI device type never calls `CoInitialize` itself (it asserts
`CO_E_NOTINITIALIZED` in `ComSmartPtr::CoCreateInstance`); since the Qt-GUI
removal (`QApplication` → `QCoreApplication`), nothing initialized COM on the
main thread, so the WASAPI endpoint scan returned empty (cached by
`hasScanned`) and `AudioDeviceManager` silently fell back to **DirectSound** —
an emulated driver (~58 ms latency, jittery callbacks → stuttering audio).

- New `HDAW::ScopedComInit` (RAII `CoInitializeEx(COINIT_MULTITHREADED)`) is
  the first statement of `main`/`main_headless`/`test_main` — covers startup
  default-init AND the `audio.*` RPCs (dispatched on the main Qt event loop).
- Saved-device restore in `AudioEngine::initialize` now switches the driver
  type FIRST, re-fetches the setup after, and only applies a saved device name
  if it exists in the new type's device list — previously it applied
  DirectSound-era names ("Primary Sound Driver") under WASAPI, got "No such
  device", and fell back to DirectSound again.
- Verified: WASAPI endpoints enumerate (Focusrite + NVIDIA HDMI), Focusrite
  opens at **10 ms / 441-sample** buffer (was 58 ms / 2560 on DirectSound);
  full gtest suite 862/862 pass.
- **Packaged-Electron note:** the shipped engine comes from
  `build/RelWithDebInfo/` — rebuild that config (`cmake --build build --config
  RelWithDebInfo`) and repackage before trusting the packaged app.

### v0.23.0 — Streaming handle sharing, incremental routing default ON, per-op bake avoidance

- `StreamingSoundPool` shares streaming file handles across clips; incremental
  clip routing flips to **default ON** (`HDAW_FORCE_INCREMENTAL_ROUTING=0` to
  opt out) with per-operation graph-bake avoidance, keeping clip edit ops under
  ~2 s for 128 clips (was ~80 s).
- Token-based HTTP auth for `UiHttpServer`.
- Per-instance pipe/shm namespace prefix for isolated plugin proxies (lesson-20
  guard); plugin respawn-storm termination: flag-once crash notification,
  sliding-window respawn budget (8/30 s), path re-resolution, pid+date log
  attribution.

### v0.22.0 — Clip disk streaming, shared decode pool, live-track race marshals

- `StreamingClipSource` background double-buffer reader: long clips stream from
  disk instead of whole-file loads; non-realtime sync-refill during export
  (Subsystem D gates).
- `DecodedSoundPool`: shared decode cache keyed by path — clips and the sampler
  share one decode; unused decodes pruned on rebuild.
- Sampler refinements: hold/glide/reverse/sample-range params, live plugin
  state persistence, loop crossfade.
- Message-thread marshaling of live-track mutations (track-FX rebuilds,
  modulation/automation/FX-editor, MIDI clip cache) — race/UAF fixes.
- Cursor-pinned wheel zoom + ruler marquee zoom; `.hdaw3` file association.

### v0.21.0 — Internal sampler instrument, FM synth, realtime-safety instrumentation

- Internal sampler instrument end-to-end (`SamplerEditor`, E2E tests) and an
  internal DX7-compatible 6-operator FM synthesizer (32 algorithms).
- Realtime-safety instrumentation: `RealtimeGuard` thread-id + lock-block
  tripwire, `BufferCheck` NaN/Inf/DC/glitch detector with atomic flag drain on
  clip/track/master/sampler paths, `HDAW_LOG_ALWAYS` bypass for tripwires.

### v0.20.0 — Rhythm pattern generator, export isolation fix

Ships the generative **rhythm pattern generator** end-to-end (engine, RPC, MCP,
UI) and fixes the **isolated-plugin export wedge**: a 475-clip export could
hang indefinitely (observed ~2.5 h at 0 bytes) because the offline render
shared the live `PluginManager`, so live graph rebuilds collided with the
export's proxy children and JUCE 8's non-realtime render-sequence bake had no
interrupt or timeout. Exports now run on a dedicated, per-export offline
plugin manager. See `docs/plans/2026-08-11-rhythm-pattern-generation.md`
(feature) and `docs/plans/2026-08-11-export-isolation-wedge.md` (fix) for the
full records.

**Rhythm pattern generation:**
- New `RhythmPatternGenerator` engine: two euclidean pulses (classic
  polyrhythm, e.g. 4-over-3) plus a rhythm-DSL voice (`E(k,n[,rot])`, groups)
  that finally drives the existing `Generative::expandToDivision`.
- `ArrangementGenerator` gains a real `generateSnare` (house 2&4 backbeat, dnb
  two-step breakbeat, techno sparse) and genre-aware style weights
  (style 0 = Techno unchanged, 1 = House, 2 = DnB).
- RPC `composition.generateRhythmPattern`, MCP `generate_rhythm_pattern`
  (plus `enableSnare`/genre styles on `generate_arrangement`), and a new
  "Rhythm" mode in `PhraseGeneratorDialog` with snare toggle and genre
  selector.

**Fix — export plugin isolation (the wedge):**
- The offline render now builds a dedicated `PluginManager::createOfflineCopy`
  with a per-export `ProxyProcessManager`, a per-domain pipe/SHM namespace
  (`hdaw_plugin_export_*`), and per-domain proxy crash-state files — live
  rebuilds, health monitoring, crash recovery, and slot IDs can no longer
  collide with export children.
- Bounded render-sequence bake wait: JUCE 8 bakes the offline graph
  asynchronously on the message thread with no interruptible path; the first
  `processBlock` waits on a FIFO-ordered message probe (max 15 s) and fails
  fast with a clear diagnostic instead of wedging forever.
- Verified against the wedge repro: a 475-clip / 2,634-note polyrhythm
  composition (A minor, 120 BPM, 240 s) exports cleanly with plugins isolated;
  in-process rendering regression-checked. Demo project:
  `polyrhythm_aminor_120bpm.hdaw3`.

**Tests:**
- New `CrashRecovery.OfflinePluginDomainIsolatedFromLive` — the export plugin
  domain cannot clobber live crash state.
- New `McpServer.ExportAudioWithMultipleIsolatedInstances`.
- PluginIsolation suite 37/37; export + MCP suites pass (50/50 in the
  isolation/export scope).

### v0.19.0 — MCP tempo/time-signature tools, editable time signature, export fix

Adds the missing tempo/time-signature surface to MCP (feature-parity gap:
`project.setTempo`/`project.setTimeSignature` existed as frontend RPCs but had
no MCP tools), puts the time signature on the live transport wire end-to-end,
and fixes the MCP `export_audio` tool being permanently blocked by a stale
cancel flag. See `docs/plans/2026-08-11-mcp-tempo-timesig-and-export-fix.md`
for the full record.

**MCP:**
- New `set_tempo` (bpm 1–999) and `set_time_signature` (numerator/denominator
  1–32) tools, backed by the existing `AudioEngineCommands` (undoable, same
  path as the frontend RPCs).
- `get_transport` now also returns `bpm`, `timeSigNumerator`,
  `timeSigDenominator`.

**Frontend:**
- `TransportSnapshot` now carries `timeSigNumerator`/`timeSigDenominator`
  end-to-end (C++ `ReadModel` → `notify.transport` → Zustand), replacing the
  hardcoded 4/4 in `transportExtrasStore` (which no longer holds time-signature
  state at all).
- The TransportBar `{n}/{d}` readout is now editable: double-click to edit
  numerator/denominator inline (Enter/blur commits via
  `project.setTimeSignature`, Escape cancels), mirroring the BPM editor.

**Fix — MCP `export_audio` stale-cancel lockout:**
- The handler refused to start whenever the server cancel flag was already set.
  The MCP client re-sends `notifications/cancelled` routinely between tool
  calls, so every export was refused (`export cancelled (flag was already
  set)`) and no WAV could ever be rendered over MCP. The flag is now consumed
  at export start.
- **Explicit cancellation:** the flag is no longer polled for mid-render
  cancel (the same client notification pattern re-armed it during the render
  and killed every export after the first block). Cancellation is now explicit
  via the new `cancel_export` tool, which aborts an in-progress render and
  deletes the partial file — mirroring the frontend's `export.cancel` RPC.

**Tests:**
- Rewrote `ExportAudioSkipsWhenCancelFlagSet` → `ExportAudioConsumesStaleCancelFlag`
  (stale flag is consumed, render proceeds, WAV written).
- New `McpServer.SetTempo` / `McpServer.SetTimeSignature` (set → read back via
  `get_project_summary`/`get_transport` → undo round-trip).
- Frontend: `transportStore.test.ts` asserts the new time-signature fields.
- Full suite: 722 tests pass (C++), Vitest transport store 4/4.

### v0.18.1 — Plugin scan & blacklist fixes

Fixes to the plugin scan/blacklist engine surfaced by the preset-listing task
(`list_plugin_presets` / `list_fx_params`). See
`docs/plans/2026-08-11-fix-scan-blacklist-bugs.md` for the full record.

**Engine fixes:**
- **Blacklist now actually loads.** `saveBlacklist()` writes `<BLACKLIST>` as the
  XML document root but `loadBlacklist()` only looked for a `<BLACKLIST>` *child*,
  so the blacklist was silently never enforced. The loader now accepts both the
  root-is-BLACKLIST form and the legacy wrapper form — blacklisted plugins are
  excluded at scan, listing, and instantiation time (with per-file skip logging).
- **Hung scanners can no longer stall a scan indefinitely.** The timeout path
  previously fell through to `readAllProcessOutput()`, which blocks forever when
  a wedged child's process handle never signals (e.g. a poison plugin half-loaded
  in the x64 scanner). The timeout path now kills with bounded re-waits, reports
  `"Scanner timed out (Ns)"` with the final process state, and never reads output
  on that path. A hard overall scan cap (`HDAW_SCAN_TOTAL_TIMEOUT_MS`, 15 min
  default) bounds the whole pass.
- **Slow plugins are no longer dropped.** The per-file scanner timeout is now the
  `HDAW_SCAN_PLUGIN_TIMEOUT_MS` knob (default 90s, was a hardcoded 30s). Also,
  `findPluginFiles` now enumerates VST3 **directory bundles** (e.g.
  `Serum2.vst3/Contents/x86_64-win/...`) in addition to single-file `.vst3`/`.clap`
  — bundle plugins were previously invisible to the engine scan entirely.
- **32-bit PE guard.** New shared header `src/common/PluginBinaryInfo.h` reads the
  PE machine type straight off disk and skips genuine 32-bit binaries (which
  cannot load in the x64 scanner) in both the engine and the scanner, with a
  clean `"skipped (32-bit binary)"` reason. FM8's exclusion is its blacklist
  entry (now active) — FM8 is actually an x64 image whose 2nd factory component
  wedges the scanner.

**Preset listing (carried over from the preset-listing task, verified live):**
- VST3 presets list via MCP `list_plugin_presets` (128-program banks for
  Zebralette3/TyrellN6/TripleCheese/Podolski/BazilleCM, Serum 2, Identity, and
  more); CLAP correctly reports empty (no program extension in CLAP 1.2.7).

**Tests:**
- New `PluginManagerScan` gtest suite (8 tests): blacklist round-trip through the
  manager, legacy wrapper form, PE machine-type detection (32/64/garbage),
  bundle enumeration, bounded timeout with hang-fixture + no-survivor tasklist
  probe, and the scanner exit-code reporting path.
- Full suite: 722 tests pass.

### v0.18.0 — File Library System

A centralized file library for managing external audio and MIDI directories.
Libraries are registered, scanned, searched, and previewed from the File Browser
or via MCP tools.

**Engine changes:**
- New `FileLibraryManager` — persistent registry (`%APPDATA%/HDAW/libraries/`),
  MIDI and audio metadata extraction (tempo, key detection via chromagram, BPM,
  channels, sample rate), search with filters (name, type, duration, BPM, key),
  lazy-loaded entries with double-checked locking, incremental scanning
  (timestamp-based change detection), partitioning for >50k entries, and error
  hardening (try/catch per file, sampleRate==0 guard, named FFT constants).
- 7 RPC methods (`library.list/add/remove/scan/search/getEntry/setAutoScan`)
  and 7 MCP tools with JSON shape parity.
- Default MIDI library created on first launch with example files (scales,
  drum patterns, chord progressions).
- Scan progress and completion events broadcast to frontend via WebSocket
  (`notify.scanProgress`, `notify.libraryScanComplete`).

**Frontend changes:**
- New Zustand `libraryStore` (DI pattern, 17 tests).
- File Browser **Library mode**: filter chip, library list with scan/progress,
  search with debounce, metadata columns (Name, Duration, BPM, Key, Size),
  drag-to-timeline, auto-play preview on click.
- Preferences **Libraries** section: add/remove libraries, auto-scan toggle.
- Scan progress events wired to store via `main.tsx` notification handlers.

**Tests:**
- 14 C++ gtests: registry CRUD, metadata extraction (MIDI + audio), search
  (name, type, sort, pagination, lazy-load), full pipeline, scan progress
  callback, partitioning.
- 17 frontend Vitest tests for the library store.
- 6 new Vitest tests for FileBrowser library mode.

### v0.24.2 — Frontend test coverage expansion

Added comprehensive unit tests for 9 previously untested UI components and
fixed TypeScript errors in 9 existing test files. Total test count increased
from 539 to 840 (301 new tests).

**New test files (9 components):**
- TransportBar (44 tests): button clicks, BPM/time sig editing, key/scale
  popover, store state transitions, RPC call verification
- PianoRoll (28 tests): clip selection, note grid, velocity/CC lanes,
  chord mode, quantize/swing controls
- FXChain (32 tests): slot rendering, plugin loading, parameter display,
  presets, A/B comparison, drag reorder
- ArrangerLane (32 tests): region rendering, drag/resize, rename, delete
- FileMenu (31 tests): menu toggle, items, keyboard shortcuts, recent projects
- ImportDialog (27 tests): file selection, format options, import flow
- ModulationPanel (24 tests): LFO display, target selection, rate/depth controls
- PluginManagerDialog (35 tests): plugin list, scanning, search/filter
- StepSequencer (48 tests): grid rendering, step interaction, pattern management

**Fixed test files (9 files):**
- AudioClipEditor.test.tsx: added missing ProjectSnapshot properties
- ErrorBoundary.test.tsx: fixed ThrowingChild component type
- meterStore.test.ts: added missing MeterLevels properties
- projectStore.test.ts: added missing TransportSnapshot properties
- ExportDialog.test.tsx: fixed Set<string> to Set<number>
- Inspector.test.tsx: added missing transport properties
- MidiThumbnailCanvas.test.tsx: added missing NoteSnapshot properties
- Mixer.test.tsx: added missing MeterLevels properties
- MixerStrip.test.tsx: added missing MeterLevels properties

**Test results:**
- 65 test files (up from 56)
- 840 tests passing (up from 539)
- Zero TypeScript errors in test files

### v0.17.0 — MIDI file import

Full MIDI file import with note data. Previously, importing a `.mid` file
created an empty MIDI clip; now the file is parsed and all notes (pitch,
velocity, duration) are imported.

**Engine changes:**
- New `importMidiFile` RPC (`project.importMidiFile`) — parses MIDI files
  using JUCE's `MidiFile`, extracts tempo, and creates clips with notes.
- Two import modes: `trackIdx == -1` (default) creates a new HDAW track
  per MIDI track in the file; `trackIdx >= 0` places all MIDI tracks as
  clips on the specified track.
- Returns `{ clipIds, trackCount }` for frontend reconciliation.

**Frontend changes:**
- Import Dialog (File → Import MIDI / Ctrl+Shift+M) now calls
  `project.importMidiFile` instead of `project.addMidiClip`.
- Timeline drag-drop of `.mid`/`.midi` files now imports with notes
  intact (previously created empty clips).

**Tests:**
- 3 new gtests: import into existing track, import creating new tracks,
  nonexistent file error handling.

### v0.16.1 — Structural refactor: oversized file decomposition

Broke the three largest files in the codebase into focused, maintainable
modules. Pure structural refactor — no behavior changes, no new features.

**FrontendRouter.cpp (1,445 → 61 lines):**
- Extracted 11 domain-specific router files into `src/frontend/router/`:
  `Router_Project`, `Router_Session`, `Router_Transport`, `Router_AudioGraph`,
  `Router_Read`, `Router_Plugin`, `Router_Midi`, `Router_Audio`,
  `Router_Export`, `Router_Preview`, `Router_Composition`.
- Extracted `RouterHelpers.h` (13 param-extraction helpers).
- `FrontendRouter.cpp` is now a 61-line top-level dispatch table.

**NoteGrid.tsx (916 → 235 lines):**
- Extracted into `NoteGrid/` directory with 7 modules:
  `noteGridTypes.ts`, `noteGridConstants.ts`, `useNoteGridDrag.ts`,
  `useNoteGridInteractions.ts`, `useNoteGridMarquee.ts`,
  `useNoteGridNoteMouseDown.ts`, `NoteGrid.tsx`.

**TimelineMinimal.tsx (911 → 497 lines):**
- Extracted into `TimelineMinimal/` directory with 4 hooks:
  `useTimelineRuler.ts`, `useTimelineDrop.ts`, `useTimelineKeyboard.ts`,
  `useTimelineClipOps.ts`.

### v0.15.1 — Batch operation crash fix

Fixed UI crashes when deleting, cutting, pasting, or splitting many clips at
once. The root cause was N individual RPC calls in a sequential loop; each
call could trigger a full routing-graph rebuild, and with large selections
the rebuilds would cascade and stall the UI.

**Frontend changes:**
- Delete and Cut now use the batch `project.removeClips` RPC (one call, not N).
- Paste now uses the batch `project.addClips` RPC with audio clip support
  (new `sourceFiles` parameter). One call for the entire clipboard.
- Split and Slice at Playhead/Transients now use batch
  `project.sliceClipsAtPlayhead` / `project.sliceClipsAtTransients` RPCs.
- Added `window.onunhandledrejection` handler — surfaces silent promise
  failures to console and toast notifications.
- Timeline wrapped in a granular `ErrorBoundary` — a render crash in the
  timeline no longer kills the entire app.

**Engine changes:**
- `addClips` extended with optional `sourceFiles` array — creates audio
  clips when a source file is provided, MIDI clips otherwise.
- New `sliceClipsAtPlayhead(clipIds)` — slices multiple clips at the
  playhead in one transaction with a single `rebuildRoutingGraph()` call.
- New `sliceClipsAtTransients(clipIds)` — same for transient-based slicing.

### v0.15.0

Plugin isolation: each plugin runs in a separate child process with
crash recovery, automatic respawn, and state survival across restarts.

## Conventions for contributors

Read `AGENTS.md` first. It documents the pitfalls that cost real
debugging time during the v0.2 UX pass, including:

- The QGraphicsView scroll-position pitfall that hid the default
  tracks at startup.
- The `sizeHint` override requirement for `TrackHeaderWidget`.
- The `setAlignment(Qt::AlignTop | Qt::AlignLeft)` requirement on
  the timeline view.
- The `DBG` macro collision with JUCE — use `HDAW_LOG` instead.
- The piano-roll `MIDI_NOTE_LIST`, scroll, and scrollbar-overlay
  traps.
- The `ClipItem::flags` requirement (no `ItemIsSelectable`).
- The `installEventFilter` trap for scene mouse events.
- The forward-declare pattern for breaking the
  `TimelineScene` / `TimelineInteraction` circular include.
- The build-pipeline gotchas (stale PDB on parallel builds,
  MOC, the stale Release binary, source list).
- The diagnostic pattern: add `HDAW_LOG` calls at construction
  and render, then read `%TEMP%/hdaw_debug.log` to cross-check.
- The loop-region init sync, TimeRuler context menu, and LoopMarker
  drag-commit bugs introduced and fixed in v0.2.2.
- The `clipSelected` → track-header selection sync pattern.
- The `TrackHeaderWidget` selection-highlight paint style.

## License

Internal project; license not yet decided.
