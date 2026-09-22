<#
.SYNOPSIS
    Reclaim disk from stale scratch: crash dumps, debugger symbol caches, the
    Windows temp folder, agent chat logs, and re-downloadable caches.

.DESCRIPTION
    DRY RUN BY DEFAULT — nothing is deleted without -Apply. Run it, read the
    report, then re-run with -Apply. Files held open by a live process are
    skipped automatically (Windows refuses the delete) and counted as LOCKED.

    Categories (see -List for the live path/age table):

      dumps    crash dumps (*.dmp/*.mdmp) + debugger symbol caches     -Hours      (24)
      traces   HDAW param traces / debug log / render WAVs / engine    -TraceHours (1)
               copies in %TEMP%
      temp     %TEMP% + C:\Windows\Temp sweep (empty dirs pruned)      -Hours      (24)
      agents   pi / omp / opencode / codex session + log files         -AgentHours (24)
      caches   npm / bun / pip / HF / Edge caches         [-Aggressive] -Hours      (24)
      agent-db sqlite bloat + old-session prune on opencode.db         [-AgentDb]

    Never touched (by design, see the report footer for the reasoning and the
    recommended command): the HDAW build tree, compositions\ renders, live
    sqlite databases, installed plugin/harness binaries, and the WER dump
    folder itself (only the stale captures inside it are removed).

.PARAMETER Apply
    Actually delete. Without it the script is read-only.

.PARAMETER Hours
    Age threshold in hours for dumps / temp. Default 24.

.PARAMETER TraceHours
    Age threshold for HDAW scratch (param traces, debug log, renders). These are
    pure diagnostics with pid-tagged names, written and then abandoned, so they
    default to a much shorter 1 h. A log still held open by a live engine cannot
    be deleted (Windows sharing rules) and is reported as LOCKED.

.PARAMETER AgentHours
    Age threshold for agent session/log files. Default 24; the live session is
    protected by the threshold because it is appended to continuously.

.PARAMETER Only
    Restrict the run to these category names (dumps, traces, temp, agents,
    caches, agent-db).

.PARAMETER Skip
    Exclude these category names.

.PARAMETER Aggressive
    Also sweep re-downloadable dev-tool caches (npm _cacache/_npx, bun, pip,
    opencode runtime cache, codex plugins, Edge cache, package-manager staging).
    Costs rebuild time; no data loss.

.PARAMETER ModelCache
    Also sweep the large model caches (HuggingFace, codex runtimes). Separate
    from -Aggressive because re-downloading models is slow and they may be
    needed offline.

.PARAMETER AgentDb
    Run the sqlite maintenance pass (scripts/cleanup-stale-db.mjs) over
    opencode.db. Add -AgentDbDays N to also delete sessions older than N days
    (default: report + VACUUM only).

.PARAMETER ExtraPath
    Additional directory to sweep with the temp rules (age = -Hours).

.EXAMPLE
    scripts\cleanup-stale.ps1
    Dry run, full report, 24 h thresholds.

.EXAMPLE
    scripts\cleanup-stale.ps1 -Apply -Skip agents
    Delete everything stale except agent histories.

.EXAMPLE
    scripts\cleanup-stale.ps1 -Apply -Only traces -TraceHours 2
    Aggressive scratch-only sweep while the engine is running.

.EXAMPLE
    scripts\cleanup-stale.ps1 -Apply -Aggressive -AgentDb -AgentDbDays 30
    Everything, including caches and the opencode database prune.
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Apply,
    [int]$Hours = 24,
    [int]$TraceHours = 1,
    [int]$AgentHours = 24,
    [string[]]$Only = @(),
    [string[]]$Skip = @(),
    [switch]$Aggressive,
    [switch]$ModelCache,
    [switch]$AgentDb,
    [int]$AgentDbDays = 0,
    [switch]$AgentDbArchivedOnly,
    [string]$AgentDbBackup,
    [string[]]$ExtraPath = @(),
    [switch]$List
)

$ErrorActionPreference = 'Continue'
if ($Hours -lt 1 -or $TraceHours -lt 1 -or $AgentHours -lt 1) {
    throw "age thresholds must be >= 1 hour (refusing to delete 'everything')"
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Ut = $env:TEMP
$La = $env:LOCALAPPDATA
$Ad = $env:APPDATA
$Hm = $env:USERPROFILE
$Script:Now = Get-Date

# Directories that must survive even when empty: crash-diag.ps1 registers
# %TEMP%\hdaw_crash_captures\wer as the WER LocalDumps folder, and WER does not
# necessarily recreate it.
$Script:ProtectedDirs = @(
    (Join-Path $Ut 'hdaw_crash_captures'),
    (Join-Path $Ut 'hdaw_crash_captures\wer')
)

function MB([double]$bytes) { return [math]::Round($bytes / 1MB, 1) }

# ---------------------------------------------------------------- categories
# Each entry: Name, Threshold (hours), Kind, Targets.
#   Kind 'files' -> per-file age check (globs or directories, walked manually)
#   Kind 'trees' -> whole-directory removal when nothing inside is newer
$CategorySpec = @(
    @{ Name = 'dumps'; Hours = $Hours; Kind = 'files'; Targets = @(
            "$La\CrashDumps\*.dmp", "$La\CrashDumps\*.mdmp",
            "$Ut\*.dmp", "$Ut\*.mdmp",
            'C:\ProgramData\Microsoft\Windows\WER\ReportQueue',
            'C:\ProgramData\Microsoft\Windows\WER\ReportArchive',
            "$La\Microsoft\Windows\WER",
            "$La\Temp\SymbolCache", "$La\Microsoft\Windows\Symbols",
            "$Hm\Symbols", 'C:\symbols'
        ) },
    @{ Name = 'dumps'; Hours = $Hours; Kind = 'trees'; Targets = @(
            "$Ut\hdaw_crash_captures\engine_*"
        ) },
    @{ Name = 'traces'; Hours = $TraceHours; Kind = 'files'; Targets = @(
            "$Ut\hdaw_*.log", "$Ut\hdaw_*.err", "$Ut\hdaw_*.wav", "$Ut\hdaw_*.dmp",
            "$Ut\hdaw_*.bin", "$Ut\hdaw_*.txt", "$Ut\hdaw_*.syx", "$Ut\hdaw_*.hdaw",
            "$Ut\hdaw_*.bat",
            "$Ut\HDAW*.exe", "$Ut\hdaw_plugin_host.exe", "$Ut\hdaw_plugin_scanner.exe"
        ) },
    @{ Name = 'temp'; Hours = $Hours; Kind = 'files'; Targets = @(
            $Ut, 'C:\Windows\Temp'
        ) },
    @{ Name = 'agents'; Hours = $AgentHours; Kind = 'files'; Targets = @(
            "$Hm\.pi\agent\sessions",
            "$Hm\.pi\agent\auth.json.*.bak",
            "$Hm\.pi\context-mode\sessions\*.db",
            "$Hm\.pi\context-mode\content\*.db",
            "$Hm\.omp\agent\sessions",
            "$Hm\.omp\agent\cache",
            "$Hm\.omp\logs",
            "$Hm\.local\share\opencode\log",
            "$Hm\.local\share\opencode\storage",
            "$Hm\.codex\.tmp",
            "$Hm\.codex\logs_*.sqlite-wal", "$Hm\.codex\logs_*.sqlite-shm",
            "$Hm\.claude\*.jsonl", "$Hm\.claude\projects",
            "$Hm\.gemini\tmp", "$Hm\.codex\sessions",
            "$Hm\.local\share\zellij", "$Hm\.aider"
        ) },
    @{ Name = 'caches'; Hours = $Hours; Kind = 'files'; Gate = 'Aggressive'; Targets = @(
            "$La\npm-cache\_cacache", "$La\npm-cache\_npx", "$La\npm-cache\_logs",
            "$Hm\.bun\install\cache",
            "$La\pip\cache",
            "$Hm\.cache\opencode",
            "$Hm\.codex\plugins",
            "$Hm\.opencode\node_modules",
            "$La\Microsoft\Edge\User Data\Default\Cache",
            "$Ut\WinGet", "$Ut\UniGetUI"
        ) },
    @{ Name = 'models'; Hours = $Hours; Kind = 'files'; Gate = 'Models'; Targets = @(
            "$Hm\.cache\huggingface", "$Hm\.cache\codex-runtimes"
        ) }
)

foreach ($p in $ExtraPath) {
    if (-not (Test-Path -LiteralPath $p)) { Write-Warning "extra path not found: $p"; continue }
    $CategorySpec += @{ Name = 'extra'; Hours = $Hours; Kind = 'files'; Targets = @($p) }
}

function Test-CategoryEnabled([string]$name) {
    if ($Skip -contains $name) { return $false }
    if ($Only.Count -gt 0 -and -not ($Only -contains $name)) { return $false }
    return $true
}

# ------------------------------------------------------------------ walkers
# Own recursive walk: PowerShell 5.1's Get-ChildItem -Recurse follows junction
# reparse points, which can escape the target tree. Skip them explicitly.
function Get-FilesUnder([string]$root) {
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    while ($stack.Count -gt 0) {
        $dir = $stack.Pop()
        foreach ($e in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)) {
            if ($e.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            if ($e.PSIsContainer) { $stack.Push($e.FullName) } else { $e }
        }
    }
}

function Get-DirsUnder([string]$root) {
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    while ($stack.Count -gt 0) {
        $dir = $stack.Pop()
        foreach ($e in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)) {
            if (-not $e.PSIsContainer) { continue }
            if ($e.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            $e
            $stack.Push($e.FullName)
        }
    }
}

function Get-TargetFiles([string]$pattern) {
    if (Test-Path -LiteralPath $pattern) {
        $item = Get-Item -LiteralPath $pattern -Force -ErrorAction SilentlyContinue
        if ($null -eq $item) { return @() }
        if ($item.PSIsContainer) { return @(Get-FilesUnder $item.FullName) }
        return @($item)
    }
    return @(Get-ChildItem -Path $pattern -Force -File -ErrorAction SilentlyContinue)
}

function Get-TargetDirs([string]$pattern) {
    return @(Get-ChildItem -Path $pattern -Force -Directory -ErrorAction SilentlyContinue |
             Where-Object { -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) })
}

function Remove-EmptySubdirs([string]$root) {
    $dirs = @(Get-DirsUnder $root) | Sort-Object { $_.FullName.Length } -Descending
    foreach ($d in $dirs) {
        if ($Script:ProtectedDirs -contains $d.FullName) { continue }
        $kids = @(Get-ChildItem -LiteralPath $d.FullName -Force -ErrorAction SilentlyContinue)
        if ($kids.Count -eq 0) {
            $Script:EmptyDirs++
            if ($Apply) { Remove-Item -LiteralPath $d.FullName -Force -ErrorAction SilentlyContinue }
        }
    }
}

# -------------------------------------------------------------------- sweep
$Script:EmptyDirs = 0
$results = New-Object System.Collections.ArrayList

function Invoke-Sweep {
    param([hashtable]$Spec)
    switch ($Spec.Gate) {
        'Aggressive' { if (-not $Aggressive) { return } }
        'Models' { if (-not $ModelCache) { return } }
        default { }
    }
    if (-not (Test-CategoryEnabled $Spec.Name)) { return }

    $cutoff = $Script:Now.AddHours(-1 * $Spec.Hours)
    $stat = [pscustomobject]@{
        Category = $Spec.Name; Hours = $Spec.Hours; Items = 0
        SizeMB = 0.0; Deleted = 0; FreedMB = 0.0; Locked = 0
    }
    $old = New-Object System.Collections.ArrayList
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'

    foreach ($target in $Spec.Targets) {
        if ($Spec.Kind -eq 'trees') {
            foreach ($d in (Get-TargetDirs $target)) {
                $files = @(Get-FilesUnder $d.FullName)
                $newest = if ($files.Count -eq 0) { $d.LastWriteTime } else { ($files | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime }
                if ($newest -ge $cutoff) { continue }
                $bytes = 0.0
                foreach ($f in $files) {
                    $bytes += $f.Length
                    if (-not $seen.Add($f.FullName)) { continue }
                    [void]$old.Add($f)
                    $stat.Items++
                    $stat.SizeMB += $f.Length
                }
                if (-not $Apply) { continue }
                $removed = 0
                foreach ($f in $files) {
                    try { Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop; $removed += $f.Length }
                    catch { $stat.Locked++ }
                }
                Remove-EmptySubdirs $d.FullName
                try { Remove-Item -LiteralPath $d.FullName -Force -Recurse -ErrorAction Stop } catch { }
                $stat.Deleted += $files.Count
                $stat.FreedMB += (MB $removed)
            }
            continue
        }
        foreach ($f in (Get-TargetFiles $target)) {
            if ($f.LastWriteTime -ge $cutoff) { continue }
            if (-not $seen.Add($f.FullName)) { continue }
            [void]$old.Add($f)
            $stat.Items++
            $stat.SizeMB += $f.Length
            if (-not $Apply) { continue }
            try {
                Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop
                $stat.Deleted++
                $stat.FreedMB += (MB $f.Length)
            } catch {
                $stat.Locked++
            }
        }
        if (-not $Apply) { continue }
        if (Test-Path -LiteralPath $target) {
            $it = Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            if ($null -ne $it -and $it.PSIsContainer) { Remove-EmptySubdirs $it.FullName }
        }
    }

    $stat.SizeMB = [math]::Round($stat.SizeMB / 1MB, 1)
    $stat.FreedMB = [math]::Round($stat.FreedMB, 1)
    [void]$results.Add($stat)

    if ($stat.Items -gt 0) {
        Write-Host ('  {0,-9} {1,6} items  {2,9} MB' -f $stat.Category, $stat.Items, $stat.SizeMB)
        if ($Apply) { Write-Host ('               deleted {0}, freed {1} MB, locked {2}' -f $stat.Deleted, $stat.FreedMB, $stat.Locked) }
    }
}

# ------------------------------------------------------------------- report
function Show-Inventory {
    Write-Host ''
    Write-Host 'Category   Hrs  Kind   Targets' -ForegroundColor Cyan
    Write-Host '--------   ---  -----  -------'
    foreach ($s in $CategorySpec) {
        $flag = switch ($s.Gate) {
            'Aggressive' { if (-not $Aggressive) { ' (needs -Aggressive)' } else { '' } }
            'Models' { if (-not $ModelCache) { ' (needs -ModelCache)' } else { '' } }
            default { '' }
        }
        Write-Host ('{0,-9}  {1,3}  {2,-6} {3}{4}' -f $s.Name, $s.Hours, $s.Kind, ($s.Targets -join '; '), $flag)
    }
    Write-Host ''
    Write-Host 'agent-db  (needs -AgentDb)  sqlite maintenance on ~/.local/share/opencode/opencode.db' -ForegroundColor Cyan
}

function Show-NotTouched {
    Write-Host ''
    Write-Host 'Not touched by this script (size measured now):' -ForegroundColor Yellow
    $notes = @(
        @{ P = "$Hm\.local\share\opencode\opencode.db"; N = 'opencode chat DB - use -AgentDb (VACUUM reclaims the freelist, no data loss)' },
        @{ P = "$Hm\.codex\plugins";                   N = 'codex plugins - -Aggressive' },
        @{ P = "$Hm\.codex\.tmp";                      N = 'codex plugin staging - -Aggressive' },
        @{ P = "$Hm\.pi\agent\npm\node_modules";       N = 'pi plugin install - keep (reinstalling costs a network round trip)' },
        @{ P = "$Hm\.omp\natives";                     N = 'omp native binaries - keep' },
        @{ P = "$Hm\.cache\huggingface";               N = 'HF model cache - -Aggressive' },
        @{ P = "$La\npm-cache\_cacache";               N = 'npm cache - -Aggressive, or: npm cache clean --force' },
        @{ P = "$Hm\.bun\install\cache";               N = 'bun cache - -Aggressive, or: bun pm cache rm' },
        @{ P = "$La\pip\cache";                        N = 'pip cache - -Aggressive, or: pip cache purge' },
        @{ P = 'C:\symbols';                          N = 'windbg symbol cache - swept by the dumps category (age-gated)' },
        @{ P = "$La\wsl";                             N = 'WSL2 ext4.vhdx - grows dynamically, never shrinks: wsl --manage <distro> --set-sparse true' },
        @{ P = (Join-Path $RepoRoot 'build');          N = 'HDAW build tree - reconfigure, do NOT delete by hand (see AGENTS.md ninja_deps trap)' },
        @{ P = (Join-Path $RepoRoot 'compositions');   N = 'render output - intentional artifacts' }
    )
    foreach ($n in $notes) {
        $size = ''
        if (Test-Path -LiteralPath $n.P) {
            $sum = (Get-ChildItem -LiteralPath $n.P -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Sum Length).Sum
            $size = '[' + (MB ([double]$sum)) + ' MB]'
        }
        Write-Host ('  {0,-10} {1}' -f $size, $n.N)
        Write-Host ('             {0}' -f $n.P) -ForegroundColor DarkGray
    }
}

# --------------------------------------------------------------------- main
if ($List) { Show-Inventory; return }

$mode = if ($Apply) { 'APPLY' } else { 'DRY RUN' }
Write-Host ''
Write-Host "=== cleanup-stale [$mode]  $(Get-Date -Format 'yyyy-MM-dd HH:mm') ===" -ForegroundColor Green
Write-Host "    thresholds: --Hours=$Hours  --TraceHours=$TraceHours  --AgentHours=$AgentHours  aggressive=$Aggressive"

if ($Apply) {
    Write-Host ''
    Write-Host 'Sweeping...'
} else {
    Write-Host ''
    Write-Host 'Scanning (nothing will be deleted; re-run with -Apply):'
}

foreach ($spec in $CategorySpec) { Invoke-Sweep $spec }

$totalItems = ($results | Measure-Object -Property Items -Sum).Sum
$totalSize = ($results | Measure-Object -Property SizeMB -Sum).Sum
$totalDeleted = ($results | Measure-Object -Property Deleted -Sum).Sum
$totalFreed = ($results | Measure-Object -Property FreedMB -Sum).Sum
$totalLocked = ($results | Measure-Object -Property Locked -Sum).Sum

Write-Host ''
Write-Host ('{0,-9} {1,7} {2,11} {3,9} {4,11} {5,7}' -f 'CATEGORY', 'ITEMS', 'SIZE MB', 'DELETED', 'FREED MB', 'LOCKED')
Write-Host ('{0,-9} {1,7} {2,11} {3,9} {4,11} {5,7}' -f '--------', '-----', '-------', '-------', '--------', '------')
foreach ($r in $results) {
    if ($r.Items -eq 0 -and $r.Deleted -eq 0) { continue }
    Write-Host ('{0,-9} {1,7} {2,11} {3,9} {4,11} {5,7}' -f $r.Category, $r.Items, $r.SizeMB, $r.Deleted, $r.FreedMB, $r.Locked)
}
Write-Host ('{0,-9} {1,7} {2,11} {3,9} {4,11} {5,7}' -f '--------', '-----', '-------', '-------', '--------', '------')
Write-Host ('{0,-9} {1,7} {2,11} {3,9} {4,11} {5,7}' -f 'TOTAL', $totalItems, $totalSize, $totalDeleted, $totalFreed, $totalLocked)
Write-Host ("empty dirs pruned: $Script:EmptyDirs")

if (-not $Apply) {
    Write-Host ''
    Write-Host "Re-run with -Apply to reclaim $totalSize MB." -ForegroundColor Yellow
}
if ($totalLocked -gt 0) {
    Write-Host ''
    Write-Host "$totalLocked item(s) were held open by a running process and left in place." -ForegroundColor Yellow
}

if ($AgentDb) {
    $dbTool = Join-Path $PSScriptRoot 'cleanup-stale-db.mjs'
    if (-not (Test-Path -LiteralPath $dbTool)) {
        Write-Warning "agent-db: $dbTool not found"
    } elseif (-not (Get-Command bun -ErrorAction SilentlyContinue)) {
        Write-Warning 'agent-db: bun not on PATH; run the .mjs with any node/bun runtime'
    } else {
        Write-Host ''
        Write-Host '=== agent-db ===' -ForegroundColor Green
        $dbArgs = @($dbTool)
        if ($AgentDbDays -gt 0) { $dbArgs += @('--days', "$AgentDbDays") }
        if ($AgentDbArchivedOnly) { $dbArgs += '--archived' }
        if ($AgentDbBackup) { $dbArgs += @('--backup', $AgentDbBackup) }
        if ($Apply) { $dbArgs += '--apply' }
        & bun @dbArgs
    }
}

Show-NotTouched
Write-Host ''
