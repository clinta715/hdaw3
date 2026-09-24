---
name: pre-build-time-sync
description: OPT-IN, WSL-ONLY clock guard. On the native Windows dev box this is a no-op and needs no action - scripts\time-sync.cmd exits 0 silently unless HDAW_TIME_SYNC=1. Set HDAW_TIME_SYNC=1 only when the source tree is reached through WSL's drvfs/9p view, which snaps the WSL clock to the Windows host (sudo ntpdate -b time.windows.com) so ninja/MSBuild timestamp checks never see drifted future/past mtimes through drvfs/9p (stale-`.obj`/stale-bundle traps). Never blocks or fails the build.
---

# Pre-Build Time Sync (WSL-only, opt-in)

**On the native Windows 11 dev box: do nothing.** `scripts\time-sync.cmd` —
called by `build-fast.bat` and the CMake `hdaw_time_sync`
`ALL` target — exits 0 immediately and prints nothing unless `HDAW_TIME_SYNC=1`
is set in the environment. Nothing in a native build crosses WSL's drvfs/9p
view, so there is no clock to synchronise and no pre-build step is required.

**When the guard is worth enabling:** only when the source tree is reached
*through WSL* (editing/building from a WSL shell against `/mnt/d/...`). WSL2
clocks drift (seconds to minutes over hours of uptime); a drifted clock makes
file mtimes seen through drvfs/9p look "in the future" (build everything, slow)
or "in the past" (never rebuild, stale `.obj`/bundles — the traps behind
AGENTS.md lessons 15/21 and the WSL-side-edit sync recipe).

## Enabling it

```
cmd:        set HDAW_TIME_SYNC=1
PowerShell: $env:HDAW_TIME_SYNC=1
WSL bash:   scripts/time-sync.sh        (runs the sync directly)
```

With the variable set, `build-fast.bat` and CMake
(`cmake\RunTimeSync.cmake` → `scripts\time-sync.cmd`) run the sync before every
build. A BARE `cmake --build --target X` or a direct `ninja` does not — run
`scripts/time-sync.sh` (or the .cmd) first there.

Behavior:

- **No-op by default:** without `HDAW_TIME_SYNC=1` the .cmd exits 0 with no
  output and never spawns `wsl.exe`; `time-sync.sh` itself is also a no-op
  outside WSL. Both exit 0 instantly.
- **Freshness window:** re-syncs at most once per `HDAW_TIME_SYNC_INTERVAL`
  seconds (default 300). `HDAW_TIME_SYNC_INTERVAL=0` → always sync.
- **Never fails the build:** all failure paths warn to stderr and exit 0.
  `HDAW_TIME_SYNC_STRICT=1` makes sync failure exit 1 (interactive use only).
- Runs `sudo ntpdate -b time.windows.com` (fallbacks: `sntp -sS`, `hwclock -s`).

## One-time setup (recommended, WSL only)

Passwordless sudo for the single command, so builds never prompt:

```
echo "$(whoami) ALL=(root) NOPASSWD: /usr/sbin/ntpdate time.windows.com" \
  | sudo tee /etc/sudoers.d/hdaw-time-sync >/dev/null
sudo chmod 440 /etc/sudoers.d/hdaw-time-sync
sudo ntpdate -b time.windows.com    # sanity: prints "adjust time server ..."
```

Without it, the hook prompts only when a terminal is attached and otherwise
warns (build proceeds).

## Troubleshooting (WSL only)

- `sudo: a password is required` → run the one-time setup above.
- `command not found: ntpdate` → `sudo apt-get install ntpdate` (or ntpsec
  for `sntp`).
- `no server suitable for synchronization found` → time server unreachable
  (offline/NAT); the build continues, retried on the next interval.

## Definition of done

Only meaningful when the guard is enabled: the last hook line before a build is
either

```
[time-sync] WSL clock synced to Windows host (time.windows.com)
```

or the warning line (build continues). If you see the warning, note it in the
build report — mtimes were NOT trustworthy for that build.

On a native Windows build there is no `[time-sync]` line at all: that is the
expected, correct state.
