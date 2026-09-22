# heap-diag.ps1 -- ready-to-run heap-corruption diagnostics for the HDAW engine.
#
# Context (docs/handoffs/2026-09-22-engine-heap-corruption-investigation.md):
# the 2026-09-22 session hit an unhandled C0000374 (heap corruption) whose
# corrupting WRITE was never pinned -- the free surfaced in the AudioProcessorGraph
# async update. This script wires up the two diagnostics that catch the corrupting
# write AT THE MOMENT IT HAPPENS.
#
# Usage:
#   heap-diag.ps1 pageheap-on            enable full-page heap for the engine exe
#   heap-diag.ps1 pageheap-off           disable page heap (restores normal perf)
#   heap-diag.ps1 status                 show current page-heap + engine state
#
# Notes:
# - Page heap detects the corrupting write AT THE CORRUPTING ACCESS (access to a
#   freed block page-faults immediately) instead of at some later free. Expect a
#   large memory slowdown; use it for a single repro session, then pageheap-off.
# - Page-heap settings live in the registry (Image File Execution Options) and
#   SURVIVE engine restarts -- always run pageheap-off when done.
# - The launcher's procdump capture (kill-after-dump, since 2026-09-22) writes the
#   dump to %TEMP%\hdaw_crash_captures\engine_*; analyze with:
#     cdb -z <dump> -c "!analyze -v; kv 40; q" -y "srv*C:\symbols*https://msdl.microsoft.com/download/symbols;<build dir>"

param([Parameter(Mandatory=$true)][string]$Action)

$engineNames = @('HDAW_headless_mcp.exe', 'HDAW_headless.exe', 'HDAW.exe')
$gflags = Get-Command gflags -ErrorAction SilentlyContinue
if (-not $gflags) {
    # Typical WinSDK location when gflags is not on PATH.
    $candidates = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\gflags.exe' -ErrorAction SilentlyContinue
    if ($candidates) { $gflags = $candidates[0] }
}
if (-not $gflags) { Write-Error 'gflags.exe not found (install Windows SDK Debugging Tools)'; exit 1 }
# Get-Command yields ApplicationInfo (use .Source); Get-ChildItem yields FileInfo (use .FullName).
$gflagsExe = if ($gflags.Source) { $gflags.Source } else { $gflags.FullName }

switch ($Action) {
    'pageheap-on' {
        foreach ($exe in $engineNames) {
            & $gflagsExe /p /enable $exe /full
        }
        Write-Host '[heap-diag] full page heap ENABLED for:' $engineNames
        Write-Host '[heap-diag] run ONE repro session now, then: heap-diag.ps1 pageheap-off'
        Write-Host '[heap-diag] the corrupting access will fault immediately and procdump (kill-after-dump) writes the dump.'
    }
    'pageheap-off' {
        foreach ($exe in $engineNames) {
            & $gflagsExe /p /disable $exe
        }
        Write-Host '[heap-diag] page heap DISABLED for:' $engineNames
    }
    'status' {
        & $gflagsExe /p
        Get-CimInstance Win32_Process -Filter "Name='HDAW_headless_mcp.exe' or Name='HDAW.exe'" |
            Select-Object ProcessId, CreationDate | Format-Table
    }
    default { Write-Error "unknown action: $Action (use pageheap-on | pageheap-off | status)"; exit 1 }
}
