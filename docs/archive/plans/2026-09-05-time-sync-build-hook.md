# Plan: Pre-build time-sync hook (WSL/Windows clock drift)

Date: 2026-09-05 · Engine touch: NO (build tooling / docs only)

## Goal
Guarantee that the WSL clock is snapped to the Windows host time before EVERY
build/compile in this repo, so ninja/MSBuild never misjudge file mtimes through
drvfs/9p under WSL2 clock drift (the stale-`.obj`/stale-bundle trap family —
AGENTS.md lessons 15/21 plus the WSL-side-edit sync recipe).

## Success Gates (all must pass to declare done)
- [ ] G1: `bash -n scripts/time-sync.sh` is clean; the script exits 0 by
      default on this WSL host (no passwordless sudo, no tty → warn path),
      never changes the system clock, and exits 1 ONLY with
      `HDAW_TIME_SYNC_STRICT=1`.
- [ ] G2: Fake-tool test proves the success path: with stub `sudo`+`ntpdate`
      earlier in PATH, the script prints the "synced" line, writes the stamp,
      and exits 0; a second run inside the freshness window is an instant no-op.
- [ ] G3: Windows-side launcher works end-to-end from WSL interop:
      `cmd.exe /c call "D:\pdf\roo projects\hdaw3\scripts\time-sync.cmd"`
      exits 0 and prints the [time-sync] line (spaces in the WSL path handled).
- [ ] G4: `cmd.exe /c "call D:\pdf\roo projects\hdaw3\build-fast.bat bogus"`
      shows the hook running FIRST, then the expected "Unknown target"
      early-exit (no real build started).
- [ ] G5: The CMake hook fires and never fails a build: `cmake.exe -P
      cmake\RunTimeSync.cmake` (real path) prints [time-sync] and exits 0;
      a scratch project replicating the `hdaw_time_sync` ALL custom target
      builds green on BOTH the Ninja and the Visual Studio generator with the
      hook running; `-DHDAW_TIME_SYNC_HOOK=OFF` disables it.
- [ ] G6: Skill files valid: `name` matches directory (pre-build-time-sync),
      description non-empty; docs/skills/README.md and AGENTS.md reference the
      skill and the hook; the CMakeLists change is limited to the new
      `hdaw_time_sync` block at the end of the file.
- [ ] G7: `git diff` audit — no changes outside the declared file set; no
      engine, RPC, or frontend source touched.

## Dependency Map
- Blast radius: build/packaging entry points only (build-fast.bat,
  frontend/build.bat, CMakeLists.txt custom target, new scripts/, new cmake
  -P script, docs).
- Upstream: users/agents invoke builds via `cmake --build`, `build-fast.bat`,
  `frontend\build.bat`, bare `ninja`/`npm run build`.
- Downstream: the hook feeds all of the above; nothing consumes its output.
- God nodes: none. Community boundaries crossed: build/docs only — no engine,
  RPC, ReadModel, audio graph, or SPSC paths involved.
- Knowledge graph: no code-graph impact (no C++/TS/RPC surface changes); a
  fast graphify reindex is run at the end for completeness.

## Pitfall Gates Triggered
- Gate 4 (Build/Packaging Stale Binaries): directly related — the hook exists
  to PREVENT this family. The change itself is additive, never fails a build,
  and is exercised by G3/G4/G5 smoke tests rather than assumed.
- Gate 15 (Stale Flags and Stale Binaries): verification reads ACTUAL execution
  output (cmd.exe runs, -P run), not just the written source.
- All other gates: N/A (no engine, frontend, RPC, or DSP code touched).

## Anti-Patterns
- No silent failure: the hook warns loudly on sync failure but proceeds.
- No over-engineering: one script + one launcher + one CMake -P script + docs.

## Steps
1. (orchestrator) Write this plan file (done before any code). ✓
2. (subagent) Create `scripts/time-sync.sh`, `scripts/time-sync.cmd`,
   `cmake/RunTimeSync.cmake` exactly per the Implementation Spec below.
3. (subagent) Wire the hook: top-level `call` in `build-fast.bat` and
   `frontend/build.bat`; append the `hdaw_time_sync` custom-target block at
   the END of `CMakeLists.txt`.
4. (subagent) Add project skill `docs/skills/pre-build-time-sync/SKILL.md`,
   index entry in `docs/skills/README.md`, and the AGENTS.md Build-section
   rule (with the `skill: "pre-build-time-sync"` invocation line).
5. (subagent) Run G1–G4 self-checks and report evidence with output.
6. (orchestrator) Re-run every gate independently, incl. G5 (scratch Ninja +
   VS-generator builds); audit the diff (G7); fast-reindex graphify;
   install the global agent skill at ~/.prime/agent/skills/pre-build-time-sync/;
   report.

---

# Implementation Spec (exact contents; subagent writes files verbatim)

All files use LF line endings (repo policy: `* text=auto eol=lf`). The .bat/.cmd
files must remain LF (the repo already ships LF .bat that cmd.exe runs fine).

## 1. NEW `scripts/time-sync.sh` (LF)

```bash
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
```

## 2. NEW `scripts/time-sync.cmd` (LF; cmd.exe runs fine with LF)

```bat
@echo off
REM ===========================================================================
REM time-sync.cmd -- Windows-side launcher for the WSL time-sync pre-build hook.
REM
REM Converts this script's location to a WSL path and runs scripts/time-sync.sh
REM inside WSL. NEVER fails a build: exits 0 whether or not WSL / ntpdate / the
REM sudo rule is available (the .sh side prints the warning; set
REM HDAW_TIME_SYNC_STRICT=1 there to turn warnings into errors).
REM ===========================================================================
setlocal

for %%I in ("%~dp0..") do set "WIN_ROOT=%%~fI"
for %%I in ("%WIN_ROOT%") do set "DRIVE=%%~dI"
set "DRIVE_LETTER=%DRIVE:~0,1%"
set "WIN_REST=%WIN_ROOT:~2%"
set "WSL_ROOT=/mnt/%DRIVE_LETTER%/%WIN_REST:\=/%"

where wsl.exe >nul 2>nul
if errorlevel 1 exit /b 0

REM Single quotes inside the argument keep the WSL path (which may contain
REM spaces) intact when wsl.exe re-parses the joined command line.
wsl.exe -e bash -lc "'%WSL_ROOT%/scripts/time-sync.sh'"
exit /b 0
```

## 3. NEW `cmake/RunTimeSync.cmake` (LF)

```cmake
# RunTimeSync.cmake - pre-build WSL time-sync hook.
# Invoked by the 'hdaw_time_sync' ALL custom target (see CMakeLists.txt) as:
#   ${CMAKE_COMMAND} -P cmake/RunTimeSync.cmake
# Runs scripts\\time-sync.cmd via cmd.exe with quoting handled by CMake
# (no shell-level nesting, so paths with spaces are safe). The hook NEVER
# fails the build: the .cmd/.sh exit 0 on every path unless the caller sets
# HDAW_TIME_SYNC_STRICT=1 explicitly (interactive use only).

cmake_minimum_required(VERSION 3.24)

get_filename_component(HDAW_SYNC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
execute_process(
    COMMAND cmd.exe /C call "${HDAW_SYNC_ROOT}/scripts/time-sync.cmd"
    RESULT_VARIABLE _sync_rc
)
if(NOT _sync_rc EQUAL 0)
    message(WARNING "hdaw time-sync hook exited ${_sync_rc}; continuing the build")
endif()
```

## 4. `CMakeLists.txt` — append at END of file (after the check_version block)

```cmake

# --- WSL time-sync hook (Windows/WSL clock drift) ---------------------------
# WSL2 clocks drift from the Windows host; ninja/MSBuild then misjudge file
# mtimes through drvfs/9p (stale-`.obj` / stale-bundle traps - AGENTS.md
# lessons 15/21). Run scripts\time-sync.cmd before every default build via
# cmake/RunTimeSync.cmake. The hook is a fast no-op outside WSL / within the
# HDAW_TIME_SYNC_INTERVAL freshness window and NEVER fails a build. Disable
# with -DHDAW_TIME_SYNC_HOOK=OFF.
set(HDAW_TIME_SYNC_HOOK ON CACHE BOOL "Run WSL time-sync before builds (Windows/WSL clock-drift guard)")
if(HDAW_TIME_SYNC_HOOK AND WIN32)
    add_custom_target(hdaw_time_sync ALL
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTimeSync.cmake"
        COMMENT "WSL time sync (pre-build hook)"
        VERBATIM
    )
endif()
```

## 5. `build-fast.bat` — insert (LF) immediately after `set CONFIG=RelWithDebInfo`

```bat
REM ── WSL time-sync hook ─────────────────────────────────────────────────────
REM Snap the WSL clock to the Windows host before ANY build. WSL2 clock drift
REM makes ninja/MSBuild misjudge mtimes through drvfs/9p (stale-`.obj` / stale-
REM bundle traps - AGENTS.md lessons 15/21). Never fails the build; no-op when
REM not in WSL or when synced within HDAW_TIME_SYNC_INTERVAL seconds.
call "%~dp0scripts\time-sync.cmd"
```

## 6. `frontend/build.bat` — insert (LF) immediately after
`if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"`

```bat
:: ── WSL time-sync hook: snap the WSL clock to the Windows host before any
::    build (clock drift -> ninja/MSBuild misjudge mtimes through drvfs/9p ->
::    stale-`.obj`/stale-bundle traps - AGENTS.md lessons 15/21). Never fails
::    the build; no-op when not in WSL or synced within HDAW_TIME_SYNC_INTERVAL.
call "%ROOT%\scripts\time-sync.cmd"
```

## 7. NEW `docs/skills/pre-build-time-sync/SKILL.md` (LF)

```markdown
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
```

## 8. `docs/skills/README.md` — add this bullet after the hdaw-guard bullet

```markdown
- [`pre-build-time-sync`](pre-build-time-sync/SKILL.md) — required before every build/compile; snaps the WSL clock to the Windows host to stop timestamp-drift build traps (stale-`.obj`/stale bundles).
```

## 9. `AGENTS.md` — add this bullet at the end of the "Build (summary)" section
(after the "See `docs/architecture.md`..." bullet)

```markdown
- **Pre-build time sync (WSL/Windows clock drift):** before ANY build/compile
  in this repo (`cmake --build`, `build-fast.bat`, `frontend\build.bat`, bare
  `ninja`, `npm run build`), invoke `skill: "pre-build-time-sync"` — it snaps
  the WSL clock to the Windows host (`sudo ntpdate -b time.windows.com`) so
  ninja/MSBuild never misjudge file mtimes through drvfs/9p under WSL2 clock
  drift (lesson 15 / the WSL-side-edit sync recipe). `build-fast.bat` and
  `frontend\build.bat` call `scripts\time-sync.cmd` automatically, and CMake
  adds a `hdaw_time_sync` ALL target covering bare `cmake --build`; direct
  `ninja` / `npm run build` runs need the explicit `scripts/time-sync.sh`.
  The hook is a fast no-op outside WSL and NEVER fails a build. See
  `docs/plans/2026-09-05-time-sync-build-hook.md` and
  `docs/skills/pre-build-time-sync/SKILL.md`.
```

## Subagent self checks (run before reporting) — corrected 2026-09-05

1. `bash -n scripts/time-sync.sh` → clean.
2. `./scripts/time-sync.sh` → rc 0, prints the WARN line (no passwordless sudo
   on this host). Never enter a password during checks.
3. `HDAW_TIME_SYNC_STRICT=1 HDAW_TIME_SYNC_INTERVAL=0 ./scripts/time-sync.sh`
   → rc 1 (strict failure path). NOTE: the env var prefix is `HDAW_TIME_SYNC_`,
   not `STRICT`.
4. Fake-tool success path — the stub `sudo` MUST consume the `-n` flag
   (bash's `exec` rejects it; the original plan's naive `exec "$@"` stub was a
   spec defect):
   - `/tmp/fakebin/sudo`:
     `#!/usr/bin/env bash` + `args=(); while [ "$#" -gt 0 ]; do case "$1" in -n) shift ;; *) args+=("$1"); shift ;; esac; done; exec "${args[@]}"`
   - `/tmp/fakebin/ntpdate`: `#!/usr/bin/env bash` + `exit 0`
   - run: `rm -f /tmp/t.stamp; PATH=/tmp/fakebin:$PATH
     HDAW_TIME_SYNC_INTERVAL=0 HDAW_TIME_SYNC_STAMP=/tmp/t.stamp
     HDAW_TIME_SYNC_STRICT=1 ./scripts/time-sync.sh`
     → rc 0 + "synced" line + stamp exists; rerun with `HDAW_TIME_SYNC_INTERVAL=300`
     → instant no-op (no output), ntpdate invoked exactly once.
5. Windows launcher end-to-end (native cmd quoting, mirroring build-fast.bat):
   write `C:\temp\hooktest\t1.bat` containing
   `@call "D:\pdf\roo projects\hdaw3\scripts\time-sync.cmd"` and run
   `cmd.exe /c C:\temp\hooktest\t1.bat` (clear `/tmp/hdaw_time_sync.stamp`
   first) → [time-sync] line + rc 0.
6. build-fast.bat early-exit: same wrapper technique calling
   `"D:\pdf\roo projects\hdaw3\build-fast.bat" bogus_target` → hook line
   FIRST, then `Unknown target: bogus_target` + Usage, bat rc 1.
7. `cmake.exe -P D:\pdf\roo projects\hdaw3\cmake\RunTimeSync.cmake` →
   [time-sync] line + rc 0 (stamp cleared first).
8. Confirm all 9 files use LF line endings (binary CRLF count == 0).

## Outcome (2026-09-05) — all gates PASS (orchestrator-verified)

- **G1 PASS** — syntax clean; default rc 0 (WARN path); strict rc 1; clock untouched.
- **G2 PASS** — stub sudo (consuming `-n`) + stub ntpdate: "synced" + stamp + rc 0;
  freshness no-op verified (ntpdate invoked exactly once across two runs).
- **G3 PASS** — uppercase-D and lowercase-d launcher invocations both reach the
  script via native cmd quoting (t1.bat); [time-sync] line printed, rc 0.
- **G4 PASS** — build-fast.bat `bogus_target` early-exit: hook fires first, then
  `Unknown target`, bat rc 1.
- **G5 PASS** — `cmake.exe -P cmake\RunTimeSync.cmake` rc 0 with hook output;
  scratch Ninja AND "Visual Studio 18 2026" builds of a replica project run the
  `hdaw_time_sync` ALL target and finish green; `-DHDAW_TIME_SYNC_HOOK=OFF`
  removes the target (verified: no time-sync output, build green).
- **G6 PASS** — skill frontmatter `name` matches directory, description present;
  docs/skills/README.md + AGENTS.md reference the skill; CMakeLists change is
  only the appended block.
- **G7 PASS** — `git diff --stat` shows only the declared file set (plus
  pre-existing unrelated user worktree changes that predate this task).

### Deviation applied (reviewed + approved by orchestrator)

`scripts/time-sync.cmd` originally built the WSL path as `/mnt/<letter>/...`
from `%~dI`, which preserves the invoking path's case — `%~dp0`/`%~dI`
canonicalize to uppercase `D:`, but WSL mounts are lowercase `/mnt/d`, so the
hook silently no-opped (rc 0, never failed, but never synced). Fix: one extra
line lowercases the drive letter via a 26-letter comparison loop. Verified on
both uppercase- and lowercase-invoked paths.
