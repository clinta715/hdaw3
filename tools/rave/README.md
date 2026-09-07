# RAVE sidecar (`tools/rave/`)

Python-only timbre-transfer sidecar for HDAW. The C++ engine
(`src/engine/RaveService.cpp::transformFile`) shells out to
`rave_transform.py` — no libtorch in C++, no realtime-graph changes.

## CLI contract (must stay in sync with `RaveService::transformFile`)

```
python tools/rave/rave_transform.py --input X.wav --model Y.rave --output Z.wav --temperature T --seed S
```

| Flag | Required | Meaning |
|------|----------|---------|
| `--input` | yes | input WAV (8/16/24/32-bit PCM; downmixed to mono) |
| `--model` | yes | RAVE model file (`.ts` / `.pt` / `.pth` / `.rave` / `.onnx`) |
| `--output` | yes | output WAV to write (mono 16-bit PCM) |
| `--temperature` | no (default `1.0`) | sampling temperature, must be within `[0, 16]` |
| `--seed` | no (default `0`) | integer seed `>= 0` |
| `--allow-fallback` | no | permit the deterministic DSP stand-in when real inference is unavailable |
| `--make-test-tone` | no | write a synthetic 2 s / 440 Hz test tone to `--output` and exit |

Exit contract: **exit 0 means `--output` was written and verified**
(non-empty, peak ≥ 0.01, all samples finite). Any failure exits nonzero
with a clear stderr message and no output is claimed. The engine
additionally rejects exit-0-without-output — keep that contract.

## Modes

- **REAL-MODEL** — torch is importable *and* `--model` loads via
  `torch.jit.load` *and* inference returns finite, non-silent audio.
  Stdout carries a `REAL-MODEL:` line.

  **Tensor convention:** the sidecar feeds the model `[N, C, T] = [1, 1, T]`
  (mono waveform batch), as RAVE v2 waveform-in/out exports expect; a `[1, T]`
  input breaks inside pqmf/cached_conv. Multi-channel outputs (e.g. stereo
  `[1, 2, T']`) are downmixed to mono before writing — never interleaved.

  **Verified against `vintage.ts`** (RAVE v2 combined export, 481 MB, CPU-only
  torch 2.14, Python 3.14): a 2.0 s fixture tone renders `[1, 2, 90112]` →
  mono 90112 frames @ 44100 Hz (≈2.04 s; the small length increase is PQMF
  block padding, normal). The `torch.jit.load` Python-3.14 `FutureWarning`
  ("not supported in Python 3.14+") is benign — loading and inference succeed.

  **Temperature honesty:** `--temperature` currently only affects the
  FALLBACK-DSP path plus `torch.manual_seed` for models that sample internally.
  The real RAVE v2 `forward` is deterministic reconstruction (`decode(encode(x))`),
  so temperature does **not** change its output; the script prints a note saying
  so on the real path rather than faking temperature application.
- **FALLBACK-DSP** — deterministic stdlib-only waveshape transform:
  `tanh` saturation whose drive (`1 + 2·temperature`) follows
  `--temperature`, plus seed-driven 2nd-harmonic/wet-mix variation
  (`random.Random(seed)`). Stdout always carries an explicit
  `FALLBACK-DSP` marker. **It does NOT reproduce RAVE fidelity** — it
  exists so the end-to-end path works on a clean machine.

Fallback selection:

- torch **not installed** → fallback automatically (clean-machine proof).
- torch installed but model unloadable / inference unusable → **hard error**
  unless `--allow-fallback` is passed (Gate 3: no silent fake success).

## Setup

```powershell
# 1. Real-model inference needs torch (fallback needs nothing):
python -m pip install torch

# 2. Stage models (both locations are gitignored, never commit binaries):
#    - repo-root ./rave/            (/rave/ in .gitignore)
#    - %APPDATA%\ACIDS\RAVE\        (Acids plugin default export dir)
Copy-Item my-timbre.ts .\rave\

# 3. (Usually unnecessary) point the engine at the sidecar explicitly —
#    fresh installs resolve it automatically via the chain below:
$env:HDAW_RAVE_SCRIPT  = "$PWD\tools\rave\rave_transform.py"
$env:HDAW_RAVE_PYTHON  = "python"
$env:HDAW_RAVE_TIMEOUT_MS = "600000"
```

### Script-path resolution chain (`RaveService::resolveScriptPath`)

No manual config is needed on a fresh install. Candidates in order — the
first configured value wins; for the fallbacks the first that exists as a
file wins:

1. **Request** — `scriptPath` on the transform request (UI / RPC / MCP).
2. **QSettings** — persisted `rave/scriptPath` (`settings.setRaveConfig` /
   `rave_set_config`).
3. **Env** — `HDAW_RAVE_SCRIPT`.
4. **Dev-tree fallback** — `<cwd>/tools/rave/rave_transform.py` (engine run
   from a source checkout).
5. **Packaged fallback** — `<exe_dir>/../rave/rave_transform.py`. The
   packaged Electron app bundles this script via `electron-builder.yml`
   `extraResources` at `resources/rave/rave_transform.py`, right next to
   `resources/engine/` where the engine exe lives.

If nothing resolves, the transform fails with the unchanged error
`RAVE transform script not configured (set scriptPath or HDAW_RAVE_SCRIPT)`.

Python resolution is unchanged: request > QSettings `rave/pythonPath` > env
`HDAW_RAVE_PYTHON` > default `python`. `HDAW_RAVE_TIMEOUT_MS` defaults to
10 min.

## Example commands

```powershell
# Help
python tools/rave/rave_transform.py --help

# Synthetic fixture input (allowed: fixtures live under tools/)
python tools/rave/rave_transform.py --make-test-tone --output tools/rave/fixtures/test_tone.wav

# End-to-end smoke test (fallback path; renders go to compositions/, gitignored)
python tools/rave/rave_transform.py --input tools/rave/fixtures/test_tone.wav --model tools/rave/fixtures/fake.rave --output compositions/rave_smoke_test.wav --temperature 1.0 --seed 0 --allow-fallback

# Strict mode: invalid model must fail loudly (Gate 3)
python tools/rave/rave_transform.py --input tools/rave/fixtures/test_tone.wav --model tools/rave/fixtures/fake.rave --output compositions/should_not_exist.wav --temperature 1.0 --seed 0

# Real model (once staged)
python tools/rave/rave_transform.py --input in.wav --model .\rave\my-timbre.ts --output compositions\rave_out.wav --temperature 0.7 --seed 42
```

## Verifying output is real audio

```powershell
python -c "import wave,struct; w=wave.open('compositions/rave_smoke_test.wav','rb'); n=w.getnframes(); d=w.readframes(n); v=struct.unpack('<'+str(n)+'h',d); print('frames',n,'rate',w.getframerate(),'peak',round(max(abs(x) for x in v)/32768,3))"
```

## Rules

- Python-only: never touch `src/`, `CMakeLists.txt`, or `tests/` from here.
- Never commit model binaries (respect `/rave/` gitignore).
- Never download models during tests — fixtures are local/synthetic.
- Render output goes to repo-root `compositions/` (gitignored), never `tools/`
  except small fixtures.
- Temperature/seed are validated, output is clipped to `[-1, 1]` and checked
  for NaN/inf before success is reported.
