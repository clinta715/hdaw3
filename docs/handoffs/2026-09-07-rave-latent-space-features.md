# Handoff: RAVE Latent-Space Features (morph / generate / probe)

**Date:** 2026-09-07
**Status:** ⛔ DEPRECATED — RAVE is deprecated as of v0.32.0 (real-model mix results were overdriven/incoherent; native synths won). Do NOT implement this roadmap; kept as historical context for the committed foundation (RAVE #1–#5).
**Branch:** `feat/fx-presets-saturator`
**Commits (foundation, all green):**
- `ac6c645` feat(rave): offline RAVE integration — sidecar, async jobs, RPC/MCP, Neural UI, settings
- `dee060e` fix(frontend): AudioClipEditor hook-order violation (React #300 on clip delete)
- `ed8ea31` test(e2e): modernize stale specs — suite 261/261
- `d748c84` fix(packaging): flat-Ninja awareness in build.bat + electron-builder extraResources
- `e812c03` fix(e2e): playwright webServer → flat `build\HDAW.exe`

## TL;DR

RAVE is integrated as an **offline sidecar toolchain** (never realtime): Python
sidecar (`tools/rave/rave_transform.py`) ↔ `HDAW::RaveService` /
`HDAW::RaveJobManager` (async jobs, cancel, notifications) ↔ `rave.*` JSON-RPC
+ MCP tools ↔ Neural bottom-panel tab + Preferences section. Real-model
inference is **proven** against `vintage.ts` (RAVE v2, 481 MB, CPU torch 2.14,
Python 3.14) in dev AND packaged layouts. The next step is latent-space work:
**model probing, clip morphing, generate-from-seed** — all pure sidecar +
contract extensions; no engine-audio changes needed.

## 1. Current architecture (exact contracts — extend, don't reinvent)

### Sidecar CLI (`tools/rave/rave_transform.py`)
```
python rave_transform.py --input X.wav --model M.ts --output Z.wav --temperature T --seed S [--allow-fallback]
                         [--make-test-tone]
```
- Modes: `REAL-MODEL` (torch + valid TorchScript) or `FALLBACK-DSP`
  (deterministic stdlib waveshape; temperature=drive, seed=variation).
- Hard-fail (exit≠0, no output) when torch present but model unloadable —
  never silently fake. Output clipped [-1,1], NaN/inf/silence rejected
  post-write (output deleted on failure).
- `try_real_inference` feeds `[1, 1, T]` float32 (`reshape(1,1,-1)`) — RAVE v2
  combined exports expect `[N, C, T]`; this was THE bug found in the
  real-model session (PQMF conv blew up on `[1,T]`).
- Temperature honesty: RAVE v2 `forward` is deterministic reconstruction;
  temperature currently affects only fallback DSP + `torch.manual_seed`.
  Latent-space work is where temperature can become REAL (noise scaling on
  the latent, see §2).

### C++ engine
- `src/engine/RaveService.{h,cpp}` — model discovery (`.ts/.pt/.pth/.rave/.onnx`),
  `transformFile` (blocking QProcess; sync RPC path), **shared resolvers**
  `resolveScriptPath` / `resolvePythonPath` / timeout:
  `request > QSettings > env (HDAW_RAVE_*) > dev cwd fallback (<cwd>/tools/rave/rave_transform.py) > packaged fallback (<exe_dir>/../rave/rave_transform.py) > 'python'`.
- `src/engine/RaveJobManager.{h,cpp}` — async jobs: `startJob` (fail-fast
  validation, then worker thread), `jobStatus`, `cancelJob` (atomic flag +
  `kill()` under job lock; QProcess lives entirely on its worker thread),
  bounded `shutdown()`. States: `running|finished|failed|cancelled` — NO fake
  percent progress. Terminal results retained (last 64). **Worker invariant:
  never touches ValueTree/ReadModel/commands/processors** — project mutation
  is message-thread-only via the existing sync `rave.importResult`.
- `AudioEngine` exposes `getRaveService()` / `getRaveJobManager()`.

### RPC surface (`src/frontend/router/Router_Rave.cpp`, namespace `rave`)
- `rave.listModels {directory?}` → `{models:[{name,path,extension,sizeBytes}]}`
  (default dirs: QSettings `rave/modelDirs` + `<cwd>/rave/` + `<cwd>/rave/models/` + legacy `%APPDATA%/ACIDS/RAVE`)
- `rave.transformFile` / `rave.transformClip` — SYNC (dev/agent use; 30 s
  RpcClient timeout makes them unsuitable for real renders from the UI)
- `rave.startTransform {inputPath, modelPath, outputPath, pythonPath?, scriptPath?, temperature?, seed?}` → `{jobId}`
- `rave.jobStatus {jobId}` / `rave.cancelJob {jobId}`
- `rave.importResult {outputPath, trackIndex?, startBeats?, alignToGrid?, noImport?, samplerTrackIndex?, samplerSlotIndex?, samplerRootNote?}`
  → `{clipId, samplerOk, samplerError}` — **startBeats is BEATS** (passed
  straight to `ProjectCommands::importAudioFile`, which converts; never
  convert in the router). Reuses `HDAW::importAudioFile` + `setSamplerSample`.
- `settings.getRaveConfig` / `settings.setRaveConfig` (partial updates;
  `timeoutMs > 0`) in `Router_Project.cpp::dispatchSettings`; QSettings keys
  `rave/modelDirs|defaultModel|pythonPath|scriptPath|timeoutMs`
  (`src/common/SettingsKeys.h`).
- Notification: `notify.raveProgress {jobId, state, message}` via
  `FrontendServer::broadcastNotificationFromAnyThread` (verified live on WS).

### MCP parity (`src/mcp/McpTools_Rave.cpp`)
`rave_list_models`, `rave_transform_file`, `rave_transform_clip`,
`rave_import_result`, `rave_start_transform`, `rave_job_status`,
`rave_cancel_job`, `rave_get_config`, `rave_set_config`.
**Every new RPC needs a matching MCP tool in the same change** (AGENTS rule).

### Frontend
- `frontend/src/components/NeuralPanel.tsx` (+`.css`) — bottom-panel `neural`
  tab: models dropdown (preselects `rave/defaultModel`), temp 0–2, seed,
  input/output paths, Start → `startTransform`, state via `notify.raveProgress`
  (jobId-ref matched, unsubscribed on unmount), Cancel, explicit Import on
  finish (trackIndex/startBeats/alignToGrid=false default, opt-in sampler send).
- `frontend/src/store/neuralStore.ts` — `pendingClip` handoff; clip context
  menu “Render with RAVE…” (audio clips with sourceFile only, `TimelineContextMenu.tsx`).
- Preferences RAVE section (`PreferencesDialog.tsx`): model dirs add/remove,
  default model, python/script paths, timeout.

### Tests (all green at commit time)
- gtest 22: `tests/unit/engine/rave_service_test.cpp` (6),
  `rave_settings_test.cpp` (10, QSettings/cwd/env guarded + restored),
  `tests/unit/frontend/rave_rpc_test.cpp` (6 incl. async fail-fast/cancel).
- Vitest 12: `__tests__/NeuralPanel.test.tsx` (9), `PreferencesDialog.rave.test.tsx`.
- E2E: `e2e/neural-panel.spec.ts` (2) inside full suite 261/261.
- Real-model proofs: `compositions/rave_real_vintage.wav`,
  `rave_engine_e2e.wav`, `rave_packaged_probe.wav` — all 90,112 frames
  @ 44.1 kHz, peak 0.775 from the 2 s test tone (identical metrics across
  direct/dev-engine/packaged-engine runs).

## 2. Latent-space foothold (what we KNOW about vintage.ts)

The failed `[1,T]` run leaked the model's TorchScript structure (traceback in
session history):
```
__torch__.Combined.forward(x) = self._rave(x)
  @torch.jit.export def decode(self: Combined, x)      ← exported on Combined
  TraceModel.forward(x) = self.decode(self.encode(x))   ← encode/decode on inner model
  encode: resample.from_target_sampling_rate → pqmf.forward → encoder → (mean, scale)
```
So the export pipeline (`combine_models.py` / `export_rave.py` in ACIDS RAVE)
traced `encode`/`decode` — **latent manipulation is mechanically possible**
via TorchScript method calls, subject to what's actually exported on the
`Combined` wrapper. `encode` returns `(mean, scale)` inside TraceModel;
the sampling step (`mean + scale * noise * temperature`) is where REAL
temperature enters.

**FIRST TASK — model probe (before any feature work):**
Introspect `vintage.ts`: `m = torch.jit.load(...); print([a for a in dir(m) if not a.startswith('_')])`,
then try `m.encode(torch.zeros(1,1,44100))` / `m.decode(z)` and record shapes,
latent dim, model sample rate (RAVE v2 zoos are 44.1k or 48k — verify, don't
assume; `resample.from_target_sampling_rate` implies internal SR conversion
exists). Capture findings in `tools/rave/README.md`. Everything below depends
on these answers; if `encode`/`decode` are NOT callable on `Combined`, the
fallback is `_rave` attribute access (`m._rave.encode`) — verify which.

## 3. Proposed slices (in order)

### Slice A — `--mode probe` + model metadata
- Sidecar: `--mode probe --model M` prints JSON `{ok, sampleRate?, latentDim?, methods:[...], error?}` (no audio).
- `RaveService::probeModel(path)` (sync, fast; cache results in QSettings or a JSON sidecar next to nothing — prefer in-memory cache keyed by path+mtime).
- RPC `rave.probeModel` + MCP `rave_probe_model`; `listModels` MAY gain cached metadata fields.
- UI: model dropdown shows SR/latent-dim badges; NeuralPanel gates morph/generate controls on probe success.
- Tests: sidecar unit-run vs fake fixture (clean error), C++ validation tests, RPC test.

### Slice B — `--mode morph` (two-clip latent interpolation)
- Sidecar: `--mode morph --input A.wav --input2 B.wav --model M --output Z [--steps N] [--curve linear|ease] --temperature T --seed S`.
  Encode A and B (equal-length windows), interpolate latents z(t) = (1-α)zA + αzB across the render, decode in chunks; temperature scales re-noising if the export exposes the sampling step (else deterministic decode — document honestly, same rule as §1).
- Without torch: FALLBACK-DSP crossfade+waveshape stand-in, clearly labeled.
- Engine: extend `RaveTransformRequest` with `mode` + `input2Path` (+ optional `steps`/`curve`); ONE job surface (`startTransform` gains `mode`/`input2Path`; keep back-compat: absent mode = transform). RaveJobManager argv builder passes them through — keep the two argv builders (service + job manager) in sync via the shared helpers.
- RPC: `rave.startTransform` accepts new fields; convenience `rave.morphClips {clipIdA, clipIdB, ...}` resolving both sourceFiles server-side (mirror `transformClip`'s validation: reject MIDI/empty-source BEFORE spawning).
- MCP: `rave_morph_clips` (+ mode field on `rave_start_transform`).
- UI: NeuralPanel mode selector Transform|Morph; Morph takes clip B from a second pending-clip handoff (extend `neuralStore` with `pendingClipB`; context-menu “Set as RAVE morph target”).
- Tests: sidecar fallback morph deterministic per seed; C++ validation (missing input2 → fail fast); RPC morphClips unknown-clip error; Vitest mode switching; E2E optional.

### Slice C — `--mode generate` (from-seed renders)
- Sidecar: `--mode generate --model M --output Z --seconds S --temperature T --seed S [--motion drift|walk|static]`: sample latent trajectory from prior (randn seeded), decode. Duration clamped (e.g. 0.25–120 s).
- Engine/RPC/MCP/UI: same extension pattern as Slice B (`mode`, `seconds`, `motion`); Generate needs NO input file — `inputPath` becomes optional per mode (validate per-mode, keep transform/morph requiring it).
- Musical tie-in (AGENTS generative pillar): generated one-shots are natural `sampler_set_sample` / `importResult` targets; consider a “generate N variations” batch later (one job per variation, existing job manager handles concurrency).

### Slice D (optional) — latent presets / trajectories
Persist named latent trajectories or morph curves as JSON in the FileLibrary/pattern-library style; only after B/C prove the UX.

## 4. Hard constraints (learned the expensive way this arc)

1. **Never realtime.** No RAVE code in `processBlock`, DSP chains, routing,
   export internals, plugin isolation. Offline sidecar only; mutation via
   existing message-thread commands (`importAudioFile`, `setSamplerSample`).
2. **UI must use async jobs** (`startTransform` + `notify.raveProgress`);
   sync RPCs die at the 30 s `RpcClient` timeout. Agents/MCP may use sync.
3. **Beats vs seconds:** `importResult.startBeats` is BEATS end-to-end;
   conversion lives inside `importAudioFile` only.
4. **No fake success:** exit 0 + no output = failure (already enforced
   engine-side AND sidecar-side); fallback DSP must always be labeled in
   stdout (`FALLBACK-DSP:` prefix) so renders are attributable.
5. **Renders go to `compositions/`** (gitignored); fixtures stay in
   `tools/rave/fixtures/`; model binaries NEVER committed (`/rave/` and
   `*.wav`/`*.gguf` ignored; `vintage.ts` is staged repo-locally under `rave/models/`).
6. **MCP parity in the same change** as any RPC addition.
7. **hdaw-guard:** plan with success gates → graph query → dispatch
   implementation to subagents → verify with evidence. QSettings/cwd/env are
   process-global in tests: guard + restore (see `rave_settings_test.cpp`).
8. **torch.jit.load FutureWarning on Python 3.14+** — benign today; a future
   slice should evaluate `torch.export` migration before it breaks.
9. Packaging: any new sidecar file must be added to the
   `frontend/electron-builder.yml` `tools/rave` filter list (currently ONLY
   `rave_transform.py` + `README.md` ship).

## 5. Environment / infra gotchas (current as of 2026-09-07)

- **Flat single-config Ninja tree**: binaries at `build/` (RelWithDebInfo);
  `build/Debug/` and `build/RelWithDebInfo/` DO NOT exist. `build.bat` /
  playwright config / electron-builder were all fixed for this — keep new
  tooling layout-aware.
- **Builds need x64 VS DevShell** (`Enter-VsDevShell ... -DevCmdArguments
  '-arch=x64 -host_arch=x64'`) or vcvars64; bare shells miss STL/SDK paths or
  link x86 libs.
- **Long jobs run detached + poll**: agent tool calls cap ~15 min; full gtest
  is ~27 min on this box, full packaging ~50 min. Pattern:
  `Start-Process cmd.exe -ArgumentList '/c ... > log 2>&1' -PassThru` then
  poll the PID/log. Kill stale `HDAW*` processes before engine runs (ports
  8765/8766 singleton; AGENTS lesson 20).
- **pi-sub-agent extension currently broken** (control socket collides with
  pi-web-ui on 8787) — use the native `subagent_spawn`/`subagent_wait_all`
  tools instead.
- Playwright: serial (`workers: 1`), never two runs at once; specs must poll
  clip assertions (`expect.toPass`) — delta sync is debounced.

## 6. Verification playbook (copy-paste from this arc)

```powershell
# targeted C++ tests
scripts\time-sync.cmd
cmake --build build --config Debug --target hdaw_tests   # under x64 DevShell
build\hdaw_tests.exe --gtest_filter=RaveService.*:RaveRpc.*:RaveSettings.*

# frontend
cd frontend; npx vitest run src/components/__tests__/NeuralPanel.test.tsx
cd frontend; npx playwright test e2e/neural-panel.spec.ts --reporter=line

# direct sidecar (real model)
python tools/rave/rave_transform.py --input tools/rave/fixtures/test_tone.wav `
  --model ".\rave\models\vintage.ts" --output compositions/probe.wav --temperature 1.0 --seed 0

# packaged-layout probe: launch frontend\release\win-unpacked\resources\engine\HDAW_headless.exe
# with cwd OUTSIDE the repo (proves the <exe_dir>/../rave fallback), then drive ws://127.0.0.1:8766.
```

Baseline at handoff: gtest 1415/18-skip/0-fail · Vitest 877/877 ·
Playwright 261/261 · packaged engine REAL-MODEL render verified.

## 7. Loose ends / known observations (not blockers)

- `build.bat` closing echo still prints old `%CONFIG%`-style paths (cosmetic).
- Paste-at-playhead overwrites a fully-covered source clip (documented
  `moveClipWithOverlap` Case 1; intended, but a UX-surprise candidate).
- `importResult` returns `samplerOk:false` + empty error when no sampler send
  requested (by design; could become `null` semantics if it ever confuses a
  client).
- Untracked, deliberately uncommitted: `.pi/`, `.pi-web/`,
  `docs/handoffs/2026-09-06-psytrance-remix-session.md` (other session).
- No real RAVE model other than `vintage.ts` on this machine; morph/generate
  slices should keep the fake-fixture + `--allow-fallback` test path working
  so CI/clean machines stay green.
