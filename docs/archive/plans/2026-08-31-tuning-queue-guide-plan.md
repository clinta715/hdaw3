
## Goal
Finish remaining handoff items: (A) tuning feedback loop (timbre-lib style), (B) export queue API polish, (C) guide doc JSON clipId note. All per hdaw-guard.

## Success Gates
- [ ] Gate A1: Python helper `timbre-lib/tune_roles.py` exists, reuses `timbre.py:extract`, computes centroid/rolloff85/mel_low/mid/high per rendered WAV (full mix and per-track 2s solo). Compares to per-role targets: kick <120Hz centroid-ish / mel_low dominant, bass 60-250Hz, arp/lead 400-3000 carved, hat >6kHz. Returns JSON with pass/fail + suggested adjustments (rootNote +/-12, filter cutoff, OctaveRange) and loop until pass or max 3 iterations.
- [ ] Gate A2: MCP tool `analyze_tuning` (or `check_role_tuning`) exposed, calls same logic on a given outputPath WAV, returns analysis. Unit test or manual MCP smoke shows centroid numbers.
- [ ] Gate B1: ExportManager supports queued export: new param `queue` (bool) on `export_audio` MCP + frontend `audio` — if queue=true and isExporting, wait (poll active with timeout 120s) for previous to finish then start, instead of immediate reject. Existing reject path unchanged when queue=false. No TOCTOU (use CAS + condition variable or poll).
- [ ] Gate B2: Frontend `export.cancel` still works; queued wait is interruptible via cancelFlag. Tests: concurrent export with queue=true succeeds sequentially; without queue still rejects.
- [ ] Gate C1: `docs/psytrance-composition-guide.md` table for add_midi_clip/add_audio_clip updated from `"clipId=N"` text to `{"clipId":N}` JSON, and §8 export note mentions queue param + CAS guard. Handoff doc `2026-08-31-psytrance-v6-bugs-handoff.md` marked Follow-up done for #6.
- [ ] Gate D: Build + tests pass (hdaw_tests --gtest_filter=*Export*:*Mcp*:*Tuning* if exists). No stale binary, no raw hex in CSS.

## Dependency Map
- A: timbre-lib/timbre.py (numpy/scipy), renders/*.wav, src/mcp (new tool), no audio thread.
- B: src/engine/ExportManager.{h,cpp} (active atomic, renderThread), src/mcp/McpExportTool.cpp, src/frontend/router/Router_Export.cpp, FrontendServer notify.
- C: docs/psytrance-composition-guide.md (markdown), no code blast radius.
- Projections: none for C, B touches ExportManager lifecycle, A is offline analysis (no ValueTree mutation).
- Pitfall gates: Gate 12 (graph mutation park) not triggered, Gate 4 stale binary, Gate 14 cross-process not touched, Gate 2 unimplemented path for new MCP tool.

## Pitfall Gates Triggered
- Gate 4: verify built binary contains new MCP tool + Export queue.
- Gate 2: new MCP tool must have full handler wired and tested (not a no-op).

## Steps
1. A: Create `timbre-lib/tune_roles.py` with ROLE_TARGETS dict, function analyze(path) via TB.extract, suggest_adjustments(). Add MCP tool `analyze_tuning` in src/mcp/McpTools_Analysis.cpp or similar that invokes python or duplicates logic in C++ (prefer python subprocess for now, but expose via MCP). Simpler: implement tuning check purely in Python and expose MCP tool that shells to python.
2. B: ExportManager: add std::condition_variable or simple poll loop for queue; add param handling in McpExportTool and Router_Export; update description.
3. C: Edit guide markdown table row for clipId + §8 export queue note; update handoff doc status.
4. Build + test all.
