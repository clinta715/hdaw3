#!/usr/bin/env bash
# mcp-launch.sh — copy-on-launch wrapper for the HDAW MCP server (Linux port
# of mcp-launch.bat). Copies HDAW_headless, hdaw_plugin_host AND
# hdaw_plugin_scanner to ${TMPDIR:-/tmp} before running, so the build-tree
# outputs stay unlocked and cmake/ninja can overwrite them freely. The plugin
# host too: getHostExePath() resolves it as a sibling of the running exe, and
# a stale host in temp breaks the READY handshake for every isolated plugin.
# The scanner likewise: EngineMCP spawns the scanner from temp at scan time,
# so a stale scanner silently downgrades every plugin scan. The next MCP
# session automatically picks up the freshly built binaries.
#
# Crash capture (procdump attach) is Windows/procdump-only and intentionally
# absent here.
#
# Relaunch contract: `engine_restart` makes the engine exit with code 42;
# the harness respawns this launcher, which re-copies the fresh binaries and
# relaunches. This script propagates exit codes verbatim via exec.
#
# Usage (in .mcp.json mcpServers):
#   "command": "/mnt/nvme2/build/hdaw3/mcp-launch.sh"
#   "args": []
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"

# Kill stale engines before copying (one engine per session — any holder of
# the target at launch time is by definition stale). These are the 15-char
# truncated comm names (Linux caps comm at 15) of HDAW_headless_mcp (the
# temp copy) and hdaw_plugin_host; the build-tree engine's comm
# (HDAW_headless) does not match, so a build-tree engine is never killed.
pkill -x HDAW_headless_m 2>/dev/null || true
pkill -x hdaw_plugin_ho 2>/dev/null || true
# SIGTERM may leave the engine alive briefly; copying onto a running binary
# fails with ETXTBSY, so wait for death, then force-kill (taskkill /F
# equivalent).
for _ in $(seq 1 50); do
    pgrep -x HDAW_headless_m >/dev/null && pgrep -x hdaw_plugin_ho >/dev/null || break
    sleep 0.1
done
pkill -9 -x HDAW_headless_m 2>/dev/null || true
pkill -9 -x hdaw_plugin_ho 2>/dev/null || true

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

# Resolve payload from build/ (Ninja single-config; no Debug fallback on
# Linux) and verify every copy: byte size, md5, and — engine only — the
# current-tool sentinel.
verify_copy() {
    local name="$1" src="$2" dst="$3"
    local src_sz dst_sz src_h dst_h
    src_sz="$(stat -c%s "$src")"
    dst_sz="$(stat -c%s "$dst")"
    if [ "$src_sz" != "$dst_sz" ]; then
        fail "Size mismatch copying $name: source $src_sz bytes, destination $dst_sz bytes. Stale engine holding the target?"
    fi
    src_h="$(md5sum "$src" | awk '{print $1}')"
    dst_h="$(md5sum "$dst" | awk '{print $1}')"
    if [ "$src_h" != "$dst_h" ]; then
        fail "Content mismatch copying $name (hash \"$src_h\" vs \"$dst_h\") - stale or mid-copy binary"
    fi
    if [ "$name" = "HDAW_headless" ]; then
        if ! grep -qa audit_song_structure "$dst"; then
            fail "Copied engine missing current MCP tool surface (audit_song_structure sentinel) - stale build? Rebuild then rerun."
        fi
    fi
}

install_payload() {
    local name="$1" dst="$2"
    local src="$BUILD_DIR/$name"
    if [ ! -f "$src" ]; then
        fail "$src not found. Build first (./build-fast.sh, or cmake --build build --target HDAW_headless hdaw_plugin_host hdaw_plugin_scanner)."
    fi
    # Unlink first: writing in place onto a binary a dying engine still
    # maps fails with ETXTBSY; a fresh inode cannot.
    rm -f "$dst"
    if ! cp -f "$src" "$dst"; then
        fail "Failed to copy $name to $dst."
    fi
    verify_copy "$name" "$src" "$dst"
}

TMPD="${TMPDIR:-/tmp}"
install_payload "HDAW_headless"     "$TMPD/HDAW_headless_mcp"
install_payload "hdaw_plugin_host"  "$TMPD/hdaw_plugin_host"
install_payload "hdaw_plugin_scanner" "$TMPD/hdaw_plugin_scanner"

# On Linux the ELF RUNPATH bakes in the Qt/SoundTouch locations, so no
# LD_LIBRARY_PATH is needed. stdout must stay clean (it IS the MCP protocol
# channel); exec passes stdio and exit codes through untouched.
exec "$TMPD/HDAW_headless_mcp" --mcp-stdio "$@"
