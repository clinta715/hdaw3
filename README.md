# HDAW — Holofonic Digital Audio Workstation

A desktop DAW built in C++20 with Qt 6 for the UI and JUCE 8 for the
audio engine. Versioned as a single self-contained application —
clone, configure, build, run.

**Current version**: 0.10.0

## Quick start

```powershell
# Configure (one-time, with Qt 6 prefix):
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64

# Build:
cmake --build build --config Debug

# Run:
build\Debug\HDAW.exe
```

Run the **Debug** binary. The `build\Release\HDAW.exe` is a stale
binary from an earlier point in the project's history and is
intentionally not maintained. See `AGENTS.md` for the full build
pipeline and the rationale.

## What works today (v0.10.0)

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
- Right-click empty timeline area for Add Track, Add MIDI Clip,
  Set BPM, Add Tempo Change.
- Right-click a clip for Delete / Open in Editor.
- Multi-clip selection (rubber band, Ctrl+click, Shift+click range,
  Ctrl+A). Move, delete, cut, copy, paste, duplicate selected clips
  (Ctrl+C/X/V/D). Paste offsets clips relative to the playhead.
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
  humanize.
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
  `juce_dsp`).
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
- MIDI file import (`.mid`, `.midi`) into per-track MIDI clips.
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
  `127.0.0.1`). `--no-mcp` CLI flag fully implemented.

## MCP server

HDAW exposes an MCP (Model Context Protocol) server so an LLM client
can drive the DAW. 38 tools cover project inspection, transport,
tracks, clips, MIDI notes, composition (PhraseGenerator), FX,
automation, undo, and audio export.

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
   `ValueTree` XML, which is portable but verbose and not
   versioned. The goal is a `.hdaw` JSON-or-binary format with
   schema versioning and forward compatibility.
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
implementation_plan.md           — current development roadmap
```

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
