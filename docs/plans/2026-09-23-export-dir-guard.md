# Plan: `export_audio` must not silently write nothing

**Status:** directed 2026-09-23 ("address all three… in the order you suggest"). **Risk: LOW**
— one guard on the export path, no DSP/render behaviour change (it only ensures the output
directory exists and makes a null stream an error instead of a silent success).
**Why:** `export_audio` reports `success: true` and writes **nothing** when the output
DIRECTORY does not exist. Measured twice in this session (two full renders lost: one on
`dub_embers`, one on `aether_dub`'s stem) and once earlier — recorded as the trap
`export-dir-must-exist` in `docs/paths/psydub.json` and in `docs/testing-mcp.md`.

## What exists (verified 2026-09-23)

| Piece | Where | Note |
| --- | --- | --- |
| The bug | `ExportManager.cpp:554` — `auto* outStream = outputPath.createOutputStream().release();` | **no directory check and no null check**: with a missing directory the stream is null and the export proceeds/fails silently while the caller reports success |
| The house pattern to mirror | `AudioEngineCommands_Composition.cpp:1944` (`f.getParentDirectory().createDirectory().wasOk()`), `AudioEngineCommands_Song.cpp:354`, `AudioRecorder.cpp:21`, `ChainLibrary.cpp:256` | **four call sites already create the parent directory first** — this is the established convention, not a new idea |
| The tool's result path | `src/mcp/McpExportTool.cpp` (`export_audio` → returns "export started" / reports success) | where a hard failure must surface as an error |

## Success gates

- **G1 — creates the directory.** `export_audio {outputPath: <dir>/x.wav}` where `<dir>` does not
  exist succeeds and the file is written with the expected size (the reproduction that failed
  twice before).
- **G2 — cannot silently no-op again.** If the stream still cannot be opened (permissions, the
  path is a directory, a locked file), the export reports **failure** — an error the caller sees —
  rather than `success: true` with no bytes. This is the deeper half of the bug: G1 alone leaves
  the silent-success path for every *other* cause.
- **G3 — no render-path behaviour change.** A successful export of an existing directory is
  byte-identical to before (the guard sits *before* the stream open, touching nothing else).
- **G4 — blast radius.** No change to `processBlock`, DSP, latency, or the render loop itself;
  the existing export/mix suites stay green.

## Steps

1. **Slice G (code-only):** the directory creation (mirroring the house pattern) + the null-stream
   error path, with tests for G1 and G2, in existing test files.
2. **Orchestrator:** retire the `export-dir-must-exist` trap in both trees and the `testing-mcp.md`
   note (or mark them fixed), then build, run the export suites, and commit.
