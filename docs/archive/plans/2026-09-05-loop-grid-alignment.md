# Loop Grid Alignment (Onset-Based Loop Align) — Phase 1

Date: 2026-09-05
Status: Plan (approved scope: Phase 1 only, import auto + manual override, detected bar count)

## Goal

Make imported audio loops land accurately on the project beat grid by fitting a
beat grid from onset analysis (BPM + phase + integer bar count + leading/trailing
slack), then writing `sourceBpm` / `offset` / `duration` / `stretchMode` /
`stretchRatio` so the loop's downbeats align exactly with the project grid and no
silence/slack is left at the clip start or end. Phase 1 is **analysis + uniform
ratio** — no change to `StretchRenderer`, `ClipSourceProcessor`, playback, or export.

## Problem (current behavior)

1. Import estimates BPM via aubio global onset-tempo (`BpmDetector`) — unreliable
   for sparse/sustained melodic content (synth leads).
2. No phase/downbeat detection: a pure ratio stretch never finds *which* beat the
   loop starts on, so pickups/partial bars land off-grid.
3. No integer bar-length detection: `tempoMatchClip` sets
   `duration = sourceDuration × (srcBpm/projBpm)` regardless of whether the loop is
   really 1/2/4 bars — the loop end drifts off-grid.
4. Slack survives: the amplitude-threshold silence trim fails on reverb tails /
   quiet material, and the stretch renders the WHOLE source file (slack included,
   then scaled by the ratio).

## Success Gates

- [ ] G1: `cmake --build build --config Debug` succeeds (new files in source lists).
- [ ] G2: New gtests pass — `loop_analyzer_test` + `loop_align_test`:
      percussion 4-bar/120 BPM → bpm≈120, bars=4; sustained synth lead 2-bar/100 BPM
      → bpm≈100, bars=2; loop with leading silence → offset≈silence×ratio; loop with
      trailing reverb tail → trailing slack trimmed (loopSpan excludes tail);
      one-shot (1 onset) → ok=false.
- [ ] G3: No regression — affected existing suites pass: `Stretch*`, `*Clip*`,
      `RpcSurface*`, mcp coverage (`McpTools*`, `mcp_coverage_test`).
- [ ] G4: RPC tests — `project.alignClipToGrid` and `project.importAudioFile`
      return correct JSON and mutate the ValueTree (stretchMode/ratio/offset/duration).
- [ ] G5: MCP tests — `align_clip_to_grid` and `import_audio_file` tools registered
      and return the analysis summary.
- [ ] G6: Frontend — `cd frontend; npm test` passes (AudioClipEditor + ImportDialog
      updated); `npm run build` (tsc) succeeds.
- [ ] G7: No new anti-patterns (diff scan) and no pitfall-gate violations.
- [ ] G8: Knowledge graph refreshed (`graphify . --update` fast) after
      new files + new RPC methods (per AGENTS.md → Knowledge Graph (graphify)).

## Dependency Map

Blast radius (verified by grep/read, not assumed):

- `importAudioFile` (`src/engine/AudioImport.cpp:31`) — **has NO callers today**
  (dead path; the UI uses `project.addAudioClip`). Enhancing + wiring it is
  additive → zero blast radius on existing callers.
- `addAudioClip` (`AudioEngineCommands_Clips.cpp:12`) — **stays untouched** (used by
  ~30 sites incl. AddTrackMenu, timeline drop, pool placement, record, and many tests).
- `alignClipToGrid` — new command, no existing callers.
- ValueTree props written (`sourceBpm`, `offset`, `duration`, `stretchMode`,
  `stretchRatio`) — all have existing listeners + restore paths:
  - `AudioEngine.cpp:1085–1148` (offset/duration → SPSC/Place; stretchMode/stretchRatio
    → `rebuildRoutingGraph`).
  - `RoutingManager.cpp:607–654` (adopts cached stretched buffer by (clipID, ratio, sr)).
  - `ClipSourceProcessor.h` reads `offset`/`duration` in **stretched-time base** —
    `sourceSample = offsetSamples + clipLocalSample` (line 305–327). So the grid-aligned
    `offset` must be `downbeatOffset_source × ratio`.
- Upstream: `ProjectCommands.h` interface (`alignClipToGrid`), Router_Project, MCP tools,
  frontend (`AudioClipEditor`, `ImportDialog`, `FileBrowser`).
- Downstream consumers: ReadModel (already carries sourceBpm/stretchRatio),
  frontend snapshot, playback via StretchCache.
- Projections: ReadModel + audio graph (existing seams, no new ones).
- SPSC paths: none new (stretch is resolved at graph-build, not RT-parametric —
  comment at `ProjectCommands.h:107–109`).

## Pitfall Gates

| Gate | Triggered? | How addressed |
|---|---|---|
| 1/6/10 Rebuild state restore | No | No new processor state; only existing ValueTree props with existing restore paths. |
| 2 Unimplemented path | Yes | Full chain traced: RPC → command → ValueTree → listener (`AudioEngine.cpp:1085`) → RoutingManager (`RoutingManager.cpp:631`) → processor. |
| 3 Audio-thread safety | No | Analyzer + commands run on command/RPC thread only. |
| 4 Stale binaries | Yes | New `.cpp` added to `CMakeLists.txt` + `tests/CMakeLists.txt` (explicit lists); verify binary after build. |
| 5 Frontend closures | No | Button/import handlers are simple RPC calls; no post-await prop reads added. |
| 8 CSS tokens | No | Reuse existing `.ace-btn` / `.ace-row` classes; no new colors. |
| 9 ID/validation | Yes | Guard null reader/formatManager; no new ID allocators. |
| 11 Message pump | No | No new entry points; analyzer runs within existing pumped processes. |
| 12 Graph mutation off message thread | No | No new graph mutations (rebuild triggered via existing listener path). |
| 13/14/15/16 | No | No DSP writes, no cross-process, no transport sequencing, no plugin lifecycle. |

Anti-patterns to avoid: repeated `rebuildRoutingGraph` per property — order property
writes so `offset`/`duration`/`sourceBpm` come BEFORE `stretchMode`/`stretchRatio`
(the stretch rebuilds then pick up final placement; matches existing `tempoMatchClip`
precedent of ≤2 rebuilds per command).

## Design

### 1. NEW `src/engine/LoopAnalyzer.{h,cpp}`

```cpp
namespace HDAW {
struct LoopAnalysis {
    bool ok = false;
    double bpm = 0.0;                 // detected source tempo
    double confidence = 0.0;          // 0..1 alignment score
    double beatInterval = 0.0;        // seconds/beat at source tempo
    double downbeatOffset = 0.0;      // source seconds from file start to grid origin (beat 0)
    int bars = 0;                     // detected integer bar count (1,2,4,8)
    int beatsPerBar = 4;              // default 4/4
    double loopSpanSourceSeconds = 0.0; // source seconds [gridOrigin, gridOrigin + bars*beatsPerBar*beatInterval]
    double leadingSlack = 0.0;        // source seconds file start → first onset
    double trailingSlack = 0.0;       // source seconds last onset → loop end
    std::vector<double> onsetTimes;   // source seconds
};
class LoopAnalyzer {
public:
    static LoopAnalysis analyze(const juce::String& sourceFile,
                                juce::AudioFormatManager& fm,
                                double maxSeconds = 60.0, double sensitivity = 0.5);
    static LoopAnalysis analyzeBuffer(const juce::AudioBuffer<float>& mono,
                                      double sampleRate, double sensitivity = 0.5);
};
}
```

Algorithm (in `analyzeBuffer`):
1. Spectral-flux onset curve (Hann 1024 / hop 256, positive difference), adaptive
   threshold + local peak pick (min spacing ~40 ms) → `onsetTimes` + strengths.
2. Beat interval via flux autocorrelation + comb refinement over BPM 60–200; keep
   the BPM maximizing onset alignment (also try T/2, 2T — pick best alignment).
3. Phase search φ ∈ [0,T): score = Σ onset-strength near φ + k·T (tol 0.12·T).
4. Downbeat refinement: of φ+{0,1,2,3}·T pick the one maximizing bar-level onset
   strength (bar starts weighted).
5. Bars: for b ∈ {1,2,4,8} with end E = φ + b·4·T, pick the b with smallest
   endResidual = E − lastOnset when residual ≤ 0.25·T (tie → larger b).
6. `leadingSlack = firstOnset − downbeatOffset`, `trailingSlack = E − lastOnset`,
   `loopSpan = b·4·T`. If < 2 onsets or confidence < 0.3 → `ok=false`.

### 2. `alignClipToGrid` command

- `src/common/ProjectCommands.h`: add `struct AlignGridResult { bool ok; double bpm,
  confidence; int bars; int beatsPerBar; double ratio; double offset; double duration;
  std::string error; }` + `virtual AlignGridResult alignClipToGrid(int clipId) = 0;`
  (place with the timestretch group, ~line 121).
- `src/engine/AudioEngineCommands.h` + implementation in
  `src/engine/AudioEngineCommands_Timestretch.cpp`:
  1. `beginTransaction("Align clip to grid")`.
  2. Find clip + sourceFile; run `LoopAnalyzer` (sync, bounded). If `!ok` →
     `endTransaction()` + return `{ok:false, error}`.
  3. `targetDuration = bars*beatsPerBar*(60/projectBpm)`;
     `ratio = clamp(targetDuration / loopSpan, 0.25, 4.0)`;
     `duration = loopSpan * ratio` (post-clamp); `offset = downbeatOffset * ratio`.
  4. Write props in order: `sourceBpm`, `offset`, `duration`, `stretchMode=2`
     (ManualRatio — ratio is grid-derived, not pure BPM), `stretchRatio`.
  5. `endTransaction()`; return summary.

### 3. Enhanced `importAudioFile` (`src/engine/AudioImport.{h,cpp}`)

- Signature → `int importAudioFile(AudioEngine&, const QString& path, int trackIdx,
  double startTimeSec = -1.0, bool alignToGrid = true)` (returns clipId, −1 fail;
  `startTimeSec < 0` = append after last clip, today's behavior).
- After clip creation: if `alignToGrid` && file readable → run `LoopAnalyzer`; on
  `ok` write grid-aligned props (same math as the command) and skip the explicit
  trailing `rebuildRoutingGraph()` (the stretchMode/stretchRatio listener rebuilds
  once; stretchRatio written last adopts the buffer). On `!ok` → keep today's
  silence-trim fallback (`detectSilenceBounds` + offset) and the explicit rebuild.
- Metadata BPM (if present) seeds the analyzer's beat interval but phase/bars/slack
  always come from onset analysis.

### 4. RPC (`src/frontend/router/Router_Project.cpp`)

- `project.importAudioFile` → `{ path, trackIndex, start(beats), alignToGrid? }`
  → serializes `AlignGridResult`.
- `project.alignClipToGrid` → `{ clipId }` → serializes `AlignGridResult`.

### 5. MCP parity

- `McpTools_Clip.cpp`: `align_clip_to_grid` (`{clipId}` → result text).
- `McpTools_Audio.cpp`: `import_audio_file` (`{path, trackIndex?, startBeat?,
  alignToGrid?}` → clipId + analysis summary).

### 6. Frontend

- `AudioClipEditor.tsx` (Timestretch section): "Detect & Align to Grid" button →
  `project.alignClipToGrid {clipId}`; render result (bars × BPM) or error.
- `ImportDialog.tsx` (audio mode) + `FileBrowser.tsx` (file double-click, audio):
  call `project.importAudioFile` instead of `project.addAudioClip`.
- `AddTrackMenu` / timeline drop / pool placement keep `addAudioClip` (one-shots /
  generic placement must NOT auto-align).

## Steps

1. Task A (C++ core): `LoopAnalyzer` + `AlignGridResult`/`alignClipToGrid` +
   `importAudioFile` enhancement + CMakeLists updates + gtests
   (`loop_analyzer_test.cpp`, `loop_align_test.cpp`).
2. Task B (RPC/MCP): Router_Project handlers + MCP tools + RPC/MCP tests.
3. Task C (frontend): AudioClipEditor button + ImportDialog/FileBrowser wiring +
   Vitest updates.

## Verification Commands

- `cmake --build build --config Debug`
- `build/Debug/hdaw_tests.exe --gtest_filter=LoopAnalyzer.*:LoopAlign.*:Stretch*:*Clip*:RpcSurface*:McpTools*`
- `build/Debug/hdaw_tests.exe --gtest_filter=mcp_coverage_test.*` (or affected MCP suite)
- `cd frontend; npm test`
- `cd frontend; npm run build`
- Full `hdaw_tests.exe` before delivery (or `run_fast_tests.bat` + affected suites
  for the fast tier, full suite before delivery).