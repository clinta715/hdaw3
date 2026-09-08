#!/usr/bin/env python3
"""Offline RAVE training sidecar for HDAW.

This wrapper performs deterministic validation, then delegates to a real RAVE
training command configured by HDAW_RAVE_TRAIN_COMMAND. It intentionally does
not fake successful training: if no command is configured, it exits non-zero.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shlex
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="HDAW offline RAVE training wrapper")
    parser.add_argument("--dataset", required=True, help="Directory containing training .wav files")
    parser.add_argument("--output-model", required=True, help="Model file expected after training")
    parser.add_argument("--name", default="hdaw_rave_model", help="Training run/model name")
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--sample-rate", type=int, default=44100)
    parser.add_argument("--dry-run", action="store_true", help="Validate only; does not write a model")
    return parser.parse_args()


def fail(message: str, code: int = 2) -> int:
    print(message, file=sys.stderr)
    return code


def main() -> int:
    args = parse_args()
    dataset = pathlib.Path(args.dataset)
    output_model = pathlib.Path(args.output_model)

    if not dataset.is_dir():
        return fail(f"dataset directory not found: {dataset}")
    if not any(p.is_file() and p.suffix.lower() == ".wav" for p in dataset.rglob("*")):
        return fail(f"dataset contains no .wav files: {dataset}")
    if args.epochs <= 0:
        return fail("epochs must be > 0")
    if args.batch_size <= 0:
        return fail("batch-size must be > 0")
    if args.sample_rate <= 0:
        return fail("sample-rate must be > 0")

    output_model.parent.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        print(f"validated dataset={dataset} output_model={output_model}")
        return 0

    command = os.environ.get("HDAW_RAVE_TRAIN_COMMAND", "").strip()
    if not command:
        return fail(
            "HDAW_RAVE_TRAIN_COMMAND is not configured; install/configure RAVE training "
            "and set this environment variable. No fake model was created.",
            3,
        )

    argv = shlex.split(command)
    argv.extend([
        "--dataset", str(dataset),
        "--output-model", str(output_model),
        "--name", args.name,
        "--epochs", str(args.epochs),
        "--batch-size", str(args.batch_size),
        "--sample-rate", str(args.sample_rate),
    ])
    return subprocess.call(argv)


if __name__ == "__main__":
    raise SystemExit(main())
