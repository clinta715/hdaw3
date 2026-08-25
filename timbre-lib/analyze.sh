#!/usr/bin/env bash
# TimbreLib analyze wrapper — runs lib_analyze.py with the ML toolchain venv.
# Usage: ./analyze.sh <folder-or-file> [--limit N] [--no-llm] [--sidecars] [--out PATH]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${TIMBRE_PY:-/home/hapbt/.prime/agent/kernel-venv/bin/python}"
if [ ! -x "$PY" ]; then
    echo "error: venv python not found at $PY (set TIMBRE_PY to override)" >&2
    exit 1
fi
exec "$PY" "$HERE/lib_analyze.py" "$@"
