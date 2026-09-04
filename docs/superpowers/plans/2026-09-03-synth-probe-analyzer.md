# Synth Probe Analyzer — Implementation Plan

Spec: `docs/superpowers/specs/2026-09-03-synth-probe-analyzer-design.md` (Approved)
Date: 2026-09-03

## Goal

Implement the standalone WAV-in synth probe analyzer in `timbre-lib/`: a CLI
that validates a WAV, measures DSP descriptors, optionally runs CLAP captioning
and a local LLM, applies deterministic per-role checks, and emits one stable
JSON report with priority-ranked recommendations.

## Success Gates (all must pass)

- [x] Gate 1: `python -m pytest timbre-lib/test_analyze_probe.py -q` passes (new
      pytest suite, first in repo). — 23 passed.
- [x] Gate 2: Every supported role (kick, bass, hat, snare, rim, clap, lead,
      arp, stab, pad, riser, fx) has a pass-signal test and a fail-signal test
      asserting `roleCheck.verdict` correctly. — `test_roles_pass_and_fail`
      parametrized over `RT.SUPPORTED_ROLES`.
- [x] Gate 3: Report schema holds for silent, clipped, non-finite, malformed,
      missing, mono, and stereo inputs; schema valid with no error key. —
      `test_silent_input`, `test_clipped_input`, `test_nonfinite_input`,
      `test_malformed_input`, `test_missing_input`, `test_mono_and_stereo`,
      `test_report_schema`.
- [x] Gate 4: DSP-only fallback verified: report is schema-valid with warnings
      when CLAP is disabled and llama_cpp is absent (no model download, no
      external process). — `test_dsp_only_fallback`.
- [x] Gate 5: Stable JSON — identical input + args produce byte-identical
      `json.dumps(report, sort_keys=True)` across two runs. —
      `test_stable_json` plus a cross-process CLI run compared byte-identical.
- [x] Gate 6: `lib_analyze.py` is byte-for-byte unchanged; existing
      `llm_stage.py` public functions (`SYSTEM`, `USER`, `build_evidence`,
      `close`, `run_llm`) are unchanged (additive changes only). `git status`
      shows only new files + `llm_stage.py` modified. — `llm_stage.py` +43/−0.
- [x] Gate 7: No C++/frontend/CMake/MCP/engine files changed — `git status`
      confirms the diff is confined to `timbre-lib/` + the plan doc. — only
      `analyze_probe.py`, `role_targets.py`, `test_analyze_probe.py`,
      `llm_stage.py` (+43), README, and this plan doc.
- [x] Gate 8: `python -m py_compile` on all four Python files, and `ruff check`
      (if available) clean on new/modified files; no syntax errors. — py_compile
      exit 0, `ruff check` "All checks passed!".

## Dependency Map

- Blast radius: confined to `timbre-lib/` (Python-only, not in the CMake build —
  verified: no CMakeLists reference to timbre). No C++, frontend, RPC, or
  audio-engine code is touched.
- Upstream consumers of modified file `llm_stage.py`: `lib_analyze.py`,
  `analyze_{targeted,ragga,psy_batch2,sfx_psytrance,psytrance,multi,dnb}.py` —
  all import only the existing symbols; the change is purely additive
  (`ROLE_SYSTEM`, `build_probe_evidence`, `run_llm_role`), so they cannot break.
- Downstream: none — analyzer is a standalone CLI producing JSON on stdout.
- `lib_analyze.py` reuse: imported as-is for `collect`/`run_all`? NO — the probe
  analyzer must NOT invoke the library-index workflow and must NOT write
  `.timbre.json` sidecars. It reuses `timbre.py` (stage 1) and optionally
  `clap_stage.py` / `llm_stage.py`.
- Knowledge graph: codebase-memory index covers C++ repo; Python scripts are
  outside its indexed scope. Grep confirms no external Python references to the
  new modules (`role_targets`, `analyze_probe`).

## Pitfall Gates

- Gate 2 (unimplemented path silently failing): every stage (validate → measure
  → CLAP → LLM → role check → recommend → report) must either produce real
  output or an explicit warning. Enforced by tests asserting warnings + schema.
- Gate 15 (verify the artifact, not the source): gates run actual pytest
  output, not code reading.
- Gate 9 (validation at trust boundary): `--role` validated against
  `role_targets.SUPPORTED_ROLES`; input path existence checked; loader catches
  read/decode failures into an error report.
- Other gates (1, 3–8, 10–14, 16) are C++/audio-thread/frontend-specific and do
  not apply to additive Python in `timbre-lib/`.

## Anti-Patterns

- No modification to `lib_analyze.py` (design: workflow unchanged).
- No `.timbre.json` sidecar writes from the analyzer.
- No LLM prompt embedding of thresholds (design: thresholds explicit in
  `role_targets.py`, testable, never in the prompt).
- Deterministic checks produce the verdict; the LLM only explains evidence and
  cannot override a check.

## Design (from spec, concretized)

### `timbre-lib/role_targets.py` (new)
- `SUPPORTED_ROLES = ("kick","bass","hat","snare","rim","clap","lead","arp","stab","pad","riser","fx")`
- `ROLE_ALIASES` + `normalize_role()` (extend tune_roles alias table).
- `ROLE_TARGETS[role] = {"checks": [{name, op, target, desc}, ...], "summary": str}`
  - op in {"le","lt","ge","gt","range"} (range target = [lo, hi]).
  - kick: centroid<=120, mel_low>=0.45, decay_s>=0.5 (short decay)
  - bass: centroid in [60,250], mel_low>=0.35, decay_s<=0.85 (controlled decay)
  - hat: centroid>=6000, mel_high>=0.12, decay_s>=0.5
  - snare: centroid in [1200,8000], body=mel_mid+mel_high>=0.4, attack_s<=0.05, decay_s>=0.5
  - rim: centroid in [800,8000], body>=0.35, attack_s<=0.05, decay_s>=0.5
  - clap: centroid in [1500,8000], body>=0.4, attack_s<=0.05, decay_s>=0.5
  - lead: tonal_fraction>0, f0_hz in [150,2500], centroid in [400,3000]
  - arp: tonal_fraction>0, f0_hz in [150,4000], centroid in [400,3500]
  - stab: tonal_fraction>0, centroid in [400,3000], decay_s>=0.3
  - pad: decay_s<=0.25 (sustained), mel_mid>=0.3, mel_high>0.03 (nonzero upper)
  - riser: energy_growth>=1.3, centroid_growth>=0.7
  - fx: energy_growth>=1.2, centroid_growth>=0.7
- `spectral_evolution(x, sr)` -> {energy_growth, centroid_growth} (per-frame RMS
  + spectral centroid trend; last-quarter / first-quarter with eps guard).
- `check_role(role, measurements)` -> roleCheck dict:
  `{role, verdict: "pass"|"fail", checks:[{name,measured,target,op,passed,desc}],
  passed_count, total_count, failures:[str], summary}`.
- `REGISTER_WINDOWS[role]` -> centroid Hz window; outside -> critical.
- `build_recommendations(role, measurements, role_check)` -> list of
  `{priority, message, check?}`:
  - critical: non-finite / silence / clipping / severely-wrong-register
  - high: each failed role check
  - medium: pass but measured value within 10% of a threshold edge
  - low: one deterministic creative-variation rec when verdict == pass

### `timbre-lib/analyze_probe.py` (new)
- `load_audio(path)` -> (x float64 mono, sr): librosa first, scipy
  `io.wavfile` + `sps.resample` fallback; raise on missing/empty/unreadable.
- `analyze_probe(path, role, name=None, plugin=None, gguf=None, use_clap=True,
  use_llm=True)` -> report dict (library entry point + CLI driver).
- Pipeline: validate role/path -> load -> finite check -> measurements
  (`timbre.extract` + `spectral_evolution`; round floats, skip on non-finite) ->
  optional CLAP captions/tags (guarded import + try/except; `--no-clap`) ->
  optional LLM prose via new `llm_stage.run_llm_role` (only when gguf given AND
  llama_cpp importable; else warning) -> `role_targets.check_role` +
  `build_recommendations` -> assemble report.
- Report shape (spec contract):
  ```json
  {"input":{"path","name","plugin"}, "role", "measurements":{}, "roleCheck":{},
   "description":"", "recommendations":[], "warnings":[], "error":null}
  ```
  - `measurements`/`roleCheck` null when unreadable/non-finite.
  - `description` = LLM prose when available, else `timbre.summarize`.
  - Warnings: stage-unavailable (CLAP/LLM), non-finite, silence, clipping.
- CLI: `python analyze_probe.py <wav> --role ROLE [--name N] [--plugin P]
  [--gguf PATH] [--no-clap] [--no-llm]`; prints indent-2 JSON; exit 0 for any
  completed analysis (incl. critical findings), exit 1 for error reports
  (missing/malformed/invalid role).

### `timbre-lib/llm_stage.py` (additive only)
- Add `ROLE_SYSTEM` (role-aware, evidence-explanation-only system prompt),
  `build_probe_evidence(words, tags, captions, role, name, plugin)`,
  `run_llm_role(evidence, role, model_path, ...)`. Existing `SYSTEM`, `USER`,
  `build_evidence`, `run_llm`, `close` untouched.

### `timbre-lib/test_analyze_probe.py` (new, pytest)
- Helper `write_wav(path, sr, samples)` (16-bit PCM via stdlib `wave` for
  pass/fail signals) and `write_wav_f32(path, sr, samples)` (IEEE float32 via
  scipy for non-finite).
- Synthetic pass/fail signals per role (pure numpy/scipy: decaying sine kick,
  sustained low bass, HP noise hat, bandpass noise snare/rim/clap, tonal
  lead/arp/stab, sustained chord-ish pad with faint high harmonic, amplitude-
  ramp noise riser, constant noise riser-fail, etc.) — tuned empirically so
  pass signals satisfy every check and fail signals violate >=1 check.
- Input-integrity tests: silent, clipped (square at ±1.0), non-finite (NaN/Inf
  float32 WAV), malformed (text file), missing file, mono, stereo.
- Fallback + stability tests: `--no-clap` + no gguf -> warnings present, schema
  valid; identical input/args twice -> identical `json.dumps(sort_keys=True)`.
- Unknown-role test -> error report.

## Steps

1. Write `timbre-lib/role_targets.py`.
2. Add additive functions to `timbre-lib/llm_stage.py`.
3. Write `timbre-lib/analyze_probe.py`.
4. Write `timbre-lib/test_analyze_probe.py` with synthetic-signal helpers.
5. Run `python -m pytest timbre-lib/test_analyze_probe.py -q`; iterate on test
   signal tuning until all pass.
6. Verify gates: stable JSON, schema on bad inputs, `git status` confined to
   timbre-lib + plan doc, `py_compile` on all files, `lib_analyze.py` untouched.

## Verification Commands

- `python -m pytest timbre-lib/test_analyze_probe.py -q`
- `python -m py_compile timbre-lib/analyze_probe.py timbre-lib/role_targets.py timbre-lib/llm_stage.py timbre-lib/test_analyze_probe.py`
- `python timbre-lib/analyze_probe.py <fixture> --role kick` (manual smoke)
- `git status --short` (diff confined to timbre-lib/ + plan doc)
- `ruff check timbre-lib/role_targets.py timbre-lib/analyze_probe.py timbre-lib/test_analyze_probe.py` (if ruff installed)