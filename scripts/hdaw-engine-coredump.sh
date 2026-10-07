#!/usr/bin/env bash
# Launch the HDAW headless engine with core dumps ENABLED and a known core dir.
#
# Why this exists: the engine is normally started from an interactive shell whose
# soft core limit is 0 (`ulimit -c 0`), so a SIGABRT/SIGSEGV leaves NO core and
# the crash is only visible as a truncated hdaw_debug.log (see the 2026-10-06
# rift_dub session: two glibc "double free or corruption" aborts, zero dumps).
# kernel.core_pattern is "core", i.e. the dump lands in the process CWD.
#
# Usage: scripts/hdaw-engine-coredump.sh [mcp-http-port] [ws-port]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-18765}"
WS="${2:-18766}"
COREDIR="${HDAW_CORE_DIR:-$ROOT/crash-captures}"
mkdir -p "$COREDIR"
cd "$COREDIR"                      # core_pattern=core -> dump lands here
ulimit -c unlimited                # 0 by default in a non-interactive shell
echo "cores -> $COREDIR/core  (binary: $ROOT/build/HDAW_headless)"
exec "$ROOT/build/HDAW_headless" --mcp-http --mcp-http-host=127.0.0.1 \
     --mcp-http-port="$PORT" --port="$WS"
