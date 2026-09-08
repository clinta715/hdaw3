# RAVE sidecar (`tools/rave/`)

Python-only timbre-transfer sidecar for HDAW. The C++ engine
(`src/engine/RaveService.cpp::transformFile`) shells out to
`rave_transform.py` — no libtorch in C++, no realtime-graph changes.

## CLI contract (must stay in sync with `RaveService::transformFile`)

```
# Transform mode (default, backward compatible)
python tools/rave/rave_transform.py --input X.wav --model Y.rave --output Z.wav --temperature T --seed S

# Probe mode (metadata only; no input/output WAV required)
python tools/rave/rave_transform.py --mode probe --model Y.rave
```

| Flag | Required | Meaning |
|------|----------|---------|
| `--mode` | no (default `transform`) | `transform` writes a WAV; `probe` prints one JSON metadata object and writes no WAV |
| `--input` | yes | input WAV (8/16/24/32-bit PCM; downmixed to mono) |
| `--model` | yes | RAVE model file (`.ts` / `.pt` / `.pth` / `.rave` / `.onnx`) |
| `--output` | yes | output WAV to write (mono 16-bit PCM) |
| `--temperature` | no (default `1.0`) | sampling temperature, must be within `[0, 16]` |
| `--seed` | no (default `0`) | integer seed `>= 0` |
| `--allow-fallback` | no | permit the deterministic DSP stand-in when real inference is unavailable |
| `--make-test-tone` | no | write a synthetic 2 s / 440 Hz test tone to `--output` and exit |

Transform exit contract: **exit 0 means `--output` was written and verified**
(non-empty, peak ≥ 0.01, all samples finite). Any failure exits nonzero
with a clear stderr message and no output is claimed. The engine
additionally rejects exit-0-without-output — keep that contract.

Probe exit contract: **stdout is exactly one compact JSON object** with at
least `ok`, `methods`, `sampleRate`, `latentDim`, `latentFrames`,
`encodeShape`, `decodeShape`, and `error`. Probe mode never requires
`--input`/`--output` and never writes a WAV. Missing torch, missing model,
or unloadable TorchScript returns `ok:false` JSON and exits nonzero (no fake
success).

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

## Probe mode / model metadata

Probe mode is the offline-only metadata path surfaced by C++
`RaveService::probeModel`, JSON-RPC `rave.probeModel`, MCP
`rave_probe_model`, and the Neural panel. It shells out to the same sidecar
but only loads the model and tries a zero-input shape probe:

```powershell
python tools/rave/rave_transform.py --mode probe --model .\rave\vintage.ts
```

Observed `vintage.ts` findings (Combined RAVE v2 export):

- Public methods include `encode` and `decode`.
- `encode(torch.zeros(1, 1, 44100))` returns shape `[1, 16, 22]`.
- Decoding that latent returns shape `[1, 2, 45056]`.
- Inferred `latentDim=16`, `latentFrames=22`.
- CPU-only torch 2.14 can load/probe it. Python 3.14 emits the known
  `FutureWarning` about unsupported Python 3.14+, but the warning is not
  part of stdout JSON.
- Sample rate is not directly exported by this vintage model unless a future
  attribute is discovered; the probe reports `sampleRate:null` in that case.


## Setup

```powershell
# 1. Real-model inference needs torch (fallback needs nothing):
python -m pip install torch

# 2. Stage models in the repo-local ignored directory (never commit binaries):
#    - repo-root ./rave/models/      (/rave/ in .gitignore; scanned by default)
New-Item -ItemType Directory -Force .\rave\models | Out-Null
Copy-Item my-timbre.ts .\rave\models\

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

# Probe metadata (strict JSON stdout; no WAV output)
python tools/rave/rave_transform.py --mode probe --model .\rave\my-timbre.ts

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

## Offline model training

HDAW can start cancellable offline RAVE training jobs through JSON-RPC, MCP, and the Neural panel. Training runs in a Python sidecar (`rave_train.py`) launched by `QProcess`; it never touches realtime audio callbacks, playback, export, DSP chains, plugin isolation, or project ValueTree state.

CLI contract:

```powershell
python tools/rave/rave_train.py --dataset DATASET_DIR --output-model OUT.ts --name NAME --epochs 10 --batch-size 8 --sample-rate 44100
```

Validation is fail-fast: the dataset directory must exist and contain at least one `.wav`, the output model path must be nonempty (the parent directory is created when possible), and epochs/batch-size/sample-rate must be positive.

The sidecar does **not** create fake model success. Real training requires an external command configured with `HDAW_RAVE_TRAIN_COMMAND`; the sidecar appends `--dataset`, `--output-model`, `--name`, `--epochs`, `--batch-size`, and `--sample-rate` to that command. If the environment variable is absent, the job fails clearly. `--dry-run` validates only and intentionally writes no model.

Engine script resolution order for training is: explicit `scriptPath`, `HDAW_RAVE_TRAIN_SCRIPT`, dev-tree `tools/rave/rave_train.py`, then packaged `resources/rave/rave_train.py`.

Repo-local acids-rave setup used by HDAW development:

```powershell
uv venv .venv-rave --python 3.11
uv pip install --python .venv-rave\Scripts\python.exe acids-rave "setuptools<81"
$env:HDAW_RAVE_TRAIN_COMMAND = ".venv-rave/Scripts/python.exe tools/rave/run_acids_rave_training.py"
python tools/rave/rave_train.py --dataset rave/datasets/psytrance_synths --output-model rave/models/psytrance_synths.ts --name psytrance_synths --epochs 10000 --batch-size 8 --sample-rate 44100
```
