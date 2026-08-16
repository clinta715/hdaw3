# HANDOFF: Drums (#1) — Serum 2 MCP tool, remaining items (#2 #3)

> **Paste this whole file into a fresh agent session.** Repo: `D:\pdf\roo projects\hdaw3`. Windows, PowerShell 5.1, MSVC/VS generator, JUCE 8 via CMake FetchContent, React 19 + TS frontend.
> **MANDATORY:** before ANY code change, load the `hdaw-guard` skill (AGENTS.md) — plan-first, success gates, subagent execution, lesson-15 stale-binary discipline.
> **Predecessor session:** `docs/handoffs/2026-08-12-render-content-quality-and-ui-zoom.md` (the ORIGINAL 4-item handoff — read first for full context).
> **Earlier same-day predecessor:** `docs/handoffs/2026-08-12-vst3-inproc-resolution-and-fxtest-failure.md` (lessons 17–19 in AGENTS.md).

## Context

This session worked through all 4 items from the original handoff. Two are DONE (#4 zoom, partially #1 drums). Two remain (#2 intonation, #3 overlaps). This handoff documents what was completed, what's in progress, and the exact next step for #1.

## Item #4 — Timeline zoom: ✅ DONE

Implemented + repackaged. The user tested in the packaged Electron app (PID 20928). All E2E tests green.

**What was built:**
- `frontend/src/hooks/useTimelineZoom.ts` — pointer-centered Ctrl/Cmd+wheel zoom (beat under cursor stays fixed via `ppsRef` + `pendingScrollRef` + `useLayoutEffect`), plain-wheel-over-ruler zooms (no modifier required), marquee-zoom export (`zoomToRange`)
- `frontend/src/components/TimelineMinimal/useTimelineRuler.ts` — Ctrl+Alt+drag on ruler = marquee-zoom to dragged beat range (before existing scrub/loop-set branches to avoid gesture collision)
- `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx` — passes `tracksRef`/`rulerRef` into `useTimelineZoom`; wires `zoomToRange` to `useTimelineRuler` as `onMarqueeZoom`; renders `.tl-zoom-rect` overlay in ruler inner
- `frontend/src/components/TimelineMinimal.css` — `.tl-zoom-rect` (token-only: `var(--accent)` + `color-mix(in srgb, var(--accent) 20%, transparent)`)
- `frontend/src/components/StatusBar.tsx` — arrange-view hint extended with zoom gesture descriptions
- `frontend/src/hooks/useTimelineZoom.test.ts` — Vitest unit tests (pointer-centered math, rulerRef sync)
- `frontend/e2e/zoom.spec.ts` — 5 Playwright E2E tests (wheel-over-ruler, ctrl+wheel-centered, marquee-zoom, plain-wheel-passthrough, shift-wheel-passthrough)

**Gestures now working:**
| Gesture | Action |
|---|---|
| Plain wheel over **ruler** | Zoom (no modifier) |
| Ctrl/Cmd+wheel anywhere | Zoom **centered on cursor** |
| Ctrl+Alt+drag on ruler | Marquee-zoom to dragged region |
| Vertical drag on tracks | Zoom (existing, kept) |
| Plain wheel over tracks | Native vertical scroll (unchanged) |
| Shift+wheel | Native horizontal scroll (unchanged) |

**Plan:** `docs/plans/2026-08-12-timeline-zoom.md`

**Repackaged Electron:** `frontend/release/win-unpacked/resources/app.asar` is current (mtime 08/12 14:54:44 > dist 14:54:01). Browser-mode `build/Debug/HDAW.exe` is STALE (still at the pre-zoom state; needs `frontend\build.bat` to update).

**Tests:** Vitest 332/332 ✅, Playwright zoom suite 8/8 ✅, Vite build ✅

---

## Item #1 — Drums: IN PROGRESS (blocker: need new MCP tool)

### Diagnosis (complete)

**Root cause confirmed:** JE8086 is "The Usual Suspects JE-8086" — a **Roland JP-8080 synthesizer emulation**, NOT a drum machine (the "8086" references the Intel CPU, not the TR-808). The project loads it on all 5 drum tracks. Each track plays a single GM drum note (36/38/42/46/39) through the same synth patch — short tonal blips at ~65 Hz, not percussive drum sounds. The `Test Press Serum Phonk` drum preset library on disk (`D:\pdf\Xfer\Serum 2 Presets\Presets\S1 Presets\Test Press Serum Phonk\`) has explicit 808/909/707 drum presets for Serum 2.

**Spectral evidence** (timbre_check.py at `%TEMP%\opencode\timbre_check.py`): solo kick render — decay 17–188ms (short) but centroid stays ~440 Hz from attack to sustain (no broadband transient drop = synth, not drum), f0 sustained at ~65 Hz = MIDI note 36 (C2 through a synth).

**Timbre decision:** user wants 808/909 drum-machine sounds, synth-programmed via Serum 2.

**Selected presets** (Test Press Serum Phonk pack):
| Drum track | Preset file |
|---|---|
| Kick (tr 3) | `D:\pdf\Xfer\Serum 2 Presets\Presets\S1 Presets\Test Press Serum Phonk\TSP_SP_Drum_damage_kick.fxp` |
| Snare (tr 4) | `...\Test Press Serum Phonk\TSP_SP_Drum_808_snare.fxp` |
| Closed Hat (tr 5) | `...\Test Press Serum Phonk\TSP_SP_Drum_short_909_hi_hat.fxp` |
| Open Hat (tr 6) | `...\Test Press Serum Phonk\TSP_SP_Drum_hi_hat_open.fxp` |
| Clap (tr 7) | `...\Test Press Serum Phonk\TSP_SP_Drum_707_clap.fxp` |

Alt presets: `Se1ene Serum Presets Vol.1\Drum\` (DR kick1-5, DR Snare1, DR Hat1-3, DR Clap1 — clean kit). Also `Vintage Darkness\Drums - EightoEight.fxp` and `Tone Rider\DRUMS - Distorted 909.fxp` worth trying for Kick.

### What was done (swap complete, preset loading blocked)

**Swap done:** via MCP (headless, isolated mode), JE8086 → Serum 2 on all 5 drum tracks. Internal FX preserved (compressor/reverb/delay on slot 1). Saved as `polyrhythm_drums_serum.hdaw3` (721840 bytes, repo root). XML verified: `VST3-Serum 2-97d12514-c441d38e` on slot 0 of all 5 drum tracks.

**Preset loading BLOCKED:** user manually loaded presets in the running packaged app, but saved project has 5 IDENTICAL Serum states (md5 `e6977e55`, 4107 chars) — same patch on every drum track. Cause unclear (same preset loaded on all 5, or Serum init on all, or save-capture issue). The manual drag-drop/browser load path is unreliable.

**Decision:** add an MCP tool `load_plugin_preset_file` to deterministically inject presets via `setStateInformation`. User confirmed this path.

### Serum 2 `.fxp` format (verified on 4 files)

All Serum 2 .fxp files use a **consistent non-standard FPCh layout** (4-byte extra field vs standard VST2):

| Offset | Size | Field |
|---|---|---|
| 0–3 | 4 | `"CcnK"` magic |
| 4–7 | 4 | chunkSize BE (= **total file size**) |
| 8–11 | 4 | `"FPCh"` (chunk program) |
| 12–15 | 4 | `00 00 00 01` (Serum-specific) |
| 16–19 | 4 | `"XfsX"` (Serum's fxID) |
| 20–23 | 4 | `00 00 00 01` (numPrograms) |
| 24–27 | 4 | `00 00 00 01` (extra Serum field) |
| 28–55 | 28 | preset name (null-padded) |
| 56–59 | 4 | **chunkSize BE** (= filesize − 60) |
| 60+ | chunkSize | **state data** (this is what `setStateInformation` expects) |

Standard VST2 FPCh has data at offset 56 (chunkSize@52); Serum 2 shifts it to offset 60. Parser strategy: try Serum-2 layout first (chunkSize@56 == filesize−60), fall back to standard (chunkSize@52 == filesize−56). Verified: `chunkSize@56 == filesize − 60` for all 4 test files (92732, 92177, 20646, 3469 byte files).

### MCP tool design (ready to implement)

**Tool name:** `load_plugin_preset_file`

**Parameters:** `trackId` (int), `slotIndex` (int), `filePath` (string — path to `.fxp`)

**Implementation model:** Router_Audio.cpp:189-209 (the `swapFxSnapshot` A/B state RPC) — the closest existing analog that calls `setStateInformation` on a live plugin instance:

```cpp
// Router_Audio.cpp:194 — the pattern to follow:
slot->getPluginInstance()->setStateInformation(bState.getData(), static_cast<int>(bState.getSize()));
// And persist to ValueTree (so save_project captures it):
slotTree.setProperty(IDs::pluginState, currentState.toBase64Encoding(), &um);
```

**Steps in the handler:**
1. Validate `trackId`/`slotIndex` point to a plugin FX slot (same as `list_plugin_presets`)
2. Read file at `filePath`, validate magic `"CcnK"`/`"FPCh"`
3. Parse header: try Serum-2 layout (chunkSize@56 == filesize−60, data@60), then standard (chunkSize@52 == filesize−56, data@56)
4. Decode the chunk bytes
5. Get the plugin instance: `proc->getTrack(ti)->getFXChain()[si]->getPluginInstance()`
6. Call `setStateInformation(chunk.getData(), chunk.getSize())`
7. Persist: `slotTree.setProperty(IDs::pluginState, juce::MemoryBlock(chunk, size).toBase64Encoding(), &um)`
8. Return `"ok"` or error

**Blast radius:** ADDS one new tool registration inside `registerFxTools` (McpTools_Audio.cpp, between `set_fx_param` and the closing `}`). No changes to existing functions. Calls existing `setStateInformation` + `setProperty` — same path as project load + A/B state swap.

**Isolation safety:** For isolated plugins, `getPluginInstance()` returns `PluginProxySlot`, whose `setStateInformation` (PluginProxySlot.cpp:622) marshals to the child via the pipe. The child's `runLifecycleOnMessageThread` (lesson 16 fix) handles the thread marshaling. No new thread-safety concerns — inherits the existing infrastructure.

**Caveat (from Claude/Serum research):** "Xfer has explicitly said Serum preset file format is complex/programmatic and isn't public." The `.fxp` chunk IS what `setStateInformation` expects (Serum reads the same format internally). But the tool should work for ANY plugin's `.fxp` (generic VST2 FPCh parser), not just Serum-specific logic.

**Files to modify:**
- `src/mcp/McpTools_Audio.cpp` — add `load_plugin_preset_file` tool in `registerFxTools`
- `tests/unit/mcp/tool_registry_test.cpp` or new test — verify tool registers + basic param validation
- Optionally: `tests/unit/mcp/fx_preset_file_test.cpp` — test the `.fxp` parsing with a fixture file

**Pitfall gates:**
- **Gate 5 (stale closure):** N/A — handler is a lambda, reads from `a` (function arg), not closure.
- **Gate 8 (CSS tokens):** N/A — C++ only.
- **Gate 3 (audio-thread safety):** `setStateInformation` is called from the MCP handler thread (frontend/router). For isolated plugins, this marshals to the child via pipe — NOT on the audio thread. For inproc plugins, this runs on the router thread — NOT the audio thread. Safe.
- **Gate 2 (unimplemented code path):** trace the FULL path: MCP handler → `getPluginInstance()->setStateInformation()` → (isolated: pipe marshal → child's `runLifecycleOnMessageThread`) → plugin loads state. Verify by loading a preset and checking `list_fx_params` returns the new param values.
- **Gate 9 (ID validation):** validate `trackId`/`slotIndex` bounds (same as existing tools).

**Success gates:**
- [ ] Tool registers and appears in `tools/list`
- [ ] Loading a known `.fxp` produces a non-empty state on the target slot (verify via `list_fx_params` showing different param values than before, OR by comparing `pluginState` md5 before/after)
- [ ] All 5 drum tracks end up with DIFFERENT Serum states (md5 all differ)
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=McpTools*` passes
- [ ] `cmake --build build --config Debug` succeeds
- [ ] Solo drum renders produce distinct drum-like sounds (not identical tonal blips)

### After the MCP tool is built + presets loaded

**Render pipeline** (script ready at `%TEMP%\opencode\build_and_render_drum_solos.py`):
1. Build 5 solo variants via MCP (load `polyrhythm_drums_serum.hdaw3`, delete all but target track, save as `solo_kick_serum.hdaw3` etc.)
2. Render each solo 8s isolated via `py_repro2.py`
3. Run `timbre_check.py` on each to confirm drum-like character (fast decay, centroid drop at onset→sustain, weak f0)
4. Surface to user for listening
5. If any drum sounds wrong: iterate (swap preset, re-render)

---

## Item #2 — Out-of-key tonal sounds: NOT STARTED

From the original handoff: "pitched content outside A minor; suspects detuned oscillators in presets."

**Established facts:**
- Sub Bass and Piano Keys Identity slots have EMPTY `pluginState` → factory init patch — first suspects.
- Lead/Bass/Pad share one identical Identity state (md5 `007bfa8a`).
- Note ranges: Lead 65–88, Bass 36–52, Pad 53–67, Sub 29–36, Piano 65–79.

**Diagnostic steps (from original handoff):**
1. Solo-render each tonal track (variant projects + `py_repro2.py`), pitch-track output (FFT peak / autocorrelation per note segment), diff against A-minor scale.
2. Correlate off-key content with Sub Bass / Piano Keys (stateless) first.
3. Open Identity editor on a stateless vs stated slot; compare osc detune/unison/fine-tune params.
4. If the Identity factory patch detunes: fix the STATES in the project (preferred) rather than the plugin.

**Success gate:** pitch analysis of every tonal track shows only A-minor scale tones, confirmed by listening.

**Note:** the timbre_check.py at `%TEMP%\opencode\timbre_check.py` is reusable for pitch-tracking (the `centroid` and `f0sus` columns give per-note pitch estimates).

---

## Item #3 — Overlapping clips in project data: NOT STARTED

From the original handoff: "loading the project in the UI shows overlapping parts."

**Established fact:** Kick track — clip `Kick Build` spans 32–64 s while `Kick BBlock` spans 40–42 s inside it. This is real data, not a display bug. The generator wrote clips directly, bypassing `moveClipWithOverlap`.

**Scan script:** `%TEMP%\opencode\proj_inspect.py` — rerun to confirm overlaps; also scan `demo_aminor_120bpm.hdaw3`.

**Fix options (decide with user):**
a) Repair the project file(s): trim/move the offending clip(s) so no same-track overlaps remain.
b) Add a load-time sanitization pass or a generator-side invariant.

**Success gate:** `proj_inspect.py` reports zero overlaps on both demo projects, and the UI shows no overlapping clips on load.

---

## Repro assets & commands (still in `%TEMP%\opencode\`)

- **Render driver:** `python %TEMP%\opencode\py_repro2.py <isolated|inproc> <end_seconds> <project_path>`
  → `render_<tag>_<mode>.wav` + log. `inproc` sets `HDAW_NO_PLUGIN_ISOLATION=1`.
  A 240 s isolated render takes ~5 min; export wait budget is 600 s.
- **Project XML inspector:** `%TEMP%\opencode\proj_inspect.py` (tracks/clips/overlaps/note ranges/FX states).
- **State decoder:** `%TEMP%\opencode\state_check.py` (base64 pluginState → hex dump).
- **Timbre analyzer:** `%TEMP%\opencode\timbre_check.py` (onset detection, decay, centroid, f0 estimation).
- **Spectral analyzer:** `%TEMP%\opencode\spec_check.py` (narrowband bands), `verify_240.py` (sectioned RMS).
- **Drum solo build+render script:** `%TEMP%\opencode\build_and_render_drum_solos.py` — builds 5 solo variants (load project → delete all but target drum track → save) and renders each 8s isolated. Ready to run AFTER presets are loaded via the new MCP tool.
- **FXP parser:** `%TEMP%\opencode\parse_fxp.py` (Serum 2 .fxp header analysis).
- **Drum probe scripts:** `%TEMP%\opencode\probe3.py`, `probe4.py` (MCP plugin/preset listing).
- **Variant projects:** `solo_kick.hdaw3`, `solo_pad.hdaw3`, `drums_only.hdaw3`, `synths_only.hdaw3` (from predecessor, JE8086-era — rebuild from `polyrhythm_drums_serum.hdaw3` once presets are loaded).
- **Swap script:** `%TEMP%\opencode\swap_drums_serum.py` (MCP-driven JE8086→Serum 2 swap, already run).

## Key files modified this session

| File | Change |
|---|---|
| `frontend/src/hooks/useTimelineZoom.ts` | Pointer-centered zoom, ruler-wheel support, `tracksRef`/`rulerRef` params, `zoomToRange` export |
| `frontend/src/hooks/useTimelineZoom.test.ts` | NEW — Vitest unit tests for pointer-centered math |
| `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx` | Pass `tracksRef`/`rulerRef` to zoom hook; wire `zoomToRange` to ruler; render `.tl-zoom-rect` overlay |
| `frontend/src/components/TimelineMinimal/useTimelineRuler.ts` | Ctrl+Alt+drag marquee-zoom handler + `zoomRect` state |
| `frontend/src/components/TimelineMinimal.css` | `.tl-zoom-rect` overlay (token-only) |
| `frontend/src/components/StatusBar.tsx` | Extended arrange-view hint with zoom gestures |
| `frontend/src/components/StatusBar.test.tsx` | Updated hint assertion to match new text |
| `frontend/e2e/zoom.spec.ts` | NEW — 5 Playwright E2E tests for zoom gestures |
| `docs/plans/2026-08-12-timeline-zoom.md` | NEW — plan document for zoom work |
| `polyrhythm_drums_serum.hdaw3` | NEW — project with JE8086→Serum 2 swapped on 5 drum tracks (empty Serum state) |

## Environment warnings (from predecessor, still valid)

- **RDP audio is ephemeral.** Vanishes on session disconnect. Check `query session` / `Get-PnpDevice -Class AudioEndpoint` before suspecting code.
- `juce::Logger::writeToLog` → OutputDebugString ONLY. Use HDAW_LOG or cdb breakpoints.
- Debug builds: bash-tool default 120 s timeout kills builds mid-link — ALWAYS pass big timeouts.
- `%TEMP%\hdaw_debug.log` is the shared engine log (append, UTC; local = UTC−5).

## Out of scope (noted, don't fix here)

- Dead duplicate PPS constants in `utils/timelineConstants.ts` (80/20/400, wrong "per second" comment).
- Browser-mode `build/Debug/HDAW.exe` still stale (needs `frontend\build.bat` for full rebuild).
- Intermittent `Render graph bake timed out after 15s` in isolated export (pre-existing, lesson 15).
- `JUCE_ASSERT_MESSAGE_THREAD` spam in test output (pre-existing, debug-only, harmless).

## Related docs

- `docs/handoffs/2026-08-12-render-content-quality-and-ui-zoom.md` (original 4-item handoff)
- `docs/handoffs/2026-08-12-vst3-inproc-resolution-and-fxtest-failure.md` (predecessor, lessons 17–19)
- `docs/postmortem-silent-clap-export.md` (lessons 11–15)
- `docs/pitfalls-frontend.md`
- `docs/plans/2026-08-12-timeline-zoom.md` (zoom plan, completed)
- AGENTS.md (full lesson list, UI design section, MCP parity rule)
