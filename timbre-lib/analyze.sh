#!/usr/bin/env bash
# TimbreLib analyze wrapper — runs lib_analyze.py with the ML toolchain venv.
# Usage: ./analyze.sh <folder-or-file> [--limit N] [--no-llm] [--sidecars] [--out PATH]
#                     [--library NAME]
#   --library NAME  after analysis, register <folder> as an HDAW *audio* library
#                   named NAME in %APPDATA%\HDAW\libraries\registry.json (deduped
#                   by path; dead hdaw_tests temp entries pruned). The next HDAW
#                   engine session (app or MCP) picks it up; scan_library then
#                   ingests the sidecars written by this run. Registry location
#                   can be overridden with HDAW_REGISTRY=<file>.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${TIMBRE_PY:-/home/hapbt/.prime/agent/kernel-venv/bin/python}"
if [ ! -x "$PY" ]; then
    echo "error: venv python not found at $PY (set TIMBRE_PY to override)" >&2
    exit 1
fi

# ---- parse --library out of args (lib_analyze.py doesn't know it) ------------
LIB_NAME=""
PASS_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --library) LIB_NAME="${2:?--library needs a NAME}"; shift 2 ;;
        --library=*) LIB_NAME="${1#--library=}"; shift ;;
        *) PASS_ARGS+=("$1"); shift ;;
    esac
done

FOLDER="${PASS_ARGS[0]:-}"
if [ -n "$LIB_NAME" ] && [ ! -d "$FOLDER" ]; then
    echo "error: --library needs a folder (got: '$FOLDER')" >&2
    exit 1
fi

"$PY" "$HERE/lib_analyze.py" "${PASS_ARGS[@]+"${PASS_ARGS[@]}"}"

# ---- HDAW library registration (--library) -----------------------------------
if [ -n "$LIB_NAME" ]; then
    "$HERE/register_library.py" --path "$FOLDER" --name "$LIB_NAME"
fi
