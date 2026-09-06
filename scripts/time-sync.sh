#!/usr/bin/env bash
# =============================================================================
# time-sync.sh - WSL/Windows clock-drift guard for HDAW builds.
#
# WHY: WSL2's virtual clock drifts away from the Windows host (seconds to
# minutes over hours of uptime). Ninja/MSBuild decide what (not) to rebuild by
# comparing file mtimes; with a drifted WSL clock, timestamps seen through
# drvfs/9p look "in the future" (rebuild everything) or "in the past" (never
# rebuild, STALE artifacts). Snapping the WSL clock to the Windows time server
# before every build removes that variable.
#
# CONTRACT:
#   * NEVER fails a build. Every failure path prints a warning to stderr and
#     exits 0. (Build stability outranks clock freshness. Set
#     HDAW_TIME_SYNC_STRICT=1 to exit 1 when the sync itself fails.)
#   * Fast no-op when: not running inside WSL; synced within the freshness
#     window (HDAW_TIME_SYNC_INTERVAL seconds, default 300); the sync needs a
#     sudo password and no terminal is attached.
#   * Primary tool: `sudo ntpdate -b time.windows.com`. The -b flag STEPS the
#     clock; ntpdate's default slew refuses offsets > ~0.5 s, which is exactly
#     the WSL-drift case. Fallbacks if ntpdate is absent: `sntp -sS`, then
#     `hwclock -s` (WSL2 virtual RTC tracks the host).
#
# PASSWORDLESS SUDO (recommended, run once):
#   echo "$(whoami) ALL=(root) NOPASSWD: /usr/sbin/ntpdate time.windows.com" \
#     | sudo tee /etc/sudoers.d/hdaw-time-sync >/dev/null
#   sudo chmod 440 /etc/sudoers.d/hdaw-time-sync
#   sudo ntpdate -b time.windows.com   # sanity: prints "adjust time server ..."
# Without it the hook falls back to prompting only when a terminal is attached.
#
# USAGE:
#   scripts/time-sync.sh                          # pre-build call (never fails)
#   HDAW_TIME_SYNC_INTERVAL=0 scripts/time-sync.sh # always re-sync
#   HDAW_TIME_SYNC_STRICT=1 scripts/time-sync.sh   # exit 1 if the sync fails
# =============================================================================
set -u

# --- 0. Only meaningful inside WSL ------------------------------------------
if [ -z "${WSL_DISTRO_NAME:-}" ] && ! uname -r 2>/dev/null | grep -qi microsoft; then
    exit 0
fi

# --- 1. Freshness stamp ------------------------------------------------------
# Skip repeated syncs inside the window. The stamp is touched on success AND
# failure (failure backoff = the interval), so a transient network/sudo problem
# warns at most once per interval instead of every build. Set INTERVAL=0 to
# always re-sync.
STAMP="${HDAW_TIME_SYNC_STAMP:-/tmp/hdaw_time_sync.stamp}"
INTERVAL="${HDAW_TIME_SYNC_INTERVAL:-300}"
if [ -f "$STAMP" ] && [ "$INTERVAL" -gt 0 ] 2>/dev/null; then
    last="$(cat "$STAMP" 2>/dev/null || true)"
    now="$(date +%s)"
    if [ -n "$last" ] && [ "$last" -ge 0 ] 2>/dev/null \
        && [ $((now - last)) -lt "$INTERVAL" ] 2>/dev/null; then
        exit 0
    fi
fi

# --- 2. Run the sync ---------------------------------------------------------
# Try passwordless sudo first; only prompt when stdin and stdout are real
# terminals (never hang a build/CI/script context with a password prompt).
sync_tool() {
    if sudo -n "$@" >/dev/null 2>&1; then
        return 0
    fi
    if [ -t 0 ] && [ -t 1 ]; then
        sudo "$@" >/dev/null 2>&1 && return 0
    fi
    return 1
}

ok=0
if command -v ntpdate >/dev/null 2>&1; then
    sync_tool ntpdate -b time.windows.com && ok=1
elif command -v sntp >/dev/null 2>&1; then
    sync_tool sntp -sS time.windows.com && ok=1
elif command -v hwclock >/dev/null 2>&1; then
    sudo -n hwclock -s >/dev/null 2>&1 && ok=1
fi

date +%s > "$STAMP" 2>/dev/null || true
if [ "$ok" -eq 1 ]; then
    echo "[time-sync] WSL clock synced to Windows host (time.windows.com)" >&2
else
    echo "[time-sync] WARN: could not sync WSL clock - install ntpdate/sntp or" \
         "configure passwordless sudo (see the header of this script)." >&2
    if [ "${HDAW_TIME_SYNC_STRICT:-0}" = "1" ]; then
        exit 1
    fi
fi
exit 0
