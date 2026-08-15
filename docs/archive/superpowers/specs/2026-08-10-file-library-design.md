# File Library System Design

**Date:** 2026-08-10
**Status:** Approved
**Scope:** Persistent indexed database for MIDI files and audio samples, accessible via MCP and frontend

---

## 1. Overview

HDAW currently has no indexed library for MIDI files or audio samples. The file browser does raw `readdirSync` per expansion with no caching, no metadata, and no search. This design adds a persistent, searchable file library system that:

- Indexes MIDI and audio files from user-configured directories
- Extracts full metadata (duration, BPM, key, track count, etc.)
- Persists the index across sessions
- Scales to large libraries (10k+ files) with per-library partitioning at 50k
- Is accessible via MCP tools, RPC, and the frontend FileBrowser
- Supports auto-scan (optional) and manual rescan

---

## 2. Data Model

### Library Registry

**File:** `<userAppData>/HDAW/libraries/registry.json`

```json
{
  "libraries": [
    {
      "id": "midi-default",
      "name": "MIDI Collection",
      "path": "C:/Users/.../HDAW/MIDI",
      "type": "midi",
      "lastScan": "2026-08-10T12:00:00Z",
      "fileCount": 1240,
      "autoScan": true
    },
    {
      "id": "samples-drumkitty",
      "name": "DrumKitty",
      "path": "D:/Samples/DrumKitty",
      "type": "audio",
      "lastScan": "2026-08-10T12:05:00Z",
      "fileCount": 8500,
      "autoScan": false
    }
  ]
}
```

### Per-Library Entry Files

**File:** `<userAppData>/HDAW/libraries/<id>.json`

**MIDI entry:**
```json
{
  "name": "jazz_walk.mid",
  "path": "C:/.../jazz_walk.mid",
  "size": 4520,
  "modified": "2026-05-01T10:00:00Z",
  "tracks": 3,
  "notes": 245,
  "durationTicks": 3840,
  "durationSeconds": 3.2,
  "tempo": 120.0,
  "timeSignature": "4/4",
  "key": "C major"
}
```

**Audio entry:**
```json
{
  "name": "kick_clean.wav",
  "path": "D:/.../kick_clean.wav",
  "size": 88244,
  "modified": "2026-03-15T08:00:00Z",
  "durationSeconds": 0.5,
  "sampleRate": 44100,
  "channels": 1,
  "bpm": 128.0,
  "key": "C",
  "format": "wav"
}
```

### Partitioning

If a single library exceeds 50,000 entries, it partitions into sub-files by first character of the filename:
- `<id>_0-9.json` — files starting with a digit
- `<id>_a.json` through `<id>_z.json` — files starting with that letter
- `_` prefix for files starting with underscore or other non-alphanumeric characters

Partitioning is transparent to consumers — the `FileLibraryManager` handles loading/searching across partitions internally.

---

## 3. Scanning System

### FileLibraryManager

**Location:** `src/engine/FileLibraryManager.h` / `.cpp`

**Key class:** `FileLibraryManager`

**Responsibilities:**
- Manages the library registry and per-library entry files
- Runs background scans per library
- Provides search and lookup API
- Handles partitioning transparently

### Scan Behavior

1. **On startup:** Load registry. For each library with `autoScan: true`, enqueue a scan task on the background thread pool.
2. **Manual scan:** Triggered via MCP tool `scan_library` or UI "Rescan" button. Runs the same scan logic.
3. **Incremental scan:** Compare file `lastModified` timestamp against stored entry. Only re-index changed/new files. Files removed from disk are pruned from the index.
4. **Progress:** Callback fires `{ libraryId, scanned, total, phase }` during scan. Phase is `"scanning"` or `"writing"`.
5. **Completion:** After scan, write the diff (changed entries only) to the library JSON. Update `lastScan` and `fileCount` in the registry.

### Metadata Extraction

**MIDI files:**
- `juce::MidiFile::readFrom()` for parsing
- Track count: `file.getNumTracks()`
- Note count: count NoteOn events with velocity > 0 across all tracks
- Duration in ticks: max of last event timestamp across tracks
- Tempo: first tempo meta event in track 0 (default 120 BPM)
- Duration in seconds: `durationTicks * (60.0 / bpm) / ticksPerQuarterNote`
- Time signature: first time signature meta event (default "4/4")
- Key: note histogram heuristic — count occurrences per pitch class (C, C#, D, ...), map to major/minor by intervals

**Audio files:**
- JUCE `AudioFormatReader` for duration, sample rate, channels, format
- BPM: BWF/iXML `bpm` tag → fallback to `BpmDetector::detect()` (onset-based)
- Key: chromagram analysis (same heuristic as MIDI)
- Format: file extension (wav, flac, mp3, ogg, aiff)

### Thread Safety

- Scan runs on `juce::ThreadPool` worker threads
- `FileLibraryManager` protects internal state with `juce::CriticalSection`
- Entry reads during scan return stale data (from last persisted state) — no partial reads
- Registry writes are atomic (write to temp file, then rename)

---

## 4. MCP Tools

**Location:** `src/mcp/McpTools_Library.cpp`

| Tool | Parameters | Description |
|------|-----------|-------------|
| `list_libraries` | — | Returns all configured libraries with metadata |
| `add_library` | `path`, `name`, `type` (midi/audio) | Add a directory, trigger initial scan |
| `remove_library` | `id` | Remove library and its index file |
| `scan_library` | `id` (or `all`) | Trigger manual scan |
| `search_library` | `query`, `type` (optional), `libraryId` (optional), `durationMin`/`durationMax` (optional), `bpmMin`/`bpmMax` (optional), `key` (optional), `offset`, `limit` | Search across libraries with filters |
| `get_library_entry` | `libraryId`, `path` | Get full metadata for a single file |
| `set_library_autoscan` | `id`, `enabled` | Toggle auto-scan |

---

## 5. RPC Surface

Exposed to frontend via WebSocket:

| Method | Description |
|--------|-------------|
| `library.list` | List all libraries |
| `library.add` | Add a library |
| `library.remove` | Remove a library |
| `library.scan` | Trigger scan |
| `library.search` | Search with filters, paginated |
| `library.progress` | **Event** — pushed to frontend during scan |

- MCP and RPC share the same `FileLibraryManager` backend
- `library.progress` events stream to the frontend during active scans
- Search returns paginated results (default 50 per page, offset-based)

---

## 6. Frontend — Enhanced FileBrowser

### Library Mode

- New kind chip: **Library** (alongside All, Samples, MIDI, Devices, Presets, Clips)
- When Library is selected, the tree view shows configured libraries as top-level nodes
- Expanding a library node shows its indexed files
- Each file row displays metadata columns: **Name**, **Duration**, **BPM/Tempo**, **Key**, **Size**
- Columns are sortable (click header)

### Scan Progress

- Active scan shows a progress indicator on the library node (spinner + count)
- "Scan" button per library node for manual rescan
- "Rescan All" button at the top

### Preview

- Auto-play toggle in the file browser toolbar (off by default)
- When enabled: selecting a file plays it immediately
  - Audio: via existing `AudioPreviewPlayer`
  - MIDI: via a new simple note-to-audio preview renderer (sine wave at note pitch)
- Stop on deselect or next selection

### Settings Panel

- New section in Preferences: **Libraries**
- List of configured libraries with path, type, entry count, last scan time
- Add/Remove buttons
- Auto-scan toggle per library
- "Rescan Now" button

### Drag/Import

- Dragging from library to timeline calls existing `addAudioClip` / `importMidiFile` RPC — no change to import path

---

## 7. Default MIDI Directory

- On first launch, create `<userAppData>/HDAW/MIDI/` if it doesn't exist
- Register it as a library named "MIDI Collection" with `autoScan: true`
- Ship a small set of example MIDI files (scales, chords, drum patterns) in this directory
- The directory is user-writable — they can add their own files

---

## 8. Error Handling

- **Directory not found:** Library scan fails gracefully, logs warning, marks library as `lastScanStatus: "error"` in registry
- **Permission denied:** Same as above — skip directory, log, mark error
- **Corrupt file:** Skip individual file, log warning, continue scan
- **Disk full during write:** Write to temp file first, rename on success — no partial writes
- **Concurrent scans:** Only one scan per library at a time. If a scan is already running for a library, the request is a no-op (or queues)

---

## 9. File Locations Summary

| File | Location |
|------|----------|
| Library registry | `<userAppData>/HDAW/libraries/registry.json` |
| Per-library entries | `<userAppData>/HDAW/libraries/<id>.json` |
| Default MIDI directory | `<userAppData>/HDAW/MIDI/` |
| Example MIDI files | Shipped in the binary/installer, copied to MIDI dir on first launch |

---

## 10. Future Extensions

- **Tags/genres:** Add a `tags` array to entries, filterable in search
- **Waveform thumbnails:** Cache audio thumbnails alongside entries
- **MIDI preview playback:** Render MIDI to audio on-the-fly for preview
- **Audio Pool integration:** Reference counting for imported files, unused detection
- **SQLite migration:** If libraries exceed JSON comfort zone, migrate the storage layer

---

## 11. Testing Strategy

- **Unit tests (gtest):**
  - `FileLibraryManager` — add/remove library, scan, incremental update, partitioning
  - Metadata extraction — MIDI parsing, audio metadata, key detection heuristic
  - Search — name filter, metadata range filters, pagination
  - Persistence — write/read round-trip, atomic writes
  - Error cases — missing directory, corrupt file, permission denied

- **Frontend tests (Vitest):**
  - Library mode rendering, sort, search filter
  - Scan progress display
  - Auto-play toggle behavior

- **E2E tests (Playwright):**
  - Add library via settings, verify files appear in browser
  - Search across libraries, verify results
  - Drag from library to timeline, verify clip creation
