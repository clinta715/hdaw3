---
name: pre-build-time-sync
description: REQUIRED before every build/compile in this repo. Snaps the WSL clock to the Windows host (sudo ntpdate -b time.windows.com) so ninja/MSBuild timestamp checks never see drifted future/past mtimes through drvfs/9p (stale-`.obj`/stale-bundle traps). Invoke before ANY build: cmake --build, build-fast.bat, frontend/build.bat, bare ninja, npm build. Never blocks or fails the build.
---

# Pre-Build Time Sync

Snap the WSL clock to the Windows time server **before every build/compile**
in this repo. WSL2 clocks drift (seconds to minutes over hours of uptime); a
drifted clock makes file mtimes seen through drvfs/9p look "in the future"
(build everything, slow) or "in the past" (never rebuild, stale `.obj`/bundles
— the traps behind AGENTS.md lessons 15/21 and the WSL-side-edit sync recipe).

## When this skill is REQUIRED

Run `scripts/time-sync.sh` immediately before ANY of these:

```
cmake --build build --config Debug
cmake --build build --config RelWithDebInfo
build-fast.bat            (any target: hdaw, debug, test, all, ninja, frontend, package)
frontend\build.bat        (any config)
ninja -C build-ninja ...
npm run build             (frontend dist)
```

`build-fast.bat`, `frontend\build.bat`, and the CMake `hdaw_time_sync` `ALL`
custom target already call the hook automatically — but a BARE `cmake --build
--target X`, a direct `ninja`, or a direct `npm run build` does not. When in
doubt, run the hook first; it is a fast no-op in the common case.

## The command

```
scripts/time-sync.sh        # WSL side (also invoked via scripts\time-sync.cmd
                            # from the .bat files; CMake uses
                            # cmake\RunTimeSync.cmake)
```

Behavior:

- **No-op outside WSL** (plain Linux/macOS/CI without WSL) — exits 0 instantly.
- **Freshness window:** re-syncs at most once per `HDAW_TIME_SYNC_INTERVAL`
  seconds (default 300). `HDAW_TIME_SYNC_INTERVAL=0` → always sync.
- **Never fails the build:** all failure paths warn to stderr and exit 0.
  `HDAW_TIME_SYNC_STRICT=1` makes sync failure exit 1 (interactive use only).
- Runs `sudo ntpdate -b time.windows.com` (fallbacks: `sntp -sS`, `hwclock -s`).

## One-time setup (recommended)

Passwordless sudo for the single command, so builds never prompt:

```
echo "$(whoami) ALL=(root) NOPASSWD: /usr/sbin/ntpdate time.windows.com" \
  | sudo tee /etc/sudoers.d/hdaw-time-sync >/dev/null
sudo chmod 440 /etc/sudoers.d/hdaw-time-sync
sudo ntpdate -b time.windows.com    # sanity: prints "adjust time server ..."
```

Without it, the hook prompts only when a terminal is attached and otherwise
warns (build proceeds).

## Troubleshooting

- `sudo: a password is required` → run the one-time setup above.
- `command not found: ntpdate` → `sudo apt-get install ntpdate` (or ntpsec
  for `sntp`).
- `no server suitable for synchronization found` → time server unreachable
  (offline/NAT); the build continues, retried on the next interval.

## Definition of done for this skill's purpose

The last hook line before a build is either:

```
[time-sync] WSL clock synced to Windows host (time.windows.com)
```

or the warning line (build continues). If you see the warning, note it in the
build report — mtimes were NOT trustworthy for that build.
