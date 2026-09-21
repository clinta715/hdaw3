#!/usr/bin/env bash
# run-tests-parallel.sh — shard the gtest suite across N processes.
#
# The engine is a singleton PER PROCESS and every proxy child gets a unique
# namespace per manager instance, so independent hdaw_tests.exe processes can
# run concurrently. gtest has no parallel runner, so this shards the test list:
#   * small suites (<6 tests) are sharded whole by suite name (short filters);
#   * large suites (FxMidiInjection, MatrixPresetsTest, PluginIsolation, ...) are
#     sharded per TEST, so the long real-plugin gates spread across shards.
#
# Usage: scripts/run-tests-parallel.sh [N] [suite-regex]
#   scripts/run-tests-parallel.sh 4                     # whole suite, 4 shards
#   scripts/run-tests-parallel.sh 4 FxMidiInjection     # only these suites
set -u
N="${1:-4}"; ONLY="${2:-}"
case "$N" in ''|*[!0-9]*) echo "N must be a number"; exit 2;; esac
# The binary is a WINDOWS exe launched from WSL: interop only forwards env vars
# listed in WSLENV, so without this every real-plugin gate silently SKIPs.
export HDAW_REAL_PLUGIN_TESTS="${HDAW_REAL_PLUGIN_TESTS:-1}"
case ":${WSLENV:-}:" in
  *":HDAW_REAL_PLUGIN_TESTS:"*) ;;
  *) export WSLENV="HDAW_REAL_PLUGIN_TESTS${WSLENV:+:$WSLENV}" ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/hdaw_tests.exe"
[ -x "$BIN" ] || { echo "missing $BIN (run build-fast test)"; exit 2; }

# CRLF: the Windows binary emits \r\n, which would ride into the parsed names.
LIST="$(HDAW_REAL_PLUGIN_TESTS=${HDAW_REAL_PLUGIN_TESTS:-1} "$BIN" --gtest_list_tests 2>/dev/null | tr -d '\r')"
# -> one "Suite.Test" per line
PAIRS="$(printf '%s\n' "$LIST" | awk '/^[^ ]/ { suite=$1; sub(/\.$/,"",suite); next } /^  / { gsub(/^ +/,""); sub(/ .*/,""); if (suite != "") print suite "." $0 }')"
[ -n "$ONLY" ] && PAIRS="$(printf '%s\n' "$PAIRS" | grep -E "^${ONLY}\.")"
TOTAL="$(printf '%s\n' "$PAIRS" | grep -c .)"
[ "$TOTAL" -gt 0 ] || { echo "no tests matched"; exit 2; }
echo "[parallel] $TOTAL tests across $N shards"

# Units to distribute: whole suites when small, individual tests when large.
UNITS="$(printf '%s\n' "$PAIRS" | awk -F. '{ c[$1]++; if (NF>1) { full[$0]=1 } } END { for (s in c) print c[s], s }' | sort -rn | while read -r cnt s; do
  if [ "$cnt" -lt 6 ]; then echo "S:$s"; else printf '%s\n' "$PAIRS" | grep -E "^${s}\." | sed 's/^/T:/'; fi
done)"
: > /tmp/hdaw_units.$$; printf '%s\n' "$UNITS" > /tmp/hdaw_units.$$

OUT="${TMPDIR:-/tmp}/hdaw_shard_$$"; mkdir -p "$OUT"
i=0
while IFS= read -r u; do
  [ -n "$u" ] || continue
  echo "$u" >> "$OUT/shard_$((i % N)).list"
  i=$((i + 1))
done < /tmp/hdaw_units.$$
rm -f /tmp/hdaw_units.$$

pids=()
for k in $(seq 0 $((N - 1))); do
  [ -f "$OUT/shard_$k.list" ] || continue
  FILTER="$(sed 's/^S://; s/^T://' "$OUT/shard_$k.list" | paste -sd:)"
  n=$(grep -c . "$OUT/shard_$k.list")
  echo "[parallel] shard $k: $n units"
  ( HDAW_REAL_PLUGIN_TESTS=${HDAW_REAL_PLUGIN_TESTS:-1} "$BIN" --gtest_filter="${FILTER}" --gtest_color=no > "$OUT/shard_$k.log" 2>&1; echo $? > "$OUT/shard_$k.rc" ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

FAIL=0
for k in $(seq 0 $((N - 1))); do
  [ -f "$OUT/shard_$k.log" ] || continue
  t="$(grep -aE '^\[  PASSED  \] [0-9]+ test|^\[  FAILED  \] [0-9]+ test' "$OUT/shard_$k.log" | tr '\n' ' ')"
  rc="$(cat "$OUT/shard_$k.rc" 2>/dev/null || echo 9)"
  echo "[shard $k] rc=$rc $t"
  [ "$rc" = "0" ] || FAIL=1
  grep -aE '^\[  FAILED  \] [A-Za-z]' "$OUT/shard_$k.log" | sed 's/^/    /'
done
echo "[parallel] logs: $OUT"
if [ "$FAIL" = 0 ]; then echo "[parallel] ALL SHARDS PASSED"; else echo "[parallel] FAILURES PRESENT"; fi
exit $FAIL
