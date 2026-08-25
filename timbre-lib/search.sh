#!/usr/bin/env bash
# TimbreLib search wrapper — runs lib_search.py with the ML toolchain venv.
# Usage: ./search.sh "<query>" [--lib <folder-or-json>] [--limit N] [--min-dur S] [--max-dur S] [--json]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${TIMBRE_PY:-/home/hapbt/.prime/agent/kernel-venv/bin/python}"
if [ ! -x "$PY" ]; then
    echo "error: venv python not found at $PY (set TIMBRE_PY to override)" >&2
    exit 1
fi
exec "$PY" "$HERE/lib_search.py" "$@"
