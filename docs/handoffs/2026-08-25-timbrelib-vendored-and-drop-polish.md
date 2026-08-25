# Handoff: TimbreLib vendored into hdaw3 + in-flight drop polish

Date: 2026-08-25 (late session; worker interrupted mid-work — state below was
recovered from transcript + verified where marked)
Focus: (1) DONE — vendored `timbre-lib` into the hdaw3 repo, committed, pushed.
(2) IN FLIGHT — "polish the drop" on sampler_demo_long.hdaw3 (HANDOFF.md step 1).

## 0. TL;DR for the next context

- `timbre-lib` now lives at `D:\pdf\roo projects\hdaw3\timbre-lib` (WINDOWS side
  truth). Sources committed; heavy/generated data gitignored (GGUF 1.9GB, WAVs,
  samples/, serum2 generated dirs, caches).
- **WSL cannot see `hdaw3\timbre-lib`** (poisoned 9p dentry for that exact name).
  Use `powershell.exe`/`git.exe` for EVERYTHING under that folder. WSL restart
  clears the poison. Do NOT `mv/mkdir/stat` that path from WSL.
- Engine (hdaw MCP) was resolved and re-loaded the demo; connection may have
  dropped again with the worker interruption — reconnect with `mcp.reload("hdaw")`.
- Repo pushed clean: local == origin/main. See §3 for the odd-but-harmless state
  (stash, WIP files) — nothing is broken.

## 1. Repo state (verified via git.exe 2026-08-25)

- HEAD: `c915802` "docs: update TimbreLib handoff paths for vendored location"
  (parent `d866798` "feat: vendor TimbreLib sidecar analysis toolchain (timbre-lib/)").
  Both PUSHED: `local HEAD == origin/main == c915802fbd6aa…`.
- Git identity (local, set this session): Clint Anderson <clinta@gmail.com>
  (`.git/config` [user] section; global config also fine).
- KNOWN UNCOMMITTED (do NOT commit without user say-so):
  - 20 modified engine files = the native sidecar-integration WIP
    (src/engine/FileLibraryManager.*, McpTools_Library.cpp, Router_Library.cpp,
    ExportManager.*, Track.cpp, MainAudioProcessor.cpp, tests file_library_test,
    mcp_coverage_test, etc.). This implements `applyTimbreSidecar` — tags/
    description from `<file>.timbre.json` into LibraryEntry + search.
  - 27 untracked: clap-libs/, renders/, src/mcp/McpTools_*.cpp split files
    (24), docs/plans/timbre-lib-sidecar-integration.md, others.
  - stash `stash@{0}: On main: batchA-mine` — a LARGE prior WIP (~300 files,
    95k insertions) was stashed under this name at some point (NOT by this
    session; created by user/other context). Do not drop; do not assume content.
- `timbre-lib/` in git: 20 files (11 root text + 9 serum2/*.py). HANDOFF.md,
  README.md, lib_search.py paths updated to the new location.

## 2. TimbreLib layout (post-move)

- Root: timbre.py, lib_analyze.py, lib_search.py, llm_stage.py, clap_stage.py,
  analyze.sh, search.sh, README.md, HANDOFF.md, audioset_labels.txt,
  .gitignore, Qwen2.5-3B-Instruct-Q4_K_M.gguf (1.9GB, ignored), samples/ (36
  items incl 17 analyzed WAVs + sidecars), serum2/ (9 py sources tracked;
  harvest_out/ + test_projects/ + wavs ignored), auto-backups/, __pycache__/,
  demo .hdaw3 files (ignored).
- `.gitignore` rules (timbre-lib/.gitignore): *.gguf, *.wav/*.mp3/*.flac/etc,
  *.serumpreset/*.fxp/*.syx, *.hdaw3, *.hdaw3.bak, __pycache__/, *.pyc,
  auto-backups/, .timbre_cache/, samples/timbre_index.json, samples/, 
  serum2/harvest_out/, serum2/test_projects/.
- Demos (working files, NOT in git): sampler_demo_long.hdaw3 (main deliverable,
  66s/32-bar, 10 sampler tracks), sampler_demo.hdaw3 + .bak (8s 4-bar base).
  ALL THREE had their `sampleFile="D:\pdf\roo projects\timbre-lib\samples\..."`
  absolute paths REWRITTEN to the hdaw3 location (6 refs each; verified 0 old
  refs remain). Load verified: all 10 samplers `hasSound:true` at new paths.

## 3. Engine / MCP facts (verified this session)

- MCP server spawns `C:\Users\hapbt\AppData\Local\Temp\HDAW_headless_mcp.exe --mcp-stdio`
  per session. If `mcp.call_tool("hdaw", ...)` returns "Connection closed":
  `await mcp.reload("hdaw")` → 187 tools. Then re-`load_project` (all state in
  the engine resets on respawn).
- The engine (respawned AFTER the registry edit) reads the UPDATED library
  registry: `%APPDATA%\HDAW\libraries\registry.json` (same dir as `Roaming\hdaw`
  — NTFS case-insensitive, ONE file):
  - TimbreLib id `f2538111f7cd`, path `D:\pdf\roo projects\hdaw3\timbre-lib\samples`,
    type audio, autoScan false, fileCount 17.
  - Per-file entry `libraries\f2538111f7cd.json` was DELETED (its paths were
    old) — the engine regenerates it on the next successful `scan_library`.
  - **TODO/verify for next session:** `scan_library {id:"f2538111f7cd"}` — note
    the param is `id`, NOT `libraryId` (libraryId errored "unknown property").
    Then `search_library {libraryId:"f2538111f7cd", query:"dark gritty"}` to
    confirm tags survive (sidecars are at the new path; they regenerate tags).
- Current engine state (at interruption): `load_project` of
  `D:\pdf\roo projects\hdaw3\timbre-lib\sampler_demo_long.hdaw3` SUCCEEDED;
  13 tracks / 34 clips; activeVoices>0 (transport may have been playing).
  Track/volume map: t1 Track1 1.0, t2 Synth 0.85, t3 Vocals 0.9, t3 Kick 1.0,
  t4 Hat 0.55, t5 PluckStab 0.7, t6 Bass 0.8, t7 Pad 0.6, t8 Bells 0.6,
  t9 TexA 0.5, t10 TexB 0.55, t11 TexC 0.5, t12 TexD 0.45.

## 4. In-flight: "polish the drop" (HANDOFF §8 item 1)

Reference (timbre-lib/HANDOFF.md §6): Drop = beats 96–128. Sections: Intro 0–32,
Groove In 32–64, Build 64–96 (arrangement seed 101), Drop 96–128 (seed 202).
Symptom (§7 #1): build/drop render quieter (mean ~0.24–0.37) than groove
(~0.42–0.49); peaks are the loudest. Plan: level pass (section gains to ~1.8),
density pass (extra hat/kick hits in drop), fresh-path 24-bit export, envelope
verify.

Collected inspection data (reuse, don't redo):
- Automation lanes (Volume always exists; `enabled` + pointCount):
  t3,t4,t5,t8,t11,t12 Volume = 5 pts; t6 (Bass) Volume = 192 pts (sidechain
  pump, per-beat 0.45/0.95 → scaled 1.0/1.3/1.5 by section); t7 (Pad) Volume
  8 pts + Transpose(104) 33 pts ENABLED (LFO); all Pan/Mute disabled.
- Clips (start, dur): t3 Kick id13@32/16, id14@48/16, id53@64/32 (generative
  drop); t4 Hat id2@0/16, id8@16/16, id15@32/16, … (6 clips); t6 Bass
  id19@32/16, id20@48/16, id56@64/32.
- Volume lane semantics: enabled lanes drive; gains >1.0 stored literally.
  set_automation_points {time,…} in BEATS (stored as seconds in XML).
- Export: `export_audio {outputPath}` (bitDepth 24, format wav); IGNORES
  trackIds (always full project); same-path re-export returns STALE file —
  always use a fresh output filename. Renders are 24-bit PCM: parse with the
  int24 loader below (int32 parse gives garbage). Exports must be serialized
  ("export already in progress" otherwise).
- workaround for "quieter drop": raise drop-section Volume gains (1.55→~1.8)
  on groove tracks for beats 96–128; density via generate_rhythm_pattern on
  Hat track at start=96 (no seed param) or duplicate Kick/Hat generative clips.
- Save the polished project to a NEW file (e.g.
  sampler_demo_long_v2.hdaw3); keep originals pristine.

Helpers (recreate in-kernel; they were in the interrupted kernel):
```
async def call(name, args=None):
    return await mcp.call_tool("hdaw", name, args or {})
def parse_result(r):   # MCP returns JSON strings OR "k=v" strings
    if isinstance(r,(dict,list)): return r
    s=str(r).strip()
    if s.startswith(('{"','[{','[')):
        try: return json.loads(s)
        except Exception: pass
    if '=' in s:
        d={}
        for p in s.split(','):
            if '=' in p:
                k,_,v=p.partition('='); d[k.strip()]=v.strip()
        return d
    return s
def load24(path):      # 24-bit WAV -> (sr, mono downmix float list)
    import wave
    with wave.open(path,'rb') as w:
        sr,nch,sw,n=w.getframerate(),w.getnchannels(),w.getsampwidth(),w.getnframes()
        raw=w.readframes(n)
    assert sw==3; n=len(raw)//3; out=[]
    for i in range(n):
        b=raw[i*3:i*3+3]; v=b[0]|(b[1]<<8)|(b[2]<<16)
        if v&0x800000: v-=1<<24
        out.append(v/8388608.0)
    return sr,[sum(out[c::nch])/nch for c in range(nch)]
def env24(path, win_s=2.0):   # RMS envelope per win_s seconds
    sr,x=load24(path); step=int(sr*win_s); e=[]
    for i in range(0,len(x)-step+1,step):
        seg=x[i:i+step]; e.append(math.sqrt(sum(v*v for v in seg)/len(seg)))
    return sr,e
async def export_full(out_path, start=None, end=None):  # fresh path + stable size
    import os, time
    if os.path.exists(out_path): os.remove(out_path)
    args={"outputPath":out_path,"bitDepth":24,"format":"wav"}
    if start is not None: args["start"]=start
    if end is not None: args["end"]=end
    await call("export_audio", args)
    last=-1; stable=0; t0=time.time()
    while time.time()-t0<240:
        try: sz=os.path.getsize(out_path)
        except FileNotFoundError: sz=-1
        if sz==last and sz>0 and stable>=4: return sz
        if sz!=last: stable=0
        last=sz; stable+=1; await asyncio.sleep(1)
    raise TimeoutError("export never stabilized")
```

## 5. Environment gotchas (learned the hard way — READ BEFORE TOUCHING)

1. **9p poisoned dentry for `hdaw3\timbre-lib` (ACTIVE)**: parent `ls` shows
   `d????????? timbre-lib`; stat=ENOENT; mkdir=EEXIST. Cause: cross-tool
   renames of that folder this session. Windows side is truthful. ANY file op
   under that folder: powershell.exe / git.exe only. WSL restart clears it.
2. **cmd/powershell.exe tool-output truncation**: a `%%bash` cell that invokes
   powershell/cmd often returns truncated/empty output (conhost quirk). Pattern
   that works: write a `.ps1` to `C:\Users\hapbt\AppData\Local\Temp\`, run
   `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<ps1>"` alone in a
   cell writing results to a Temp marker txt, then `cat` the marker in the NEXT
   cell. Never chain powershell output into the cell's own stdout.
3. **Orphaned engines block file ops / steal names** (AGENTS.md lesson 20): a
   stale `HDAW_headless_mcp.exe` held a handle on timbre-lib and blocked the
   `mv` (EACCES) — killed it, move succeeded. Before git/proxy/file work:
   check `Get-Process HDAW*`; kill orphans.
4. **MCP library tool params**: scan_library → `id`; search_library →
   `libraryId`. engine summary "name=New Project" even after load (harmless).
5. **The registry**: `%APPDATA%\HDAW\libraries\registry.json` (== `Roaming\hdaw`,
   same dir). Engine loaded it fresh on respawn — do registry edits only while
   the engine is stopped/orphaned, else the engine's in-memory copy wins.
6. **git.exe vs WSL git on this repo**: use `git.exe -C "D:\pdf\roo projects\hdaw3" ...`
   for any command that might enumerate the tree (status/add of timbre-lib);
   WSL git chokes on the poisoned subtree. Local git identity is set.
7. Kernel venv `/home/hapbt/.prime/agent/kernel-venv/bin/python` has the ML
   stack (torch/transformers/llama-cpp/librosa) — `TIMBRE_PY` default per
   timbre-lib README (for when BPM/key detection step is picked up).

## 6. Suggested next actions (in order)

1. (verify) `scan_library {id:"f2538111f7cd"}` → regenerates per-file index at
   the new path (tags from sidecars); confirm `search_library` returns tags.
2. Finish the drop polish (see §4): baseline export → env24 → level+density
   edits → fresh export → verify drop mean ≈ groove mean → save v2 project.
3. Decide on the 20-file sidecar WIP + stash "batchA-mine" (user's call;
   likely combine into one feature commit when verified).
4. Optional: BPM/key detection in timbre.py (librosa tempo + chroma) so
   sidecars/index carry bpm/key (currently they don't).
5. WSL: `wsl --shutdown` (or reboot) eventually clears the poisoned dentry.

## 7. What happened this session (timeline, for context)

- Moved `D:\pdf\roo projects\timbre-lib` → `hdaw3\timbre-lib` (mv blocked by
  orphaned engine → killed → success). Windows-side Move-Item retry + rename
  dance poisoned the 9p dentry.
- Wrote timbre-lib/.gitignore; updated lib_search.py/README paths.
- Fixed registry path + deleted stale per-file entry; verified via list_libraries.
- Commit d866798 (vendor, 20 files) + c915802 (handoff path fix) pushed.
- Rewrote demo .hdaw3 sampleFile paths (3 files × 6 refs).
- Respawned hdaw MCP (`mcp.reload`), loaded sampler_demo_long.hdaw3 (13/34,
  all samplers sound). Collected lane/clip inspection (above).
- Interrupted mid drop-polish inspection (worker interruption; this doc).

## 8. UPDATE (2026-08-25, later session) — WIP landed, state resolved

- The 20-file sidecar-integration WIP is COMMITTED and PUSHED as TWO commits
  (user-approved): `88eaf07` refactor: split monolithic McpTools source into
  per-domain files (26 files: 3 modified + 22 new + CMakeLists) and `fde2a38`
  feat: TimbreLib sidecar ingestion, offline-render exclusion, plugin state
  fidelity (18 files, incl. FileLibraryManager sidecar ingest, ExportManager
  cancelAndJoin, PluginHost lastSetState re-apply, sidecar tests, plan doc).
- Verification evidence (plan gates G1-G4): build-fast.bat test green; fresh
  RelWithDebInfo hdaw_tests.exe (binaries newer than all WIP sources);
  FileLibraryTest 25/25 incl. 5 new sidecar tests; McpCoverageTest 40/40 incl.
  LibrarySidecarSearchAndGetEntry; full-suite rerun of every originally-failed
  family green except documented env cases.
- Full-suite gaps found (PRE-EXISTING, proven vs pre-WIP Debug baseline):
  (a) RelWithDebInfo full suite needs hdaw_plugin_host.exe +
      hdaw_plugin_scanner.exe — `build-fast.bat test` builds ONLY hdaw_tests;
      build them with `cmake --build build --config RelWithDebInfo --target
      hdaw_plugin_host hdaw_plugin_scanner`.
  (b) RealtimeSafety.* is Debug-only BY DESIGN (`#if JUCE_DEBUG` around all of
      BufferCheck) — fails in RelWithDebInfo regardless of code state.
  (c) McpServer.DiagnosticClapExportMatrix = known-flaky (real installed CLAPs,
      silent/cancelled exports in this env); AutoGain.TooLoudTargetClampsAtUnity
      + GlobalScale.NonClippingMixUntouched fail on the pre-WIP baseline too
      ("failed to read render").
- stash@{0} "batchA-mine" = only 6 frontend files (131 insertions), NOT the
  ~300-file/95k-insertion WIP the earlier note claimed. Untouched.
- Still untracked (intentionally excluded): clap-libs/ (zero refs), renders/,
  test.zip (96 MB WAV zip), docs/handoffs/2026-08-24-dnb-crash-generator-bugs-backlog.md.
