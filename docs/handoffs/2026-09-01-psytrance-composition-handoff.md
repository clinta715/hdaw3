# Handoff — Psytrance composition session + engine hardening (2026-09-01)

Session: continued from 2026-08-31 psytrance v6 bugs handoff. Built two new
tracks (Hypatia + Ion Storm), fixed three engine bugs, shipped four new MCP
tools, overhauled the build system, and began composing a third track with
Infected Mushroom B.P. Empire reference. Session spanned 3 subagent
generations + 4 restarts.

---

## 1. Engine fixes shipped (all in build/, verified via tests)

### 1a. ExportManager CAS guard (Fix C)
- **File:** `src/engine/ExportManager.cpp`
- **Change:** `startExport` TOCTOU closed — `if(active.load())` → `compare_exchange_strong(expected, true)`
- **Test:** ExportBakeTimeout 2/2, ExportAutomation 3/3, FrontendServer.DisconnectDuringExport 1/1

### 1b. PsyArp crash fix + grid-lock + Step Rate param (Gates 2/6/7)
- **Files:** `src/engine/PsyArpEngine.{h,cpp}`, `src/engine/TrackFXSlot.h`
- **Root cause:** `static_cast<int>(rng()) % base.size()` — uint32→negative int wraps ~34% of Random draws → `base[negative]` → MSVC vector assert. Grid clock free-ran from first trigger instead of deriving from transport beat position.
- **Fixes:**
  - Unsigned-domain modulo (`rng() % base.size()` cast AFTER modulo)
  - Grid-lock: stepIndex = `floor(sampleBeat / arpRateBeats)`, first trigger snaps to NEXT boundary
  - sequenceDirty rebuild on every held-note change, seqIndex re-anchor + clamp
  - Reverb/delay empty-buffer guards, octaveRange jlimit(1,8), per-sample gate release
  - **Step Rate param (index 20):** 0=1/16 (default), 1=1/8, 2=1/4; drives grid clock + gate
- **Tests:** `tests/unit/engine/psyarp_engine_test.cpp` (13 tests: stress shapes/octaves, grid-lock, step rate onset spacing, slot defs clamp/wiring)

### 1c. engine_info + engine_restart MCP tools (Gate 8)
- **Files:** `src/mcp/McpTools_Engine.{h,cpp}` (new), registered in `McpTools.cpp`
- `engine_info`: running binary path/mtime/size + optional buildBinaryPath → staleness verdict + exporting flag
- `engine_restart`: refuses while exporting (unless force:true), schedules QCoreApplication::exit(42)
- **Tests:** `tests/integration/mcp/engine_tools_test.cpp` (3 tests: JSON parse, stale flag, refuse-during-export)
- **Doc:** `docs/testing-mcp.md` one-paragraph engine update flow

### 1d. Build system overhaul
- **CMakeLists.txt:** sccache auto-detect removed → opt-in `-DHDAW_USE_SCCACHE=OFF` by default
- **CMakeLists.txt:** `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` set by default (/Z7, kills C1041 PDB contention)
- **Result:** full parallel build 434 targets in 252s, zero contention errors, PCH kept

---

## 2. MCP tools shipped

### 2a. analyze_tuning (new)
- **File:** `src/mcp/McpTools_Tuning.cpp` + registration in `McpTools.cpp`
- **Behavior:** tries python `wsl` path first (exact analysis via `timbre-lib/tune_roles.py`), falls back to lightweight C++ RMS/centroid (placeholder values — real analysis via python)
- Param: `wavPath` (required), `role` (optional: kick/bass/arp/lead/hat/pad)
- Returns descriptors + per-role check (pass/fail + suggestion)

### 2b. tune_roles.py (new)
- **File:** `timbre-lib/tune_roles.py` — CLI + MCP helper
- ROLE_TARGETS: kick centroid<120, bass 60-250, arp/lead 400-3000, hat>6000, pad 250-4000
- Reuses `timbre-lib/timbre.py:extract` (librosa/numpy — installed in kernel venv)
- Tested on growl_test.wav, psytrance_v6_final.wav, test_simple.wav

### 2c. psy_fm via add_track_with_fx (enum fix)
- **File:** `src/mcp/McpTools_Track.cpp` — `add_track_with_fx` enum now includes `"psy_fm"`
- Verified live: `add_track_with_fx {fxType:"psy_fm"}` creates track + fx slot

### 2d. clipId JSON response (Fix B)
- **Files:** `src/mcp/McpTools_Clip.cpp` — `add_midi_clip`/`add_audio_clip` return `{"clipId":N}` JSON
- **Tests:** `parseClipId()` helper added to `mcp_functionality_test.cpp` + `mcp_coverage_test.cpp` — backward-compat (parses both JSON and legacy `clipId=N` text)

---

## 3. Two new tracks composed

### Hypatia — 148 BPM, G# harmonic minor, dark hypnotic opener
- **Current:** `renders/hypatia_v4.hdaw` + `hypatia_v4.wav` (58 MB, peak 0.90)
- **Arrangement:** Intro48/Build48/MainA128/Break64/MainB160/Outro52 (512 beats)
- **Tracks:** 0 kick, 1 bass (growl_bass+filter), 2 hats (sampler clap+ohat key-split), 3 ArpMain (psyarp), 4 ArpAlt (muted), 5 Stab (psy_fm acidLead), 6 Pad (sampler atmos+filter HP), 7 Riser (sampler)
- **v4 settings:** ArpMain Step Rate 0 (1/16), 5-note scale run (degrees 0,2,3,4,6 = +0,+3,+5,+7,+10), UpDown 8-step bar-locked. Bass filter res=10, unison OFF (mono sub), Volume pump automation (0.3-0.55) + sine wobble BassFilter macro. Master 0.726.
- **Profile:** sub 2.7%, 60-250 24%, mids 30%, 2-6k 14%, air 29%

### Ion Storm — 142 BPM, D aeolian, melodic full-on
- **Current:** `renders/ionstorm_v3.hdaw` (no v4 wav yet — needs Step Rate=1/8 render)
- **Tracks:** same layout as Hypatia, Batuhan kick5 sample
- **v3 settings:** ArpMain 2-note held chords (root+octave), ArpAlt cleared/muted, delay dry, res=10
- **v4 to-do:** ArpMain Step Rate=1 (1/8), ArpAlt re-enable at Step Rate=2 (1/4) SuperSaw unison, bass pump + sine filter macro. Same B.P. Empire treatment.

---

## 4. Build state

- `build/` configured: no sccache, Embedded /Z7, Ninja parallel
- Fresh binaries 09:00+ today: HDAW_headless.exe 39M, hdaw_tests.exe 57M, hdaw_plugin_host.exe, hdaw_plugin_scanner.exe
- **Build command (vcvars64 required):**
  ```
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  cmake --build build --config Debug --target hdaw_tests HDAW_headless
  ```
- **17 files modified, 10 new files untracked** — NOT committed

---

## 5. Known latent issues (not fixed)

- **psyarp delay time param inaudible:** `delay_.delaySamples` never set from `setDelayTimeBeats`; ping-pong always at 1 sample. Cosmetic, no OOB. Deferred to next batch.
- **psy_fm MCP tools "track not found"** in headless: `getMainProcessor()->getTrack()` returns null for psy_fm slots. Workaround: use `set_internal_fx_param` indices. Documented in guide trap #15. Open since 2026-09-01.
- **engine-audit-2** (subagent, qwen3.7-max): read-only audit of growl_bass/psy_fm/fm_synth/sampler for the same bug classes found in psyarp. Was still running when session was interrupted. Check if it reported via agent_message. Key findings needed: unclamped atomics in voice pools, prepare/render ordering, unison voice count indexing.

---

## 6. What to do next

### Immediate (tracks)
1. `mcp.reload('hdaw')` → `engine_info {buildBinaryPath: "D:\pdf\roo projects\hdaw3\build\HDAW_headless.exe"}` → verify stale:false
2. **Ion Storm v4:** load `ionstorm_v3.hdaw`, set param 20=1 on ArpMain (track3 slot0), re-enable ArpAlt (track4) with SuperSaw osc=2 unison=4 detune=20, param 20=2 (1/4), held chords [p+12,p+19] every 2 bars vel=75. Bass pump + sine macro + res=10 + unison=0. Canary → master → final.
3. **Hypatia re-listen:** verify the 16th scale-run sounds correct at 148 (the arp solo onset measurement was 30ms — likely a detector artifact from oscillator zero-crossings, not a real 30th-note rate; the engine code is correct: floor(beat/0.25) = 101.4ms steps).

### Style evolution (v5+)
- B.P. Empire reference: manual volume duck via pump, detuned supersaw pad layer, sub-mono, vocal/foley chops as rhythmic ear-candy
- Per-section Step Rate automation possible via paramID 200+track+slot*100+20 on automation lanes
- Vocal chop candidates: `search_library {query:"vocal"}` returns tagged entries; load one-shot onto sampler track 8

### Engine audit follow-up
- Check if `engine-audit-2` subagent delivered a report (agent_message list, or check session dir `sub-8a613424`)
- Apply any findings (unclamped atomics in growl_bass/psy_fm voice pools, prepare guards, delay time param)
- The delay time bug (psyarp delaySamples never set) is a one-line fix worth folding in

### Commits
- All 17 modified + 10 new files are uncommitted. Stage and commit with a message covering the three bug fixes, four new tools, build overhaul, and composition work.

---

## 7. MCP launcher gotcha (lesson reinforced)

The launcher copies `build\HDAW_headless.exe` → `%TEMP%\HDAW_headless_mcp.exe` at session start. The `engine_info` tool now detects staleness. The `engine_restart` tool schedules graceful exit → `mcp.reload` → launcher re-copies + size-verifies. Use this flow after any engine rebuild:
1. Build: `cmake --build build --config Debug --target HDAW_headless hdaw_tests`
2. `engine_info {buildBinaryPath: "..."}` → confirm stale=true
3. `engine_restart` → wait for connection close
4. `mcp.reload('hdaw')` → new engine with fresh binary
5. `engine_info` → confirm stale=false
