#!/usr/bin/env python3
"""Adapter from HDAW's generic training-sidecar args to the installed acids-rave CLI.

Called by tools/rave/rave_train.py through HDAW_RAVE_TRAIN_COMMAND. It performs
RAVE preprocessing, training, then TorchScript export to --output-model.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
import time


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="HDAW -> acids-rave training adapter")
    p.add_argument("--dataset", required=True)
    p.add_argument("--output-model", required=True)
    p.add_argument("--name", default="hdaw_rave_model")
    p.add_argument("--epochs", type=int, default=10000, help="Mapped to RAVE --max_steps")
    p.add_argument("--batch-size", type=int, default=8)
    p.add_argument("--sample-rate", type=int, default=44100)
    return p.parse_args()


def run(argv: list[str], env: dict[str, str]) -> None:
    print("[hdaw-rave-adapter]", " ".join(argv), flush=True)
    rc = subprocess.call(argv, env=env)
    if rc != 0:
        raise SystemExit(rc)


def newest_run_dir(runs_root: pathlib.Path, name: str) -> pathlib.Path:
    candidates = []
    named = runs_root / name
    if named.exists():
        candidates.append(named)
        candidates.extend([p for p in named.rglob("*") if p.is_dir()])
    candidates.extend([p for p in runs_root.rglob("*") if p.is_dir() and name.lower() in str(p).lower()])
    scored = []
    for p in candidates:
        has_ckpt = any(p.rglob("*.ckpt"))
        has_gin = any(p.rglob("*.gin"))
        if has_ckpt or has_gin:
            scored.append((p.stat().st_mtime, p))
    if scored:
        return sorted(scored)[-1][1]
    if named.exists():
        return named
    raise SystemExit(f"could not locate RAVE run directory under {runs_root}")


def main() -> int:
    args = parse_args()
    dataset = pathlib.Path(args.dataset).resolve()
    output_model = pathlib.Path(args.output_model).resolve()
    output_model.parent.mkdir(parents=True, exist_ok=True)

    here = pathlib.Path(__file__).resolve()
    repo = here.parents[2]
    rave_exe = pathlib.Path(os.environ.get("HDAW_ACIDS_RAVE_EXE", repo / ".venv-rave" / "Scripts" / "rave.exe"))
    if not rave_exe.exists():
        raise SystemExit(f"rave.exe not found: {rave_exe}")

    work = output_model.parent / ".training" / args.name
    db = work / "db"
    runs = work / "runs"
    db.mkdir(parents=True, exist_ok=True)
    runs.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.setdefault("PYTHONUTF8", "1")

    run([str(rave_exe), "preprocess",
         "--input_path", str(dataset),
         "--output_path", str(db),
         "--sampling_rate", str(args.sample_rate),
         "--channels", "1"], env)

    train_argv = [str(rave_exe), "train",
        "--db_path", str(db),
        "--out_path", str(runs),
        "--name", args.name,
        "--batch", str(args.batch_size),
        "--max_steps", str(args.epochs),
        "--save_every", str(max(1, args.epochs)),
        "--val_every", str(max(1, args.epochs)),
        "--workers", os.environ.get("HDAW_RAVE_WORKERS", "0"),
        "--channels", "1"]
    if os.environ.get("HDAW_RAVE_SMOKE_TEST") == "1":
        train_argv.append("--smoke_test")
    run(train_argv, env)

    run_dir = pathlib.Path(os.environ.get("HDAW_RAVE_RUN_DIR", "")) if os.environ.get("HDAW_RAVE_RUN_DIR") else newest_run_dir(runs, args.name)
    # acids-rave export treats --output as a directory and writes
    # <name>.ts inside it. HDAW's contract is --output-model as the exact file
    # path, so export into the parent directory and verify that exact file.
    if output_model.exists() and output_model.is_dir():
        raise SystemExit(f"output model path is a directory; remove it first: {output_model}")

    run([str(rave_exe), "export",
         "--run", str(run_dir),
         "--output", str(output_model.parent),
         "--name", output_model.stem,
         "--channels", "1",
         "--sr", str(args.sample_rate)], env)

    if not output_model.is_file():
        raise SystemExit(f"export did not create output model file: {output_model}")
    print(f"[hdaw-rave-adapter] wrote {output_model}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
